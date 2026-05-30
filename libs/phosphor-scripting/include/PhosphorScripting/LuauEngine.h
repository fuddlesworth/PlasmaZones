// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <phosphorscripting_export.h>

#include <QByteArray>
#include <QString>
#include <QVariant>
#include <QVariantList>

#include <atomic>
#include <memory>

struct lua_State;

namespace PhosphorScripting {

class LuauWatchdog;

/**
 * @brief An embedded, sandboxed Luau virtual machine with a QVariant API.
 *
 * Lifecycle: construct → @ref init → @ref runPrelude (zero or more, to install
 * host globals such as a `pz` standard library) → @ref sandbox (freeze globals
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

    /// @p watchdog may be shared across engines; nullptr disables the timeout
    /// safety net (useful for short, trusted scripts in tests).
    explicit LuauEngine(std::shared_ptr<LuauWatchdog> watchdog = nullptr);
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

private:
    static void interruptCallback(lua_State* L, int gc);

    /// Watchdog-guarded `lua_pcall`. Function + @p nargs must already be on the
    /// stack; on Ok, @p nresults results are left on the stack. On failure the
    /// error object is popped and (optionally) returned via @p message.
    CallStatus guardedPcall(int nargs, int nresults, int timeoutMs, QString* message);

    lua_State* m_L = nullptr;
    std::shared_ptr<LuauWatchdog> m_watchdog;
    std::shared_ptr<std::atomic<bool>> m_interrupt;
    bool m_sandboxed = false;
    bool m_timedOut = false;
    int m_lastTimeoutMs = 0;
};

} // namespace PhosphorScripting
