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
 * @brief Caller's declared priority direction for `registerDirectories` /
 *        `setDirectories` input.
 *
 * Strategies in this library always see the **canonical** scan order:
 * `[lowest-priority, ..., highest-priority]`, suitable for reverse-
 * iterate-first-wins. The base normalises the caller's input into this
 * shape before storing it in `m_directories` and handing it to the
 * strategy, so the canonical convention is enforced once at the
 * registration boundary instead of being repeated as a comment-driven
 * contract every strategy author has to remember.
 *
 * Pick the value that matches how your input is **already** ordered — do
 * NOT pre-reverse and then claim the opposite. Both spellings are
 * equivalent; the enum's only job is to keep the next strategy author
 * from silently inverting every override by feeding `locateAll`'s
 * natural (highest-first) output as if it were the canonical
 * (lowest-first) shape.
 */
enum class RegistrationOrder : quint8 {
    /// `[sys-lowest, ..., sys-highest, user]` — the canonical strategy
    /// view. The daemon's curve/profile/script setup explicitly builds
    /// this shape via `std::reverse(locateAll(...))` + `userDir.append`.
    /// Pass-through; the base stores the list verbatim.
    LowestPriorityFirst,
    /// `[user, sys-highest, ..., sys-lowest]` — the natural output of
    /// `QStandardPaths::locateAll(GenericDataLocation, ...)` followed by
    /// a `prepend(userDir)`. The base reverses internally before
    /// storing, so the strategy still sees `LowestPriorityFirst`.
    HighestPriorityFirst,
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
     * Single-path registration carries no priority direction (one entry
     * has nothing to be priority-ordered against), so no `RegistrationOrder`
     * parameter is needed.
     *
     * `liveReload` is a **set-wide one-way enable**: once any call passes
     * `LiveReload::On`, the watcher is created and persists for the rest
     * of the set's lifetime. Subsequent `LiveReload::Off` calls do not
     * skip watching for the new directory — the per-rescan re-attach loop
     * (which exists to recover atomic-rename inode swaps) installs a
     * watch for every directory in `m_directories` regardless of how it
     * was registered. The flag's effect is therefore limited to the
     * **immediate** post-registration window: an `Off` registration after
     * a prior `On` call won't have a watch *until the next rescan
     * completes*, but it will from then on. Callers that need to stop
     * watching entirely should destroy and rebuild the set.
     *
     * @return The total number of registered directories after this call.
     */
    int registerDirectory(const QString& directory, LiveReload liveReload = LiveReload::Off);

    /**
     * @brief Register multiple directories in caller-declared priority order.
     *
     * The base normalises @p directories into the canonical
     * `[lowest-priority, ..., highest-priority]` shape before storing
     * — see `RegistrationOrder` for the two accepted input forms. The
     * stored list is what `directories()` returns and what the strategy
     * sees on every rescan, so all four in-tree strategies can rely on
     * the same iteration shape regardless of which form the caller used.
     *
     * Same set-wide one-way `liveReload` semantics as `registerDirectory`.
     */
    int registerDirectories(const QStringList& directories, LiveReload liveReload = LiveReload::Off,
                            RegistrationOrder order = RegistrationOrder::LowestPriorityFirst);

    /**
     * @brief Replace the registered directory set with @p directories.
     *
     * Unlike `registerDirectories` (append-only), this is a full replacement:
     * directories present in the current set but not in @p directories are
     * dropped, their direct watches are removed, and any ancestor proxy
     * watches they alone depended on are released. Directories present in
     * both sets are preserved (no churn). Directories new to the set are
     * appended in @p directories' order.
     *
     * Use this when the underlying source-of-truth for the directory list
     * is dynamic (e.g. XDG paths that change as packages install /
     * uninstall) and the consumer needs to drop stale entries cleanly.
     *
     * The strategy is invoked synchronously with the post-replacement
     * directory list. Per-file watches are re-synced from the strategy's
     * return value, so files belonging only to dropped directories are
     * cleaned up on the same call.
     *
     * Same set-wide one-way `liveReload` semantics as `registerDirectory`:
     * passing `On` enables the watcher (if not already enabled), passing
     * `Off` does not disarm it.
     *
     * Same `RegistrationOrder` semantics as `registerDirectories` —
     * @p directories is normalised into the canonical
     * `[lowest-priority, ..., highest-priority]` shape before being
     * stored and passed to the strategy.
     *
     * @return The total number of registered directories after this call.
     */
    int setDirectories(const QStringList& directories, LiveReload liveReload = LiveReload::Off,
                       RegistrationOrder order = RegistrationOrder::LowestPriorityFirst);

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
    ///
    /// Intended to be called on the GUI thread. The returned QStringList
    /// is a by-value, implicitly-shared snapshot — once obtained on the
    /// GUI thread it can be propagated to worker threads safely (e.g.
    /// captured into a `QtConcurrent::run` lambda for the shader-warming
    /// path). Calling this method *concurrently with* a mutating call on
    /// the GUI thread (`registerDirectories`, `setDirectories`,
    /// `requestRescan`, `rescanNow`) is a data race on the underlying
    /// QString refcounts; snapshot first, share later.
    QStringList directories() const;

    /// Helper for consumers building search-path APIs on top of the set:
    /// canonicalises (`QDir::cleanPath`) every entry in @p candidates,
    /// drops empties, and returns the subset that is NOT already in
    /// @p alreadyRegistered (and is not duplicated within @p candidates
    /// itself). Preserves caller order.
    ///
    /// Both `addSearchPaths`-style entry points in `PhosphorShell::ShaderRegistry`
    /// and `PhosphorAnimationShaders::AnimationShaderRegistry` use this
    /// to skip log-line spam when the same path is registered twice (the
    /// base's own `registerDirectories` is silent on dedup, so the log
    /// has to be filtered upstream).
    static QStringList filterNewSearchPaths(const QStringList& candidates, const QStringList& alreadyRegistered);

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
    void releaseAncestorWatchFor(const QString& targetKey);
    void onWatchedPathChanged();
    void syncFileWatches(const QStringList& desiredPaths);

    IScanStrategy* m_strategy = nullptr;
    QStringList m_directories; ///< registration order
    /// Owns the underlying `QFileSystemWatcher` when live-reload is
    /// enabled. `nullptr` is the canonical "live-reload off" state —
    /// `m_watcher != nullptr` is the single source of truth, no
    /// parallel `m_liveReloadEnabled` flag.
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
    /// Reentry depth — incremented at the top of every `rescanAll`, decremented
    /// at the bottom. A bool would clobber on nested invocations (a slot wired
    /// to `rescanCompleted` calling `registerDirectories` runs `rescanAll`
    /// synchronously while the outer is still on the stack). The replay branch
    /// fires only when depth returns to zero, so a nested scan never strands
    /// the outer scan's pending replay.
    int m_rescanDepth = 0;
    bool m_rescanRequestedWhileRunning = false;
};

} // namespace PhosphorFsLoader
