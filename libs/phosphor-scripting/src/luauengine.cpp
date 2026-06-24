// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorScripting/LuauEngine.h>
#include <PhosphorScripting/LuauWatchdog.h>

#include "luaumarshal.h"

#include <lua.h>
#include <lualib.h>
#include <luacode.h>

#include <cstdlib>
#include <locale.h>

namespace PhosphorScripting {

namespace {
// Compile/run budget for prelude + module chunks (well-behaved host code).
constexpr int ChunkTimeoutMs = 1000;

// RAII guard that pins the calling thread's LC_NUMERIC to "C" for its
// lifetime. luau_compile() lexes number literals through Luau's parser, which
// converts them with strtod() — and strtod() honours LC_NUMERIC. Under a locale
// whose decimal separator is not '.' (e.g. de_DE, fr_FR, most non-English
// European locales use ','), strtod("0.1") parses only "0" and stops at the
// '.', so Luau rejects the literal as a "Malformed number" and the entire chunk
// fails to compile. The bundled tiling algorithms and the pluau prelude are all
// written with '.'-decimal literals, so on a comma-decimal locale every one of
// them would fail to load and the user would see no algorithms at all. Scoping
// the override to the compiling thread via uselocale() keeps it off the global
// process locale, so user-facing number formatting elsewhere is untouched.
class ScopedCNumericLocale
{
public:
    ScopedCNumericLocale()
    {
        // Created once and intentionally never freed (process-lifetime). The
        // locale object is immutable after creation, so sharing the same
        // handle across threads via uselocale() is safe.
        static const locale_t s_cLocale = newlocale(LC_NUMERIC_MASK, "C", static_cast<locale_t>(nullptr));
        if (s_cLocale != static_cast<locale_t>(nullptr)) {
            m_previous = uselocale(s_cLocale);
        }
    }
    ~ScopedCNumericLocale()
    {
        if (m_previous != static_cast<locale_t>(nullptr)) {
            uselocale(m_previous);
        }
    }
    ScopedCNumericLocale(const ScopedCNumericLocale&) = delete;
    ScopedCNumericLocale& operator=(const ScopedCNumericLocale&) = delete;

private:
    locale_t m_previous = static_cast<locale_t>(nullptr);
};

// Read the error object on the top of the stack as text. Luau leaves a
// non-string error object (e.g. `error({})`) un-coercible, in which case
// lua_tostring returns null — substitute a placeholder so the caller never
// surfaces an empty diagnostic.
QString errorText(lua_State* L)
{
    const char* msg = lua_tostring(L, -1);
    return msg ? QString::fromUtf8(msg) : QStringLiteral("<non-string error object>");
}
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
    // only while enforcement is on (scoped to the protected call around script
    // execution — see guardedPcall). Returning nullptr makes Luau raise a
    // catchable out-of-memory error inside that protected call. Clamp osize to
    // used (mirroring the free path) so a contract-violating osize > used can
    // never underflow the projection and spuriously reject a valid allocation.
    const std::size_t safeOld = budget ? ((osize <= budget->used) ? osize : budget->used) : osize;
    if (budget && budget->enforce && budget->cap != 0) {
        const std::size_t projected = budget->used - safeOld + nsize;
        if (projected > budget->cap) {
            return nullptr;
        }
    }

    void* block = std::realloc(ptr, nsize);
    if (block && budget) {
        budget->used = budget->used - safeOld + nsize;
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
    char* bc = nullptr;
    {
        // Pin LC_NUMERIC to "C" so '.'-decimal literals lex correctly
        // regardless of the user's locale (see ScopedCNumericLocale).
        const ScopedCNumericLocale cNumeric;
        bc = luau_compile(source.constData(), static_cast<size_t>(source.size()), nullptr, &bcSize);
    }
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
            *error = errorText(m_L);
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
        // Heap-cap enforcement is armed per protected call (see guardedPcall),
        // not latched on here: only the untrusted module body / function bodies
        // execute inside a pcall, so the cap bounds them while host-side
        // marshalling, module loading, and field reads — which touch the VM
        // OUTSIDE any pcall — never hit a hard, uncatchable OOM abort().
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
    char* bc = nullptr;
    {
        // Pin LC_NUMERIC to "C" so '.'-decimal literals lex correctly
        // regardless of the user's locale (see ScopedCNumericLocale).
        const ScopedCNumericLocale cNumeric;
        bc = luau_compile(source.constData(), static_cast<size_t>(source.size()), nullptr, &bcSize);
    }
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
            *error = errorText(m_L);
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
    // A valid registry ref for a (non-nil) table is strictly positive; collapse
    // LUA_NOREF (-1) and LUA_REFNIL (0) into the single -1 "invalid" sentinel so
    // the >= 0 handle checks elsewhere never treat a non-handle as valid.
    if (handle <= 0) {
        if (error) {
            *error = QStringLiteral("failed to anchor module table");
        }
        return -1;
    }
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
    // Enforce the heap cap only for the duration of this protected call, so a
    // runaway script allocation becomes a catchable Luau OOM here (errorJmp is
    // set) rather than an uncatchable abort() in host-side marshalling that
    // runs outside any pcall. Trusted preludes run before sandbox() and stay
    // unenforced. Not re-entrant — the VM is single-threaded.
    m_memory.enforce = m_sandboxed;
    const int rc = lua_pcall(m_L, nargs, nresults, 0);
    m_memory.enforce = false;
    if (m_watchdog) {
        m_watchdog->disarm(this);
    }
    m_interrupt->store(false, std::memory_order_relaxed);

    if (rc != LUA_OK) {
        if (message) {
            *message = errorText(m_L);
        }
        lua_pop(m_L, 1);
        return m_timedOut ? CallStatus::TimedOut : CallStatus::Error;
    }
    return CallStatus::Ok;
}

} // namespace PhosphorScripting
