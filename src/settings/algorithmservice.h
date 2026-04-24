// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QHash>
#include <QObject>
#include <QString>
#include <QVariantList>
#include <QVariantMap>

namespace PhosphorTiles {
class AlgorithmRegistry;
class ScriptedAlgorithmLoader;
}

namespace PlasmaZones {

class Settings;

/// Algorithm registry / loader surface extracted from SettingsController.
///
/// Owns the Q_INVOKABLE-facing helpers for the scripted-algorithm lifecycle:
///   * Catalog queries (availableAlgorithms, generateAlgorithmPreview,
///     generateAlgorithmDefaultPreview).
///   * Filesystem CRUD (import, export, duplicate, delete, create-new,
///     openAlgorithmsFolder, openAlgorithm, openLayoutFile).
///   * Async registration watchers — when a new script file is dropped on
///     disk the loader's QFileSystemWatcher picks it up asynchronously; this
///     service bridges the "file written → algorithmCreated signal" gap and
///     times out after 10 s via algorithmOperationFailed.
///
/// Does NOT own the registry or the scripted-algorithm loader — those stay
/// as `std::unique_ptr` members on SettingsController. The service holds raw
/// pointers passed in at construction time. SettingsController must declare
/// the `std::unique_ptr<AlgorithmService>` AFTER the registry / loader so
/// reverse-order member destruction tears down the service (disconnecting
/// its watchers) BEFORE the borrowed registry / loader unique_ptrs reset.
///
/// Signals are forwarded to SettingsController via signal-to-signal connects
/// so QML-facing SettingsController::availableAlgorithmsChanged /
/// ::algorithmCreated / ::algorithmOperationFailed remain the public surface.
class AlgorithmService : public QObject
{
    Q_OBJECT

public:
    AlgorithmService(Settings* settings, PhosphorTiles::AlgorithmRegistry* registry,
                     PhosphorTiles::ScriptedAlgorithmLoader* loader, QObject* parent = nullptr);
    ~AlgorithmService() override;

    // ── Catalog queries ─────────────────────────────────────────────────
    QVariantList availableAlgorithms() const;
    QVariantList generateAlgorithmPreview(const QString& algorithmId, int windowCount, double splitRatio,
                                          int masterCount) const;
    QVariantList generateAlgorithmDefaultPreview(const QString& algorithmId) const;

    // ── Filesystem CRUD ─────────────────────────────────────────────────
    void openAlgorithmsFolder();
    bool importAlgorithm(const QString& filePath);
    void openAlgorithm(const QString& algorithmId);
    void openLayoutFile(const QString& layoutId);
    bool deleteAlgorithm(const QString& algorithmId);
    bool duplicateAlgorithm(const QString& algorithmId);
    bool exportAlgorithm(const QString& algorithmId, const QString& destPath);
    QString createNewAlgorithm(const QString& name, const QString& baseTemplate, bool supportsMasterCount,
                               bool supportsSplitRatio, bool producesOverlappingZones, bool supportsMemory);

Q_SIGNALS:
    void availableAlgorithmsChanged();
    void algorithmCreated(const QString& algorithmId);
    void algorithmOperationFailed(const QString& reason);

private:
    QString scriptedFilePath(const QString& algorithmId) const;
    void watchForAlgorithmRegistration(const QString& expectedId);
    void cancelAlgorithmWatcher(const QString& expectedId);

    Settings* m_settings = nullptr;
    PhosphorTiles::AlgorithmRegistry* m_registry = nullptr;
    PhosphorTiles::ScriptedAlgorithmLoader* m_loader = nullptr;

    QHash<QString, QMetaObject::Connection> m_algorithmWatchers;
};

} // namespace PlasmaZones
