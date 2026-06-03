// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorScripting/LuauWatchdog.h>

namespace PhosphorScripting {

using Clock = std::chrono::steady_clock;

LuauWatchdog::LuauWatchdog()
{
    m_thread = std::thread([this] {
        threadMain();
    });
}

LuauWatchdog::~LuauWatchdog()
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

void LuauWatchdog::registerFlag(const void* key, std::shared_ptr<std::atomic<bool>> flag)
{
    if (!key || !flag) {
        return;
    }
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        Entry& entry = m_entries[key];
        entry.flag = std::move(flag);
        entry.deadline = Clock::time_point::max();
        ++entry.generation;
    }
    m_cv.notify_all();
}

void LuauWatchdog::unregister(const void* key)
{
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_entries.erase(key);
    }
    m_cv.notify_all();
}

void LuauWatchdog::arm(const void* key, int timeoutMs)
{
    if (timeoutMs <= 0) {
        return;
    }
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto it = m_entries.find(key);
        if (it == m_entries.end()) {
            return;
        }
        // Compute the deadline inside the lock so contention can't push it into
        // the past and fire a spurious interrupt.
        it->second.deadline = Clock::now() + std::chrono::milliseconds(timeoutMs);
        ++it->second.generation;
    }
    m_cv.notify_all();
}

void LuauWatchdog::disarm(const void* key)
{
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto it = m_entries.find(key);
        if (it != m_entries.end()) {
            ++it->second.generation;
            it->second.deadline = Clock::time_point::max();
        }
    }
    m_cv.notify_all();
}

void LuauWatchdog::threadMain()
{
    std::unique_lock<std::mutex> lock(m_mutex);
    while (!m_shutdown) {
        // Earliest armed deadline across all entries.
        const void* nextKey = nullptr;
        auto nextDeadline = Clock::time_point::max();
        uint64_t nextGeneration = 0;
        for (const auto& [key, entry] : m_entries) {
            if (entry.deadline < nextDeadline) {
                nextDeadline = entry.deadline;
                nextKey = key;
                nextGeneration = entry.generation;
            }
        }

        if (!nextKey || nextDeadline == Clock::time_point::max()) {
            m_cv.wait(lock, [this] {
                if (m_shutdown) {
                    return true;
                }
                for (const auto& [key, entry] : m_entries) {
                    if (entry.deadline != Clock::time_point::max()) {
                        return true;
                    }
                }
                return false;
            });
            continue;
        }

        const bool predicateWoke = m_cv.wait_until(lock, nextDeadline, [this, nextKey, nextGeneration] {
            if (m_shutdown) {
                return true;
            }
            auto it = m_entries.find(nextKey);
            if (it == m_entries.end()) {
                return true; // unregistered
            }
            return it->second.generation != nextGeneration; // disarmed / re-armed
        });

        if (m_shutdown) {
            break;
        }
        if (predicateWoke) {
            continue; // state changed; recompute
        }

        // Deadline fired and the entry still holds the generation we captured:
        // raise the interrupt flag. We touch only the shared atomic, never the
        // engine — so a concurrent engine teardown is safe.
        auto it = m_entries.find(nextKey);
        if (it != m_entries.end() && it->second.generation == nextGeneration) {
            it->second.deadline = Clock::time_point::max();
            if (it->second.flag) {
                it->second.flag->store(true, std::memory_order_relaxed);
            }
        }
    }
}

} // namespace PhosphorScripting
