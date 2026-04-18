// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include "scriptedalgorithmwatchdog.h"

#include <PhosphorTiles/ScriptedAlgorithm.h>

#include "tileslogging.h"

namespace PhosphorTiles {

ScriptedAlgorithmWatchdog& ScriptedAlgorithmWatchdog::instance()
{
    static ScriptedAlgorithmWatchdog inst;
    return inst;
}

ScriptedAlgorithmWatchdog::ScriptedAlgorithmWatchdog()
{
    m_thread = std::thread([this]() {
        threadMain();
    });
}

ScriptedAlgorithmWatchdog::~ScriptedAlgorithmWatchdog()
{
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_shutdown = true;
    }
    m_cv.notify_all();
    if (m_thread.joinable()) {
        m_thread.join();
    }
}

void ScriptedAlgorithmWatchdog::arm(ScriptedAlgorithm* algo, int timeoutMs)
{
    if (!algo || timeoutMs <= 0) {
        return;
    }
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        Entry& entry = m_entries[algo];
        entry.deadline = deadline;
        ++entry.generation;
    }
    m_cv.notify_all();
}

void ScriptedAlgorithmWatchdog::disarm(ScriptedAlgorithm* algo)
{
    if (!algo) {
        return;
    }
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto it = m_entries.find(algo);
        if (it != m_entries.end()) {
            // Bump the generation so any in-flight watchdog check notices the
            // disarm and skips the interrupt. Erasing the entry outright would
            // also work, but keeping the record lets the next arm() re-use the
            // same generation counter.
            ++it->second.generation;
            // Push the deadline far into the future so the watchdog thread
            // ignores this entry until the next arm().
            it->second.deadline = std::chrono::steady_clock::time_point::max();
        }
    }
    m_cv.notify_all();
}

void ScriptedAlgorithmWatchdog::unregister(ScriptedAlgorithm* algo)
{
    if (!algo) {
        return;
    }
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_entries.erase(algo);
    }
    m_cv.notify_all();
}

void ScriptedAlgorithmWatchdog::threadMain()
{
    std::unique_lock<std::mutex> lock(m_mutex);
    while (!m_shutdown) {
        // Find the entry with the earliest deadline.
        ScriptedAlgorithm* nextAlgo = nullptr;
        auto nextDeadline = std::chrono::steady_clock::time_point::max();
        uint64_t nextGeneration = 0;
        for (const auto& [algo, entry] : m_entries) {
            if (entry.deadline < nextDeadline) {
                nextDeadline = entry.deadline;
                nextAlgo = algo;
                nextGeneration = entry.generation;
            }
        }

        if (!nextAlgo || nextDeadline == std::chrono::steady_clock::time_point::max()) {
            // Nothing armed (either no entries, or every entry is disarmed
            // with deadline == max). Sleep until a new arm() notifies or
            // shutdown. The predicate must check for an armed deadline, not
            // just "non-empty" — otherwise we busy-loop on the disarmed set.
            m_cv.wait(lock, [this]() {
                if (m_shutdown) {
                    return true;
                }
                for (const auto& [algo, entry] : m_entries) {
                    if (entry.deadline != std::chrono::steady_clock::time_point::max()) {
                        return true;
                    }
                }
                return false;
            });
            continue;
        }

        // Wait until either the deadline expires, a new arm() arrives (which
        // may be earlier than nextDeadline), or shutdown is signalled. Use
        // wait_until + predicate so the loop observes generation changes on
        // the targeted entry.
        const bool predicateTriggered = m_cv.wait_until(lock, nextDeadline, [this, nextAlgo, nextGeneration]() {
            if (m_shutdown) {
                return true;
            }
            auto it = m_entries.find(nextAlgo);
            if (it == m_entries.end()) {
                return true; // unregistered
            }
            // Generation changed → disarm or re-arm happened; recompute.
            return it->second.generation != nextGeneration;
        });

        if (m_shutdown) {
            break;
        }
        if (predicateTriggered) {
            // Disarm / unregister / re-arm — recompute on next iteration.
            continue;
        }

        // Timeout expired and the entry still has the generation we captured:
        // fire the interrupt. Keep holding the mutex while calling
        // interruptEngine() — ScriptedAlgorithm's destructor calls unregister()
        // on this same mutex, so while we hold it the instance cannot be
        // destroyed from under us. QJSEngine::setInterrupted is documented
        // thread-safe relative to the target engine's evaluation thread.
        auto it = m_entries.find(nextAlgo);
        if (it != m_entries.end() && it->second.generation == nextGeneration) {
            // Push the deadline out so this entry does not repeatedly fire
            // until the owner disarms.
            it->second.deadline = std::chrono::steady_clock::time_point::max();
            nextAlgo->interruptEngine();
        }
    }
}

} // namespace PhosphorTiles
