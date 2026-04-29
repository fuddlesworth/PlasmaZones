// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <phosphortiles_export.h>

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <thread>
#include <unordered_map>

namespace PhosphorTiles {

class ScriptedAlgorithm;

/**
 * @brief Per-loader watchdog for scripted algorithms
 *
 * A single OS thread services every live @ref ScriptedAlgorithm instance
 * registered with this watchdog by tracking the earliest deadline across
 * all armed algorithms and interrupting the one whose deadline expires
 * first. Replaces the previous "one std::thread per algorithm" model
 * (which did not scale and wasted one thread per registered script even
 * while idle) and the previous `instance()` Meyer's singleton (which
 * meant every process shared one watchdog regardless of plugin
 * topology).
 *
 * Ownership: @ref ScriptedAlgorithmLoader holds the watchdog via
 * @c std::shared_ptr and hands a @c shared_ptr copy to every
 * @ref ScriptedAlgorithm it constructs. The shared-ownership shape is
 * required because the registry destroys algorithms via
 * @c QObject::deleteLater — an algorithm's destructor (which calls
 * @ref unregister on this watchdog) can therefore run on a later event-
 * loop pass, after the loader is already gone. The watchdog thread is
 * joined in @c ~ScriptedAlgorithmWatchdog, which fires only when the
 * very last @c shared_ptr (the loader's or a deferred-delete algo's) is
 * released — so the "watchdog outlives every algorithm using it"
 * invariant is maintained by reference counting rather than member-
 * ordering.
 *
 * Usage (from @ref ScriptedAlgorithm::guardedCall):
 *  1. @ref arm(this, timeoutMs) before invoking the guarded JS callable.
 *  2. @ref disarm(this) after the JS call returns — advances the instance's
 *     generation so a race with the watchdog thread is detected and the
 *     eventual @ref ScriptedAlgorithm::interruptEngine call is suppressed.
 *
 * Thread-safety: all public methods are thread-safe. The watchdog thread
 * lives for the lifetime of the watchdog instance (joined in destructor).
 */
class PHOSPHORTILES_EXPORT ScriptedAlgorithmWatchdog
{
public:
    ScriptedAlgorithmWatchdog();
    ~ScriptedAlgorithmWatchdog();

    /**
     * @brief Arm the watchdog for @p algo with a timeout of @p timeoutMs ms
     *
     * If an arm entry already exists for @p algo (e.g. nested guardedCall),
     * the previous entry is replaced — the watchdog always honours the most
     * recent arm.
     */
    void arm(ScriptedAlgorithm* algo, int timeoutMs);

    /**
     * @brief Disarm the watchdog for @p algo
     *
     * Advances @p algo 's generation counter under the mutex so an in-flight
     * watchdog check observes the new generation and skips the interrupt.
     */
    void disarm(ScriptedAlgorithm* algo);

    /**
     * @brief Remove @p algo from the watchdog tracking map
     *
     * Called by @ref ScriptedAlgorithm 's destructor so the watchdog thread
     * cannot dereference a freed instance.
     *
     * @invariant Every @ref ScriptedAlgorithm that registers here holds a
     *   @c shared_ptr to this watchdog for its entire lifetime (see class
     *   comment). The caller of `unregister` is therefore still one of the
     *   owners when this method runs, so the watchdog instance is
     *   guaranteed alive across the call. Do not "optimize" the
     *   algorithm's watchdog reference to a raw or weak pointer — that
     *   would invalidate this invariant.
     */
    void unregister(ScriptedAlgorithm* algo);

    // Non-copyable, non-movable — ownership is shared via std::shared_ptr
    // (see class comment above for rationale). Move would invalidate the
    // mutex / condvar / std::thread members.
    ScriptedAlgorithmWatchdog(const ScriptedAlgorithmWatchdog&) = delete;
    ScriptedAlgorithmWatchdog& operator=(const ScriptedAlgorithmWatchdog&) = delete;
    ScriptedAlgorithmWatchdog(ScriptedAlgorithmWatchdog&&) = delete;
    ScriptedAlgorithmWatchdog& operator=(ScriptedAlgorithmWatchdog&&) = delete;

private:
    struct Entry
    {
        std::chrono::steady_clock::time_point deadline;
        uint64_t generation = 0; ///< Bumped on every arm() and disarm()
    };

    void threadMain();

    std::mutex m_mutex;
    std::condition_variable m_cv;
    std::unordered_map<ScriptedAlgorithm*, Entry> m_entries;
    bool m_shutdown = false;
    std::thread m_thread;
};

} // namespace PhosphorTiles
