// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

// Override-CRUD methods for AnimationsPageController. Extracted from
// animationspagecontroller.cpp to keep that file under the 800-line
// cap (CLAUDE.md). All methods here are members of the same class
// — separate translation unit, no API change.
//
// Group covers:
//   * Path derivation (userProfilesDir / profileFilePath / userMotionSetsDir)
//   * Existence + read (hasOverride / rawProfile / resolvedProfile)
//   * Write + clear (setOverride / clearOverride)
//
// Sibling _shaders.cpp owns the shader-tree side; the main TU owns
// pending-changes tracking, async-revert, and the section catalog.

#include "animationspagecontroller.h"

#include "../config/configdefaults.h"
#include "../core/isettings.h"
#include "../core/logging.h"
#include "animations_controller_detail.h"

#include <PhosphorAnimation/Profile.h>
#include <PhosphorAnimation/ProfilePaths.h>

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
    // assert in debug builds so a future caller that forgets the gate
    // crashes fast rather than producing a path-traversal file open.
    Q_ASSERT(isValidEventPath(path));
    return userProfilesDir() + QLatin1Char('/') + path + QStringLiteral(".json");
}

QString AnimationsPageController::userMotionSetsDir() const
{
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

    fillLibraryDefaults(merged);
    return merged;
}

bool AnimationsPageController::setOverride(const QString& path, const QVariantMap& profileJson)
{
    if (!isValidEventPath(path))
        return false;

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

    // Snapshot ONLY if this is the first edit to this path; remove the
    // snapshot if the write below fails so hasPendingChanges() doesn't
    // report a phantom pending edit pointing at content we never touched.
    // Bail before touching disk if the snapshot couldn't be captured —
    // an unrecoverable revert is worse than the failed write.
    const bool firstSnapshot = !m_pendingFileSnapshots.contains(filePath);
    if (!snapshotFileIfFirst(filePath)) {
        qCWarning(lcConfig) << "setOverride: refusing to write" << filePath << "without a recoverable snapshot";
        return false;
    }

    QSaveFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        if (firstSnapshot)
            m_pendingFileSnapshots.remove(filePath);
        return false;
    }
    file.write(QJsonDocument(obj).toJson(QJsonDocument::Indented));
    if (!file.commit()) {
        if (firstSnapshot)
            m_pendingFileSnapshots.remove(filePath);
        return false;
    }

    const bool nowPending = hasPendingChanges();
    Q_EMIT overrideChanged(path);
    if (wasPending != nowPending)
        Q_EMIT pendingChangesChanged();
    return true;
}

bool AnimationsPageController::clearOverride(const QString& path)
{
    if (!isValidEventPath(path))
        return false;
    const QString filePath = profileFilePath(path);
    QFile file(filePath);
    if (!file.exists())
        return false;
    // Capture dirty state AFTER the cheap validity / file.exists() guards
    // so an invalid path or no-op clear doesn't pay for a
    // hasPendingChanges() walk. Mirrors setOverride() ordering above so
    // a future refactor doesn't see two divergent capture-point shapes.
    const bool wasPending = hasPendingChanges();
    // Mirror setOverride's snapshot-rollback symmetry: capture whether
    // this call is the first to touch the file, snapshot, and on
    // remove() failure roll the snapshot back so hasPendingChanges()
    // doesn't report a phantom pending edit pointing at a file the
    // user never actually touched (the unsaved-changes badge would
    // light up and Discard would write the original content back over
    // an unchanged original — harmless but confusing UX).
    const bool firstSnapshot = !m_pendingFileSnapshots.contains(filePath);
    if (!snapshotFileIfFirst(filePath)) {
        qCWarning(lcConfig) << "clearOverride: refusing to delete" << filePath << "without a recoverable snapshot";
        return false;
    }
    if (!file.remove()) {
        if (firstSnapshot)
            m_pendingFileSnapshots.remove(filePath);
        return false;
    }
    Q_EMIT overrideChanged(path);
    if (wasPending != hasPendingChanges())
        Q_EMIT pendingChangesChanged();
    return true;
}

// ─── Preset library — delegated ────────────────────────────────────────

} // namespace PlasmaZones
