// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "animationspagecontroller.h"

#include "core/interfaces/isettings.h"
#include "core/platform/logging.h"
#include "phosphor_i18n.h"
#include "settings/utils/animationfileutils.h"
#include "settings/stores/animationpresetlibrary.h"
#include "animationpreviewcontroller.h"
#include "animations_controller_detail.h"
#include "settings/services/motionsetdomain.h"
#include "settings/stores/shadersetstore.h"

#include <PhosphorAnimation/AnimationShaderRegistry.h>
#include <PhosphorAnimation/ProfilePaths.h>
#include <PhosphorAnimation/ShaderProfileTree.h>

#include <QLoggingCategory>
#include <QSet>

namespace PlasmaZones {

/// `JsonNameKey`, `profileToVariantMap`, `mergeMissingFields`, and
/// `fillLibraryDefaults` live in
/// `animations_controller_detail.h` so the sibling TU that uses them
/// (animationspagecontroller_overrides.cpp) shares the exact same
/// implementations without relying on unity-build TU merging.
///
/// `humanizeSegment` (segment title-casing for label display) also lives in
/// `animations_controller_detail.h` so animationspagecontroller_paths.cpp
/// shares the exact same implementation. Both `eventSections` (this TU)
/// and `eventLabel` (paths TU) call through to the header version.
using namespace animations_controller_detail;

// ─── Construction ──────────────────────────────────────────────────────

AnimationsPageController::AnimationsPageController(PhosphorAnimationShaders::AnimationShaderRegistry* shaderRegistry,
                                                   ISettings* settings, QObject* parent)
    // Id is the headless staging-domain identity, deliberately distinct from
    // the "animations" sidebar nav-parent. The controller is wired into the
    // framework via registerDomain() (NOT registerPage) — see the
    // registration site in settingscontroller_pageregistration.cpp. Keeping
    // the two ids separate means QML / D-Bus callers can address the nav
    // parent ("animations", which redirects to "animations-general") without
    // colliding with this staging controller's own identity.
    : PhosphorControl::PageController(QStringLiteral("animations-staging"), parent)
    , m_shaderRegistry(shaderRegistry)
    , m_settings(settings)
{
    auto profilesDirFn = [this]() {
        return userProfilesDir();
    };
    auto motionSetsDirFn = [this]() {
        return userMotionSetsDir();
    };
    auto writeOverrideFn = [this](const QString& path, const QVariantMap& profile) {
        return setOverride(path, profile);
    };
    // The SHADER half of a motion set. Both halves of an event are set on one
    // card in the UI, so a set has to carry both or it captures half of what
    // the user sees as a single thing.
    auto readShadersFn = [this]() {
        return allRawShaderProfiles();
    };
    auto writeShaderFn = [this](const QString& path, const QVariantMap& shader) {
        const QVariantMap params = shader.value(JsonShaderParametersKey).toMap();

        // ── De-seed, the counterpart of the snapshot capturing built-in
        // defaults (see motionsetdomain's snapshot). Decoration does this in
        // Settings::setDecorationProfileTree: a set carries the seeded look so
        // it is self-contained, and the write side then strips whatever still
        // matches locally, so applying a set REPRODUCES the default rather than
        // freezing it as a user override.
        //
        // Without this, applying any motion set would convert every built-in
        // default it carried into an explicit stored override, permanently
        // opting those events out of future default improvements — which is
        // exactly the failure the capture was meant to avoid.
        //
        // Compared against what the path would resolve to with its OWN
        // override removed, not against the built-in default. Those differ
        // wherever the pack comes from an ancestor: comparing to the built-in
        // default would leave the de-seed unfired and store a direct leaf
        // override that permanently shadows the recipient's own category
        // assignment — the very shadowing this block exists to prevent, just
        // one level up. This is the clear-then-regenerate-then-compare shape
        // Settings::setDecorationProfileTree already uses.
        if (params.isEmpty() && shader.contains(JsonEffectIdKey)) {
            const QString id = shader.value(JsonEffectIdKey).toString();
            QString inheritedId;
            if (m_settings != nullptr) {
                auto candidate = m_settings->shaderProfileTree();
                candidate.clearOverride(path);
                inheritedId = PhosphorAnimationShaders::resolveShaderWithDefault(candidate, path).effectiveEffectId();
            }
            if (!id.isEmpty() && id == inheritedId) {
                // Drop any stored override so the path resolves through the
                // default again. A path that carries no override is ALREADY in
                // the requested state, and that is a success — clearShaderOverride
                // reports "nothing to clear" as false, which is the right answer
                // for an explicit user clear and the wrong one here.
                if (rawShaderProfile(path).isEmpty()) {
                    return true;
                }
                return clearShaderOverride(path);
            }
        }
        // The three states the shader tree models, each needing its own API.
        // An engaged-empty effectId is NOT a clear: it is the sentinel for
        // "this event deliberately runs no pack", which also blocks a parent's
        // pack from cascading in, and setShaderOverride stores exactly that for
        // an empty id. Routing it to clearShaderOverride would drop the entry
        // and silently re-enable inheritance.
        if (shader.contains(JsonEffectIdKey))
            return setShaderOverride(path, shader.value(JsonEffectIdKey).toString(), params);
        // No effectId at all: the event inherits its pack and overrides only
        // the parameter map. setShaderParametersOnPaths starts from the stored
        // profile, so it preserves that unset state.
        //
        // Its return is a COUNT, and 0 is ambiguous — it means "nothing needed
        // writing" but also "every path was refused", so it cannot be used as a
        // success test. The set domain gates the path on eventPathSupportsShaderLeg
        // before any write happens, which removes the refusal case here; the
        // remaining 0 really is a no-op.
        setShaderParametersOnPaths({path}, params);
        return true;
    };
    // Sub-services are constructed before the dirty-forwarder wiring below.
    // Nothing is missed by that ordering: the forwarder seeds
    // m_lastHadPendingChanges from the real post-construction state (just
    // below), so it does not need to have observed any signal fired during
    // construction.
    m_presets = new AnimationPresetLibrary(profilesDirFn, /*snapshot=*/{}, /*rollback=*/{}, this);
    // Set and preset file CRUD is IMMEDIATE, matching decoration
    // (decorationpagecontroller_sets.cpp wires no snapshot hooks at all).
    // Saving, deleting, renaming or importing one used to be staged and undone
    // by Discard here, so the same footer button meant two different things on
    // the two pages: on Decoration it never touched your sets, on Animations it
    // silently reverted them.
    //
    // The per-event overrides a set APPLIES are still staged, exactly as
    // decoration's tree writes are — they are config keys, and Discard is
    // `Settings::load()`.
    m_motionSets = new ShaderSetStore(
        motionset::makeConfig(
            [this]() {
                return motionTree();
            },
            motionSetsDirFn, writeOverrideFn, readShadersFn, writeShaderFn,
            [this](const QString& path) {
                // What the path RENDERS with:
                // ancestor chain and built-in
                // default included. readShaders
                // above answers direct overrides
                // only, which is the right shape
                // for the entries a set stores
                // and the wrong one for the
                // self-containment sweep.
                if (m_settings == nullptr) {
                    return QString();
                }
                return PhosphorAnimationShaders::resolveShaderWithDefault(m_settings->shaderProfileTree(), path)
                    .effectiveEffectId();
            },
            [this](const QString& effectId) {
                // Mirrors acceptableShaderEffectId's
                // membership gate, including its
                // startup grace: an unscanned
                // registry answers "known" for
                // everything, because refusing
                // every set during that window
                // would be worse than accepting
                // one that the write then checks
                // again anyway.
                if (effectId.isEmpty() || m_shaderRegistry == nullptr || m_shaderRegistry->effectIds().isEmpty()) {
                    return true;
                }
                return m_shaderRegistry->hasEffect(effectId);
            }),
        this);
    // Live-preview data source for the shader browser's detail dialog. Both
    // borrows are the controller's own, so the lifetimes already agree.
    m_preview = new AnimationPreviewController(shaderRegistry, settings, this);

    m_lastHadPendingChanges = hasPendingChanges();
    m_lastStockSuppressedEvents = stockSuppressedEvents();
    // CLAUDE.md: only emit a signal when the value actually changed. The
    // sub-services and the mutators raise pendingChangesChanged unconditionally
    // (a no-op revert, a refused write), so gate the outward dirtyChanged on an
    // observed state flip rather than forwarding every raise.
    connect(this, &AnimationsPageController::pendingChangesChanged, this, [this]() {
        const bool current = hasPendingChanges();
        if (current == m_lastHadPendingChanges)
            return;
        m_lastHadPendingChanges = current;
        Q_EMIT dirtyChanged();
    });

    connect(m_presets, &AnimationPresetLibrary::userPresetsChanged, this,
            &AnimationsPageController::userPresetsChanged);
    connect(m_presets, &AnimationPresetLibrary::toastRequested, this, &AnimationsPageController::toastRequested);
    connect(m_presets, &AnimationPresetLibrary::pendingChangesChanged, this,
            &AnimationsPageController::pendingChangesChanged);
    connect(m_motionSets, &ShaderSetStore::pendingChangesChanged, this,
            &AnimationsPageController::pendingChangesChanged);
    // A set's `active` flag is derived from the live state of every event it
    // covers, so it goes stale whenever one of those is edited anywhere else.
    //
    // BOTH halves, because a motion set carries both. The timing half moves on
    // `overrideChanged` and the pack half on `shaderProfileChanged`, and only
    // the first was wired: assigning a pack left every row's badge showing
    // whatever it said before, so a set the user had just made current never
    // lit up and one they had just edited away from stayed lit. Harmless while
    // a set carried timing alone, which is why it survived — the pack half
    // could not affect the flag it was missing from.
    //
    // The decoration domain has one signal for its whole tree
    // (`DecorationPageController::profilesChanged`), so it has never had a
    // half to forget. This is the animation side's two-signal tax.
    connect(this, &AnimationsPageController::overrideChanged, m_motionSets, &ShaderSetStore::notifyLiveStateChanged);
    connect(this, &AnimationsPageController::shaderProfileChanged, m_motionSets,
            &ShaderSetStore::notifyLiveStateChanged);
    // A set apply's per-path overrideChanged emissions ride through the
    // writeOverride callback (which the controller wires to its own
    // setOverride). ShaderSetStore therefore exposes no overrideChanged
    // signal — the controller is the single source of truth for it.

    if (m_shaderRegistry) {
        connect(m_shaderRegistry, &PhosphorAnimationShaders::AnimationShaderRegistry::effectsChanged, this,
                &AnimationsPageController::shaderEffectsChanged);
        // A registry rescan can flip a pack's validity / contract class,
        // which is one of the stock-suppression gate's inputs.
        connect(m_shaderRegistry, &PhosphorAnimationShaders::AnimationShaderRegistry::effectsChanged, this,
                &AnimationsPageController::maybeEmitStockSuppressedEventsChanged);
    }
    if (m_settings) {
        // Sole emitter of pendingChangesChanged for shader-tree edits: EVERY
        // tree change reaches this NOTIFY. Not all of them through the setter,
        // though — our own mutators and the scoped Discard go through
        // Settings::setShaderProfileTree, while a reload, a profile switch and a
        // whole-tree Discard re-fire it from Settings' own notify-property loop
        // (`shaderProfileTreeJson` carries this signal). What matters here is
        // that this lambda is bound to the SIGNAL rather than to the setter, so
        // it sees all of them either way. Because dirtiness
        // is now value-based (hasPendingChanges diffs live-vs-committed), the
        // lambda no longer distinguishes own-writes from reloads or touches any
        // flag — it just refreshes the cards and re-evaluates the dirty state.
        //
        // There is no guard here, and none is owed: the async discard worker
        // this once had to step around went with the per-file staging, so
        // Discard is now a synchronous Settings::load() and every NOTIFY it
        // fires is one this lambda should act on.
        connect(
            m_settings, &ISettings::shaderProfileTreeChanged, this,
            [this]() {
                // Path-agnostic broadcast — the tree is a single Q_PROPERTY so we
                // can't tell which path moved without diffing, and a change we
                // did not make (a Discard, a profile switch, a set apply) really
                // could have moved anything.
                //
                // Gated on the depth counter, like the timing arm below: a
                // group write this controller made announces its own paths, and
                // broadcasting over it would defeat the cards' path filter at
                // drag rate — which is exactly when the parameter sliders reach
                // that writer.
                if (m_selfShaderWriteDepth == 0) {
                    Q_EMIT shaderProfileChanged(QString());
                }
                // The live tree moved, so the value-based dirty compare must
                // be re-run on the next hasPendingChanges() query.
                m_treeDirtyCache.reset();
                // Tree assignment is the primary input of the stock-suppression
                // gate (see stockSuppressedEvents).
                maybeEmitStockSuppressedEventsChanged();
                Q_EMIT pendingChangesChanged();
            },
            Qt::DirectConnection);
        // The TIMING half of the same dirty state. Without this the memoised
        // verdict never learns the tree moved, so a page with real unsaved
        // timing edits reports itself clean and the footer never appears.
        connect(
            m_settings, &ISettings::motionProfileTreeChanged, this,
            [this]() {
                m_treeDirtyCache.reset();
                Q_EMIT pendingChangesChanged();

                // Cards do not refresh on `pendingChangesChanged` (it is just
                // the flag), so without a broadcast here every timing write
                // this controller did NOT make — a Settings::load() on
                // Discard, a settings-profile activation, a motion-set apply,
                // the v8 migration — left every card showing its pre-change
                // duration, curve, override toggle and inherit breadcrumb,
                // with the next edit committing the stale value back. The
                // shader tree's twin above already relays exactly this.
                //
                // Gated on the depth counter so a write this controller DID
                // make keeps its per-path emissions and the prefix filter
                // cards apply to them. Broadcasting unconditionally would
                // defeat that filter and make every visible card re-walk its
                // whole chain on every slider tick.
                if (m_selfTreeWriteDepth == 0) {
                    Q_EMIT overrideChanged(QString());
                }
            },
            Qt::DirectConnection);

        // The animations master toggle gates the whole suppression predicate:
        // with animations off no pack owns any event and every stock effect
        // is (re)loaded, so the conflict chip must come back.
        connect(m_settings, &ISettings::animationsEnabledChanged, this,
                &AnimationsPageController::maybeEmitStockSuppressedEventsChanged);
    }
}

AnimationsPageController::~AnimationsPageController() = default;

// Slider bounds for the spring editor: a deliberately narrower, usable
// subset of the engine clamp range (Spring clamps omega to [0.1, 200] and
// zeta to [0, 10]) — see the header's declaration block for the perceptual
// rationale. The engine clamp, not the slider, is the validity boundary.
qreal AnimationsPageController::springOmegaMin() const
{
    return 1.0;
}

qreal AnimationsPageController::springOmegaMax() const
{
    return 40.0;
}

qreal AnimationsPageController::springZetaMin() const
{
    return 0.1;
}

qreal AnimationsPageController::springZetaMax() const
{
    return 4.0;
}

QObject* AnimationsPageController::previewController() const
{
    return m_preview;
}

QString AnimationsPageController::previewKind() const
{
    return QStringLiteral("animation");
}

void AnimationsPageController::setUserProfilesDirOverride(const QString& dir)
{
    m_userProfilesDirOverride = dir;
}

bool AnimationsPageController::isValidEventPath(const QString& path) const
{
    if (path.isEmpty())
        return false;
    // Defensive prefilter — `allBuiltInPaths()` doesn't contain any of
    // these characters so membership alone would be enough, but the
    // explicit check keeps the security intent visible at the call site.
    if (path.contains(QLatin1Char('/')) || path.contains(QLatin1Char('\\')) || path.contains(QLatin1String("..")))
        return false;
    static const QSet<QString> kKnownPathSet = []() {
        const QStringList paths = PhosphorAnimation::ProfilePaths::allBuiltInPaths();
        return QSet<QString>(paths.cbegin(), paths.cend());
    }();
    return kKnownPathSet.contains(path);
}

// ─── Pending-changes ───────────────────────────────────────────────────

bool AnimationsPageController::hasPendingChanges() const
{
    // Value-based, not a sticky flag: a tree is dirty exactly when the live
    // value differs from the committed baseline. This lets a per-page kebab
    // revert only ONE surface's paths and have hasPendingChanges() report the
    // truth — a sticky bool would either stay set after a scoped revert
    // (phantom "unsaved changes" footer) or get cleared wholesale by an
    // external scoped write (dropping OTHER pages' still-pending edits).
    //
    // BOTH halves of an event are covered, because both are config: the pack
    // in `Animations/ShaderProfileTree` and the timing in
    // `Animations/MotionProfileTree`. Until schema v8 the timing half was
    // per-event FILES with no baseline to diff, so it needed a snapshot map,
    // an async restore worker and a disk memo in front of them. None of that
    // survived the move.
    if (m_settings == nullptr)
        return false;
    // The compare is the expensive half: each side is a config-store read plus
    // a parse, and this predicate runs several times per mutation at
    // slider-drag rate. Memoised; invalidated wherever either side can move —
    // the live values via the tree-changed lambdas in the ctor, the committed
    // baseline via refreshDirtyState().
    if (!m_treeDirtyCache.has_value()) {
        m_treeDirtyCache = m_settings->shaderProfileTree() != m_settings->committedShaderProfileTree()
            || m_settings->motionProfileTree() != m_settings->committedMotionProfileTree();
    }
    return *m_treeDirtyCache;
}

void AnimationsPageController::maybeEmitStockSuppressedEventsChanged()
{
    // Every other dirty signal in this controller is flip-gated; this NOTIFY
    // used to be the one unguarded emitter, re-running the rule editor's
    // conflict-chip bindings on every tree edit even though the computed list
    // can only ever contain the minimize/maximize pair.
    const QStringList current = stockSuppressedEvents();
    if (current == m_lastStockSuppressedEvents)
        return;
    m_lastStockSuppressedEvents = current;
    Q_EMIT stockSuppressedEventsChanged();
}

bool AnimationsPageController::isDirty() const
{
    return hasPendingChanges();
}

void AnimationsPageController::apply()
{
    commitPending();
    // commitPending is synchronous — every per-edit write already landed in
    // config through setOverride, and dirtiness is value-based against the
    // committed baseline. Signal completion immediately so the chrome's
    // applyAllAsync wait-counter ticks down.
    Q_EMIT applyResult(true, QString());
}

void AnimationsPageController::discard()
{
    // Nothing here touches the filesystem, so there is no worker to dispatch:
    // `Settings::load()` in the caller does the reverting and this drops the
    // page's own view of it. The inherited discardResult still goes out so the
    // chrome's wait-counter ticks down.
    revertPending();
    Q_EMIT discardResult(true, QString());
}

void AnimationsPageController::commitPending()
{
    const bool had = hasPendingChanges();
    // No shader-tree flag to clear: tree dirtiness is value-based now. The tree's
    // committed baseline is refreshed by Settings::save() (captureBaseline), which
    // runs in the same apply pass; SettingsController::save() calls
    // refreshDirtyState() afterwards so this controller re-evaluates once the
    // baseline has caught up (apply() may run before the settings save in the
    // domain walk, leaving the tree transiently "divergent" here).
    if (had)
        Q_EMIT pendingChangesChanged();
}

void AnimationsPageController::refreshDirtyState()
{
    // Poke the value-based dirty check after an external commit point the
    // controller can't observe on its own — specifically Settings::save()'s
    // captureBaseline, which makes committedShaderProfileTree() catch up to the
    // live tree without firing shaderProfileTreeChanged (the value didn't move,
    // only the baseline did). The pendingChangesChanged → dirtyChanged forwarder
    // gates on an actual flip, so a no-op refresh costs nothing.
    //
    // Baseline moved means the memoised tree-dirty verdict is stale.
    m_treeDirtyCache.reset();
    Q_EMIT pendingChangesChanged();
}

bool AnimationsPageController::revertPending()
{
    // discard() / revertPending() is the StagingDomain contract for "undo
    // everything since the last apply". Every value this page writes is a
    // config key, so the actual revert is `Settings::load()` in the caller;
    // this drops the page's own memo of the dirty verdict and re-emits so an
    // OPEN page rebinds.
    //
    // IMPORTANT CALLER CONTRACT: the in-memory trees on m_settings are NOT
    // reverted here — that state is owned by Settings and is refreshed only by
    // a subsequent Settings::load(). SettingsController::discard() pairs
    // discard() with a follow-up load(); any future direct caller MUST do the
    // same, otherwise hasPendingChanges() returns false while m_settings still
    // holds unsaved edits.
    m_treeDirtyCache.reset();
    // Empty path is the tree-wide reload broadcast the cards already
    // understand (AnimationEventCard's `_pathAffectsThisCard` returns true for
    // it), which is right here: a reload can have moved any path.
    Q_EMIT overrideChanged(QString());
    Q_EMIT shaderProfileChanged(QString());
    Q_EMIT userPresetsChanged();
    if (m_motionSets != nullptr)
        m_motionSets->notifyLiveStateChanged();
    Q_EMIT pendingChangesChanged();
    return true;
}

bool AnimationsPageController::revertPendingUnder(const QStringList& eventPaths)
{
    // Scoped sibling for the per-page kebab Discard: restore ONLY the timing
    // overrides at eventPaths from the committed baseline, leaving every other
    // page's staged edits pending. One tree write for the batch.
    if (m_settings == nullptr)
        return false;
    const QVariantMap committed = m_settings->committedMotionProfileTree();
    QVariantMap live = m_settings->motionProfileTree();
    QStringList restored;
    for (const QString& path : eventPaths) {
        if (!isValidEventPath(path))
            continue;
        const QJsonObject baseline = treeProfileForPath(committed, path);
        if (treeProfileForPath(live, path) == baseline)
            continue; // not staged
        live = treeWithOverrideForPath(live, path, baseline);
        restored.append(path);
    }
    if (restored.isEmpty())
        return true;
    writeMotionTree(live);
    m_treeDirtyCache.reset();
    for (const QString& path : restored)
        Q_EMIT overrideChanged(path);
    Q_EMIT pendingChangesChanged();
    return true;
}

// ─── Path discovery ────────────────────────────────────────────────────
// `eventPathAcceptsWindowRules`, `sectionForPath`, `eventLabel`, `parentChain`
// live in `animationspagecontroller_paths.cpp`. Same class, separate TU, no
// API change.

QVariantList AnimationsPageController::eventSections() const
{
    using namespace PhosphorAnimation;
    // The event taxonomy is static for the process lifetime; cache the
    // materialised QVariantList in a mutable member so QML rebindings
    // skip the O(n) rebuild after the first call. Computed lazily on
    // first read rather than at construction because the helpers it
    // calls (sectionForPath, eventLabel) are const member functions
    // that need `this`.
    if (!m_eventSectionsCache.isEmpty()) {
        return m_eventSectionsCache;
    }

    const QStringList paths = ProfilePaths::allBuiltInPaths();

    // Pre-compute the set of paths that are some other path's parent —
    // any path X with a child Y such that parentPath(Y) == X. Drives the
    // isCategory flag below in O(1) per row instead of an O(n) scan
    // (which made eventSections O(n²) overall and lit up profiler runs
    // on the first drilldown evaluation).
    QSet<QString> parentPaths;
    parentPaths.reserve(paths.size());
    for (const QString& path : paths) {
        const QString parent = ProfilePaths::parentPath(path);
        if (!parent.isEmpty())
            parentPaths.insert(parent);
    }

    // Track section insertion order via a parallel list; QHash would lose
    // taxonomy ordering and the QML drilldown should mirror header order.
    QStringList sectionOrder;
    QHash<QString, QVariantList> sectionPaths;

    for (const QString& path : paths) {
        const QString section = sectionForPath(path);
        if (!sectionPaths.contains(section)) {
            sectionOrder.append(section);
        }
        QVariantMap entry;
        entry.insert(QStringLiteral("path"), path);
        entry.insert(QStringLiteral("label"), eventLabel(path));
        entry.insert(QStringLiteral("parent"), ProfilePaths::parentPath(path));
        // A "category" path is one whose label sits at a section/sub-
        // section root (e.g. "window", "popup") rather than a leaf
        // event — i.e. another built-in path uses it as parent.
        entry.insert(QStringLiteral("isCategory"), parentPaths.contains(path));
        // Whether a per-window Rule can reach this event at all. True for
        // exactly ten leaves — the four `window.appearance` leaves, the five
        // `window.movement` leaves, and `scrolling.tabSwitch` — and false for
        // EVERY other path, including the `window` category nodes above those
        // leaves. `ProfilePaths::eventPathResolvesPerWindow` is the authority;
        // the rest (the desktop switches, the scrolling strip, the OSDs, the
        // popups, the panels, the cursor, the editor's own actions and widgets,
        // the whole shell subtree) are resolved without a window, and a rule's
        // animation action is matched against one. Carried on the entry so the
        // rule editor's picker can filter on data it already receives instead of
        // spelling a second copy of the list in QML.
        entry.insert(QStringLiteral("acceptsWindowRules"), ProfilePaths::eventPathResolvesPerWindow(path));
        sectionPaths[section].append(entry);
    }

    QVariantList result;
    result.reserve(sectionOrder.size());
    for (const QString& section : sectionOrder) {
        QVariantMap sectionEntry;
        sectionEntry.insert(QStringLiteral("section"), section);
        sectionEntry.insert(QStringLiteral("label"), segmentLabel(section));
        sectionEntry.insert(QStringLiteral("paths"), sectionPaths.value(section));
        result.append(sectionEntry);
    }
    m_eventSectionsCache = result;
    return m_eventSectionsCache;
}

QVariantList AnimationsPageController::userPresets() const
{
    return m_presets ? m_presets->userPresets() : QVariantList{};
}

bool AnimationsPageController::addUserPreset(const QString& name, const QVariantMap& profileJson)
{
    return m_presets && m_presets->addUserPreset(name, profileJson);
}

bool AnimationsPageController::removeUserPreset(const QString& name)
{
    return m_presets && m_presets->removeUserPreset(name);
}

// Motion sets live entirely in the shared ShaderSetStore reached through
// `setsBridge()` — QML talks to it directly.

} // namespace PlasmaZones
