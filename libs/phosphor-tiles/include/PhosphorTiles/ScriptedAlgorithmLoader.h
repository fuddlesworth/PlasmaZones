// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <phosphortiles_export.h>
#include <QByteArray>
#include <QHash>
#include <QObject>
#include <QString>

#include <memory>

namespace PhosphorFsLoader {
class WatchedDirectorySet;
}

namespace PhosphorTiles {

class ITileAlgorithmRegistry;
class ScriptedAlgorithmWatchdog;

/**
 * @brief Discovers, loads, and hot-reloads ScriptedAlgorithm instances
 *
 * Scans system and user algorithm directories for .js files, creates
 * ScriptedAlgorithm instances, and registers them with the injected
 * ITileAlgorithmRegistry. Watches directories and files via
 * QFileSystemWatcher with debounced refresh so that new/modified/
 * deleted scripts are picked up automatically.
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
     *
     * @p registry is the tile-algorithm registry the loader registers
     * discovered scripts against. Caller owns @p registry and must keep
     * it alive for the loader's lifetime.
     */
    explicit ScriptedAlgorithmLoader(const QString& subdirectory, ITileAlgorithmRegistry* registry,
                                     QObject* parent = nullptr);
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

private:
    class JsScanStrategy;
    QStringList performScan(const QStringList& directoriesInScanOrder);

    void loadFromDirectory(const QString& dir, bool isUserDir);
    QStringList algorithmDirectories() const;
    QStringList validatedJsFiles(const QString& dirPath, int maxFiles) const;

    QString m_subdirectory; ///< XDG-relative path (e.g. "plasmazones/algorithms")
    ITileAlgorithmRegistry* m_registry = nullptr; ///< Borrowed; owner outlives loader
    /// Per-loader watchdog. Held via shared_ptr because the registry's
    /// unregisterAlgorithm uses deleteLater() — the algorithm's dtor
    /// (which calls m_watchdog->unregister(this)) can run on a later
    /// event-loop pass, after this loader is already gone. Each
    /// algorithm shares ownership of the watchdog so the thread is
    /// joined only when the very last user releases its strong
    /// reference (typically here in ~Loader, occasionally in a
    /// deferred-delete ~ScriptedAlgorithm). Per-loader instead of a
    /// process-wide singleton means each composition root (daemon,
    /// editor, settings) gets its own supervisor thread.
    std::shared_ptr<ScriptedAlgorithmWatchdog> m_watchdog;
    std::unique_ptr<JsScanStrategy> m_strategy;
    std::unique_ptr<PhosphorFsLoader::WatchedDirectorySet> m_watcher;
    QHash<QString, QString> m_scriptIdToPath; ///< script ID -> file path
    /// Signature of the last registered script set — sorted (id, path,
    /// size, mtime) digest. Used by scanAndRegister() to suppress
    /// redundant algorithmsChanged() emissions on filesystem pokes that
    /// touched no actual content (editor-save of an unrelated file in the
    /// watched dir, lstat-only events, etc.), so downstream D-Bus fan-out
    /// stays quiet when nothing actually changed.
    QByteArray m_lastScriptSignature;
};

} // namespace PhosphorTiles
