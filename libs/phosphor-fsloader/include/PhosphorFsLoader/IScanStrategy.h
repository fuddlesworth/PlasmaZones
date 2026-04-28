// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <QtCore/QString>
#include <QtCore/QStringList>

namespace PhosphorFsLoader {

/**
 * @brief Pluggable enumeration / parse / commit policy for `WatchedDirectorySet`.
 *
 * The base class owns *when* a rescan happens (the watcher, the
 * 50 ms debounce, parent-watch promotion, the rescan-during-rescan race
 * guard, and re-arming individual file watches after the scan). The
 * strategy owns *what* a rescan means for one specific schema:
 *
 *   • Walking the directory (top-level `*.json`? subdirs with
 *     `metadata.json`? `*.js` matching a regex? — fully strategy-defined).
 *   • Parsing each entry into whatever the consumer's registry needs.
 *   • Computing the cross-directory user-wins / first-wins layering, if
 *     any — the base passes the registered directory list verbatim and
 *     does not pre-merge.
 *   • Committing to the consumer's registry (signals, replacement,
 *     stale-entry purge).
 *
 * The single `performScan` callback is invoked by the base every time
 * a rescan fires. Strategies are owned by the consumer (typically a
 * Q-stable member); the base holds them by reference and never copies.
 *
 * ## Convention for cross-directory layering
 *
 * The base does not impose a layering convention, but the in-tree
 * strategies all converge on:
 *
 *   • Caller registers directories in `[system-lowest-priority, ...,
 *     system-highest-priority, user]` order.
 *   • Strategy reverse-iterates and applies first-registration-wins.
 *
 * That combination yields the XDG semantic
 * (`user > sys-highest > sys-mid > ... > sys-lowest`) without any
 * per-strategy bookkeeping. New strategies should follow this shape
 * unless there is a documented reason to deviate.
 *
 * ## Emit semantics
 *
 * The base emits `WatchedDirectorySet::rescanCompleted` unconditionally
 * on every rescan, regardless of whether any state changed — the base
 * has no way to compare strategy-private payloads. Strategies that need
 * change-only emit semantics MUST diff inside `performScan` and gate
 * their own consumer-facing signal on the result (see
 * `ScriptedAlgorithmLoader`'s SHA-1 signature for the canonical
 * pattern, and the `BatchedSink<T>::lastBatchChanged` flag used by
 * CurveLoader / ProfileLoader).
 */
class IScanStrategy
{
public:
    virtual ~IScanStrategy() = default;

    IScanStrategy(const IScanStrategy&) = delete;
    IScanStrategy& operator=(const IScanStrategy&) = delete;

    /**
     * @brief Run a full rescan across the registered directories.
     *
     * Called from the base's `rescanAll` slot after parent-watch
     * promotion has run. The strategy is free to do as much or as
     * little work as it wants, including no-op early-exits when the
     * filesystem state hasn't changed.
     *
     * @param directoriesInScanOrder  Registered directories in the
     *                                order the consumer registered
     *                                them. The base does not impose a
     *                                user-first or system-first
     *                                convention — strategies decide
     *                                how to interpret the order
     *                                (`DirectoryLoader` reverse-iterates
     *                                for first-wins under the standard
     *                                "system-first, user-last" caller
     *                                convention).
     *
     * @return Absolute paths (files OR subdirectories) that the base
     *         should install per-path watches on after this rescan.
     *         Re-armed every rescan to compensate for
     *         `QFileSystemWatcher`'s automatic-drop-on-atomic-rename
     *         behaviour. May be empty — the consumer might rely
     *         entirely on directory-level watches and not need
     *         per-file resolution.
     *
     *         Strategies SHOULD NOT include the directories passed in
     *         `directoriesInScanOrder` themselves — the base already
     *         watches those directly via `attachWatcherForDir`. The
     *         base silently dedupes if a strategy violates this, but
     *         the contract keeps the watch set minimal and the
     *         per-file diagnostics clean.
     */
    virtual QStringList performScan(const QStringList& directoriesInScanOrder) = 0;

protected:
    IScanStrategy() = default;
};

} // namespace PhosphorFsLoader
