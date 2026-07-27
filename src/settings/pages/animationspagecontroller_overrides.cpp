// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

// Override-CRUD methods for AnimationsPageController. All methods here are
// members of the same class as animationspagecontroller.cpp — separate
// translation unit, no API change.
//
// Group covers:
//   * Path derivation (userProfilesDir / profileFilePath / userMotionSetsDir)
//   * Existence + read (hasOverride / rawProfile / resolvedProfile), including
//     the per-path disk-read memo (cachedDiskProfile / invalidateDiskProfileCache)
//   * Write + clear (setOverride, and the clear family: removeOverrideFile /
//     clearOverride / clearOverridesForPaths / clearAllOverrides /
//     clearOverridesUnder)
//   * Scoped dirty check (hasScopedPendingFiles)
//
// Sibling _shaders.cpp owns the shader-tree side; the main TU owns
// pending-changes tracking, async-revert, and the section catalog.

#include "animationspagecontroller.h"

#include "config/configdefaults.h"
#include "core/interfaces/isettings.h"
#include "core/platform/logging.h"
#include "phosphor_i18n.h"
#include "animations_controller_detail.h"

#include <PhosphorAnimation/PhosphorProfileRegistry.h>
#include <PhosphorAnimation/Profile.h>
#include <PhosphorAnimation/ProfilePaths.h>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLoggingCategory>
#include <QSaveFile>
#include <QStandardPaths>

namespace PlasmaZones {

using namespace animations_controller_detail;

QString AnimationsPageController::userProfilesDir() const
{
    if (!m_userProfilesDirOverride.isEmpty())
        return m_userProfilesDirOverride;
    const QString base = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
    return base + ConfigDefaults::userProfilesSubdir();
}

QString AnimationsPageController::profileFilePath(const QString& path) const
{
    // Filenames mirror the path (e.g. `editor.snapIn.json`) — same
    // convention as the daemon's shipped defaults. Validation on @p
    // path happens at every call site so this helper trusts its input;
    // explicit early-return guards against a future caller that forgets
    // the gate (Q_ASSERT alone is debug-only and would silently fall
    // through to a path-traversal file open in release builds).
    if (!isValidEventPath(path)) {
        qCWarning(lcConfig) << "profileFilePath: refusing to compute path for invalid event path" << path;
        return {};
    }
    return userProfilesDir() + QLatin1Char('/') + path + QStringLiteral(".json");
}

QString AnimationsPageController::userMotionSetsDir() const
{
    // Test-mode layout: production places profiles at
    //   `<XDG>/plasmazones/profiles`
    //   `<XDG>/plasmazones/motionsets`
    // i.e. two SIBLING dirs under a shared `plasmazones` root. The
    // `userProfilesDirOverride()` test hook substitutes a single tmp
    // root for the profiles dir directly (no `/profiles` suffix —
    // tests dump profile files into `tmp.path()` so they can introspect
    // straightforwardly), so we mirror that one-level-up layout by
    // returning `<override>/motionsets`. The override therefore models:
    //   `<override>`            = stand-in for `<XDG>/plasmazones/profiles`
    //   `<override>/motionsets` = stand-in for `<XDG>/plasmazones/motionsets`
    // Not perfectly parallel to production, but every test depends on
    // the "profile files at tmp root" convention so restructuring would
    // be a multi-test churn for no behavioural benefit.
    // The suffix is derived from the production accessor, which tracks a
    // rename of the LEAF segment only: mid() from lastIndexOf('/') keeps just
    // the last segment, and a separator-free accessor would make lastIndexOf
    // return -1 and mid(-1) yield the whole string. Both shapes are unreal
    // today (the accessor is "/plasmazones/motionsets").
    if (!m_userProfilesDirOverride.isEmpty()) {
        const QString subdir = ConfigDefaults::userMotionSetsSubdir();
        return m_userProfilesDirOverride + subdir.mid(subdir.lastIndexOf(QLatin1Char('/')));
    }
    const QString base = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
    return base + ConfigDefaults::userMotionSetsSubdir();
}

bool AnimationsPageController::hasOverride(const QString& path) const
{
    if (!isValidEventPath(path))
        return false;
    return QFileInfo::exists(profileFilePath(path));
}

QVariantMap AnimationsPageController::rawProfile(const QString& path) const
{
    if (!isValidEventPath(path))
        return {};
    // Sanitised on the same terms as `cachedDiskProfile`, because QML reads
    // BOTH for one card: `rawProfile` decides which fields the event owns (and
    // so whether the revert link shows), `resolvedProfile` supplies the number
    // beside it. Leaving this raw made a file with a rejected field render as
    // "Overridden for this event", with a live revert link, next to the
    // INHERITED value the sanitizer had substituted — the card contradicting
    // itself on the one screen this controller exists to make honest.
    // Through the memo, not a second read of the same file: QML calls this once
    // per write path on every refreshFromTree, which runs after every commit —
    // i.e. every tick of a duration drag. An uncached read here would falsify
    // `cachedDiskProfile`'s own "one read per path per mutation" contract.
    return cachedDiskProfile(path);
}

QVariantMap AnimationsPageController::cachedDiskProfile(const QString& path) const
{
    const auto cached = m_diskProfileCache.constFind(path);
    if (cached != m_diskProfileCache.constEnd()) {
        return cached.value();
    }
    QVariantMap fresh = sanitizedProfileMap(readProfileJson(profileFilePath(path)));
    m_diskProfileCache.insert(path, fresh);
    return fresh;
}

void AnimationsPageController::invalidateDiskProfileCache() const
{
    m_diskProfileCache.clear();
}

QVariantMap AnimationsPageController::resolvedProfile(const QString& path) const
{
    using namespace PhosphorAnimation;
    if (path.isEmpty())
        return {};

    QVariantMap merged;
    PhosphorProfileRegistry* registry = PhosphorProfileRegistry::defaultRegistry();

    QString cur = path;
    while (!cur.isEmpty()) {
        QVariantMap source;
        if (isValidEventPath(cur)) {
            // The user override files this controller writes are read FIRST,
            // ahead of the registry. The registry is fed by a ProfileLoader
            // watching this same directory, and that watch is debounced, so
            // for ~50 ms after any write the registry still holds the pre-edit
            // entry. The registry does catch up on its own once the debounce
            // lands; the PAGE does not, because its only re-read runs
            // synchronously inside the overrideChanged handler this mutation
            // is about to emit. Registry-first therefore handed that read the
            // state the user had just left. Disk is what this process wrote,
            // so disk is the answer. Skipped for non-event paths so a crafted
            // parent like "../" can't cause a stray file open.
            //
            // Cost: a level that HAD a registry entry used to resolve from RAM
            // with no filesystem I/O at all. Bounded by chain depth (four levels
            // at the deepest, e.g. window.movement.snapIn), but this function
            // backs a QML binding that every card in scope re-evaluates on each
            // overrideChanged, and setOverride emits one of those per write path
            // per slider tick — so the naive form costs (visible cards x 4)
            // synchronous opens and parses per tick of a duration drag. The
            // per-path cache below collapses that back to one read per path per
            // mutation; see `invalidateDiskProfileCache` for the invalidation
            // points. The alternative — keeping the RAM read and forcing a
            // rescan on writes too — costs a full directory re-parse per tick,
            // which is worse again.
            //
            // Sanitised, not raw: the registry path this replaces ran every file
            // through Profile::fromJson. See `sanitizedProfileMap`.
            source = cachedDiskProfile(cur);
        }
        if (source.isEmpty() && registry) {
            // No user file at this level: the registry supplies whatever the
            // hosting process registered and this controller cannot write —
            // bundled and system-dir profiles here. (A process that also seeds
            // shell animation families, i.e. the daemon, would see those too;
            // the settings app registers none.)
            const auto entry = registry->resolve(cur);
            if (entry.has_value()) {
                source = profileToVariantMap(*entry);
            }
        }
        mergeMissingFields(merged, source);
        cur = ProfilePaths::parentPath(cur);
    }

    // Seed the ROOT of the chain from the user's Global animation settings.
    // The daemon fills the same fields the same way at
    // kSettingsDrivenProfilePaths (daemon.cpp), including on the branch where
    // a user Global.json owns the path. It additionally carries presetName,
    // which ISettings exposes no accessor for and which is decorative. The walk above only sees user-authored profile
    // JSON, so without this the settings app resolves inheritance against library defaults and every card reports the
    // built-in 200 ms while the user's Global card shows their real value — the two disagree on one screen. Lowest
    // precedence: mergeMissingFields only fills fields no ancestor supplied, so a real override at any level still
    // wins.
    if (m_settings != nullptr) {
        // Every Global field ISettings exposes; presetName is daemon-side only
        // and has no ISettings accessor. minDistance / sequenceMode /
        // staggerInterval are all user-editable on the Global card, and seeding
        // only duration+curve left the other three resolving to library
        // defaults here while the daemon animated with the user's value — the
        // same "the two disagree on one screen" bug this seed exists to close,
        // left half-closed.
        using P = Profile;
        QVariantMap settingsGlobal;
        settingsGlobal.insert(QLatin1String(P::JsonFieldDuration), m_settings->animationDuration());
        settingsGlobal.insert(QLatin1String(P::JsonFieldMinDistance), m_settings->animationMinDistance());
        settingsGlobal.insert(QLatin1String(P::JsonFieldSequenceMode), m_settings->animationSequenceMode());
        settingsGlobal.insert(QLatin1String(P::JsonFieldStaggerInterval), m_settings->animationStaggerInterval());
        const QString curve = m_settings->animationEasingCurve();
        if (!curve.isEmpty()) {
            settingsGlobal.insert(QLatin1String(P::JsonFieldCurve), curve);
        }
        mergeMissingFields(merged, settingsGlobal);
    }

    fillLibraryDefaults(merged);
    return merged;
}

bool AnimationsPageController::setOverride(const QString& path, const QVariantMap& profileJson)
{
    if (!isValidEventPath(path))
        return false;

    // Refuse writes while a Discard worker is in flight — the worker
    // is concurrently rewriting profile files from the snapshot map
    // taken before the worker started. A simultaneous setOverride on
    // a path the worker is processing would race: last writer wins on
    // disk (non-deterministic), and the worker's finished handler
    // would drop this path's snapshot when it merges its results back,
    // silently losing the user's concurrent edit. The QML does NOT gate the
    // editor controls on `discarding` — only Main.qml's Apply/Discard buttons
    // are — so this is the sole guard, not defence-in-depth behind a UI one.
    if (m_asyncRevertInFlight) {
        qCWarning(lcConfig) << "setOverride: refusing write while async discard is in flight; path=" << path;
        return false;
    }

    const QString dir = userProfilesDir();
    if (!QDir().mkpath(dir)) {
        // Without this log a read-only or full ~/.local/share turns every
        // animation edit into a silent no-op: the group writer folds the
        // false into allWritten with no toast of its own, so the slider
        // snaps back with nothing in the journal.
        qCWarning(lcConfig) << "setOverride: cannot create profiles directory" << dir;
        return false;
    }
    // Capture dirty state AFTER the cheap validity / mkpath guards so an
    // invalid path doesn't pay for a hasPendingChanges() walk.
    const bool wasPending = hasPendingChanges();

    const QString filePath = profileFilePath(path);

    // Strip the name field for the equality compare against on-disk content
    // (`readProfileJson` strips it too). Same `obj` is later given the name
    // back for the write — the canonical name is always the path.
    QJsonObject obj = QJsonObject::fromVariantMap(profileJson);
    obj.remove(JsonNameKey);
    // Deliberately UNCACHED (bypasses cachedDiskProfile): the memo stores the
    // sanitized map, which cannot serve this byte-exact raw-object equality
    // compare. This is the one extra read per write path per mutation on top
    // of the memo contract's "one read per path per mutation".
    const QJsonObject existing = readProfileJson(filePath);
    if (existing == obj) {
        // Round-trip with no real change — bail early so a QML two-way
        // binding cycle doesn't dirty the page or fire spurious
        // overrideChanged / pendingChangesChanged emissions.
        return true;
    }
    obj.insert(JsonNameKey, path);

    // Snapshot before touching disk, and bail if the pre-edit content could not
    // be captured: an unrecoverable revert is worse than a failed write.
    if (!snapshotFileIfFirst(filePath)) {
        qCWarning(lcConfig) << "setOverride: refusing to write" << filePath << "without a recoverable snapshot";
        return false;
    }

    QSaveFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        dropFileSnapshotIfUnchanged(filePath);
        return false;
    }
    file.write(QJsonDocument(obj).toJson(QJsonDocument::Indented));
    if (!file.commit()) {
        dropFileSnapshotIfUnchanged(filePath);
        return false;
    }

    // Disk changed under the cache. Must happen before any signal below, since
    // the QML handlers wired to `overrideChanged` re-read through
    // `resolvedProfile` synchronously on this stack.
    invalidateDiskProfileCache();

    // The write may have restored the file to exactly its pre-edit content (the
    // user undid an edit by hand). The staged snapshot is then a phantom: disk
    // already holds what Discard would write back, so keeping it would leave the
    // page dirty forever with nothing to restore.
    //
    // The drop owns the signal for the transition it causes, so the compare below
    // must not fire again for the same flip.
    // Emit-side convention here (the clear paths use Defer and own the signal
    // themselves): `dropped` stands in for "the drop already emitted". Note
    // this does NOT rest on a drop always flipping — it often does not, since
    // hasPendingChanges() is equally true when other files are snapshotted or
    // the shader tree diverges from its committed baseline. It rests on the
    // weaker and actually true property: a drop that flipped has already
    // emitted, and a drop that did not flip left wasPending == nowPending, so
    // suppressing the compare below loses nothing either way.
    // The early return above
    // guarantees the written content differs from what was on disk, so a
    // snapshot staged by THIS call can never match afterwards — a successful
    // drop therefore means a pre-existing entry went away, which is a real
    // transition the drop announced.
    const bool dropped = dropFileSnapshotIfUnchanged(filePath);
    const bool nowPending = hasPendingChanges();
    Q_EMIT overrideChanged(path);
    if (!dropped && wasPending != nowPending)
        Q_EMIT pendingChangesChanged();
    return true;
}

AnimationsPageController::OverrideFileRemoval AnimationsPageController::removeOverrideFile(const QString& path)
{
    const QString filePath = profileFilePath(path);
    QFile file(filePath);
    if (!file.exists())
        return OverrideFileRemoval::Absent;
    // Mirror setOverride's snapshot symmetry, through the one shared rollback
    // primitive: it drops the staged entry only while disk still matches it.
    if (!snapshotFileIfFirst(filePath)) {
        qCWarning(lcConfig) << "removeOverrideFile: refusing to delete" << filePath << "without a recoverable snapshot";
        return OverrideFileRemoval::Failed;
    }
    if (!file.remove()) {
        dropFileSnapshotIfUnchanged(filePath, SnapshotDropSignal::Defer);
        return OverrideFileRemoval::Failed;
    }
    // Deleting a file that did not exist before this session's edits returns it
    // to its pre-edit state (the snapshot is `nullopt` = "was absent"), so the
    // staged entry is a phantom and has to go, or the page stays dirty with
    // nothing to discard.
    //
    // Deferred: mid-sequence that flip is not the outcome, only a step toward
    // it. A batch that drops a phantom on its first path and then stages a real
    // snapshot on a later one would otherwise leave "clean" as the last thing
    // the page heard while ending dirty. Every caller re-samples and emits once.
    dropFileSnapshotIfUnchanged(filePath, SnapshotDropSignal::Defer);
    // This function deliberately does not refresh the profile store, so the
    // cache drop cannot ride along on `refreshProfileStore`. Without it the
    // next `resolvedProfile` would serve the deleted file's contents back — the
    // stale-inherited-value bug, reintroduced one layer down.
    invalidateDiskProfileCache();
    return OverrideFileRemoval::Removed;
}

bool AnimationsPageController::clearOverride(const QString& path)
{
    if (!isValidEventPath(path))
        return false;
    // Same race rationale as setOverride above — refuse mutation
    // while the Discard worker has the snapshot map captured. A
    // clearOverride on a path the worker is rewriting would let the
    // worker re-create the file the user just deleted.
    if (m_asyncRevertInFlight) {
        qCWarning(lcConfig) << "clearOverride: refusing delete while async discard is in flight; path=" << path;
        return false;
    }
    // Capture dirty state AFTER the cheap validity / existence guards so an
    // invalid path or no-op clear doesn't pay for a hasPendingChanges() walk.
    // Mirrors setOverride() ordering above so a future refactor doesn't see two
    // divergent capture-point shapes.
    if (!hasOverride(path))
        return false;
    const bool wasPending = hasPendingChanges();
    // Absent here means the file went away between that check and this call.
    // The caller's intent is satisfied and nothing was done, so it reports the
    // same "nothing to clear" as the guard above rather than a failure.
    //
    // A FAILED removal can still have moved the dirty state: the snapshot is
    // staged before the delete is attempted, and if an external writer touched
    // the file in between, the deferred drop finds disk no longer matching and
    // declines, leaving the entry behind. The page is dirty and has to be told
    // even though the clear did not happen. The drop used to own this signal
    // before it was deferred, so the early return has to carry it now.
    //
    // UNTESTED, deliberately: reaching it needs the file's bytes to change
    // between snapshotFileIfFirst and the drop, inside one call, with the
    // removal also failing. There is no seam in the public surface to
    // interleave that, and adding one purely for the test would be a worse
    // trade than saying plainly that this branch rests on inspection.
    if (removeOverrideFile(path) != OverrideFileRemoval::Removed) {
        if (wasPending != hasPendingChanges())
            Q_EMIT pendingChangesChanged();
        return false;
    }
    // Before the signal, not after: the page re-reads resolvedProfile from
    // inside its overrideChanged handler, and the profile registry is behind
    // the delete until the loader's debounced rescan lands. Also before the
    // nowPending sample, since a rescan fans signals out synchronously and a
    // listener that mutates would otherwise be measured against a stale read.
    refreshProfileStore();
    const bool nowPending = hasPendingChanges();
    Q_EMIT overrideChanged(path);
    // Sole emit for this flip now: the snapshot drop inside removeOverrideFile
    // deferred its own.
    if (wasPending != nowPending)
        Q_EMIT pendingChangesChanged();
    return true;
}

int AnimationsPageController::clearAllOverrides()
{
    // Refuse outright while the discard worker owns the snapshot map: every
    // clearOverride() below would refuse individually, and the caller would read
    // the resulting 0 as "there was nothing to clear" rather than "nothing was
    // cleared". Report it as a refusal instead.
    if (m_asyncRevertInFlight) {
        qCWarning(lcConfig) << "clearAllOverrides: refusing while an async discard is in flight";
        Q_EMIT toastRequested(PhosphorI18n::tr("Cannot reset while a discard is in progress."));
        return -1;
    }
    // Clear every built-in event path. Paths without an override file are
    // skipped, so only real overrides are removed and snapshotted.
    return clearOverridesForPaths(PhosphorAnimation::ProfilePaths::allBuiltInPaths(),
                                  QLatin1String("clearAllOverrides"));
}

int AnimationsPageController::clearOverridesForPaths(const QStringList& eventPaths, QLatin1String context)
{
    const bool wasPending = hasPendingChanges();
    QStringList cleared;
    int failed = 0;
    for (const QString& path : eventPaths) {
        if (!isValidEventPath(path))
            continue;
        switch (removeOverrideFile(path)) {
        case OverrideFileRemoval::Removed:
            cleared.append(path);
            break;
        case OverrideFileRemoval::Absent:
            // Nothing to clear at this path, which is most of them on any
            // scoped reset. Not a failure: the desired end state already holds.
            //
            // And deliberately NOT a rescan trigger, unlike the same enumerator
            // in `clearFieldOnPaths`. There, every path reaching the removal was
            // classified as CARRYING the field a moment earlier, so Absent means
            // the file vanished underneath and the registry may still hold it.
            // Here the caller hands over a whole subtree with no such
            // precondition, so Absent is the common case and refreshing on it
            // would re-read the entire profiles directory on nearly every call.
            break;
        case OverrideFileRemoval::Failed:
            // The file is there and could not be removed. Counting it as a
            // clear would report a reset that did not happen.
            ++failed;
            break;
        }
    }
    // ONE rescan for the whole batch, ahead of the signals the pages refresh
    // on. Refreshing inside the loop would re-read and re-parse every file in
    // the profiles directory once per deleted file.
    if (!cleared.isEmpty())
        refreshProfileStore();
    // Sampled here, after the refresh and before the signals, for the same
    // reason clearOverride does it: a rescan fans out synchronously, and a
    // listener that mutates in response would otherwise be measured against a
    // stale read. The two clear paths must not diverge on this — the last two
    // regressions in this signal both came in through an unstated divergence.
    const bool nowPending = hasPendingChanges();
    for (const QString& path : cleared)
        Q_EMIT overrideChanged(path);
    // One dirty signal for the batch, describing the NET flip. Every snapshot
    // drop inside the loop deferred its own precisely so an intermediate state
    // could not be mistaken for the outcome: a batch that drops a phantom
    // early and stages a real snapshot later ends dirty, and must not have
    // announced "clean" on the way through.
    if (wasPending != nowPending)
        Q_EMIT pendingChangesChanged();
    if (failed > 0) {
        qCWarning(lcConfig) << context << ":" << failed << "override files could not be removed";
        Q_EMIT toastRequested(PhosphorI18n::tr("Some animation overrides could not be reset."));
        // Report the incomplete reset the same way as the in-flight refusal: a
        // positive count here would let the caller finish its reset path and
        // declare the page clean while override files remain on disk.
        return -1;
    }
    return int(cleared.size());
}

int AnimationsPageController::clearOverridesUnder(const QStringList& eventPaths)
{
    // Scoped clearAllOverrides: same refusal/partial contract, but only over the
    // caller's own page subtree. A path with no override file is skipped, so
    // passing the full in-scope path list clears just the real overrides and
    // snapshots each for a later Discard.
    if (m_asyncRevertInFlight) {
        qCWarning(lcConfig) << "clearOverridesUnder: refusing while an async discard is in flight";
        // Deliberately not "Cannot reset": this entry point also backs a
        // per-event card's Override toggle, and a user flipping one switch did
        // not experience that as a reset.
        Q_EMIT toastRequested(PhosphorI18n::tr("Cannot change this while a discard is in progress."));
        return -1;
    }
    return clearOverridesForPaths(eventPaths, QLatin1String("clearOverridesUnder"));
}

bool AnimationsPageController::hasScopedPendingFiles(const QStringList& eventPaths) const
{
    // The file half of a per-page dirty check: any in-scope path whose override
    // file carries a staged (snapshotted) edit. Keyed by the same file path
    // setOverride/clearOverride snapshot under.
    for (const QString& path : eventPaths) {
        if (!isValidEventPath(path))
            continue;
        if (m_pendingFileSnapshots.contains(profileFilePath(path)))
            return true;
    }
    return false;
}

} // namespace PlasmaZones
