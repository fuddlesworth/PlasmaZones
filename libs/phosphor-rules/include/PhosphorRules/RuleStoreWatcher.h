// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <QObject>

#include <memory>

#include "phosphorrules_export.h"

namespace PhosphorFsLoader {
class WatchedDirectorySet;
}

namespace PhosphorRules {

class RuleStore;

/**
 * @brief Opt-in cross-process auto-reload for a RuleStore.
 *
 * Watches the store's backing @c rules.json and calls
 * @ref RuleStore::load() when it changes on disk, so a separate-process
 * consumer that OWNS its store (standalone @c plasmazones-settings /
 * @c plasmazones-editor, which have no D-Bus path to the daemon driving
 * reloads) reflects another process's writes without a manual reload.
 *
 * The hard parts are delegated to @c PhosphorFsLoader::WatchedDirectorySet:
 *   - a 50&nbsp;ms debounce that coalesces the temp-write + atomic-rename save
 *     burst into one reload,
 *   - re-arming the per-file watch after every rescan (an atomic rename swaps
 *     the inode, which @c QFileSystemWatcher silently drops),
 *   - parent-directory watching while the file does not exist yet, promoting to
 *     a direct watch when it materialises.
 *
 * Self-writes need no special handling: @ref RuleStore::load() is
 * idempotent — it re-reads the file but only emits @c rulesChanged when the
 * content actually differs — so a watcher event from the store's own
 * @c save() reloads to identical content and emits nothing.
 *
 * This is NOT for the daemon: the daemon is the sole writer and drives its own
 * reloads. Opt-in by construction — a consumer creates this only when it wants
 * the behaviour, so the base @ref RuleStore (and the daemon) carry no
 * watcher overhead.
 *
 * GUI-thread only — inherits @c WatchedDirectorySet's thread affinity: construct
 * and call @ref start() from the thread that owns the store.
 */
class PHOSPHORRULES_EXPORT RuleStoreWatcher : public QObject
{
    Q_OBJECT

public:
    /**
     * @param store  Borrowed store to reload on external change. Must outlive
     *               this watcher.
     * @param parent Qt parent.
     */
    explicit RuleStoreWatcher(RuleStore& store, QObject* parent = nullptr);
    ~RuleStoreWatcher() override;

    RuleStoreWatcher(const RuleStoreWatcher&) = delete;
    RuleStoreWatcher& operator=(const RuleStoreWatcher&) = delete;

    /**
     * @brief Begin watching.
     *
     * Registers the store file's parent directory with live reload and installs
     * the initial per-file watch. Deferred from the ctor so a consumer can
     * connect to the store's @c rulesChanged before the first scan runs.
     * Calling more than once is a no-op after the first.
     *
     * The registration triggers one immediate synchronous reload of the store;
     * because @ref RuleStore::load() is idempotent, that is a no-op when
     * the file has not changed since the store's own constructor load.
     */
    void start();

private:
    // Forward-declared; defined in the .cpp so the fsloader dependency stays
    // PRIVATE to this library (the header never includes a PhosphorFsLoader
    // type). The out-of-line dtor below completes both unique_ptr members.
    class ReloadStrategy;

    RuleStore* m_store;
    std::unique_ptr<ReloadStrategy> m_strategy;
    std::unique_ptr<PhosphorFsLoader::WatchedDirectorySet> m_watcher;
    bool m_started = false;
};

} // namespace PhosphorRules
