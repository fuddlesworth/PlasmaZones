// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QObject>
#include <QString>
#include <QVariantList>
#include <QVariantMap>

#include <functional>

namespace PlasmaZones {

/// User-saved Profile preset library. Each preset is a JSON file under
/// the same directory as event overrides — the two are distinguished by
/// whether the file's `name` field matches a known
/// `PhosphorAnimation::ProfilePaths::` constant.
///
/// Hosted as a child QObject of AnimationsPageController; the controller
/// re-emits this object's `userPresetsChanged()` signal to QML.
class AnimationPresetLibrary : public QObject
{
    Q_OBJECT

public:
    using ProfilesDirFn = std::function<QString()>;
    /// Capture a file's pre-edit content before it is overwritten or removed.
    /// False = the capture failed, and the caller must NOT write: proceeding
    /// would permanently lose content Discard could no longer restore. Same
    /// contract as ShaderSetStore::FileSnapshotFn.
    using SnapshotFn = std::function<bool(const QString& /*filePath*/)>;
    /// Companion to SnapshotFn: drop the capture again when the write it was
    /// taken for failed, so the page does not report an unsaved change to a
    /// file nothing touched. Same contract as
    /// ShaderSetStore::FileSnapshotRollbackFn.
    using SnapshotRollbackFn = std::function<void(const QString& /*filePath*/)>;

    explicit AnimationPresetLibrary(ProfilesDirFn profilesDirFn, SnapshotFn snapshot, SnapshotRollbackFn rollback,
                                    QObject* parent = nullptr);

    QVariantList userPresets() const;
    bool addUserPreset(const QString& name, const QVariantMap& profileJson);
    bool removeUserPreset(const QString& name);

Q_SIGNALS:
    void userPresetsChanged();
    void pendingChangesChanged();
    /// Message for the host page to surface. Every refusal path in the two
    /// mutators emits one, so a rejected name or an unwritable directory
    /// cannot fail silently with the dialog already closed.
    void toastRequested(const QString& text);

private:
    /// Absolute path the named preset serialises to. Empty when @p name
    /// slugifies to an empty string, or when the profiles directory is
    /// unconfigured.
    QString presetFilePath(const QString& presetName) const;
    /// The profiles directory, or an empty string when no accessor is wired.
    QString profilesDir() const;

    ProfilesDirFn m_profilesDir;
    SnapshotFn m_snapshot;
    SnapshotRollbackFn m_rollback;
};

} // namespace PlasmaZones
