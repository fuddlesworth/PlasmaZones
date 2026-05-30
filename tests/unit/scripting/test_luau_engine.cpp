// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Unit tests for the PhosphorScripting Luau host: happy-path module calls +
// QVariant marshalling, prelude globals, the interrupt watchdog, compile-error
// surfacing, and the sandbox guarantees.

#include <QtTest>

#include <PhosphorScripting/LuauEngine.h>
#include <PhosphorScripting/LuauWatchdog.h>

#include <memory>

using namespace PhosphorScripting;

class TestLuauEngine : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void happyPathAndMarshalling();
    void marshalRoundTrip();
    void preludeGlobals();
    void watchdogKillsInfiniteLoopAndRecovers();
    void compileErrorSurfaces();
    void sandboxHolds();
};

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

QTEST_MAIN(TestLuauEngine)
#include "test_luau_engine.moc"
