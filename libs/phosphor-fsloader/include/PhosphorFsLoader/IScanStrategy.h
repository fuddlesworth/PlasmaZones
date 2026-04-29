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
 * The base **enforces** a canonical iteration shape so strategies don't
 * have to negotiate it from comments:
 *
 *   • The base normalises every caller's input into
 *     `[lowest-priority, ..., highest-priority]` order at the
 *     registration boundary (`registerDirectories` /
 *     `setDirectories` accept a `RegistrationOrder` enum so the
 *     caller declares which form their input is already in; the base
 *     reverses if needed).
 *   • Strategies receive `directoriesInScanOrder` already in this
 *     canonical shape and reverse-iterate with first-registration-wins.
 *
 * That combination yields the XDG semantic
 * (`user > sys-highest > sys-mid > ... > sys-lowest`) without any
 * per-strategy bookkeeping. The canonical shape is the strategy's
 * compile-time contract — implementations are free to assume it.
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
     * Reentry-safe: a strategy may synchronously call
     * `WatchedDirectorySet::requestRescan()` from inside its own
     * `performScan` (e.g. from a slot wired to its own commit signal
     * that fires while this scan is still on the stack). The base's
     * race-guard captures the request and replays it after the
     * outermost rescan returns; the inner scan does not deadlock or
     * lose the event. Pinned by
     * `test_watcheddirectoryset.cpp::testRescanRace_requestDuringScan`.
     *
     * @param directoriesInScanOrder  Registered directories in the
     *                                base's canonical
     *                                `[lowest-priority, ..., highest-priority]`
     *                                shape (the base normalises here
     *                                via the `RegistrationOrder`
     *                                parameter on `registerDirectories`).
     *                                Strategies reverse-iterate with
     *                                first-registration-wins to apply
     *                                the override, yielding
     *                                `user > sys-highest > ... > sys-lowest`.
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
