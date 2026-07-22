// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "config/settings.h"
#include "config/configdefaults.h"
#include "core/platform/logging.h"

#include <PhosphorAnimation/CurveRegistry.h>
#include <PhosphorAnimation/Profile.h>

#include <QJsonObject>

namespace PlasmaZones {

// ── Animations (PhosphorConfig::Store-backed) ───────────────────────────────
// Snapping + autotile geometry-change transitions. The animation Profile is one
// nested JSON blob under Animations/AnimationProfile; each per-field accessor
// decomposes / composes the blob. `animationsEnabled` stays a standalone bool
// in settings.cpp.

// Process-wide fallback registry for standalone Settings instances
// constructed without an injected CurveRegistry (tests, settings-app
// tools that don't share a registry with the daemon). Consolidated
// here as a single function-local static so every fallback path
// resolves curve wire-format strings through the same registry —
// avoids the "two identical spring:14,0.6 strings resolve to different
// shared_ptr<const Curve> instances" surprise that three separate
// per-method statics introduced.
static PhosphorAnimation::CurveRegistry& fallbackCurveRegistry()
{
    static PhosphorAnimation::CurveRegistry s_registry;
    return s_registry;
}

namespace {
/// Read the Profile blob as a mutable QJsonObject. Stored as a nested
/// QVariantMap; an absent / malformed entry returns an empty object so
/// callers that then write back produce a minimal blob containing only
/// the patched field.
QJsonObject readProfileObject(const PhosphorConfig::Store& store)
{
    const QVariantMap map =
        store.read<QVariantMap>(ConfigDefaults::animationsGroup(), ConfigDefaults::animationProfileKey());
    return QJsonObject::fromVariantMap(map);
}

/// Write @p obj as the Profile blob (stored as a nested QVariantMap).
void writeProfileObject(PhosphorConfig::Store& store, const QJsonObject& obj)
{
    store.write(ConfigDefaults::animationsGroup(), ConfigDefaults::animationProfileKey(), obj.toVariantMap());
}
} // namespace

PhosphorAnimation::Profile Settings::animationProfile() const
{
    // An absent / malformed blob returns an empty object, which yields a
    // default-constructed Profile (unset-optional fields). Profile::effective*
    // methods then substitute library defaults — "garbage in disk → sensible
    // defaults".
    const QJsonObject obj = readProfileObject(*m_store);
    if (obj.isEmpty()) {
        return PhosphorAnimation::Profile{};
    }
    const PhosphorAnimation::CurveRegistry& reg = m_curveRegistry ? *m_curveRegistry : fallbackCurveRegistry();
    return PhosphorAnimation::Profile::fromJson(obj, reg);
}

void Settings::setAnimationProfile(const PhosphorAnimation::Profile& profile)
{
    refreshCleanBackendFromDisk();
    // Change detection compares the merged QJsonObject against the
    // current stored blob via QJsonObject::operator==, NOT through a
    // semantic `Profile::operator==` check. The semantic comparison hits
    // two traps in practice:
    //
    //   1. `Profile::fromJson` resolves the curve field through the
    //      runtime `CurveRegistry`. Plugin-registered curves that aren't
    //      loaded yet produce a null curve pointer, so two blobs that
    //      reference different unresolved curves still compare equal
    //      (both have null curves) — silently data-losing a user's
    //      curve choice when the plugin arrives later.
    //   2. Inversely, when the blob round-trips cleanly the new Profile
    //      may differ slightly (e.g. `effective*` defaults vs unset
    //      optionals) and the semantic `operator==` returns `false` even
    //      though the stored blob would be unchanged — firing a signal
    //      storm every call.
    //
    // QJsonObject equality at the blob level sidesteps both: the only
    // thing that fires signals is a real change to what gets written
    // to disk.
    //
    // Per-field signal emission still goes through the parsed prev /
    // new Profiles because each `xxxChanged` signal describes an
    // observable semantic field, not a blob identity. A blob-identity
    // change with identical effective fields (e.g. curve precision
    // canonicalisation) correctly fires `animationProfileChanged`
    // without fanning out to the per-field signals.

    // Merge: start from the stored blob and overlay every field the
    // incoming Profile emitted. `Profile::toJson` only emits set-optional
    // fields and a non-null curve — unset optionals are absent from the
    // output. Those absent fields leave the corresponding on-disk key
    // untouched, matching the partial-update semantics of the per-field
    // setters: a caller that wants to "clear" a field sets it explicitly
    // via a per-field setter or by constructing a Profile with the
    // intended value, not by leaving it unset.
    //
    // The loop is additive only (no removes) so unknown on-disk keys —
    // future schema extensions, third-party plugin fields — survive
    // intact. Asymmetric removal would wipe them on the first aggregate
    // setter call, and contradict the per-field setters' merge semantics.
    const QJsonObject current = readProfileObject(*m_store);
    QJsonObject merged = current;
    const QJsonObject incoming = profile.toJson();
    for (auto it = incoming.constBegin(); it != incoming.constEnd(); ++it) {
        merged.insert(it.key(), it.value());
    }

    if (merged == current) {
        // Nothing to write — skip both the store write and every signal.
        // Guards the slider-drag-at-30-Hz signal-storm case: a merge that
        // produces a structurally identical object must not wake observers.
        return;
    }

    // Snapshot per-field effective values BEFORE the write so we can
    // emit per-field signals only for fields whose semantic observable
    // actually changed. Parses the PREVIOUS disk bytes through the same
    // registry the runtime uses, matching the invariant that per-field
    // `xxxChanged` signals describe what `animationDuration()` (etc.)
    // will return before vs. after the write.
    const PhosphorAnimation::Profile prev = animationProfile();
    const int prevDuration = qRound(prev.effectiveDuration());
    // Read the pre-write curve directly off the on-disk blob via the
    // same path the live `animationEasingCurve()` getter takes. Going
    // through `prev.curve->toString()` would null-fall-back to the
    // ConfigDefaults default whenever `CurveRegistry::tryCreate`
    // failed to resolve the stored spec — comparing that fallback
    // against the unchanged on-disk value below would always flag a
    // difference and fire a spurious `animationEasingCurveChanged`.
    const QString prevCurveWire = animationEasingCurve();
    const int prevMinDistance = prev.effectiveMinDistance();
    const int prevSequenceMode = static_cast<int>(prev.effectiveSequenceMode());
    const int prevStaggerInterval = prev.effectiveStaggerInterval();

    writeProfileObject(*m_store, merged);

    // Per-field: only emit when the post-write OBSERVABLE differs from
    // the pre-write observable. Compare against `animationProfile()`
    // re-read post-write — NOT against `profile.effective*()` (the
    // incoming arg), because `merged` preserves prev's values for any
    // field the caller left unset. With the incoming `profile` having
    // all-unset fields, `profile.effective*()` returns library
    // defaults; comparing those against `prev*` would fire spurious
    // signals even though the on-disk value is unchanged. Cache the
    // post-write Profile in one read instead of paying a JSON parse +
    // CurveRegistry resolve per field.
    //
    // Per-field signals are emitted BEFORE the aggregate, matching
    // `patchProfileField`'s ordering so QML consumers binding to both
    // a per-field setter and the aggregate observe a consistent
    // emission order regardless of which code path mutated the Profile.
    const auto next = animationProfile();
    if (qRound(next.effectiveDuration()) != prevDuration) {
        Q_EMIT animationDurationChanged();
    }
    // `animationEasingCurve()` reads the curve directly off the merged
    // JSON blob (preserving any unresolved raw spec) — distinct from
    // `next.curve->toString()` which could lose detail when the curve
    // failed to resolve through `CurveRegistry::tryCreate`. The wire
    // form is what disk-level equality tracks.
    const QString newCurveWire = animationEasingCurve();
    if (newCurveWire != prevCurveWire) {
        Q_EMIT animationEasingCurveChanged();
    }
    if (next.effectiveMinDistance() != prevMinDistance) {
        Q_EMIT animationMinDistanceChanged();
    }
    if (static_cast<int>(next.effectiveSequenceMode()) != prevSequenceMode) {
        Q_EMIT animationSequenceModeChanged();
    }
    if (next.effectiveStaggerInterval() != prevStaggerInterval) {
        Q_EMIT animationStaggerIntervalChanged();
    }

    // Aggregate last — consumers that want to observe the Profile
    // atomically (the daemon's fan-out hook) get one signal per call,
    // after every per-field signal has fired.
    Q_EMIT animationProfileChanged();
    Q_EMIT settingsChanged();
}

// ─── Per-field projections over the Profile blob ───────────────────────────
// Each setter patches ONE field in the stored JSON blob directly rather
// than round-tripping through Profile::toJson. Round-tripping would drop
// any unresolved raw curve spec preserved by `setAnimationEasingCurve` —
// `animationProfile()` goes through `CurveRegistry::tryCreate`, which nulls
// the curve pointer on resolution failure, and `Profile::toJson` then omits
// the null field. Patching the raw JSON blob leaves every field the setter
// isn't touching exactly where it was.
//
// Getters route through `animationProfile()` for anything that needs the
// `effective*` defaulting, and read the blob directly for the curve field
// (so unresolved specs round-trip through the getter/setter pair).
//
// Hot path: settings-UI slider drag ~30 Hz. Per call: one read + one JSON
// parse + one field-insert + one serialise + one store write. Acceptable.
//
// The shared merge primitive `patchProfileField` (declared in settings.h)
// owns the read → guard → insert → write → emit-trio sequence so each
// per-field setter is a one-liner over its type-specific pre-processing
// (clamp for numerics, registry resolution for the curve string). The
// helper is defined in this TU because it is consumed exclusively here;
// keeping it private to settings.cpp keeps the .h surface compact.
// Cross-process coherence for COMPOSITE values (multi-field JSON blobs stored
// under one key: the animation Profile, the shader/decoration profile trees,
// the per-algorithm autotile map, the snapping and tiling trigger lists
// including the zoneSpan pair). Scalar keys are written atomically and
// last-writer-wins per key is acceptable; composites are not, in two ways.
// Read-modify-write setters (the animation Profile field patches) merge into
// the WHOLE blob, so a stale cached document resurrects old sibling fields
// over a value the other process just committed — proven live: a daemon-side
// sequenceMode write landing between the settings app's save and the daemon's
// reload permanently reverted a just-saved duration (#795). Whole-replace
// setters (the trees, the per-algo map) compare against the cached blob for
// their no-op guard, so a stale cache can wrongly swallow a write the disk
// actually needs. Every composite setter therefore refreshes first.
// Refreshing is only legal on a CLEAN backend: with local writes pending the
// in-memory document is the freshest truth and a reparse would drop them.
// The dirty flag makes this self-limiting on the slider hot path — the first
// write of an edit burst marks the backend dirty, so later ticks skip the
// reparse until the next save.
// The refresh adopts external sibling values into the live store and the
// baseline WITHOUT per-property NOTIFY emissions — deliberately, unlike
// load(): this is a per-write guard, and the UI adopts external state
// through the controller's reload path (onExternalSettingsChanged), not
// here. Until that reload runs, a binding on an externally-changed sibling
// may read stale; it self-heals on the next load().
// The call sits at the TOP of each setter (not inside patchProfileField)
// because the per-field setters evaluate their currentValue argument at the
// call site: a refresh inside the helper would run AFTER that read, and the
// no-op guard would compare against the stale value — swallowing exactly the
// healing write the refresh exists to enable.
void Settings::refreshCleanBackendFromDisk()
{
    if (!m_configBackend || m_configBackend->isDirty()) {
        return;
    }
    m_configBackend->reparseConfiguration();
    // A clean backend means no local uncommitted writes, so before the
    // reparse the live store matched m_baseline. The reparse may adopt
    // externally-committed changes to ANY key; without advancing the
    // baseline those would be attributed to the local user as unsaved
    // edits (phantom per-page dirty markers, and a per-page Discard would
    // revert the external change to the stale session baseline).
    captureBaseline();
}

template<typename T>
void Settings::patchProfileField(const char* jsonFieldName, const T& currentValue, const T& newValue,
                                 void (Settings::*fieldChangedSignal)())
{
    if (currentValue == newValue) {
        // No-op guard — the setter contract requires that signals only
        // fire when an observable changes. A slider drag at constant
        // value wakes no observers.
        return;
    }
    QJsonObject obj = readProfileObject(*m_store);
    obj.insert(QLatin1String(jsonFieldName), newValue);
    writeProfileObject(*m_store, obj);
    Q_EMIT(this->*fieldChangedSignal)();
    Q_EMIT animationProfileChanged();
    Q_EMIT settingsChanged();
}

int Settings::animationDuration() const
{
    return qRound(animationProfile().effectiveDuration());
}

void Settings::setAnimationDuration(int duration)
{
    refreshCleanBackendFromDisk();
    const int clamped =
        qBound(ConfigDefaults::animationDurationMin(), duration, ConfigDefaults::animationDurationMax());
    patchProfileField<int>(PhosphorAnimation::Profile::JsonFieldDuration, animationDuration(), clamped,
                           &Settings::animationDurationChanged);
}

QString Settings::animationEasingCurve() const
{
    // Read the curve field directly from the Profile JSON blob so any
    // wire-format string round-trips through the setter/getter — even
    // unresolved specs (user plugin curve name not yet registered,
    // hand-edited config with a typo). `animationProfile()` routes the
    // curve through `CurveRegistry::tryCreate` which nulls the pointer
    // on resolution failure; the raw string would then be lost on
    // read-back. The runtime still gracefully falls back to the library
    // default at animation time — persisting the raw string here just
    // preserves the user's intent across restarts.
    const QJsonObject obj = readProfileObject(*m_store);
    const QString spec = obj.value(QLatin1String(PhosphorAnimation::Profile::JsonFieldCurve)).toString();
    return spec.isEmpty() ? ConfigDefaults::animationEasingCurve() : spec;
}

void Settings::setAnimationEasingCurve(const QString& curve)
{
    // NOT routed through patchProfileField — the easing setter has a pre-
    // resolution no-op-guard contract that the generic helper cannot
    // express. Specifically: the original behaviour compares the raw
    // CALLER string against the current stored string before resolution,
    // so two consecutive `setAnimationEasingCurve("alias")` calls with
    // the same alias short-circuit the second call regardless of
    // canonicalisation. Routing through the helper would compare the
    // post-resolution `toStore` against the current stored string — a
    // strictly different no-op gate that would, in the alias-with-already-
    // canonicalised-disk case, suppress signals the original would have
    // fired. Preserving observable behaviour > sharing the merge code.
    //
    // The merge sequence (read → insert → write → emit-trio) IS
    // duplicated relative to patchProfileField; that is intentional and
    // documented here so a future "tidy this up" pass does not re-route
    // through the helper without revisiting the no-op semantics first.

    // Compare against the raw-stored wire string (same shape the getter
    // returns) so no-op assignments short-circuit before any write.
    refreshCleanBackendFromDisk();
    if (animationEasingCurve() == curve) {
        return;
    }

    // Resolution through the registry is informational only — it tunes
    // the warning below and produces a canonical wire-form when the spec
    // resolves. Either way, the blob keeps the caller's string intact so
    // the user's edit round-trips cleanly through the Q_PROPERTY.
    PhosphorAnimation::CurveRegistry& reg = m_curveRegistry ? *m_curveRegistry : fallbackCurveRegistry();
    auto resolved = reg.tryCreate(curve);
    QString toStore;
    if (resolved) {
        // Known curve — store the canonical wire-format so a later
        // read-back matches what the runtime sees (prevents spurious
        // config rewrites when a user-supplied alias like "0.25,1.0,..."
        // resolves to a slightly-different-precision canonical form).
        toStore = resolved->toString();
    } else {
        // DEBUG, and the spec is NOT echoed. `curve` is the caller's raw string, and
        // animationEasingCurve is a registered STRING setting — so any session-bus peer
        // reaches this with content of its own choosing, unbounded in length and rate. That
        // is the same log-injection vector the per-screen writers were demoted for, one call
        // frame deeper still. "It did not resolve" is the signal; the failing text is not
        // worth an attacker-writable line in the daemon's log.
        qCDebug(lcConfig) << "setAnimationEasingCurve: curve spec did not resolve — persisting raw "
                             "(the library default applies at animation time)";
        toStore = curve;
    }

    QJsonObject obj = readProfileObject(*m_store);
    obj.insert(QLatin1String(PhosphorAnimation::Profile::JsonFieldCurve), toStore);
    writeProfileObject(*m_store, obj);

    Q_EMIT animationEasingCurveChanged();
    Q_EMIT animationProfileChanged();
    Q_EMIT settingsChanged();
}

int Settings::animationMinDistance() const
{
    return animationProfile().effectiveMinDistance();
}

void Settings::setAnimationMinDistance(int distance)
{
    refreshCleanBackendFromDisk();
    const int clamped =
        qBound(ConfigDefaults::animationMinDistanceMin(), distance, ConfigDefaults::animationMinDistanceMax());
    patchProfileField<int>(PhosphorAnimation::Profile::JsonFieldMinDistance, animationMinDistance(), clamped,
                           &Settings::animationMinDistanceChanged);
}

int Settings::animationSequenceMode() const
{
    return static_cast<int>(animationProfile().effectiveSequenceMode());
}

void Settings::setAnimationSequenceMode(int mode)
{
    refreshCleanBackendFromDisk();
    const int clamped =
        qBound(ConfigDefaults::animationSequenceModeMin(), mode, ConfigDefaults::animationSequenceModeMax());
    patchProfileField<int>(PhosphorAnimation::Profile::JsonFieldSequenceMode, animationSequenceMode(), clamped,
                           &Settings::animationSequenceModeChanged);
}

int Settings::animationStaggerInterval() const
{
    return animationProfile().effectiveStaggerInterval();
}

void Settings::setAnimationStaggerInterval(int ms)
{
    refreshCleanBackendFromDisk();
    const int clamped =
        qBound(ConfigDefaults::animationStaggerIntervalMin(), ms, ConfigDefaults::animationStaggerIntervalMax());
    patchProfileField<int>(PhosphorAnimation::Profile::JsonFieldStaggerInterval, animationStaggerInterval(), clamped,
                           &Settings::animationStaggerIntervalChanged);
}

} // namespace PlasmaZones
