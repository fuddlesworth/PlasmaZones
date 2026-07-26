// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <phosphortiles_export.h>
#include <PhosphorFsLoader/WatchedDirectorySet.h>
#include <QByteArray>
#include <QHash>
#include <QObject>
#include <QSet>
#include <QStringList>
#include <QString>

#include <memory>

namespace PhosphorScripting {
class LuauEngine;
class LuauWatchdog;
}

namespace PhosphorTiles {

class ITileAlgorithmRegistry;

/**
 * @brief Discovers, loads, and hot-reloads LuauTileAlgorithm instances
 *
 * Scans system and user algorithm directories for .luau files, creates
 * LuauTileAlgorithm instances, and registers them with the injected
 * ITileAlgorithmRegistry. Watches directories and files via
 * QFileSystemWatcher with debounced refresh so that new/modified/
 * deleted scripts are picked up automatically.
 *
 * The application injects the subdirectory name (relative to
 * `QStandardPaths::GenericDataLocation`) at construction — the library is
 * brand-agnostic. For Phosphor this is `"plasmazones/algorithms"`.
 *
 * User scripts under `writableLocation/<subdirectory>/` override system
 * scripts declaring the same script ID (which defaults to `script:<basename>`
 * but is overridden by the script's own `id` metadata, so it is not simply the
 * filename). System dirs come from every XDG GenericDataLocation entry, in
 * order. A script may only ever shadow another
 * script by that XDG priority, never a C++ built-in: a script whose id collides
 * with a built-in is refused outright.
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
     * @brief Discover and load all .luau algorithms from system + user dirs
     *
     * Rescans every algorithm directory and diffs the result against the
     * previous scan: unchanged files keep their existing registry entry, new
     * and edited ones are (re)built, and entries whose file disappeared are
     * unregistered. No registry entry is dropped up front — the loader's own
     * tracking maps are rebuilt from scratch each scan and the stale sweep runs
     * at the end.
     *
     * Directories are iterated USER-FIRST with first-registration-wins on
     * script ID, which is what produces `user > sys-highest > … > sys-lowest`.
     * (The list handed in is system-first; `performScan` reverse-iterates it.)
     *
     * @p liveReload defaults to `On` so production callers (daemon,
     * editor, settings) get hot-reload by default. Pass `Off` from
     * tests / batch-import contexts that want a one-shot scan with no
     * background watcher attached. The flag is forwarded to the
     * underlying `WatchedDirectorySet` and inherits its one-way-enable
     * semantics: once any call passes `On`, the watcher stays armed
     * for the loader's lifetime.
     *
     * Idempotent on the registry: a re-scan that resolves to an empty
     * directory set still drives the diff path so previously-registered
     * scripts get unregistered as stale.
     *
     * Change-detection is observable via the `algorithmsChanged` signal,
     * which fires from inside the strategy when the on-disk script set
     * differs from the previous scan's signature.
     */
    void scanAndRegister(PhosphorFsLoader::LiveReload liveReload = PhosphorFsLoader::LiveReload::On);

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
    class LuauScanStrategy;
    QStringList performScan(const QStringList& directoriesInScanOrder);

    /// Identity of a script file that registered successfully on some scan.
    /// `size` + `mtimeMs` are the same (size, mtime) pair the change signature
    /// uses, so "unchanged on disk" means exactly the same thing in both places.
    struct ScriptStamp
    {
        QString id; ///< Registry id the file registered under
        qint64 size = -1;
        qint64 mtimeMs = -1;
    };

    void loadFromDirectory(const QString& dir, bool isUserDir, const QString& canonicalUserDir,
                           const QHash<QString, ScriptStamp>& prevStamps);
    QStringList algorithmDirectories() const;
    QStringList validatedLuauFiles(const QString& dirPath, int maxFiles) const;

    /// Lazily build the VM shared by all trusted bundled scripts (init + pluau
    /// prelude + sandbox, paid once). Returns nullptr if VM setup fails, in
    /// which case bundled scripts fall back to their own per-script VMs.
    std::shared_ptr<PhosphorScripting::LuauEngine> ensureSharedEngine();

    QString m_subdirectory; ///< XDG-relative path (e.g. "plasmazones/algorithms")
    ITileAlgorithmRegistry* m_registry = nullptr; ///< Borrowed; owner outlives loader
    /// Per-loader watchdog. Held via shared_ptr because the registry's
    /// unregisterAlgorithm uses deleteLater() — the algorithm's dtor
    /// (which calls m_watchdog->unregister(this)) can run on a later
    /// event-loop pass, after this loader is already gone. Each
    /// algorithm shares ownership of the watchdog so the thread is
    /// joined only when the very last user releases its strong
    /// reference (typically here in ~Loader, occasionally in a
    /// deferred-delete ~LuauTileAlgorithm). Per-loader instead of a
    /// process-wide singleton means each composition root (daemon,
    /// editor, settings) gets its own supervisor thread.
    std::shared_ptr<PhosphorScripting::LuauWatchdog> m_watchdog;
    /// VM shared by all trusted bundled scripts so the per-VM baseline plus the
    /// `pluau` prelude is paid once instead of per script. shared_ptr because a
    /// deferred-deleted algorithm may outlive this loader and still hold (and
    /// on teardown, release its module from) this engine. Untrusted user
    /// scripts get their own isolated engines instead (not this one). Created
    /// lazily by ensureSharedEngine() on the first bundled script.
    std::shared_ptr<PhosphorScripting::LuauEngine> m_sharedEngine;
    bool m_sharedEngineFailed = false; ///< Latches a failed setup so we don't retry every scan.
    std::unique_ptr<LuauScanStrategy> m_strategy;
    std::unique_ptr<PhosphorFsLoader::WatchedDirectorySet> m_watcher;
    QHash<QString, QString> m_scriptIdToPath; ///< script ID -> file path
    /// Files the last scan refused (failed to load, duplicate id, or
    /// built-in collision). Rebuilt every scan by loadFromDirectory and
    /// merged into the desired per-file watch list so an in-place fix of
    /// a broken script still triggers a rescan; these files own no
    /// registry entry, so they never appear in m_scriptIdToPath.
    QSet<QString> m_refusedFilePaths;
    /// Canonical file path -> identity of the algorithm that path registered,
    /// for every file that actually took a registry entry on the last scan.
    /// A file whose stamp still matches disk and whose id still holds a live
    /// scripted registry entry is left alone: without this, every inotify wake
    /// rebuilt a sandboxed Luau VM and re-parsed the source for every script in
    /// the corpus, on the GUI thread, even when the signature diff went on to
    /// prove nothing had changed. Refused files (invalid, duplicate id,
    /// built-in collision) own no registry entry and so are never stamped.
    QHash<QString, ScriptStamp> m_scriptStamps;
    /// Signature of the last registered script set — sorted (id, path,
    /// size, mtime) digest. Used by scanAndRegister() to suppress
    /// redundant algorithmsChanged() emissions on filesystem pokes that
    /// touched no actual content (editor-save of an unrelated file in the
    /// watched dir, lstat-only events, etc.), so downstream D-Bus fan-out
    /// stays quiet when nothing actually changed.
    QByteArray m_lastScriptSignature;
};

} // namespace PhosphorTiles
