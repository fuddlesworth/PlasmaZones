// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <phosphortiles_export.h>
#include <QFileSystemWatcher>
#include <QHash>
#include <QObject>
#include <QString>
#include <QTimer>

namespace PhosphorTiles {

/**
 * @brief Discovers, loads, and hot-reloads ScriptedAlgorithm instances
 *
 * Scans system and user algorithm directories for .js files, creates
 * ScriptedAlgorithm instances, and registers them with AlgorithmRegistry.
 * Watches directories and files via QFileSystemWatcher with debounced
 * refresh (same pattern as ShaderRegistry) so that new/modified/deleted
 * scripts are picked up automatically.
 *
 * User scripts in ~/.local/share/plasmazones/algorithms/ override system
 * scripts with the same filename.
 */
class PHOSPHORTILES_EXPORT ScriptedAlgorithmLoader : public QObject
{
    Q_OBJECT

public:
    explicit ScriptedAlgorithmLoader(QObject* parent = nullptr);
    ~ScriptedAlgorithmLoader() override;

    /**
     * @brief Discover and load all .js algorithms from system + user dirs
     *
     * Clears existing scripted algorithms from the registry, then rescans
     * all algorithm directories. System directories are loaded first so that
     * user directories can override by filename.
     */
    bool scanAndRegister();

    /**
     * @brief Create the user algorithm directory if it does not exist
     */
    void ensureUserDirectoryExists();

    /**
     * @brief Writable user directory path for custom algorithms
     * @return Absolute path to ~/.local/share/plasmazones/algorithms/
     */
    QString userAlgorithmDir() const;

Q_SIGNALS:
    /**
     * @brief Emitted after any add/remove/reload of scripted algorithms
     */
    void algorithmsChanged();

private Q_SLOTS:
    void onDirectoryChanged(const QString& path);
    void onFileChanged(const QString& path);
    void performDebouncedRefresh();

private:
    void setupFileWatcher();
    void reWatchFiles();
    void loadFromDirectory(const QString& dir, bool isUserDir);
    void scheduleRefresh();
    QStringList algorithmDirectories() const;
    void watchDirectory(const QString& dirPath);
    QStringList validatedJsFiles(const QString& dirPath, int maxFiles) const;

    QFileSystemWatcher* m_watcher = nullptr;
    QTimer* m_refreshTimer = nullptr;
    QHash<QString, QString> m_scriptIdToPath; ///< script ID -> file path

    static constexpr int RefreshDebounceMs = 500;
};

} // namespace PhosphorTiles
