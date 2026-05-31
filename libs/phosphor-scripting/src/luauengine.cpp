// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorScripting/LuauEngine.h>
#include <PhosphorScripting/LuauWatchdog.h>

#include "luaumarshal.h"

#include <lua.h>
#include <lualib.h>
#include <luacode.h>

#include <cstdlib>

namespace PhosphorScripting {

namespace {
// Compile/run budget for prelude + module chunks (well-behaved host code).
constexpr int ChunkTimeoutMs = 1000;
} // namespace

LuauEngine::LuauEngine(std::shared_ptr<LuauWatchdog> watchdog, std::size_t memoryCapBytes)
    : m_watchdog(std::move(watchdog))
    , m_interrupt(std::make_shared<std::atomic<bool>>(false))
{
    m_memory.cap = memoryCapBytes;
}

void* LuauEngine::allocate(void* ud, void* ptr, std::size_t osize, std::size_t nsize)
{
    auto* budget = static_cast<MemoryBudget*>(ud);

    // Free: osize is the real old size (0 when ptr is null), per the Luau
    // frealloc contract.
    if (nsize == 0) {
        std::free(ptr);
        if (budget) {
            budget->used -= (osize <= budget->used) ? osize : budget->used;
        }
        return nullptr;
    }

    // Grow/alloc: reject anything that would push live bytes past the cap, but
    // only once enforcement is on (after sandbox()). Returning nullptr makes
    // Luau raise a catchable out-of-memory error inside the protected call.
    if (budget && budget->enforce && budget->cap != 0) {
        const std::size_t projected = budget->used - osize + nsize;
        if (projected > budget->cap) {
            return nullptr;
        }
    }

    void* block = std::realloc(ptr, nsize);
    if (block && budget) {
        budget->used = budget->used - osize + nsize;
        if (budget->used > budget->peak) {
            budget->peak = budget->used;
        }
    }
    return block;
}

LuauEngine::~LuauEngine()
{
    if (m_watchdog) {
        m_watchdog->unregister(this);
    }
    if (m_L) {
        lua_close(m_L);
    }
}

bool LuauEngine::init(QString* error)
{
    if (m_L) {
        return true; // already initialised
    }
    // Custom allocator so we can cap heap use; mirrors luaL_newstate (which is
    // just lua_newstate over a plain malloc wrapper) but routes through our
    // accounting. Enforcement stays off until sandbox(), so this trusted setup
    // and the preludes below — none of which run inside a protected call —
    // cannot be OOM-killed by a tight cap.
    m_memory.used = 0;
    m_memory.peak = 0;
    m_memory.enforce = false;
    m_L = lua_newstate(&LuauEngine::allocate, &m_memory);
    if (!m_L) {
        if (error) {
            *error = QStringLiteral("lua_newstate failed");
        }
        return false;
    }
    luaL_openlibs(m_L);

    lua_callbacks(m_L)->userdata = this;
    lua_callbacks(m_L)->interrupt = &LuauEngine::interruptCallback;

    if (m_watchdog) {
        m_watchdog->registerFlag(this, m_interrupt);
    }
    return true;
}

bool LuauEngine::runPrelude(const QString& chunkName, const QByteArray& source, QString* error)
{
    if (!m_L) {
        if (error) {
            *error = QStringLiteral("engine not initialised");
        }
        return false;
    }

    size_t bcSize = 0;
    char* bc = luau_compile(source.constData(), static_cast<size_t>(source.size()), nullptr, &bcSize);
    // luau_compile allocates the result with its own allocator (outside the VM
    // heap cap) and encodes syntax errors into a non-null blob — a null return
    // means allocation failure, not a syntax error.
    if (!bc) {
        if (error) {
            *error = QStringLiteral("luau_compile failed (out of memory)");
        }
        return false;
    }
    const int loadStatus = luau_load(m_L, chunkName.toUtf8().constData(), bc, bcSize, 0);
    std::free(bc);
    if (loadStatus != 0) {
        if (error) {
            *error = QString::fromUtf8(lua_tostring(m_L, -1));
        }
        lua_pop(m_L, 1);
        return false;
    }

    QString message;
    if (guardedPcall(0, 0, ChunkTimeoutMs, &message) != CallStatus::Ok) {
        if (error) {
            *error = message;
        }
        return false;
    }
    return true;
}

void LuauEngine::sandbox()
{
    if (m_L && !m_sandboxed) {
        luaL_sandbox(m_L);
        m_sandboxed = true;
        // From here on every allocation is on behalf of untrusted module code
        // (load + calls), all of which runs inside protected calls — so the cap
        // can be enforced safely, turning a runaway into a catchable OOM.
        m_memory.enforce = true;
    }
}

int LuauEngine::loadModule(const QString& chunkName, const QByteArray& source, QString* error)
{
    if (!m_L) {
        if (error) {
            *error = QStringLiteral("engine not initialised");
        }
        return -1;
    }

    size_t bcSize = 0;
    char* bc = luau_compile(source.constData(), static_cast<size_t>(source.size()), nullptr, &bcSize);
    // See runPrelude: a null blob is an allocation failure, not a syntax error.
    if (!bc) {
        if (error) {
            *error = QStringLiteral("luau_compile failed (out of memory)");
        }
        return -1;
    }
    const int loadStatus = luau_load(m_L, chunkName.toUtf8().constData(), bc, bcSize, 0);
    std::free(bc);
    if (loadStatus != 0) {
        if (error) {
            *error = QString::fromUtf8(lua_tostring(m_L, -1));
        }
        lua_pop(m_L, 1);
        return -1;
    }

    QString message;
    if (guardedPcall(0, 1, ChunkTimeoutMs, &message) != CallStatus::Ok) {
        if (error) {
            *error = message;
        }
        return -1;
    }

    if (!lua_istable(m_L, -1)) {
        if (error) {
            *error = QStringLiteral("module did not return a table");
        }
        lua_pop(m_L, 1);
        return -1;
    }

    const int handle = lua_ref(m_L, -1); // does not pop
    lua_pop(m_L, 1);
    return handle;
}

void LuauEngine::releaseModule(int handle)
{
    if (m_L && handle >= 0) {
        lua_unref(m_L, handle);
    }
}

QVariant LuauEngine::moduleField(int moduleHandle, const QString& key) const
{
    if (!m_L || moduleHandle < 0) {
        return {};
    }
    lua_getref(m_L, moduleHandle);
    if (!lua_istable(m_L, -1)) {
        lua_pop(m_L, 1);
        return {};
    }
    lua_getfield(m_L, -1, key.toUtf8().constData());
    const QVariant value = Marshal::toVariant(m_L, -1);
    lua_pop(m_L, 2); // field + module
    return value;
}

bool LuauEngine::hasFunction(int moduleHandle, const QString& name) const
{
    if (!m_L || moduleHandle < 0) {
        return false;
    }
    lua_getref(m_L, moduleHandle);
    if (!lua_istable(m_L, -1)) {
        lua_pop(m_L, 1);
        return false;
    }
    lua_getfield(m_L, -1, name.toUtf8().constData());
    const bool isFn = lua_isfunction(m_L, -1);
    lua_pop(m_L, 2); // field + module
    return isFn;
}

LuauEngine::CallOutcome LuauEngine::callModule(int moduleHandle, const QString& function, const QVariantList& args,
                                               int timeoutMs)
{
    CallOutcome out;
    if (!m_L || moduleHandle < 0) {
        out.message = QStringLiteral("invalid module handle");
        return out;
    }

    lua_getref(m_L, moduleHandle);
    if (!lua_istable(m_L, -1)) {
        lua_pop(m_L, 1);
        out.message = QStringLiteral("invalid module handle");
        return out;
    }
    lua_getfield(m_L, -1, function.toUtf8().constData());
    if (!lua_isfunction(m_L, -1)) {
        lua_pop(m_L, 2);
        out.message = QStringLiteral("module has no function '%1'").arg(function);
        return out;
    }
    lua_remove(m_L, -2); // drop module, keep the function

    for (const QVariant& arg : args) {
        Marshal::pushVariant(m_L, arg);
    }

    QString message;
    const CallStatus status = guardedPcall(static_cast<int>(args.size()), 1, timeoutMs, &message);
    if (status != CallStatus::Ok) {
        out.status = status;
        out.message = message;
        return out;
    }

    out.result = Marshal::toVariant(m_L, -1);
    lua_pop(m_L, 1);
    out.status = CallStatus::Ok;
    return out;
}

void LuauEngine::interruptCallback(lua_State* L, int gc)
{
    if (gc >= 0) {
        return; // ignore GC-step safepoints
    }
    auto* self = static_cast<LuauEngine*>(lua_callbacks(L)->userdata);
    if (self && self->m_interrupt && self->m_interrupt->load(std::memory_order_relaxed)) {
        self->m_timedOut = true;
        luaL_error(L, "script exceeded %d ms — interrupted", self->m_lastTimeoutMs);
    }
}

LuauEngine::CallStatus LuauEngine::guardedPcall(int nargs, int nresults, int timeoutMs, QString* message)
{
    m_interrupt->store(false, std::memory_order_relaxed);
    m_timedOut = false;
    m_lastTimeoutMs = timeoutMs;

    if (m_watchdog && timeoutMs > 0) {
        m_watchdog->arm(this, timeoutMs);
    }
    const int rc = lua_pcall(m_L, nargs, nresults, 0);
    if (m_watchdog) {
        m_watchdog->disarm(this);
    }
    m_interrupt->store(false, std::memory_order_relaxed);

    if (rc != LUA_OK) {
        if (message) {
            *message = QString::fromUtf8(lua_tostring(m_L, -1));
        }
        lua_pop(m_L, 1);
        return m_timedOut ? CallStatus::TimedOut : CallStatus::Error;
    }
    return CallStatus::Ok;
}

} // namespace PhosphorScripting
