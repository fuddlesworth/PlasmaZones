// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <phosphorscripting_export.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <thread>
#include <unordered_map>

namespace PhosphorScripting {

/**
 * @brief One supervisor thread that interrupts runaway scripts across many engines.
 *
 * Each @ref LuauEngine registers a shared interrupt flag keyed by its own
 * address. On @ref arm the watchdog records a deadline; if the deadline passes
 * before @ref disarm, the watchdog sets `*flag = true`. The engine's interrupt
 * callback (running on the VM thread at the next safepoint) reads the flag and
 * unwinds via `luaL_error`.
 *
 * Crucially the watchdog only ever writes a `std::shared_ptr<std::atomic<bool>>`
 * it co-owns — it never touches the engine or its `lua_State`. A late write
 * racing engine teardown is therefore safe (the atomic outlives the engine via
 * the shared_ptr), which removes the use-after-free lock-discipline a
 * `setInterrupted`-style watchdog needs.
 *
 * One watchdog may be shared (via `std::shared_ptr`) by every engine in a
 * composition root, so N engines cost one supervisor thread, not N.
 */
class PHOSPHORSCRIPTING_EXPORT LuauWatchdog
{
public:
    LuauWatchdog();
    ~LuauWatchdog();

    LuauWatchdog(const LuauWatchdog&) = delete;
    LuauWatchdog& operator=(const LuauWatchdog&) = delete;

    /// Register @p key's interrupt flag. Idempotent; replaces an existing flag.
    void registerFlag(const void* key, std::shared_ptr<std::atomic<bool>> flag);

    /// Remove @p key entirely. Safe to call from the engine destructor.
    void unregister(const void* key);

    /// Start a @p timeoutMs deadline for @p key. No-op if @p key is unregistered
    /// or @p timeoutMs <= 0.
    void arm(const void* key, int timeoutMs);

    /// Cancel @p key's pending deadline.
    void disarm(const void* key);

private:
    struct Entry
    {
        std::shared_ptr<std::atomic<bool>> flag;
        std::chrono::steady_clock::time_point deadline = std::chrono::steady_clock::time_point::max();
        uint64_t generation = 0;
    };

    void threadMain();

    std::unordered_map<const void*, Entry> m_entries;
    std::thread m_thread;
    std::mutex m_mutex;
    std::condition_variable m_cv;
    bool m_shutdown = false;
};

} // namespace PhosphorScripting
