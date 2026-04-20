// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <PhosphorJsonLoader/IDirectoryLoaderSink.h>
#include <PhosphorJsonLoader/ParsedEntry.h>
#include <PhosphorJsonLoader/phosphorjsonloader_export.h>

#include <QtCore/QHash>
#include <QtCore/QObject>
#include <QtCore/QSet>
#include <QtCore/QString>
#include <QtCore/QStringList>
#include <QtCore/QTimer>

QT_BEGIN_NAMESPACE
class QFileSystemWatcher;
QT_END_NAMESPACE

namespace PhosphorJsonLoader {

/**
 * @brief Opt-in policy for directory watching.
 *
 * `On` installs a `QFileSystemWatcher` on every scanned directory (or
 * its parent, if the target doesn't exist yet — so fresh installs that
 * create the user-data dir later still pick up edits without a
 * restart). Edits trigger a 50 ms debounced rescan.
 *
 * `Off` is fire-and-forget — useful for tests, batch imports, and
 * consumers that want explicit refresh semantics via `requestRescan`.
 */
enum class LiveReload : quint8 {
    Off,
    On,
};

/**
 * @brief Generic JSON-directory loader with debounced live reload.
 *
 * Handles the scaffolding every `LoadBag → register with a registry`
 * loader needs:
 *
 *   • Top-level `*.json` scan of every registered directory, in order
 *     (later directories override earlier — pass dirs in system-first,
 *     user-last for the standard user-wins layering).
 *   • `QFileSystemWatcher`-backed live reload with a 50 ms single-shot
 *     debounce, which coalesces the save-temp-rename dance most
 *     editors perform into a single rescan.
 *   • **Parent-directory watching** when the target doesn't exist yet.
 *     Fresh-install consumers that create the user-data dir on first
 *     write still get hot-reload — the loader notices when the target
 *     materialises and promotes to direct watch.
 *   • **Stale-entry purge** on rescan: deleted files' keys are
 *     reported to the sink as `removedKeys` so the sink can
 *     unregister them. The sink always gets the authoritative current
 *     set, not an append-only view.
 *
 * Schema-specific concerns live in the consumer-supplied
 * `IDirectoryLoaderSink`: parse one file, commit one batch.
 *
 * ## Thread safety
 *
 * GUI-thread only. The watcher and the debounce timer live on the
 * thread the loader was constructed on. Call `loadFromDirectory` and
 * `requestRescan` from the same thread.
 */
class PHOSPHORJSONLOADER_EXPORT DirectoryLoader : public QObject
{
    Q_OBJECT

public:
    /// Tracked entry — mirrors `ParsedEntry` minus the payload.
    /// Exposed via `entries()` for tests and debug introspection.
    struct Entry
    {
        QString key;
        QString sourcePath;
        QString systemSourcePath;
    };

    /**
     * @brief Construct with a borrowed sink.
     *
     * `sink` must outlive the loader. The loader holds a raw pointer
     * — `QPointer` isn't appropriate (the sink isn't necessarily a
     * QObject), and `unique_ptr` would force consumers to hand over
     * ownership, which is inconvenient for registry-backed sinks.
     */
    explicit DirectoryLoader(IDirectoryLoaderSink* sink, QObject* parent = nullptr);
    ~DirectoryLoader() override;

    DirectoryLoader(const DirectoryLoader&) = delete;
    DirectoryLoader& operator=(const DirectoryLoader&) = delete;

    /**
     * @brief Register a directory for scanning + (optionally) watching.
     *
     * Idempotent on the directory path — adding the same directory
     * twice is a no-op on the second call. Returns the count of
     * entries CURRENTLY registered after the scan (not the delta).
     */
    int loadFromDirectory(const QString& directory, LiveReload liveReload = LiveReload::Off);

    /**
     * @brief Register multiple directories in order.
     *
     * Later directories override earlier on key collision. Pass dirs
     * in the "system-first, user-last" order (the reverse of
     * `QStandardPaths::locateAll`'s natural output — see the daemon's
     * `setupAnimationProfiles` for the canonical pattern).
     */
    int loadFromDirectories(const QStringList& directories, LiveReload liveReload = LiveReload::Off);

    /**
     * @brief Trigger a debounced rescan of every registered directory.
     *
     * Safe to call multiple times in rapid succession — the single-shot
     * debounce timer collapses into one fire. Consumers wire this into
     * their own cross-process notification channels (D-Bus signal on
     * config rewrite, etc.) to cover `QFileSystemWatcher`'s known
     * atomic-rename blind spots.
     */
    void requestRescan();

    /// Count of entries currently tracked by the loader.
    int registeredCount() const;

    /// Current entries (post last rescan), sorted by `key` for
    /// deterministic order across platforms and Qt versions. Intended
    /// for tests + debug.
    QList<Entry> entries() const;

    /// Per-file size cap. Files larger than this are skipped with a
    /// warning — guards the GUI thread against a pathological user JSON
    /// (or a mis-mounted filesystem returning runaway sizes). 1 MiB is
    /// far above any legitimate curve / profile / layout schema in this
    /// library's ecosystem.
    static constexpr qint64 kMaxFileBytes = 1 * 1024 * 1024;

Q_SIGNALS:
    /**
     * @brief Fired after every rescan, coalesced by the 50 ms debounce.
     *
     * Emitted unconditionally on rescan completion — consumers typically
     * don't need this (the sink's `commitBatch` already handled the
     * registry update) but it's useful for test harnesses that want to
     * synchronise on "a scan happened".
     */
    void entriesChanged();

private Q_SLOTS:
    void rescanAll();

private:
    void installWatcherIfNeeded();
    void attachWatcherForDir(const QString& directory);
    void onWatchedPathChanged();
    void syncFileWatches();

    IDirectoryLoaderSink* m_sink = nullptr;
    QStringList m_directories; ///< scan order (earlier = lower priority)
    QHash<QString, Entry> m_entries; ///< key → tracked entry
    QFileSystemWatcher* m_watcher = nullptr;
    QSet<QString> m_watchedParents; ///< parents watched for dir-creation
    /// Individual files currently in the watcher's file set. Needed
    /// because editors that write in place (no atomic rename) do NOT
    /// fire `directoryChanged`; only `fileChanged` catches them. Must
    /// be re-armed every rescan — QFileSystemWatcher drops entries on
    /// atomic-rename saves (most editors).
    QSet<QString> m_watchedFiles;
    QTimer m_debounceTimer;
    bool m_liveReloadEnabled = false;
};

} // namespace PhosphorJsonLoader
