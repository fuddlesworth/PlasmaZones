// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

// Override-CRUD methods for AnimationsPageController. All methods here are
// members of the same class as animationspagecontroller.cpp — separate
// translation unit, no API change.
//
// Group covers:
//   * Path derivation (userProfilesDir / profileFilePath / userMotionSetsDir)
//   * Existence + read (hasOverride / rawProfile / resolvedProfile)
//   * Write + clear (setOverride / clearOverride / clearAllOverrides)
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
    if (!m_userProfilesDirOverride.isEmpty())
        return m_userProfilesDirOverride + QStringLiteral("/motionsets");
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
    return readProfileJson(profileFilePath(path)).toVariantMap();
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
        if (registry) {
            const auto entry = registry->resolve(cur);
            if (entry.has_value()) {
                source = profileToVariantMap(*entry);
            }
        }
        if (source.isEmpty() && isValidEventPath(cur)) {
            // Registry not published, or no entry at this path. Fall
            // back to a direct user-dir read so unit tests (which never
            // bootstrap a registry) still get walk-up resolution over
            // their own override files. Skip the read for non-event
            // paths so a crafted parent like "../" can't cause a stray
            // file open.
            source = readProfileJson(profileFilePath(cur)).toVariantMap();
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
    // silently losing the user's concurrent edit. The QML chrome already gates the
    // editor controls on `discarding`, but defence-in-depth at the
    // C++ Q_INVOKABLE entry point protects programmatic callers.
    if (m_asyncRevertInFlight) {
        qCWarning(lcConfig) << "setOverride: refusing write while async discard is in flight; path=" << path;
        return false;
    }

    const QString dir = userProfilesDir();
    if (!QDir().mkpath(dir))
        return false;
    // Capture dirty state AFTER the cheap validity / mkpath guards so an
    // invalid path doesn't pay for a hasPendingChanges() walk.
    const bool wasPending = hasPendingChanges();

    const QString filePath = profileFilePath(path);

    // Strip the name field for the equality compare against on-disk content
    // (`readProfileJson` strips it too). Same `obj` is later given the name
    // back for the write — the canonical name is always the path.
    QJsonObject obj = QJsonObject::fromVariantMap(profileJson);
    obj.remove(JsonNameKey);
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

    // The write may have restored the file to exactly its pre-edit content (the
    // user undid an edit by hand). The staged snapshot is then a phantom: disk
    // already holds what Discard would write back, so keeping it would leave the
    // page dirty forever with nothing to restore.
    //
    // The drop owns the signal for the transition it causes, so the compare below
    // must not fire again for the same flip.
    const bool dropped = dropFileSnapshotIfUnchanged(filePath);
    const bool nowPending = hasPendingChanges();
    Q_EMIT overrideChanged(path);
    if (!dropped && wasPending != nowPending)
        Q_EMIT pendingChangesChanged();
    return true;
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
    const QString filePath = profileFilePath(path);
    QFile file(filePath);
    if (!file.exists())
        return false;
    // Capture dirty state AFTER the cheap validity / file.exists() guards
    // so an invalid path or no-op clear doesn't pay for a
    // hasPendingChanges() walk. Mirrors setOverride() ordering above so
    // a future refactor doesn't see two divergent capture-point shapes.
    const bool wasPending = hasPendingChanges();
    // Mirror setOverride's snapshot symmetry, through the one shared rollback
    // primitive: it drops the staged entry only while disk still matches it.
    if (!snapshotFileIfFirst(filePath)) {
        qCWarning(lcConfig) << "clearOverride: refusing to delete" << filePath << "without a recoverable snapshot";
        return false;
    }
    if (!file.remove()) {
        dropFileSnapshotIfUnchanged(filePath);
        return false;
    }
    // Deleting a file that did not exist before this session's edits returns it
    // to its pre-edit state (the snapshot is `nullopt` = "was absent"), so the
    // staged entry is a phantom and has to go, or the page stays dirty with
    // nothing to discard. The drop owns the signal for that flip (see setOverride).
    const bool dropped = dropFileSnapshotIfUnchanged(filePath);
    const bool nowPending = hasPendingChanges();
    Q_EMIT overrideChanged(path);
    if (!dropped && wasPending != nowPending)
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
    // Clear every built-in event path. clearOverride is a no-op (returns false)
    // for paths without an override file, so only real overrides are removed and
    // snapshotted; it emits overrideChanged / pendingChangesChanged per removed
    // file so the pages refresh and the staged-changes state updates.
    int cleared = 0;
    int failed = 0;
    const QStringList paths = PhosphorAnimation::ProfilePaths::allBuiltInPaths();
    for (const QString& path : paths) {
        if (clearOverride(path)) {
            ++cleared;
        } else if (hasOverride(path)) {
            // The file is still there, so this was a real failure and not the
            // "nothing to clear" no-op. Counting it as either would report a reset
            // that did not happen.
            ++failed;
        }
    }
    if (failed > 0) {
        qCWarning(lcConfig) << "clearAllOverrides:" << failed << "override files could not be removed";
        Q_EMIT toastRequested(PhosphorI18n::tr("Some animation overrides could not be reset."));
        // Report the incomplete reset the same way as the refusal above: a
        // positive count here would let the caller finish its reset path and
        // declare the page clean while override files remain on disk.
        return -1;
    }
    return cleared;
}

int AnimationsPageController::clearOverridesUnder(const QStringList& eventPaths)
{
    // Scoped clearAllOverrides: same refusal/partial contract, but only over the
    // caller's own page subtree. clearOverride() is a no-op (false) for a path
    // with no override file, so passing the full in-scope path list clears just
    // the real overrides and snapshots each for a later Discard.
    if (m_asyncRevertInFlight) {
        qCWarning(lcConfig) << "clearOverridesUnder: refusing while an async discard is in flight";
        Q_EMIT toastRequested(PhosphorI18n::tr("Cannot reset while a discard is in progress."));
        return -1;
    }
    int cleared = 0;
    int failed = 0;
    for (const QString& path : eventPaths) {
        if (!isValidEventPath(path))
            continue;
        if (clearOverride(path))
            ++cleared;
        else if (hasOverride(path))
            ++failed;
    }
    if (failed > 0) {
        qCWarning(lcConfig) << "clearOverridesUnder:" << failed << "override files could not be removed";
        Q_EMIT toastRequested(PhosphorI18n::tr("Some animation overrides could not be reset."));
        return -1;
    }
    return cleared;
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
