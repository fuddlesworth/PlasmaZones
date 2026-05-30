// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorScripting/LuauEngine.h>
#include <PhosphorScripting/LuauWatchdog.h>

#include "luaumarshal.h"
#include "scriptinglogging.h"

#include <lua.h>
#include <lualib.h>
#include <luacode.h>

#include <cstdlib>

namespace PhosphorScripting {

namespace {
// Compile/run budget for prelude + module chunks (well-behaved host code).
constexpr int ChunkTimeoutMs = 1000;
} // namespace

LuauEngine::LuauEngine(std::shared_ptr<LuauWatchdog> watchdog)
    : m_watchdog(std::move(watchdog))
    , m_interrupt(std::make_shared<std::atomic<bool>>(false))
{
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
    m_L = luaL_newstate();
    if (!m_L) {
        if (error) {
            *error = QStringLiteral("luaL_newstate failed");
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
