// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

// AnimationsPageController group-write methods: the per-field merge, the
// per-field clear, the shader-leg group mutators and the divergence measure
// that an event card applies across its whole write-path group (its own event
// path plus its declared mirrors).
//
// Same class as animationspagecontroller.cpp, separate TU, no API change.
// These were JS loops inside AnimationEventCard.qml until the write policy and
// `rawProfile`'s drop-versus-substitute rules had to be kept in step across two
// languages. See the "Group writes" block in the header for the full rationale
// and for what deliberately stayed in QML.

#include "animationspagecontroller.h"

#include "animations_controller_detail.h"
#include "core/interfaces/isettings.h"
#include "core/platform/logging.h"
#include "phosphor_i18n.h"

#include <PhosphorAnimation/Profile.h>
#include <PhosphorAnimation/ShaderProfileTree.h>

#include <QJsonDocument>
#include <QList>
#include <QPair>
#include <QJsonObject>
#include <QVariant>

namespace PlasmaZones {

using namespace animations_controller_detail;

namespace {

/// Canonical form of one path's comparable stored state, as a stable byte
/// string. Built from a QJsonObject rather than by concatenating values so key
/// ordering is the object's own (sorted) order and two paths holding the same
/// values always serialise identically.
QByteArray comparableStateKey(const QVariantMap& profile, const QVariantMap& shader, bool compareCurve)
{
    QJsonObject compared;
    const auto durationIt = profile.constFind(QLatin1String(PhosphorAnimation::Profile::JsonFieldDuration));
    if (durationIt != profile.constEnd())
        compared.insert(QLatin1String(PhosphorAnimation::Profile::JsonFieldDuration),
                        QJsonValue::fromVariant(durationIt.value()));
    if (compareCurve) {
        const auto curveIt = profile.constFind(QLatin1String(PhosphorAnimation::Profile::JsonFieldCurve));
        if (curveIt != profile.constEnd())
            compared.insert(QLatin1String(PhosphorAnimation::Profile::JsonFieldCurve),
                            QJsonValue::fromVariant(curveIt.value()));
    }

    QJsonObject root;
    root.insert(QLatin1String("timing"), compared);
    root.insert(QLatin1String("shader"), QJsonObject::fromVariantMap(shader));
    return QJsonDocument(root).toJson(QJsonDocument::Compact);
}

/// The caller's path list with duplicates removed, preserving order.
///
/// QML builds a card's write group as `[eventPath].concat(mirrorPaths)` with no
/// dedup, so a card naming its own event path as a mirror hands the same file in
/// twice — which double-counts `changed`, pays a second write, and inflates the
/// divergence banner. Order is preserved because the primary must stay first.
///
/// LINEAR, via a seen-set. The obvious `if (!out.contains(path))` shape is
/// O(n^2) in the CALLER's list, and this runs as the first statement of every
/// group writer, BEFORE `isValidEventPath` can reject anything — so it would be
/// the one step a 20k-entry Q_INVOKABLE call could still make quadratic on the
/// GUI thread, which is exactly the dedup the header calls free.
QStringList distinctPaths(const QStringList& paths)
{
    QStringList out;
    out.reserve(paths.size());
    QSet<QString> seen;
    seen.reserve(paths.size());
    for (const QString& path : paths) {
        if (!seen.contains(path)) {
            seen.insert(path);
            out.append(path);
        }
    }
    return out;
}

} // namespace

bool AnimationsPageController::setOverrideMergedOnPaths(const QStringList& rawPaths, const QVariantMap& fields,
                                                        const QVariant& curveFromCommit)
{
    const QStringList paths = distinctPaths(rawPaths);
    if (m_asyncRevertInFlight) {
        // Refused as a whole, like every sibling group writer. Without this the
        // per-path `setOverride` calls refuse individually, `allWritten` goes
        // false, and a duration drag mid-discard silently does nothing — the
        // slider just snaps back on the next refresh with no explanation.
        qCWarning(lcConfig) << "setOverrideMergedOnPaths: refusing while an async discard is in flight";
        Q_EMIT toastRequested(PhosphorI18n::tr("Cannot change this while a discard is in progress."));
        return false;
    }

    // An invalid QVariant is QML's `undefined` arriving here, and it is the
    // signal for "the user did not touch the curve". Distinguished from a valid
    // empty string, which is a real (if unusual) authored value.
    const bool curveEdited = curveFromCommit.isValid() && !curveFromCommit.isNull();
    const QString editedCurve = curveEdited ? curveFromCommit.toString() : QString();

    // Every path's stored profile is read BEFORE the first write. `setOverride`
    // invalidates the whole disk memo, so reading inside the write loop would
    // miss the memo on every iteration after the first and cost one real file
    // open per path per call — and this runs at drag rate. The paths are
    // distinct files and none of the writes can affect another's stored
    // content, so reading them all up front is equivalent.
    QList<QVariantMap> bases;
    bases.reserve(paths.size());
    for (const QString& path : paths)
        bases.append(rawProfile(path));

    const QLatin1String curveKey(PhosphorAnimation::Profile::JsonFieldCurve);
    bool allWritten = true;
    for (int i = 0; i < paths.size(); ++i) {
        const QVariantMap& base = bases.at(i);
        QVariantMap merged = base;
        for (auto it = fields.constBegin(); it != fields.constEnd(); ++it)
            merged.insert(it.key(), it.value());

        // The curve is the one field with three outcomes rather than two, and
        // the decision is made against the path's PRE-merge stored profile.
        // Reading it post-merge would let a `fields` entry stand in for "this
        // path's own curve" and travel to every path, which is exactly the
        // "the card must not decide a curve on the user's behalf" rule this
        // parameter exists to enforce.
        if (curveEdited) {
            merged.insert(curveKey, editedCurve);
        } else {
            const QVariant own = base.value(curveKey);
            if (own.metaType().id() == QMetaType::QString && !own.toString().isEmpty()) {
                // Restore this path's own curve over anything `fields` carried.
                merged.insert(curveKey, own);
            } else {
                // No curve of its own, so none is written. The remove() matters
                // for two inputs: a curve supplied through `fields`, and a
                // stored curve that is present-but-empty. Either would land as
                // an engaged value that BLOCKS inheritance, so the path would
                // stop following its parent's curve without the user asking.
                merged.remove(curveKey);
            }
        }

        if (!setOverride(paths.at(i), merged))
            allWritten = false;
    }
    return allWritten;
}

int AnimationsPageController::clearFieldOnPaths(const QStringList& rawPaths, const QString& field)
{
    const QStringList paths = distinctPaths(rawPaths);
    // Allowlisted rather than passed through to the JSON: this removes a key
    // from a file, and the only two fields a card's revert links own are the
    // timing pair. Anything else reaching here would be a caller bug, and
    // silently honouring it could strip a motion set's fields.
    if (field != QLatin1String(PhosphorAnimation::Profile::JsonFieldCurve)
        && field != QLatin1String(PhosphorAnimation::Profile::JsonFieldDuration)) {
        qCWarning(lcConfig) << "clearFieldOnPaths: refusing to clear unrecognised field" << field;
        return -1;
    }
    // Refused outright while the discard worker owns the snapshot map, for the
    // same reason clearAllOverrides refuses: every write below would fail
    // individually and the caller would read the resulting 0 as "the field was
    // already inherited everywhere" rather than "nothing happened".
    if (m_asyncRevertInFlight) {
        qCWarning(lcConfig) << "clearFieldOnPaths: refusing while an async discard is in flight";
        Q_EMIT toastRequested(PhosphorI18n::tr("Cannot change this while a discard is in progress."));
        return -1;
    }

    // TWO passes, not one interleaved loop. `setOverride` emits its
    // `overrideChanged` synchronously, while `removeOverrideFile` emits nothing
    // and relies on the batch's single `refreshProfileStore()` below. Mixed in
    // one loop, a rewritten path's signal fires while an already-deleted path's
    // file is gone from disk but its entry is still live in the profile
    // registry — and `resolvedProfile` falls through to the registry exactly
    // when the disk read comes back empty, which is the stale-inherited-value
    // bug the disk-first read exists to close. Classify, remove, refresh, then
    // write: every signal is emitted against a settled store.
    const bool wasPending = hasPendingChanges();
    QStringList toRemove;
    QList<QPair<QString, QVariantMap>> toRewrite;
    for (const QString& path : paths) {
        QVariantMap raw = rawProfile(path);
        if (!raw.contains(field))
            continue;
        raw.remove(field);
        if (raw.isEmpty())
            toRemove.append(path);
        else
            toRewrite.append({path, raw});
    }

    QStringList removed;
    int changed = 0;
    int failed = 0;
    // Tracks the classification-time-present / removal-time-absent race, so the
    // registry catch-up below still runs when it is the only thing that happened.
    bool sawAbsent = false;
    for (const QString& path : toRemove) {
        // An override with nothing left in it is removed outright. An empty
        // file and no file resolve identically, but the card's toggle and the
        // pending-changes walk both key on the file existing.
        //
        // removeOverrideFile rather than clearOverride: the latter rescans the
        // whole profiles directory per call, so a mirrored card's single revert
        // click paid one full re-read and re-parse per path.
        switch (removeOverrideFile(path)) {
        case OverrideFileRemoval::Removed:
            removed.append(path);
            ++changed;
            break;
        case OverrideFileRemoval::Absent:
            // rawProfile said the field was there, so the file existed a moment
            // ago. Something else removed it in between; the desired end state
            // holds either way, but the REGISTRY may still hold the vanished
            // file's entry, so this still owes a rescan.
            sawAbsent = true;
            break;
        case OverrideFileRemoval::Failed:
            ++failed;
            break;
        }
    }

    // ONE rescan for every removal, BEFORE any signal — including the ones
    // `setOverride` emits for itself in the rewrite pass below.
    // Also refreshed when every removal reported Absent. A file present at
    // classification time can vanish before `removeOverrideFile` runs, and then
    // `removed` is empty while the profile registry still holds an entry for a
    // file that is gone — which is exactly when `resolvedProfile` falls through
    // to the registry, so it would serve the stale inherited value.
    if (!removed.isEmpty() || sawAbsent)
        refreshProfileStore();

    for (const auto& [path, raw] : toRewrite) {
        // setOverride owns its own invalidate and its own signals, and performs
        // no rescan (a write is covered by the disk-first read).
        if (setOverride(path, raw))
            ++changed;
        else
            ++failed;
    }

    // Sampled after every mutation, like both clear paths.
    const bool nowPending = hasPendingChanges();
    for (const QString& path : removed)
        Q_EMIT overrideChanged(path);
    // One dirty signal for the batch's net flip, NOT gated on whether a rewrite
    // ran. A rewrite's `setOverride` emits only when IT observes a flip, and it
    // samples pending state after the removal pass has already made the page
    // dirty — so in a mixed batch neither the rewrite nor a `toRewrite`-gated
    // batch emit fires, and the clean→dirty transition is lost entirely.
    //
    // A redundant emit costs nothing: `pendingChangesChanged` is a "may have
    // changed" signal, and the ctor's forwarder gates the outward `dirtyChanged`
    // on an observed flip of `m_lastHadPendingChanges`.
    if (wasPending != nowPending)
        Q_EMIT pendingChangesChanged();
    if (failed > 0) {
        qCWarning(lcConfig) << "clearFieldOnPaths:" << failed << "paths could not be updated";
        Q_EMIT toastRequested(PhosphorI18n::tr("Some animation overrides could not be reverted."));
        return -1;
    }
    return changed;
}

bool AnimationsPageController::anyPathSupportsShaderLeg(const QStringList& rawPaths) const
{
    const QStringList paths = distinctPaths(rawPaths);
    for (const QString& path : paths) {
        if (supportsShaderLeg(path))
            return true;
    }
    return false;
}

bool AnimationsPageController::allPathsHoldShaderEffect(const QStringList& rawPaths, const QString& effectId) const
{
    // An empty list is FALSE, not a vacuous true. "Every path already carries
    // this effect" reads as "at least one does" at the call site, and this is a
    // Q_INVOKABLE with no other gate in front of it.
    if (rawPaths.isEmpty()) {
        return false;
    }
    const QStringList paths = distinctPaths(rawPaths);
    using namespace PhosphorAnimationShaders;
    if (!m_settings)
        return false;
    // ONE tree read for the whole group, like divergentPathCount — the header's
    // rule is that nothing here calls `rawShaderProfile` in a loop, because each
    // call rebuilds the tree.
    const ShaderProfileTree tree = m_settings->shaderProfileTree();
    for (const QString& path : paths) {
        // Gated, so an unrecognised path cannot make the caller's list the bound
        // on the work done here. A non-supporting path can never hold an id.
        if (!isValidEventPath(path) || !supportsShaderLeg(path))
            return false;
        const QVariantMap raw = tree.hasOverride(path) ? shaderProfileToMap(tree.directOverride(path)) : QVariantMap();
        const auto it = raw.constFind(JsonEffectIdKey);
        // Absent effectId means no direct override, which is never equal to a
        // stored one — not even to the engaged-empty sentinel.
        if (it == raw.constEnd() || it.value().metaType().id() != QMetaType::QString)
            return false;
        if (it.value().toString() != effectId)
            return false;
    }
    return true;
}

int AnimationsPageController::setShaderOverrideOnPaths(const QStringList& rawPaths, const QString& effectId,
                                                       const QVariantMap& parameters)
{
    const QStringList paths = distinctPaths(rawPaths);
    using namespace PhosphorAnimationShaders;
    if (!m_settings)
        return 0;
    if (m_asyncRevertInFlight) {
        // Refused as a whole, like clearFieldOnPaths and the descendant clear:
        // the per-path calls would refuse individually and the caller would
        // read the resulting 0 as "nothing to write".
        qCWarning(lcConfig) << "setShaderOverrideOnPaths: refusing while an async discard is in flight";
        Q_EMIT toastRequested(PhosphorI18n::tr("Cannot change this while a discard is in progress."));
        return -1;
    }

    // The SAME boundary checks the per-path `setShaderOverride` performs. Not
    // optional: this is the only path QML uses, so skipping them left the id
    // that reaches the persisted tree entirely unvalidated. Checked once
    // because an id is per-call, not per-path.
    if (!acceptableShaderEffectId(effectId, QLatin1String("setShaderOverrideOnPaths"))) {
        return -1;
    }

    // ONE tree read, every path applied into it, ONE write. Going through
    // `setShaderOverride` per path instead cost a full tree rebuild AND a full
    // settings write per path — and each write fires a path-agnostic
    // `shaderProfileChanged("")` that refreshes every visible card. This is
    // reached from the shader param sliders, so it runs at drag rate.
    //
    // Writing once also means the group is applied ATOMICALLY: no card can
    // observe a half-written group and latch a divergence banner that the next
    // path's write immediately clears.
    ShaderProfileTree tree = m_settings->shaderProfileTree();
    int written = 0;
    int mutated = 0;
    for (const QString& path : paths) {
        // Skipped, not attempted: a non-supporting path is rejected downstream
        // anyway, and skipping keeps the warning out of the log for a call that
        // was never going to land. The divergence measure omits the shader axis
        // for these same paths, so the two together keep the banner off for a
        // group mixing supporting and non-supporting paths.
        if (!isValidEventPath(path) || !supportsShaderLeg(path))
            continue;
        ShaderProfile profile;
        profile.effectId = effectId;
        if (!parameters.isEmpty())
            profile.parameters = parameters;
        // Compare-and-skip, like the per-path setter: a param slider that lands
        // back on its current value, or a Reset that was already at defaults,
        // would otherwise still pay the settings write and the page-wide
        // broadcast below. Counted as written either way, because the requested
        // end state holds for this path.
        if (tree.hasOverride(path) && tree.directOverride(path) == profile) {
            ++written;
            continue;
        }
        tree.setOverride(path, profile);
        ++written;
        ++mutated;
    }
    if (mutated > 0)
        m_settings->setShaderProfileTree(tree);
    return written;
}

int AnimationsPageController::clearShaderOverrideOnPaths(const QStringList& rawPaths)
{
    const QStringList paths = distinctPaths(rawPaths);
    using namespace PhosphorAnimationShaders;
    if (!m_settings)
        return 0;
    if (m_asyncRevertInFlight) {
        qCWarning(lcConfig) << "clearShaderOverrideOnPaths: refusing while an async discard is in flight";
        Q_EMIT toastRequested(PhosphorI18n::tr("Cannot change this while a discard is in progress."));
        return -1;
    }

    // One read, one write, for the same reasons as the setter above.
    // `isValidEventPath` gates the loop so an unrecognised path cannot make the
    // caller's list the bound on the work done here.
    ShaderProfileTree tree = m_settings->shaderProfileTree();
    int cleared = 0;
    for (const QString& path : paths) {
        if (!isValidEventPath(path) || !tree.hasOverride(path))
            continue;
        tree.clearOverride(path);
        ++cleared;
    }
    if (cleared > 0)
        m_settings->setShaderProfileTree(tree);
    return cleared;
}

int AnimationsPageController::clearShaderOverrideDescendantsOnPaths(const QStringList& rawPaths)
{
    const QStringList paths = distinctPaths(rawPaths);
    int cleared = 0;
    for (const QString& path : paths) {
        // Gated so an unrecognised path cannot cost a tree rebuild apiece; the
        // header's bound claim rests on this.
        if (!isValidEventPath(path))
            continue;
        const int n = clearShaderOverrideDescendants(path);
        // Never summed in. A -1 folded into the total would be
        // indistinguishable from a smaller successful clear, and the caller
        // needs to tell a refusal from a no-op to decide whether to close its
        // editor.
        if (n < 0)
            return -1;
        cleared += n;
    }
    return cleared;
}

int AnimationsPageController::divergentPathCount(const QString& primaryPath, const QStringList& rawMirrorPaths,
                                                 bool compareCurve) const
{
    // Gated like every sibling in this file. An unvalidated primary yields the
    // all-empty comparison key (rawProfile returns {}, supportsShaderLeg false),
    // so every valid mirror would read as divergent and the banner would latch at
    // a count no edit could ever clear.
    if (!isValidEventPath(primaryPath)) {
        return 0;
    }
    // Deduped, and the primary removed if it names itself: a repeat would be
    // counted twice in the banner's "%1 of the events" figure.
    QStringList mirrorPaths = distinctPaths(rawMirrorPaths);
    mirrorPaths.removeAll(primaryPath);
    if (mirrorPaths.isEmpty())
        return 0;

    // The shader tree is read ONCE for the whole comparison. `rawShaderProfile`
    // rebuilds it on every call (a settings read plus `ShaderProfileTree::
    // fromJson` plus a prune walk), and this runs from `refreshFromTree`, i.e.
    // on every tick of a duration drag for every visible card. Per path it was
    // a full rebuild each.
    const PhosphorAnimationShaders::ShaderProfileTree tree =
        m_settings ? m_settings->shaderProfileTree() : PhosphorAnimationShaders::ShaderProfileTree{};

    const auto keyFor = [this, compareCurve, &tree](const QString& path) {
        // The shader axis is compared only where a shader can actually be
        // stored. A non-supporting path holds nothing there permanently, so
        // comparing it against a supporting path's real leg would report a
        // divergence over an axis no control could converge.
        const QVariantMap shader = (supportsShaderLeg(path) && tree.hasOverride(path))
            ? shaderProfileToMap(tree.directOverride(path))
            : QVariantMap();
        return comparableStateKey(rawProfile(path), shader, compareCurve);
    };

    const QByteArray primary = keyFor(primaryPath);
    int diverged = 0;
    for (const QString& mirror : mirrorPaths) {
        if (keyFor(mirror) != primary)
            ++diverged;
    }
    // Plus one for the primary, which every diverging mirror differs FROM and
    // which the converging edit also rewrites. Zero when nothing diverges, so a
    // caller never renders a stale count.
    return diverged > 0 ? diverged + 1 : 0;
}

} // namespace PlasmaZones
