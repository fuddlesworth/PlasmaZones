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
     * `sink` must outlive the loader. Taken by reference rather than
     * raw pointer so there is no need for a null-check at the call
     * site — the previous pointer-based signature relied on
     * `Q_ASSERT_X(sink, …)` to catch null, but that's compiled out in
     * release builds. A reference makes "sink is always valid" a
     * compile-time guarantee.
     */
    explicit DirectoryLoader(IDirectoryLoaderSink& sink, QObject* parent = nullptr);
    ~DirectoryLoader() override;

    DirectoryLoader(const DirectoryLoader&) = delete;
    DirectoryLoader& operator=(const DirectoryLoader&) = delete;

    /**
     * @brief Register a directory for scanning + (optionally) watching.
     *
     * Idempotent on the directory path — adding the same directory
     * twice is a no-op on the second call. Returns the count of
     * entries CURRENTLY registered after the scan (not the delta).
     *
     * `liveReload` is a one-way enable: once any call passes
     * `LiveReload::On`, the loader keeps watching for the rest of its
     * lifetime. Subsequent `LiveReload::Off` calls do not disarm the
     * watcher — they just skip arming new watches for the newly-added
     * directory. Callers that need to stop watching should destroy and
     * rebuild the loader.
     */
    int loadFromDirectory(const QString& directory, LiveReload liveReload = LiveReload::Off);

    /**
     * @brief Register multiple directories in order.
     *
     * Later directories override earlier on key collision. Pass dirs
     * in the "system-first, user-last" order (the reverse of
     * `QStandardPaths::locateAll`'s natural output — see the daemon's
     * `setupAnimationProfiles` for the canonical pattern).
     *
     * Same one-way `liveReload` semantics as `loadFromDirectory`.
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

    /// Hard cap on entries parsed per rescan (summed across every
    /// registered directory). At 10k entries a rescan has already burned
    /// the 50 ms debounce budget many times over; a user (or malicious
    /// same-user process) filling `~/.local/share/…/profiles/` with tiny
    /// JSON files would otherwise block the GUI thread unboundedly on
    /// every watch fire. Sinks are consulted in scan order so the first
    /// 10k files (alphabetic within each dir) still register; excess is
    /// skipped with a one-shot warning.
    ///
    /// Tests can override via `setMaxEntriesForTest` when exercising the
    /// cap.
    static constexpr int kMaxEntries = 10'000;

    /// Test-only: override the debounce interval (default 50 ms).
    ///
    /// Live-reload tests otherwise wait on `QSignalSpy::wait(N ms)` with
    /// N chosen to accommodate the 50 ms debounce plus filesystem / CI
    /// scheduling jitter — which is why loaded CI runs see flakes at
    /// N=2000 ms. Shrinking the debounce to ~1 ms lets tests wait <500 ms
    /// deterministically without changing production behaviour.
    ///
    /// Production code MUST NOT call this — the 50 ms debounce is a hard
    /// requirement for collapsing the 2-3 event save-temp-rename dance
    /// every editor performs.
    void setDebounceIntervalForTest(int ms);

    /// Test-only: override the per-rescan entry cap (default
    /// `kMaxEntries`). Lets the cap regression test trip the guard with
    /// 3-digit file counts rather than having to materialise 10k files
    /// on the CI filesystem.
    void setMaxEntriesForTest(int cap);

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
    /// Effective per-rescan entry cap — initialised from `kMaxEntries`
    /// at construction; tests override via `setMaxEntriesForTest`.
    int m_maxEntries = kMaxEntries;
};

} // namespace PhosphorJsonLoader
