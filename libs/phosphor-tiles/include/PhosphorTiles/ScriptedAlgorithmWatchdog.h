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
 * Ownership: @ref ScriptedAlgorithmLoader owns one watchdog via
 * unique_ptr and passes a borrowed pointer to every
 * @ref ScriptedAlgorithm it constructs. The watchdog must outlive every
 * algorithm it tracks — the loader's destructor unregisters its scripts
 * (which destroys the algorithm instances) before the loader's
 * unique_ptr<watchdog> member is destroyed in reverse-declaration order.
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
     */
    void unregister(ScriptedAlgorithm* algo);

    // Non-copyable, non-movable — owned as a unique_ptr by ScriptedAlgorithmLoader
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
