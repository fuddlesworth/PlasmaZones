// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <phosphorscripting_export.h>

#include <QByteArray>
#include <QString>
#include <QVariant>
#include <QVariantList>

#include <atomic>
#include <cstddef>
#include <memory>

struct lua_State;

namespace PhosphorScripting {

class LuauWatchdog;

/**
 * @brief An embedded, sandboxed Luau virtual machine with a QVariant API.
 *
 * Lifecycle: construct → @ref init → @ref runPrelude (zero or more, to install
 * host globals such as a `pluau` standard library) → @ref sandbox (freeze globals
 * + stdlib) → @ref loadModule (per script). Scripts are then driven via
 * @ref callModule / @ref moduleField.
 *
 * The `lua_State` is private: the public surface is entirely `QVariant`-based so
 * no Luau symbols cross the library boundary. Domain bindings marshal their
 * params into nested `QVariantMap`/`QVariantList` and read results back the same
 * way.
 *
 * Not thread-safe — like the underlying VM, all calls must occur on the owning
 * thread. Runaway scripts are bounded by the shared @ref LuauWatchdog.
 */
class PHOSPHORSCRIPTING_EXPORT LuauEngine
{
public:
    enum class CallStatus {
        Ok,
        TimedOut,
        Error,
    };

    struct CallOutcome
    {
        CallStatus status = CallStatus::Error;
        QVariant result; ///< Set when status == Ok (nil → invalid QVariant).
        QString message; ///< Set when status != Ok.
    };

    /// Default per-engine heap ceiling for untrusted script execution. Generous
    /// headroom over a tiling algorithm's real working set (which is well under
    /// 1 MiB beyond the ~1–2 MiB VM + stdlib baseline), while still bounding a
    /// runaway allocation long before it can exhaust the host.
    static constexpr std::size_t DefaultMemoryCapBytes = 64ull * 1024 * 1024;

    /// @p watchdog may be shared across engines; nullptr disables the timeout
    /// safety net (useful for short, trusted scripts in tests). @p memoryCapBytes
    /// caps heap allocated once the engine is sandboxed (0 = unlimited); the
    /// watchdog bounds CPU time, this bounds memory. See @ref sandbox.
    explicit LuauEngine(std::shared_ptr<LuauWatchdog> watchdog = nullptr,
                        std::size_t memoryCapBytes = DefaultMemoryCapBytes);
    ~LuauEngine();

    LuauEngine(const LuauEngine&) = delete;
    LuauEngine& operator=(const LuauEngine&) = delete;

    /// Create the VM, open the (restricted) standard libraries, and wire the
    /// interrupt callback. Returns false (and sets @p error) on failure.
    bool init(QString* error = nullptr);

    /// Compile + run a chunk that installs host globals. Must be called BEFORE
    /// @ref sandbox while the global table is still writable.
    bool runPrelude(const QString& chunkName, const QByteArray& source, QString* error = nullptr);

    /// Freeze the global table and standard libraries. Call once, after all
    /// preludes and before loading untrusted modules.
    void sandbox();

    /// Compile + load a module chunk and run it; the chunk is expected to
    /// `return` a table. Returns a handle (>= 0) to that table, or -1 on error.
    int loadModule(const QString& chunkName, const QByteArray& source, QString* error = nullptr);

    /// Release a module handle from @ref loadModule.
    void releaseModule(int handle);

    /// Read a (non-function) field from a loaded module as a QVariant. Returns
    /// an invalid QVariant if the handle/key is absent.
    QVariant moduleField(int moduleHandle, const QString& key) const;

    /// Whether the module exposes a callable field named @p name.
    bool hasFunction(int moduleHandle, const QString& name) const;

    /// Call `module[function](args...)` under the watchdog, returning a single
    /// result. @p timeoutMs <= 0 means no timeout.
    CallOutcome callModule(int moduleHandle, const QString& function, const QVariantList& args, int timeoutMs);

    bool isValid() const noexcept
    {
        return m_L != nullptr;
    }

    /// Live heap bytes currently allocated by the VM (host accounting).
    std::size_t memoryUsedBytes() const noexcept
    {
        return m_memory.used;
    }

    /// High-water mark of @ref memoryUsedBytes over the engine's lifetime.
    std::size_t peakMemoryBytes() const noexcept
    {
        return m_memory.peak;
    }

    /// The configured heap ceiling (0 = unlimited).
    std::size_t memoryCapBytes() const noexcept
    {
        return m_memory.cap;
    }

private:
    static void interruptCallback(lua_State* L, int gc);

    /// Custom `lua_Alloc`: tracks live/peak bytes and, while enforcement is
    /// armed (per protected call, once sandboxed — see @ref guardedPcall), fails
    /// (returns nullptr) any allocation that would push live bytes past the cap
    /// — Luau turns that into a catchable OOM inside the pcall.
    static void* allocate(void* ud, void* ptr, std::size_t osize, std::size_t nsize);

    /// Heap accounting backing the custom allocator. Plain (non-atomic): the VM
    /// — and therefore every allocation — is single-threaded. @ref enforce is
    /// armed only around the `lua_pcall` of sandboxed script execution; it stays
    /// off for trusted init/prelude/sandbox and for all host-side marshalling
    /// and module loading (which touch the VM outside any protected call), so a
    /// tight cap can never trigger an uncatchable OOM abort() in those phases.
    struct MemoryBudget
    {
        std::size_t used = 0;
        std::size_t peak = 0;
        std::size_t cap = 0; ///< 0 = unlimited
        bool enforce = false;
    };

    /// Watchdog-guarded `lua_pcall`. Function + @p nargs must already be on the
    /// stack; on Ok, @p nresults results are left on the stack. On failure the
    /// error object is popped and (optionally) returned via @p message.
    CallStatus guardedPcall(int nargs, int nresults, int timeoutMs, QString* message);

    lua_State* m_L = nullptr;
    std::shared_ptr<LuauWatchdog> m_watchdog;
    std::shared_ptr<std::atomic<bool>> m_interrupt;
    MemoryBudget m_memory;
    bool m_sandboxed = false;
    // VM-thread-only: written by guardedPcall before arming the watchdog and
    // read after disarming; the interrupt callback that consumes them also runs
    // on the VM thread (the watchdog thread only flips the atomic m_interrupt).
    bool m_timedOut = false;
    int m_lastTimeoutMs = 0;
};

} // namespace PhosphorScripting
