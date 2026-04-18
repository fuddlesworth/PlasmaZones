// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

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
 * refresh so that new/modified/deleted scripts are picked up automatically.
 *
 * The application injects the subdirectory name (relative to
 * `QStandardPaths::GenericDataLocation`) at construction — the library is
 * brand-agnostic. For PlasmaZones this is `"plasmazones/algorithms"`.
 *
 * User scripts under `writableLocation/<subdirectory>/` override system
 * scripts with the same filename (system dirs come from every XDG
 * GenericDataLocation entry, in order).
 */
class PHOSPHORTILES_EXPORT ScriptedAlgorithmLoader : public QObject
{
    Q_OBJECT

public:
    /**
     * @brief Construct a loader for @p subdirectory under XDG data dirs.
     *
     * @p subdirectory is a relative path (no leading slash) appended to
     * every `QStandardPaths::GenericDataLocation` entry. Pass the empty
     * string to disable all discovery (the loader becomes a no-op).
     */
    explicit ScriptedAlgorithmLoader(const QString& subdirectory, QObject* parent = nullptr);
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
     * @return Absolute path to the configured subdirectory under
     *         `QStandardPaths::writableLocation(GenericDataLocation)`,
     *         or empty string if the subdirectory was empty.
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

    QString m_subdirectory; ///< XDG-relative path (e.g. "plasmazones/algorithms")
    QFileSystemWatcher* m_watcher = nullptr;
    QTimer* m_refreshTimer = nullptr;
    QHash<QString, QString> m_scriptIdToPath; ///< script ID -> file path

    static constexpr int RefreshDebounceMs = 500;
};

} // namespace PhosphorTiles
