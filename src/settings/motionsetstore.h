// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QHash>
#include <QObject>
#include <QString>
#include <QVariantList>
#include <QVariantMap>

#include <functional>

namespace PlasmaZones {

/// Persistence + CRUD for motion-set JSON files (collections of per-path
/// override snapshots). Hosted as a child QObject of
/// AnimationsPageController; the controller re-emits this object's
/// `motionSetsChanged()` signal to QML.
///
/// Lives in its own translation unit to keep
/// animationspagecontroller.cpp under the project's 800-line cap. The
/// store has no knowledge of the override-write path: applyMotionSet()
/// takes a callable that the controller wires to its own `setOverride`,
/// preserving snapshot/pending semantics.
class MotionSetStore : public QObject
{
    Q_OBJECT

public:
    /// Callback signature for committing one motion-set entry as a
    /// per-path override. Wired to `AnimationsPageController::setOverride`
    /// by the parent — the store doesn't reach across the controller
    /// boundary itself.
    using WriteOverrideFn = std::function<bool(const QString& /*path*/, const QVariantMap& /*profile*/)>;

    /// Snapshot helper signature — captures pre-edit content of a file
    /// path into the controller's pending-changes store. Wired to
    /// `AnimationsPageController::snapshotFileIfFirst`.
    using SnapshotFn = std::function<void(const QString& /*filePath*/)>;

    /// Profiles-dir accessor — the `saveCurrentAsMotionSet` walk reads
    /// every override file in this directory to build the snapshot.
    using ProfilesDirFn = std::function<QString()>;

    /// @param motionSetsDirFn Pure accessor returning the absolute path
    ///        of the motion-sets directory; called per operation so the
    ///        controller's `setUserProfilesDirOverride` test hook keeps
    ///        working without invalidating cached state.
    explicit MotionSetStore(ProfilesDirFn profilesDirFn, std::function<QString()> motionSetsDirFn,
                            WriteOverrideFn writeOverride, SnapshotFn snapshot, QObject* parent = nullptr);

    QVariantList availableMotionSets() const;
    bool applyMotionSet(const QString& name);
    bool saveCurrentAsMotionSet(const QString& name, const QString& description);
    bool removeMotionSet(const QString& name);

    /// Absolute path the named motion-set serialises to. Empty when
    /// @p setName slugifies to an empty string.
    QString motionSetFilePath(const QString& setName) const;

Q_SIGNALS:
    void motionSetsChanged();
    void pendingChangesChanged();

private:
    ProfilesDirFn m_profilesDir;
    std::function<QString()> m_motionSetsDir;
    WriteOverrideFn m_writeOverride;
    SnapshotFn m_snapshot;
};

} // namespace PlasmaZones
