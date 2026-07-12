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

    explicit AnimationPresetLibrary(ProfilesDirFn profilesDirFn, SnapshotFn snapshot, QObject* parent = nullptr);

    QVariantList userPresets() const;
    bool addUserPreset(const QString& name, const QVariantMap& profileJson);
    bool removeUserPreset(const QString& name);

    /// Absolute path the named preset serialises to. Empty when @p name
    /// slugifies to an empty string.
    QString presetFilePath(const QString& presetName) const;

Q_SIGNALS:
    void userPresetsChanged();
    void pendingChangesChanged();

private:
    ProfilesDirFn m_profilesDir;
    SnapshotFn m_snapshot;
};

} // namespace PlasmaZones
