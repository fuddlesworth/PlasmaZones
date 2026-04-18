// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

// Internal header — not part of the PhosphorTiles public API. Lives under src/
// so it cannot be included by out-of-tree consumers.

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <thread>
#include <unordered_map>

namespace PhosphorTiles {

class ScriptedAlgorithm;

/**
 * @brief Process-wide shared watchdog for scripted algorithms
 *
 * A single OS thread services every live @ref ScriptedAlgorithm instance by
 * tracking the earliest deadline across all armed algorithms and interrupting
 * the one whose deadline expires first. Replaces the previous "one
 * std::thread per algorithm" model, which did not scale and wasted one thread
 * per registered script even while idle.
 *
 * Usage (from @ref ScriptedAlgorithm::guardedCall):
 *  1. @ref arm(this, timeoutMs) before invoking the guarded JS callable.
 *  2. @ref disarm(this) after the JS call returns — advances the instance's
 *     generation so a race with the watchdog thread is detected and the
 *     eventual @ref ScriptedAlgorithm::interruptEngine call is suppressed.
 *
 * Thread-safety: all public methods are thread-safe. The watchdog thread
 * lives for the life of the process (joined in the singleton's destructor
 * on shutdown).
 */
class ScriptedAlgorithmWatchdog
{
public:
    /// Return the process-wide singleton. Starts the watchdog thread on first call.
    static ScriptedAlgorithmWatchdog& instance();

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
     */
    void unregister(ScriptedAlgorithm* algo);

    // Non-copyable, non-movable — owned as a Meyer's singleton
    ScriptedAlgorithmWatchdog(const ScriptedAlgorithmWatchdog&) = delete;
    ScriptedAlgorithmWatchdog& operator=(const ScriptedAlgorithmWatchdog&) = delete;
    ScriptedAlgorithmWatchdog(ScriptedAlgorithmWatchdog&&) = delete;
    ScriptedAlgorithmWatchdog& operator=(ScriptedAlgorithmWatchdog&&) = delete;

private:
    ScriptedAlgorithmWatchdog();
    ~ScriptedAlgorithmWatchdog();

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
