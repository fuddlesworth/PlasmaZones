// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "animationspagecontroller.h"

#include "../config/configdefaults.h"
#include "../core/animationshadersupportedpaths.h"
#include "../core/isettings.h"
#include "../core/logging.h"
#include "../phosphor_i18n.h"
#include "animationpresetlibrary.h"
#include "animations_controller_detail.h"
#include "dbusutils.h"
#include "motionsetdomain.h"
#include "shadersetstore.h"

#include <PhosphorAnimation/AnimationShaderEffect.h>
#include <PhosphorAnimation/AnimationShaderRegistry.h>
#include <PhosphorAnimation/Easing.h>
#include <PhosphorAnimation/PhosphorProfileRegistry.h>
#include <PhosphorAnimation/Profile.h>
#include <PhosphorAnimation/ProfilePaths.h>
#include <PhosphorAnimation/ShaderProfile.h>
#include <PhosphorAnimation/ShaderProfileTree.h>

#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFuture>
#include <QFutureWatcher>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLoggingCategory>
#include <QSaveFile>
#include <QSet>
#include <QStandardPaths>
#include <QUrl>
#include <QtConcurrent/QtConcurrent>

#include <algorithm>

namespace PlasmaZones {

namespace {

/// Ceiling on a single snapshotted profile file. The snapshot map holds these
/// in memory for the session, and the file is a filesystem boundary a user can
/// hand-place anything at. Matches ShaderSetStore's set-file cap.
constexpr qint64 kMaxSnapshotBytes = 4 * 1024 * 1024;

} // namespace

namespace animations_controller_detail {

/// `JsonNameKey`, `profileToVariantMap`, `readProfileJson`,
/// `mergeMissingFields`, and `fillLibraryDefaults` live in
/// `animations_controller_detail.h` so sibling TUs
/// (animationspagecontroller_overrides.cpp, _shaders.cpp) share the exact
/// same implementations without relying on unity-build TU merging.
///
/// `humanizeSegment` (segment title-casing for label display) also lives in
/// `animations_controller_detail.h` so animationspagecontroller_paths.cpp
/// shares the exact same implementation. Both `eventSections` (this TU)
/// and `eventLabel` (paths TU) call through to the header version.

} // namespace animations_controller_detail

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
    // Forward the existing pendingChangesChanged() signal to the
    // framework's dirtyChanged() so ApplicationController picks up
    // animation-page edits as part of the global dirty flag.
    //
    // CLAUDE.md: "Only emit signals when value actually changes." A
    // handful of internal call sites emit pendingChangesChanged
    // unconditionally (revertPending / asyncRevertPending /
    // setShaderOverride no-op branches). Gating the forwarder on the
    // observed state-flip keeps the dirty Q_PROPERTY's NOTIFY contract
    // honest — downstream listeners only re-evaluate on real changes.
    // Forward the snapshot helper as a callable so the sub-services can
    // capture pre-edit content without coupling to the controller's
    // m_pendingFileSnapshots layout. The bool return matters: a false means
    // the pre-edit content could NOT be captured, and a caller that writes
    // anyway loses it permanently. Both consumers honour it and refuse the
    // write (ShaderSetStore::snapshotFile, AnimationPresetLibrary's mutators).
    auto snapshotFn = [this](const QString& filePath) -> bool {
        return snapshotFileIfFirst(filePath);
    };
    // The companion to snapshotFn. A sub-service that snapshots a file and then
    // fails to write it has staged a file it never touched, so the page would
    // report unsaved changes with nothing to discard. This drops that entry
    // again — but only when the file on disk still matches what was staged, so
    // a snapshot covering an EARLIER edit that did land is never thrown away.
    auto snapshotRollbackFn = [this](const QString& filePath) {
        dropFileSnapshotIfUnchanged(filePath);
    };
    auto profilesDirFn = [this]() {
        return userProfilesDir();
    };
    auto motionSetsDirFn = [this]() {
        return userMotionSetsDir();
    };
    auto writeOverrideFn = [this](const QString& path, const QVariantMap& profile) {
        return setOverride(path, profile);
    };
    // The store is reachable straight from QML (`animationsPage.setsBridge`),
    // so the in-flight-discard gate the old controller-level forwarders
    // enforced has to travel WITH it — otherwise a set write could land
    // mid-revert and be clobbered by the worker's restore walk.
    auto mutationGuardFn = [this]() -> QString {
        if (m_asyncRevertInFlight)
            return PhosphorI18n::tr("Cannot modify sets while a discard is in progress.");
        return QString();
    };

    // Sub-services are constructed before the dirty-forwarder wiring below.
    // Nothing is missed by that ordering: the forwarder seeds
    // m_lastHadPendingChanges from the real post-construction state (just
    // below), so it does not need to have observed any signal fired during
    // construction.
    m_presets = new AnimationPresetLibrary(profilesDirFn, snapshotFn, snapshotRollbackFn, this);
    m_motionSets = new ShaderSetStore(motionset::makeConfig(profilesDirFn, motionSetsDirFn, writeOverrideFn, snapshotFn,
                                                            snapshotRollbackFn, mutationGuardFn),
                                      this);

    m_lastHadPendingChanges = hasPendingChanges();
    connect(this, &AnimationsPageController::pendingChangesChanged, this, [this]() {
        const bool current = hasPendingChanges();
        if (current == m_lastHadPendingChanges)
            return;
        m_lastHadPendingChanges = current;
        Q_EMIT dirtyChanged();
    });

    connect(m_presets, &AnimationPresetLibrary::userPresetsChanged, this,
            &AnimationsPageController::userPresetsChanged);
    // CLAUDE.md: only emit a signal when the value actually changed. The
    // sub-services and the mutators raise pendingChangesChanged unconditionally
    // (a no-op revert, a refused write), so gate the outward dirtyChanged on an
    // observed state flip rather than forwarding every raise.
    connect(m_presets, &AnimationPresetLibrary::toastRequested, this, &AnimationsPageController::toastRequested);
    connect(m_presets, &AnimationPresetLibrary::pendingChangesChanged, this,
            &AnimationsPageController::pendingChangesChanged);
    connect(m_motionSets, &ShaderSetStore::pendingChangesChanged, this,
            &AnimationsPageController::pendingChangesChanged);
    // A set's `active` flag is derived from the live override files, so it
    // goes stale whenever an event's profile is edited anywhere else.
    connect(this, &AnimationsPageController::overrideChanged, m_motionSets, &ShaderSetStore::notifyLiveStateChanged);
    // A set apply's per-path overrideChanged emissions ride through the
    // writeOverride callback (which the controller wires to its own
    // setOverride). ShaderSetStore therefore exposes no overrideChanged
    // signal — the controller is the single source of truth for it.

    if (m_shaderRegistry) {
        connect(m_shaderRegistry, &PhosphorAnimationShaders::AnimationShaderRegistry::effectsChanged, this,
                &AnimationsPageController::shaderEffectsChanged);
    }
    if (m_settings) {
        // Qt::DirectConnection is mandatory here: the
        // m_mutatingShaderTree depth check below distinguishes "our
        // own write" (depth > 0) from "external reload" (depth == 0),
        // and that only works when the NOTIFY fires SYNCHRONOUSLY
        // inside the MutatingShaderTreeScope. A queued connection
        // would dispatch the lambda after the scope's destructor has
        // already restored depth=0, the guard sees external-reload
        // semantics, and the lambda silently clears m_shaderTreeDirty
        // on the user's own write — a silent revert of staged edits.
        //
        // m_asyncRevertInFlight guards a second race:
        // SettingsController::discard() pairs our discard() with a
        // follow-up Settings::load(), which fires shaderProfileTreeChanged
        // while the asyncRevert worker is still running. The lambda must
        // NOT clear m_shaderTreeDirty in that window — the worker's
        // finished handler owns the terminal clear-and-emit sequence as
        // part of discardResult — so it short-circuits while the flag is
        // set.
        connect(
            m_settings, &ISettings::shaderProfileTreeChanged, this,
            [this]() {
                // Path-agnostic broadcast — the tree is a single Q_PROPERTY so we
                // can't tell which path moved without diffing. QML pages refresh
                // every visible event card on this signal which is cheap enough.
                Q_EMIT shaderProfileChanged(QString());
                // If this signal arrived from an external reload (Discard from
                // another page, import, settings.load()), the on-disk tree is
                // now authoritative — drop the staged-dirty flag so
                // hasPendingChanges() does not report phantom edits. The
                // m_mutatingShaderTree guard distinguishes our own writes
                // (which keep the dirty flag set) from external reloads.
                //
                // While an asyncRevert worker is running, skip the clear:
                // the worker's finished handler will reset m_shaderTreeDirty
                // and emit pendingChangesChanged + discardResult together as
                // the terminal sequence. Letting the lambda clear early would
                // race the terminal emit and could fire pendingChangesChanged
                // before discardResult, breaking the chrome's wait-counter.
                if (m_asyncRevertInFlight)
                    return;
                if (m_mutatingShaderTree == 0 && m_shaderTreeDirty) {
                    m_shaderTreeDirty = false;
                    Q_EMIT pendingChangesChanged();
                }
            },
            Qt::DirectConnection);
    }
}

AnimationsPageController::~AnimationsPageController() = default;

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

// ─── Pending-changes snapshot machinery ────────────────────────────────

bool AnimationsPageController::snapshotFileIfFirst(const QString& filePath)
{
    if (filePath.isEmpty())
        return false;
    if (m_pendingFileSnapshots.contains(filePath))
        return true;
    QFile f(filePath);
    if (!f.exists()) {
        m_pendingFileSnapshots.insert(filePath, std::nullopt);
        return true;
    }
    // A hand-placed profile file is a filesystem boundary like any other. Cap it
    // rather than slurping an arbitrarily large blob into the snapshot map for
    // the rest of the session, and refuse (so callers bail) rather than write
    // over content we cannot restore.
    const QFileInfo info(filePath);
    if (!info.isFile() || info.size() > kMaxSnapshotBytes) {
        qCWarning(lcConfig) << "snapshotFileIfFirst: refusing to snapshot" << filePath
                            << "— not a regular file, or over the" << kMaxSnapshotBytes << "byte cap";
        return false;
    }
    if (!f.open(QIODevice::ReadOnly)) {
        // Mid-session permission drift on an existing file would
        // silently lose pre-edit content if a write proceeded without
        // a snapshot — log so the journal flags the data-loss path,
        // and return false so direct callers can refuse the write.
        qCWarning(lcConfig) << "snapshotFileIfFirst: cannot read existing file" << filePath << "for revert snapshot —"
                            << f.errorString();
        return false;
    }
    m_pendingFileSnapshots.insert(filePath, f.readAll());
    return true;
}

bool AnimationsPageController::dropFileSnapshotIfUnchanged(const QString& filePath)
{
    const auto it = m_pendingFileSnapshots.constFind(filePath);
    if (it == m_pendingFileSnapshots.cend())
        return false;

    // The snapshot is the ONLY copy of the file's pre-edit content, so it may be
    // dropped only when the file still holds exactly that content: the write it
    // was taken for never landed. A mismatch means some earlier write did land,
    // and the entry is still Discard's way back.
    QFile f(filePath);
    if (it.value().has_value()) {
        // Compare sizes first: it settles the common mismatch without a read, and
        // it keeps this off the unbounded-readAll path that snapshotFileIfFirst
        // already refuses to take.
        if (QFileInfo(filePath).size() != it.value()->size())
            return false;

        if (!f.exists() || !f.open(QIODevice::ReadOnly) || f.readAll() != *it.value())
            return false;
    } else if (f.exists()) {
        // Staged as "did not exist", but something created it since.
        return false;
    }

    const bool wasPending = hasPendingChanges();
    m_pendingFileSnapshots.remove(filePath);
    // Sole owner of the signal for this transition: the sub-services used to
    // emit alongside their rollback call, which fired twice for one flip and
    // once even when the rollback declined to drop.
    if (wasPending != hasPendingChanges())
        Q_EMIT pendingChangesChanged();
    return true;
}

bool AnimationsPageController::hasPendingChanges() const
{
    return !m_pendingFileSnapshots.isEmpty() || m_shaderTreeDirty;
}

bool AnimationsPageController::isDirty() const
{
    return hasPendingChanges();
}

void AnimationsPageController::apply()
{
    // Refuse Apply while an asyncRevertPending worker is still
    // rewriting profile files from its captured snapshot. Without
    // this, an apply() call mid-revert would clear m_pendingFileSnapshots
    // and m_shaderTreeDirty — letting the worker's still-running
    // file restores silently UNDO writes the user wanted to keep,
    // then emit discardResult(true) on a now-clean page. Symmetric
    // to the per-mutator guards added in pass 36 (setOverride etc.)
    // and to RuleController::m_asyncCommitInFlight.
    if (m_asyncRevertInFlight) {
        Q_EMIT applyResult(false, PhosphorI18n::tr("Cannot save while a discard is in progress."));
        return;
    }
    commitPending();
    // commitPending is synchronous (just clears the snapshot map +
    // dirty bit; the per-edit writes already hit disk through
    // setOverride). Signal completion immediately so the chrome's
    // applyAllAsync wait-counter ticks down.
    Q_EMIT applyResult(true, QString());
}

void AnimationsPageController::discard()
{
    // The async revert moves the QSaveFile loop off the GUI thread
    // (motion-set discards can touch dozens of profile files) and
    // emits the inherited discardResult on completion — chrome
    // wait-counter then ticks down.
    asyncRevertPending();
}

void AnimationsPageController::commitPending()
{
    const bool had = hasPendingChanges();
    m_pendingFileSnapshots.clear();
    m_shaderTreeDirty = false;
    if (had)
        Q_EMIT pendingChangesChanged();
}

bool AnimationsPageController::revertPending()
{
    // discard() / revertPending() is the StagingDomain contract for "undo
    // everything since the last apply". This method:
    //   * Restores every snapshotted profile file from disk.
    //   * Clears our own dirty flag (m_shaderTreeDirty) only when all
    //     snapshots restore successfully — partial-failure keeps the flag
    //     so a retry path still sees hasPendingChanges()==true.
    //
    // IMPORTANT CALLER CONTRACT: the in-memory shader tree on m_settings
    // (Settings::shaderProfileTree) is NOT reverted here — that state is
    // owned by Settings, not this page, and is refreshed only by a
    // subsequent Settings::load(). SettingsController::discard() pairs
    // discard() with a follow-up load(); any future direct caller of
    // discard() MUST do the same, otherwise hasPendingChanges() returns
    // false while m_settings->shaderProfileTree() still holds unsaved
    // edits.
    using namespace PhosphorAnimation;

    // The async worker is mid-way through rewriting these same files, and it
    // merges its results back into m_pendingFileSnapshots when it lands. A
    // synchronous restore running underneath it would race that walk on disk
    // and then have its map edits overwritten by the worker's reply.
    if (m_asyncRevertInFlight) {
        qCWarning(lcConfig) << "revertPending: blocked while an async discard is in flight";
        return false;
    }
    if (!hasPendingChanges())
        return true;

    const QString profilesDir = userProfilesDir();
    const QString setsDir = userMotionSetsDir();
    const QStringList knownPaths = ProfilePaths::allBuiltInPaths();
    const QSet<QString> knownPathSet(knownPaths.cbegin(), knownPaths.cend());

    QStringList overrideEvents;
    bool anyPreset = false;
    bool anyMotionSet = false;

    // Failed restores are kept in the snapshot map so a follow-up revert
    // can retry. Successful restores are removed below.
    QHash<QString, std::optional<QByteArray>> retained;

    for (auto it = m_pendingFileSnapshots.cbegin(); it != m_pendingFileSnapshots.cend(); ++it) {
        const QString& filePath = it.key();
        const auto& content = it.value();

        bool restored = false;
        if (!content.has_value()) {
            // File didn't exist before this session — remove if present.
            if (!QFile::exists(filePath) || QFile::remove(filePath))
                restored = true;
        } else {
            QSaveFile f(filePath);
            if (f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
                f.write(*content);
                if (f.commit())
                    restored = true;
            }
        }

        if (!restored) {
            qCWarning(lcConfig) << "AnimationsPageController::revertPending: failed to restore" << filePath;
            retained.insert(filePath, content);
            continue;
        }

        // Classify so the right signal goes out for the restored file.
        // QDir-vs-QDir comparison normalises trailing slashes, "..", and
        // duplicate slashes so a string compare can't false-negative on
        // a path that's semantically equal but lexically different
        // (e.g. setsDir with a trailing slash from QStandardPaths).
        const QFileInfo info(filePath);
        const QDir absDir(info.absolutePath());
        const QString stem = info.completeBaseName();
        if (absDir == QDir(setsDir)) {
            anyMotionSet = true;
        } else if (absDir == QDir(profilesDir)) {
            if (knownPathSet.contains(stem))
                overrideEvents.append(stem);
            else
                anyPreset = true;
        }
    }
    m_pendingFileSnapshots = std::move(retained);

    // Clear shader-tree dirty when (and only when) every file snapshot was
    // successfully restored. If `retained` is non-empty, some restores
    // failed and the caller may retry — leave the dirty flag so the
    // retry path still sees hasPendingChanges()==true.
    if (m_pendingFileSnapshots.isEmpty()) {
        m_shaderTreeDirty = false;
    }

    // Bulk emit so QML sub-pages refresh exactly the rows that moved.
    for (const QString& path : overrideEvents)
        Q_EMIT overrideChanged(path);
    if (anyPreset)
        Q_EMIT userPresetsChanged();
    if (anyMotionSet && m_motionSets)
        m_motionSets->notifyLiveStateChanged();
    Q_EMIT pendingChangesChanged();
    // The restore ran. Individual files may have failed and been retained for a
    // retry, which is not the same thing as a refusal.
    return true;
}

void AnimationsPageController::asyncRevertPending()
{
    // POD payload threaded between GUI thread and the worker.
    // Captured by value into the QtConcurrent lambda + returned by
    // value through QFuture so it lifecycles cleanly across threads.
    struct WorkerResult
    {
        QHash<QString, std::optional<QByteArray>> retained;
        QStringList overrideEvents;
        bool anyPreset = false;
        bool anyMotionSet = false;
    };

    using namespace PhosphorAnimation;

    if (m_asyncRevertInFlight) {
        // Second invocation while a worker is running would race the
        // first — the second worker's reply could overwrite the live
        // m_pendingFileSnapshots map AFTER the first worker already
        // truncated some files on disk, producing inconsistent state.
        // Surface a quick failure so the framework's discard counter
        // ticks down and the user knows to retry.
        Q_EMIT discardResult(false, PhosphorI18n::tr("Discard already in flight."));
        return;
    }
    if (!hasPendingChanges()) {
        Q_EMIT discardResult(true, QString());
        return;
    }

    // Snapshot every input the worker needs by value. The worker
    // touches nothing else on `this` — that's what keeps the I/O
    // loop safe on a non-GUI thread.
    const QString profilesDir = userProfilesDir();
    const QString setsDir = userMotionSetsDir();
    const QStringList knownPaths = ProfilePaths::allBuiltInPaths();
    const QSet<QString> knownPathSet(knownPaths.cbegin(), knownPaths.cend());
    const QHash<QString, std::optional<QByteArray>> snapshots = m_pendingFileSnapshots;
    // Track the keys the worker is going to process so the finished
    // handler can MERGE results back into m_pendingFileSnapshots.
    // Today the m_asyncRevertInFlight guard rejects concurrent
    // setOverride / clearOverride / clearAllOverrides /
    // setShaderOverride / clearShaderOverride* / addUserPreset /
    // removeUserPreset calls, and — through the
    // mutationGuard closure handed to ShaderSetStore — every set
    // mutator (applySet / saveCurrentAsSet / removeSet / updateSet /
    // importSet) during the worker
    // run, so a fresh post-discard mutator would be the only way new
    // entries could appear — kept the merge as belt-and-braces against
    // a future change that opens the mutator gate (or a post-discard
    // race between the worker reply and a user click on Save).
    const QSet<QString> dispatchedKeys(snapshots.keyBegin(), snapshots.keyEnd());

    m_asyncRevertInFlight = true;
    auto* watcher = new QFutureWatcher<WorkerResult>(this);
    connect(
        watcher, &QFutureWatcher<WorkerResult>::finished, this,
        [this, watcher, dispatchedKeys]() {
            const WorkerResult result = watcher->result();
            watcher->deleteLater();

            // Back on the GUI thread — merge retained map with the live
            // map: keys we dispatched + the worker did NOT retain (i.e.
            // successfully restored) are removed from the live map; keys
            // the worker retained are kept (its content matches what we
            // dispatched — concurrent edits to those keys would also be
            // dropped, but that's correct: the user asked to discard
            // them and the file restore failed).
            for (const QString& key : dispatchedKeys) {
                if (!result.retained.contains(key))
                    m_pendingFileSnapshots.remove(key);
            }
            if (m_pendingFileSnapshots.isEmpty())
                m_shaderTreeDirty = false;

            for (const QString& path : result.overrideEvents)
                Q_EMIT overrideChanged(path);
            if (result.anyPreset)
                Q_EMIT userPresetsChanged();
            if (result.anyMotionSet && m_motionSets)
                m_motionSets->notifyLiveStateChanged();
            Q_EMIT pendingChangesChanged();
            // Emit discardResult LAST and clear the in-flight flag AFTER
            // the emit so any DirectConnection slot wired to
            // discardResult still observes m_asyncRevertInFlight==true and
            // routes through the worker-aware paths (e.g. test harnesses
            // that read state on the result signal). The mutator gate
            // re-opens together with the flag clear.
            const QString errorMsg = result.retained.isEmpty()
                ? QString()
                : PhosphorI18n::tr("Failed to restore %1 profile file(s). They remain pending.")
                      .arg(result.retained.size());
            Q_EMIT discardResult(result.retained.isEmpty(), errorMsg);
            m_asyncRevertInFlight = false;
        },
        Qt::DirectConnection);

    QFuture<WorkerResult> future = QtConcurrent::run([profilesDir, setsDir, knownPathSet, snapshots]() {
        WorkerResult result;
        for (auto it = snapshots.cbegin(); it != snapshots.cend(); ++it) {
            const QString& filePath = it.key();
            const auto& content = it.value();

            bool restored = false;
            if (!content.has_value()) {
                if (!QFile::exists(filePath) || QFile::remove(filePath))
                    restored = true;
            } else {
                QSaveFile f(filePath);
                if (f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
                    f.write(*content);
                    if (f.commit())
                        restored = true;
                }
            }

            if (!restored) {
                qCWarning(lcConfig) << "AnimationsPageController::asyncRevertPending: failed to restore" << filePath;
                result.retained.insert(filePath, content);
                continue;
            }

            // QDir-vs-QDir comparison normalises trailing slashes, "..",
            // and duplicate slashes so a string compare can't
            // false-negative on a path that's semantically equal but
            // lexically different. Worker-thread safety: QDir's normalised
            // comparison is pure-function (no thread-local state).
            const QFileInfo info(filePath);
            const QDir absDir(info.absolutePath());
            const QString stem = info.completeBaseName();
            if (absDir == QDir(setsDir)) {
                result.anyMotionSet = true;
            } else if (absDir == QDir(profilesDir)) {
                if (knownPathSet.contains(stem))
                    result.overrideEvents.append(stem);
                else
                    result.anyPreset = true;
            }
        }
        return result;
    });
    watcher->setFuture(future);
}

// ─── Path discovery ────────────────────────────────────────────────────
// `sectionForPath`, `eventLabel`, `parentPath`, `parentChain` live in
// `animationspagecontroller_paths.cpp` so this TU stays under the
// project's 800-line cap. Same class, separate TU, no API change.

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
        sectionPaths[section].append(entry);
    }

    QVariantList result;
    result.reserve(sectionOrder.size());
    for (const QString& section : sectionOrder) {
        QVariantMap sectionEntry;
        sectionEntry.insert(QStringLiteral("section"), section);
        sectionEntry.insert(QStringLiteral("label"), humanizeSegment(section));
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
    // Defence-in-depth: the sub-services write through the snapshot
    // callback wired by the controller ctor, so a concurrent mutator
    // here while asyncRevertPending's worker is rewriting profile files
    // would race the worker on disk. The QML chrome gates the picker on
    // `discarding`; this guard protects programmatic callers.
    if (m_asyncRevertInFlight) {
        qCWarning(lcConfig) << "addUserPreset: blocked during discard";
        Q_EMIT toastRequested(PhosphorI18n::tr("Cannot modify presets while a discard is in progress."));
        return false;
    }
    return m_presets && m_presets->addUserPreset(name, profileJson);
}

bool AnimationsPageController::removeUserPreset(const QString& name)
{
    if (m_asyncRevertInFlight) {
        qCWarning(lcConfig) << "removeUserPreset: blocked during discard";
        Q_EMIT toastRequested(PhosphorI18n::tr("Cannot modify presets while a discard is in progress."));
        return false;
    }
    return m_presets && m_presets->removeUserPreset(name);
}

// Motion sets live entirely in the shared ShaderSetStore reached through
// `setsBridge()` — QML talks to it directly. The in-flight-discard gate the
// old forwarders enforced now travels with the store as its mutationGuard
// (wired in the constructor).

// ─── Shader effects ────────────────────────────────────────────────────
// The effectToMap / parameterInfoToMap / shaderProfileToMap helpers used
// by both animationspagecontroller.cpp and animationspagecontroller_shaders.cpp
// live in animations_controller_detail.h as inline functions so the two
// TUs don't depend on unity-build merging for cross-TU linkage.

} // namespace PlasmaZones
