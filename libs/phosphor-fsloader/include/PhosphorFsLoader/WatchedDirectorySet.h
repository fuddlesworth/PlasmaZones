// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <PhosphorFsLoader/phosphorfsloader_export.h>

#include <QtCore/QHash>
#include <QtCore/QObject>
#include <QtCore/QSet>
#include <QtCore/QString>
#include <QtCore/QStringList>
#include <QtCore/QTimer>

QT_BEGIN_NAMESPACE
class QFileSystemWatcher;
QT_END_NAMESPACE

namespace PhosphorFsLoader {

class IScanStrategy;

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
 * @brief Watcher + debounce + rescan scaffolding for filesystem-backed loaders.
 *
 * Owns the cross-cutting plumbing every "scan a set of directories,
 * register entries, watch for changes, rescan on edit" loader needs:
 *
 *   • Registration of one or more directories, in caller-chosen order.
 *   • `QFileSystemWatcher`-backed live reload with a 50 ms single-shot
 *     debounce that coalesces the editor save-temp-rename dance into
 *     one rescan.
 *   • **Parent-directory watching** when the target doesn't exist yet
 *     (fresh-install consumers that create their data dir on first
 *     write still get hot-reload — the base notices when the target
 *     materialises and promotes to a direct watch).
 *   • Re-arming individual file watches after every rescan, so editors
 *     that save via atomic rename (the new inode is a different watch
 *     candidate) still fire on the next edit.
 *   • A rescan-during-rescan race guard that ensures a watcher event
 *     delivered while the scan is in progress is replayed afterwards
 *     instead of being silently dropped.
 *
 * Schema-specific concerns (file extension, parsing, registry commits,
 * user-wins layering) live in the consumer-supplied `IScanStrategy`.
 *
 * ## Thread safety
 *
 * GUI-thread only. The watcher and the debounce timer live on the
 * thread the set was constructed on. Call every public method from the
 * same thread.
 */
class PHOSPHORFSLOADER_EXPORT WatchedDirectorySet : public QObject
{
    Q_OBJECT

public:
    /**
     * @brief Construct with a borrowed strategy.
     *
     * `strategy` must outlive the set. Taken by reference rather than
     * raw pointer so there is no need for a null-check at the call
     * site; "strategy is always valid" is a compile-time guarantee.
     */
    explicit WatchedDirectorySet(IScanStrategy& strategy, QObject* parent = nullptr);
    ~WatchedDirectorySet() override;

    WatchedDirectorySet(const WatchedDirectorySet&) = delete;
    WatchedDirectorySet& operator=(const WatchedDirectorySet&) = delete;

    /**
     * @brief Register a directory for scanning + (optionally) watching.
     *
     * Idempotent on the directory path — adding the same directory
     * twice is a no-op on the second call. Triggers an immediate
     * synchronous rescan via the strategy.
     *
     * `liveReload` is a one-way enable: once any call passes
     * `LiveReload::On`, the set keeps watching for the rest of its
     * lifetime. Subsequent `LiveReload::Off` calls do not disarm the
     * watcher — they just skip arming new watches for the newly-added
     * directory. Callers that need to stop watching should destroy and
     * rebuild the set.
     *
     * @return The total number of registered directories after this call.
     */
    int registerDirectory(const QString& directory, LiveReload liveReload = LiveReload::Off);

    /**
     * @brief Register multiple directories in registration order.
     *
     * The order is preserved verbatim and passed through to the
     * strategy on every rescan. The base does not impose a system-
     * first or user-first convention.
     *
     * Same one-way `liveReload` semantics as `registerDirectory`.
     */
    int registerDirectories(const QStringList& directories, LiveReload liveReload = LiveReload::Off);

    /**
     * @brief Trigger a debounced rescan of every registered directory.
     *
     * Safe to call multiple times in rapid succession — the single-shot
     * debounce timer collapses into one fire. Consumers wire this into
     * their own cross-process notification channels (D-Bus signal on
     * config rewrite, etc.) to cover `QFileSystemWatcher`'s known
     * atomic-rename blind spots.
     *
     * If a rescan is already in progress on the calling thread (e.g.
     * the strategy's commit step re-entered us via a signal
     * connection), the request is replayed at the end of the running
     * rescan instead of being silently dropped.
     */
    void requestRescan();

    /// Run a rescan synchronously on the calling stack. Bypasses the
    /// debounce timer — useful for `Q_INVOKABLE` "refresh now" entry
    /// points that need the strategy's commit step to land before the
    /// caller returns.
    ///
    /// Safe to call regardless of live-reload state. If a debounced
    /// rescan is pending it is cancelled (the synchronous run covers
    /// the same ground).
    void rescanNow();

    /// Currently-registered directories in registration order.
    QStringList directories() const;

    /// Test-only: override the debounce interval (default 50 ms).
    ///
    /// Production code MUST NOT call this — the 50 ms debounce is a hard
    /// requirement for collapsing the 2-3 event save-temp-rename dance
    /// every editor performs.
    void setDebounceIntervalForTest(int ms);

    /// Test-only: introspection of the ancestor-watch bookkeeping used
    /// by `attachWatcherForDir` when a target directory does not yet
    /// exist. Returns the actually-watched ancestor for `target`, or an
    /// empty string if no such ancestor watch is recorded.
    QString watchedAncestorForTest(const QString& target) const;

    /// Test-only: returns true if `path` is currently in the internal
    /// parent-watch set.
    bool hasParentWatchForTest(const QString& path) const;

Q_SIGNALS:
    /**
     * @brief Fired after every rescan, coalesced by the 50 ms debounce.
     *
     * Emitted unconditionally on rescan completion — the strategy's
     * commit step has already touched the consumer's registry by the
     * time this fires.
     */
    void rescanCompleted();

private Q_SLOTS:
    void rescanAll();

private:
    void installWatcherIfNeeded();
    void attachWatcherForDir(const QString& directory);
    void onWatchedPathChanged();
    void syncFileWatches(const QStringList& desiredPaths);

    IScanStrategy* m_strategy = nullptr;
    QStringList m_directories; ///< registration order
    QFileSystemWatcher* m_watcher = nullptr;
    QSet<QString> m_watchedParents; ///< parents watched for dir-creation
    /// Back-reference from a configured target directory whose own path
    /// does not yet exist to the specific ancestor we climbed to and
    /// installed a watch on. Needed because `attachWatcherForDir` may
    /// watch a non-immediate ancestor (the climb skips over missing
    /// intermediaries), and on promotion — when the target later
    /// materialises — the immediate parent may still not exist, so
    /// removing `info.absolutePath()` from `m_watchedParents` would
    /// miss the actually-watched ancestor and leak the watch for the
    /// set's lifetime. Canonicalised with `QDir::cleanPath` on both
    /// key and value so symlinked or trailing-slash duplicates collapse.
    QHash<QString, QString> m_parentWatchFor;
    /// Individual files currently in the watcher's file set. Re-armed
    /// every rescan from the strategy's returned path list.
    QSet<QString> m_watchedFiles;
    QTimer m_debounceTimer;
    bool m_liveReloadEnabled = false;
    bool m_rescanInProgress = false;
    bool m_rescanRequestedWhileRunning = false;
};

} // namespace PhosphorFsLoader
