// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Unit tests for the PhosphorScripting Luau host: happy-path module calls +
// QVariant marshalling, prelude globals, the interrupt watchdog, compile-error
// surfacing, and the sandbox guarantees.

#include <QtTest>

#include <PhosphorScripting/LuauEngine.h>
#include <PhosphorScripting/LuauWatchdog.h>

#include <cstdio>
#include <cstdlib>
#include <locale.h>
#include <memory>

using namespace PhosphorScripting;

class TestLuauEngine : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void happyPathAndMarshalling();
    void marshalRoundTrip();
    void marshalDeepNesting();
    void preludeGlobals();
    void preludeCompileErrorSurfaces();
    void watchdogKillsInfiniteLoopAndRecovers();
    void compileErrorSurfaces();
    void sandboxHolds();
    void sandboxBlocksEscapeVectors();
    void runtimeErrorSurfaces();
    void distinctNamedFunctionsMutateSharedUpvalue();
    void nonTableModuleRejected();
    void callMissingFunctionFails();
    void memoryCapEnforcedAndRecovers();
    void memoryCapAllowsNormalWork();
    void compilesFloatLiteralsUnderCommaDecimalLocale();
    void executesNumberFormattingUnderCommaDecimalLocale();
};

namespace {
// Open the first installed comma-decimal locale from a candidate list, or
// nullptr if none is available. Callers QSKIP on nullptr.
locale_t openCommaDecimalLocale()
{
    static const char* const kCandidates[] = {"de_DE.UTF-8", "fr_FR.UTF-8", "nl_NL.UTF-8", "es_ES.UTF-8",
                                              "it_IT.UTF-8", "de_DE",       "fr_FR"};
    for (const char* name : kCandidates) {
        const locale_t loc = newlocale(LC_NUMERIC_MASK, name, static_cast<locale_t>(nullptr));
        if (loc != static_cast<locale_t>(nullptr)) {
            return loc;
        }
    }
    return static_cast<locale_t>(nullptr);
}
} // namespace

void TestLuauEngine::happyPathAndMarshalling()
{
    LuauEngine engine;
    QVERIFY(engine.init());
    engine.sandbox();

    QString err;
    const QByteArray mod = R"LUA(
        return {
            metadata = { id = "echo", name = "Echo" },
            tile = function(ctx)
                return { { x = ctx.x, y = 0, width = ctx.w, height = 5 } }
            end,
        }
    )LUA";
    const int handle = engine.loadModule(QStringLiteral("echo"), mod, &err);
    QVERIFY2(handle >= 0, qPrintable(err));

    QCOMPARE(engine.moduleField(handle, QStringLiteral("metadata")).toMap().value(QStringLiteral("name")).toString(),
             QStringLiteral("Echo"));

    QVariantMap ctx;
    ctx[QStringLiteral("x")] = 100;
    ctx[QStringLiteral("w")] = 50;
    const auto out = engine.callModule(handle, QStringLiteral("tile"), {ctx}, 200);
    QCOMPARE(out.status, LuauEngine::CallStatus::Ok);

    const QVariantList zones = out.result.toList();
    QCOMPARE(zones.size(), 1);
    const QVariantMap z = zones.at(0).toMap();
    QCOMPARE(z.value(QStringLiteral("x")).toInt(), 100);
    QCOMPARE(z.value(QStringLiteral("width")).toInt(), 50);
    QCOMPARE(z.value(QStringLiteral("height")).toInt(), 5);

    engine.releaseModule(handle);
}

void TestLuauEngine::marshalRoundTrip()
{
    LuauEngine engine;
    QVERIFY(engine.init());
    engine.sandbox();

    // Echo the argument straight back so nested map/list/scalar survive the
    // QVariant -> Lua -> QVariant round-trip.
    const int handle = engine.loadModule(QStringLiteral("echo"), "return { echo = function(ctx) return ctx end }");
    QVERIFY(handle >= 0);

    QVariantMap nested;
    nested[QStringLiteral("flag")] = true;
    nested[QStringLiteral("count")] = 7;
    nested[QStringLiteral("ratio")] = 0.25;
    nested[QStringLiteral("name")] = QStringLiteral("master");
    nested[QStringLiteral("sizes")] = QVariantList{1, 2, 3};

    const auto out = engine.callModule(handle, QStringLiteral("echo"), {nested}, 200);
    QCOMPARE(out.status, LuauEngine::CallStatus::Ok);

    const QVariantMap r = out.result.toMap();
    QCOMPARE(r.value(QStringLiteral("flag")).toBool(), true);
    QCOMPARE(r.value(QStringLiteral("count")).toInt(), 7);
    QCOMPARE(r.value(QStringLiteral("ratio")).toDouble(), 0.25);
    QCOMPARE(r.value(QStringLiteral("name")).toString(), QStringLiteral("master"));
    QCOMPARE(r.value(QStringLiteral("sizes")).toList(), (QVariantList{1, 2, 3}));
}

void TestLuauEngine::marshalDeepNesting()
{
    LuauEngine engine;
    QVERIFY(engine.init());
    engine.sandbox();

    // Echo back a map → list → map nest, exercising recursive marshalling in
    // both directions (and the lua_checkstack reservation at each level).
    const int handle = engine.loadModule(QStringLiteral("echo"), "return { echo = function(ctx) return ctx end }");
    QVERIFY(handle >= 0);

    QVariantMap inner;
    inner[QStringLiteral("leaf")] = 9;
    QVariantList list;
    list << QVariant(inner) << QVariant(2);
    QVariantMap outer;
    outer[QStringLiteral("items")] = list;
    outer[QStringLiteral("name")] = QStringLiteral("x");

    const auto out = engine.callModule(handle, QStringLiteral("echo"), {outer}, 200);
    QCOMPARE(out.status, LuauEngine::CallStatus::Ok);
    const QVariantMap r = out.result.toMap();
    QCOMPARE(r.value(QStringLiteral("name")).toString(), QStringLiteral("x"));
    const QVariantList items = r.value(QStringLiteral("items")).toList();
    QCOMPARE(items.size(), 2);
    QCOMPARE(items.at(0).toMap().value(QStringLiteral("leaf")).toInt(), 9);
    QCOMPARE(items.at(1).toInt(), 2);
    engine.releaseModule(handle);
}

void TestLuauEngine::preludeGlobals()
{
    LuauEngine engine;
    QVERIFY(engine.init());
    // Install a host global BEFORE sandboxing, then a module uses it.
    QVERIFY(engine.runPrelude(QStringLiteral("pre"), "function double(n) return n * 2 end"));
    engine.sandbox();

    const int handle =
        engine.loadModule(QStringLiteral("m"), "return { tile = function(ctx) return { double(ctx.n) } end }");
    QVERIFY(handle >= 0);

    QVariantMap ctx;
    ctx[QStringLiteral("n")] = 21;
    const auto out = engine.callModule(handle, QStringLiteral("tile"), {ctx}, 200);
    QCOMPARE(out.status, LuauEngine::CallStatus::Ok);
    QCOMPARE(out.result.toList().value(0).toInt(), 42);
}

void TestLuauEngine::preludeCompileErrorSurfaces()
{
    LuauEngine engine;
    QVERIFY(engine.init());
    // A prelude that fails to compile must return false with a non-empty error,
    // not silently install nothing.
    QString err;
    QVERIFY(!engine.runPrelude(QStringLiteral("bad"), "function broken(", &err));
    QVERIFY(!err.isEmpty());
}

void TestLuauEngine::watchdogKillsInfiniteLoopAndRecovers()
{
    auto watchdog = std::make_shared<LuauWatchdog>();
    LuauEngine engine(watchdog);
    QVERIFY(engine.init());
    engine.sandbox();

    const int hang = engine.loadModule(QStringLiteral("hang"), "return { tile = function(ctx) while true do end end }");
    QVERIFY(hang >= 0);
    const auto out = engine.callModule(hang, QStringLiteral("tile"), {}, 150);
    QCOMPARE(out.status, LuauEngine::CallStatus::TimedOut);

    // The engine must recover and run a well-behaved module afterwards.
    const int ok = engine.loadModule(QStringLiteral("ok"), "return { tile = function(ctx) return { 1, 2, 3 } end }");
    QVERIFY(ok >= 0);
    const auto out2 = engine.callModule(ok, QStringLiteral("tile"), {}, 200);
    QCOMPARE(out2.status, LuauEngine::CallStatus::Ok);
    QCOMPARE(out2.result.toList().size(), 3);
}

void TestLuauEngine::compileErrorSurfaces()
{
    LuauEngine engine;
    QVERIFY(engine.init());
    engine.sandbox();

    QString err;
    const int handle =
        engine.loadModule(QStringLiteral("bad"), "return { tile = function(ctx) this is not lua }", &err);
    QCOMPARE(handle, -1);
    QVERIFY(!err.isEmpty());
}

void TestLuauEngine::sandboxHolds()
{
    LuauEngine engine;
    QVERIFY(engine.init());
    engine.sandbox();

    // Each assert that holds proves a guarantee; if any fails, tile() errors.
    const QByteArray probe = R"LUA(
        return { tile = function(ctx)
            assert(io == nil, "io reachable")
            assert(os == nil or os.execute == nil, "os.execute reachable")
            assert(loadstring == nil, "loadstring reachable")
            assert(not pcall(function() string.format = nil end), "stdlib writable")
            return {}
        end }
    )LUA";
    const int handle = engine.loadModule(QStringLiteral("probe"), probe);
    QVERIFY(handle >= 0);

    const auto out = engine.callModule(handle, QStringLiteral("tile"), {}, 200);
    QCOMPARE(out.status, LuauEngine::CallStatus::Ok);
}

void TestLuauEngine::sandboxBlocksEscapeVectors()
{
    LuauEngine engine;
    QVERIFY(engine.init());
    engine.sandbox();

    // Each entry must be true: the named escape vector is unreachable from a
    // sandboxed script. Returning a bool map (rather than asserting in Lua) so a
    // single regression names exactly which capability leaked.
    // Note: Luau keeps getfenv/setfenv for compatibility, but luaL_sandbox
    // freezes the global table, so neither can be used to mutate the
    // environment — that frozen-ness is the actual guarantee, asserted below.
    const QByteArray probe = R"LUA(
        return { probe = function()
            return {
                io = io == nil,
                osExecute = (os == nil) or (os.execute == nil),
                loadstring = loadstring == nil,
                loadfile = loadfile == nil,
                dofile = dofile == nil,
                stringDump = string.dump == nil,
                stdlibFrozen = not pcall(function() string.format = nil end),
                globalsFrozen = not pcall(function() _G.__pzevil = true end),
                envFrozenViaGetfenv = (getfenv == nil) or not pcall(function() getfenv().__pzevil2 = true end),
            }
        end }
    )LUA";
    const int handle = engine.loadModule(QStringLiteral("escape"), probe);
    QVERIFY(handle >= 0);

    const auto out = engine.callModule(handle, QStringLiteral("probe"), {}, 200);
    QCOMPARE(out.status, LuauEngine::CallStatus::Ok);
    const QVariantMap caps = out.result.toMap();
    QVERIFY(!caps.isEmpty());
    for (auto it = caps.constBegin(); it != caps.constEnd(); ++it) {
        QVERIFY2(it.value().toBool(), qPrintable(QStringLiteral("escape vector reachable: ") + it.key()));
    }
    engine.releaseModule(handle);
}

void TestLuauEngine::runtimeErrorSurfaces()
{
    LuauEngine engine;
    QVERIFY(engine.init());
    engine.sandbox();

    // A string error propagates as Error with the message preserved.
    const int h1 = engine.loadModule(QStringLiteral("e1"), "return { f = function() error('kaboom') end }");
    QVERIFY(h1 >= 0);
    const auto o1 = engine.callModule(h1, QStringLiteral("f"), {}, 200);
    QCOMPARE(o1.status, LuauEngine::CallStatus::Error);
    QVERIFY(o1.message.contains(QStringLiteral("kaboom")));

    // A non-string error object (no __tostring) must still yield a non-empty
    // diagnostic rather than an empty string (errorText placeholder).
    const int h2 = engine.loadModule(QStringLiteral("e2"), "return { f = function() error({ code = 1 }) end }");
    QVERIFY(h2 >= 0);
    const auto o2 = engine.callModule(h2, QStringLiteral("f"), {}, 200);
    QCOMPARE(o2.status, LuauEngine::CallStatus::Error);
    QVERIFY(!o2.message.isEmpty());
}

void TestLuauEngine::distinctNamedFunctionsMutateSharedUpvalue()
{
    LuauEngine engine;
    QVERIFY(engine.init());
    engine.sandbox();

    const int h =
        engine.loadModule(QStringLiteral("counter"),
                          "local n = 0 return { add = function() n = n + 1 end, sub = function() n = n - 1 end, "
                          "get = function() return n end }");
    QVERIFY(h >= 0);
    QVERIFY(engine.hasFunction(h, QStringLiteral("add")));
    QVERIFY(engine.hasFunction(h, QStringLiteral("sub")));

    engine.callModule(h, QStringLiteral("add"), {}, 200);
    engine.callModule(h, QStringLiteral("add"), {}, 200);
    engine.callModule(h, QStringLiteral("add"), {}, 200);
    QCOMPARE(engine.callModule(h, QStringLiteral("get"), {}, 200).result.toInt(), 3);

    engine.callModule(h, QStringLiteral("sub"), {}, 200);
    QCOMPARE(engine.callModule(h, QStringLiteral("get"), {}, 200).result.toInt(), 2);
    engine.releaseModule(h);
}

void TestLuauEngine::nonTableModuleRejected()
{
    LuauEngine engine;
    QVERIFY(engine.init());
    engine.sandbox();

    QString err;
    const int handle = engine.loadModule(QStringLiteral("num"), "return 42", &err);
    QCOMPARE(handle, -1);
    QVERIFY(err.contains(QStringLiteral("table")));
}

void TestLuauEngine::callMissingFunctionFails()
{
    LuauEngine engine;
    QVERIFY(engine.init());
    engine.sandbox();

    const int handle = engine.loadModule(QStringLiteral("m"), "return { a = 1 }");
    QVERIFY(handle >= 0);
    const auto out = engine.callModule(handle, QStringLiteral("nope"), {}, 200);
    QCOMPARE(out.status, LuauEngine::CallStatus::Error);
    QVERIFY(out.message.contains(QStringLiteral("nope")));
}

void TestLuauEngine::memoryCapEnforcedAndRecovers()
{
    // Cap well above the VM + stdlib baseline (~1-2 MiB) but far below what the
    // runaway below grabs, so init/sandbox succeed and the cap bites only once
    // the untrusted script starts allocating.
    constexpr std::size_t kCap = 16ull * 1024 * 1024;
    auto watchdog = std::make_shared<LuauWatchdog>();
    LuauEngine engine(watchdog, kCap);
    QVERIFY(engine.init());
    engine.sandbox();
    QCOMPARE(engine.memoryCapBytes(), kCap);

    // Grow a table without bound. The allocator fails the realloc that would
    // cross the cap; Luau raises a catchable OOM inside the protected call, so
    // this surfaces as Error (not a crash, and not a TimedOut — the generous
    // timeout is only a backstop).
    const int hog = engine.loadModule(
        QStringLiteral("hog"),
        "return { tile = function(ctx) local t = {} for i = 1, 1000000000 do t[i] = i end return {} end }");
    QVERIFY(hog >= 0);
    const auto out = engine.callModule(hog, QStringLiteral("tile"), {}, 5000);
    QCOMPARE(out.status, LuauEngine::CallStatus::Error);
    QVERIFY(!out.message.isEmpty());
    // The cap was actually the limiter — live bytes never exceeded it.
    QVERIFY(engine.peakMemoryBytes() <= kCap);

    // The engine must recover and run a well-behaved module afterwards.
    const int ok = engine.loadModule(QStringLiteral("ok"), "return { tile = function(ctx) return { 1, 2, 3 } end }");
    QVERIFY(ok >= 0);
    const auto out2 = engine.callModule(ok, QStringLiteral("tile"), {}, 200);
    QCOMPARE(out2.status, LuauEngine::CallStatus::Ok);
    QCOMPARE(out2.result.toList().size(), 3);
}

void TestLuauEngine::memoryCapAllowsNormalWork()
{
    // The default cap must never get in the way of ordinary tiling work.
    LuauEngine engine; // default DefaultMemoryCapBytes
    QVERIFY(engine.init());
    engine.sandbox();

    const int handle =
        engine.loadModule(QStringLiteral("m"), "return { tile = function(ctx) return { { x = 0, width = 10 } } end }");
    QVERIFY(handle >= 0);
    const auto out = engine.callModule(handle, QStringLiteral("tile"), {}, 200);
    QCOMPARE(out.status, LuauEngine::CallStatus::Ok);

    // Baseline footprint sits far under the cap — documents the headroom.
    QVERIFY(engine.peakMemoryBytes() > 0);
    QVERIFY(engine.peakMemoryBytes() < engine.memoryCapBytes());
    QVERIFY(engine.peakMemoryBytes() < 16ull * 1024 * 1024);
}

void TestLuauEngine::compilesFloatLiteralsUnderCommaDecimalLocale()
{
    // Regression for discussion #690: luau_compile() lexes number literals with
    // strtod(), which honours LC_NUMERIC. Under a locale whose decimal separator
    // is ',', an unguarded compile parses "0.25" as just "0" with a trailing
    // ".25", so Luau rejects every '.'-decimal literal as "Malformed number" and
    // the pluau prelude + every bundled algorithm fail to load. The engine must
    // pin LC_NUMERIC to "C" across the compile so '.'-decimal literals parse
    // regardless of the ambient locale.
    const locale_t commaLocale = openCommaDecimalLocale();
    if (commaLocale == static_cast<locale_t>(nullptr)) {
        QSKIP("No comma-decimal locale installed; cannot exercise the LC_NUMERIC regression");
    }

    const locale_t previous = uselocale(commaLocale);
    // Guard against a no-op test: confirm the ambient locale really does
    // mis-parse '.' decimals, so a pass proves the engine's fix rather than an
    // environment where the bug could never reproduce.
    const bool ambientMisparsesDot = (std::strtod("0.5", nullptr) != 0.5);

    LuauEngine engine;
    const bool inited = engine.init();
    QString err;
    const int handle = engine.loadModule(
        QStringLiteral("ratios"),
        "return { tile = function(ctx) return { { x = 0, y = 0, width = 0.25, height = 0.5 } } end }", &err);

    // Restore the thread locale before any QVERIFY can early-return, then free
    // the now-unused handle.
    uselocale(previous);
    freelocale(commaLocale);

    QVERIFY(inited);
    QVERIFY2(ambientMisparsesDot, "selected locale is not actually comma-decimal; the test would be a no-op");
    QVERIFY2(handle >= 0, qPrintable(err));

    engine.sandbox();
    const auto out = engine.callModule(handle, QStringLiteral("tile"), {}, 200);
    QCOMPARE(out.status, LuauEngine::CallStatus::Ok);
    const QVariantMap z = out.result.toList().value(0).toMap();
    QCOMPARE(z.value(QStringLiteral("width")).toDouble(), 0.25);
    QCOMPARE(z.value(QStringLiteral("height")).toDouble(), 0.5);
    engine.releaseModule(handle);
}

void TestLuauEngine::executesNumberFormattingUnderCommaDecimalLocale()
{
    // Companion to compilesFloatLiteralsUnderCommaDecimalLocale: even after a
    // chunk compiles, Luau's runtime number→string conversions (string.format,
    // tostring) go through locale-sensitive snprintf. The engine pins LC_NUMERIC
    // to "C" across script execution too, so a script that formats a decimal
    // emits '.'-decimal output regardless of the ambient locale.
    const locale_t commaLocale = openCommaDecimalLocale();
    if (commaLocale == static_cast<locale_t>(nullptr)) {
        QSKIP("No comma-decimal locale installed; cannot exercise the LC_NUMERIC regression");
    }

    // Compile under the default locale (the compile path has its own test); this
    // case isolates the runtime-execution path, so the locale only needs to be
    // comma-decimal while the script body runs below.
    LuauEngine engine;
    QVERIFY(engine.init());
    engine.sandbox();
    QString err;
    const int handle = engine.loadModule(QStringLiteral("fmt"),
                                         "return { fmt = function() return string.format('%.3f', 0.25) end }", &err);
    QVERIFY2(handle >= 0, qPrintable(err));

    const locale_t previous = uselocale(commaLocale);
    // Sanity: confirm the C library really formats decimals with ',' under this
    // locale, so a pass proves the engine's runtime guard rather than a no-op
    // environment.
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%.1f", 0.5);
    const bool ambientFormatsWithComma = (QByteArray(buf) == "0,5");

    const auto out = engine.callModule(handle, QStringLiteral("fmt"), {}, 200);

    // Restore the thread locale before any QVERIFY can early-return.
    uselocale(previous);
    freelocale(commaLocale);

    QVERIFY2(ambientFormatsWithComma, "selected locale is not actually comma-decimal; the test would be a no-op");
    QCOMPARE(out.status, LuauEngine::CallStatus::Ok);
    QCOMPARE(out.result.toString(), QStringLiteral("0.250"));
    engine.releaseModule(handle);
}

QTEST_MAIN(TestLuauEngine)
#include "test_luau_engine.moc"
