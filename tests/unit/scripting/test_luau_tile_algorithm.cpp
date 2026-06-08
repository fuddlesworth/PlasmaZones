// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Phase 2 exit criterion: a LuauTileAlgorithm runs end-to-end through the
// TilingAlgorithm interface — happy-path zone computation + metadata accessors,
// split-tree marshalling (memory), and lifecycle hooks.

#include <QtTest>

#include <PhosphorTiles/AutotileConstants.h>
#include <PhosphorTiles/LuauTileAlgorithm.h>
#include <PhosphorTiles/SplitTree.h>
#include <PhosphorTiles/TilingParams.h>
#include <PhosphorTiles/TilingState.h>

#include <PhosphorScripting/LuauEngine.h>
#include <PhosphorScripting/LuauWatchdog.h>

#include "../helpers/ScriptTestHelpers.h"

#include <QTemporaryDir>

#include <memory>

using namespace PhosphorTiles;

class TestLuauTileAlgorithm : public QObject
{
    Q_OBJECT

private:
    std::shared_ptr<PhosphorScripting::LuauWatchdog> m_watchdog;
    QTemporaryDir m_tmp;
    static QString scriptPath(const QString& name)
    {
        return QStringLiteral(P_LUAU_TEST_DIR "/data/") + name;
    }

private Q_SLOTS:
    void initTestCase()
    {
        m_watchdog = std::make_shared<PhosphorScripting::LuauWatchdog>();
        QVERIFY(m_tmp.isValid());
    }

    void loadsAndExposesMetadata();
    void computesMasterStackZones();
    void marshalsSplitTree();
    void lifecycleHooksRunAndPersist();
    void metadataOutOfRangeValuesClamped();
    void customParamInvertedRangeNormalized();
    void malformedZonesKeepCountAndStayInArea();
    void missingTileFunctionIsInvalid();
    void runtimeErrorYieldsEmptyZones();
    void zoneCountCappedAtMaxZones();
    void oversizedScriptRejected();
    void enumAndBoolCustomParamsParsed();
    void onWindowRemovedHookRuns();
    void nonFiniteOverrideRejected();
    void scriptedMetadataFlagsExposed();
    void zoneNumberDisplayInvalidFallsBack();
    void metadataInfiniteSplitRatioClamped();
    void erroringOverrideFallsBack();
    void customParamValueReachesTile();
    void sharedEngineHostsIndependentModules();
};

void TestLuauTileAlgorithm::loadsAndExposesMetadata()
{
    LuauTileAlgorithm algo(scriptPath(QStringLiteral("master-stack.luau")), m_watchdog);
    QVERIFY(algo.isValid());
    QCOMPARE(algo.name(), QStringLiteral("Master + Stack"));
    QCOMPARE(algo.id(), QStringLiteral("master-stack"));
    QVERIFY(algo.supportsMasterCount());
    QVERIFY(algo.supportsSplitRatio());
    QVERIFY(algo.isScripted());
    QCOMPARE(algo.defaultSplitRatio(), 0.6);
}

void TestLuauTileAlgorithm::computesMasterStackZones()
{
    LuauTileAlgorithm algo(scriptPath(QStringLiteral("master-stack.luau")), m_watchdog);
    QVERIFY(algo.isValid());

    TilingState state(QStringLiteral("screen-1"));
    state.setSplitRatio(0.6);

    TilingParams p;
    p.windowCount = 5;
    p.screenGeometry = QRect(0, 0, 1920, 1080);
    p.state = &state;
    p.innerGap = 8;
    p.outerGaps = EdgeGaps::uniform(0); // full screen is the usable area

    const QVector<QRect> zones = algo.calculateZones(p);
    QCOMPARE(zones.size(), 5);

    // First zone is the master column: ~60% of the 1920 width, full height.
    QVERIFY(zones[0].width() > 1100 && zones[0].width() < 1160);
    QCOMPARE(zones[0].height(), 1080);
    // Every zone stays inside the screen and none overlap.
    const QRect area(0, 0, 1920, 1080);
    for (int i = 0; i < zones.size(); ++i) {
        QVERIFY(area.contains(zones[i]));
        for (int j = i + 1; j < zones.size(); ++j) {
            QVERIFY(!zones[i].intersects(zones[j]));
        }
    }
}

void TestLuauTileAlgorithm::marshalsSplitTree()
{
    LuauTileAlgorithm algo(scriptPath(QStringLiteral("memory-hooks.luau")), m_watchdog);
    QVERIFY(algo.isValid());
    QVERIFY(algo.supportsMemory());

    TilingState state(QStringLiteral("screen-1"));
    auto tree = std::make_unique<SplitTree>();
    tree->insertAtEnd(QStringLiteral("w1"));
    tree->insertAtEnd(QStringLiteral("w2"));
    tree->insertAtEnd(QStringLiteral("w3"));
    QCOMPARE(tree->leafCount(), 3);
    state.setSplitTree(std::move(tree));

    TilingParams p;
    p.windowCount = 5; // intentionally != leafCount
    p.screenGeometry = QRect(0, 0, 1920, 1080);
    p.state = &state;
    p.innerGap = 8;

    // The script returns one zone per tree leaf — so a count of 3 proves the
    // tree (and its leafCount) marshalled across correctly.
    const QVector<QRect> zones = algo.calculateZones(p);
    QCOMPARE(zones.size(), 3);
}

void TestLuauTileAlgorithm::lifecycleHooksRunAndPersist()
{
    LuauTileAlgorithm algo(scriptPath(QStringLiteral("memory-hooks.luau")), m_watchdog);
    QVERIFY(algo.isValid());
    QVERIFY(algo.supportsLifecycleHooks());

    TilingState state(QStringLiteral("screen-1"));
    state.addWindow(QStringLiteral("a"));
    state.addWindow(QStringLiteral("b"));

    // Fire the hook twice; the module-local counter must persist between calls.
    algo.onWindowAdded(&state, 0);
    algo.onWindowAdded(&state, 1);

    TilingParams p;
    p.windowCount = 4;
    p.screenGeometry = QRect(0, 0, 1920, 1080);
    p.state = &state; // no split tree → tile() returns `added` zones
    p.innerGap = 8;

    const QVector<QRect> zones = algo.calculateZones(p);
    QCOMPARE(zones.size(), 2); // added == 2
}

void TestLuauTileAlgorithm::metadataOutOfRangeValuesClamped()
{
    using namespace PlasmaZones::TestHelpers;
    using namespace AutotileDefaults;
    // Raw metadata table (bypassing pluau.algorithm) so the C++ parse + clamp path
    // sees the out-of-range values directly.
    const QString path = writeTempScript(m_tmp, QStringLiteral("clamp.luau"), QStringLiteral(R"LUA(
        return {
            metadata = {
                id = "clamp-test", name = "Clamp Test",
                defaultSplitRatio = 5.0,   -- above MaxSplitRatio
                minimumWindows = 9999,     -- above MaxMetadataWindows
                defaultMaxWindows = 9999,  -- above MaxMetadataWindows
            },
            tile = function(ctx) return {} end,
        }
    )LUA"));
    QVERIFY(!path.isEmpty());

    LuauTileAlgorithm algo(path, m_watchdog);
    QVERIFY(algo.isValid());
    // The bogus 5.0 ratio is pulled back into the legal split range.
    QVERIFY(algo.defaultSplitRatio() >= MinSplitRatio && algo.defaultSplitRatio() <= MaxSplitRatio);
    QVERIFY(algo.defaultSplitRatio() < 5.0);
    QVERIFY(algo.minimumWindows() >= MinMetadataWindows && algo.minimumWindows() <= MaxMetadataWindows);
    QVERIFY(algo.minimumWindows() < 9999);
    QVERIFY(algo.defaultMaxWindows() >= MinMetadataWindows && algo.defaultMaxWindows() <= MaxMetadataWindows);
    QVERIFY(algo.defaultMaxWindows() < 9999);
}

void TestLuauTileAlgorithm::customParamInvertedRangeNormalized()
{
    using namespace PlasmaZones::TestHelpers;
    const QString path = writeTempScript(m_tmp, QStringLiteral("cp.luau"), QStringLiteral(R"LUA(
        return {
            metadata = {
                id = "cp-test", name = "CP Test",
                customParams = {
                    { name = "ratio", type = "number", min = 1.0, max = 0.0, default = 0.5 },
                },
            },
            tile = function(ctx) return {} end,
        }
    )LUA"));
    QVERIFY(!path.isEmpty());

    LuauTileAlgorithm algo(path, m_watchdog);
    QVERIFY(algo.isValid());
    QVERIFY(algo.supportsCustomParams());
    const QVariantList defs = algo.customParamDefList();
    QCOMPARE(defs.size(), 1);
    const QVariantMap cp = defs.at(0).toMap();
    // The inverted [1.0, 0.0] range must have been swapped to [0.0, 1.0].
    QVERIFY(cp.value(QStringLiteral("minValue")).toDouble() <= cp.value(QStringLiteral("maxValue")).toDouble());
}

void TestLuauTileAlgorithm::malformedZonesKeepCountAndStayInArea()
{
    using namespace PlasmaZones::TestHelpers;
    // tile() returns two malformed entries: an empty map and one with negative
    // dimensions. Both must survive as one zone each (count stays aligned with
    // the window count) and be clamped back inside the area.
    const QString path = writeTempScript(m_tmp, QStringLiteral("malformed.luau"), QStringLiteral(R"LUA(
        return {
            metadata = { id = "malformed", name = "Malformed" },
            tile = function(ctx)
                return { {}, { x = -100, y = 0, width = -5, height = 10 } }
            end,
        }
    )LUA"));
    QVERIFY(!path.isEmpty());

    LuauTileAlgorithm algo(path, m_watchdog);
    QVERIFY(algo.isValid());

    TilingParams p;
    p.windowCount = 2;
    p.screenGeometry = QRect(0, 0, 1920, 1080);
    p.outerGaps = EdgeGaps::uniform(0);

    const QRect area(0, 0, 1920, 1080);
    const QVector<QRect> zones = algo.calculateZones(p);
    QCOMPARE(zones.size(), 2); // not collapsed to fewer than the window count
    for (const QRect& z : zones) {
        QVERIFY(!z.isEmpty());
        QVERIFY(area.contains(z));
    }
}

void TestLuauTileAlgorithm::missingTileFunctionIsInvalid()
{
    using namespace PlasmaZones::TestHelpers;
    const QString path = writeTempScript(m_tmp, QStringLiteral("notile.luau"), QStringLiteral(R"LUA(
        return { metadata = { id = "notile", name = "No Tile" } }
    )LUA"));
    QVERIFY(!path.isEmpty());

    LuauTileAlgorithm algo(path, m_watchdog);
    QVERIFY(!algo.isValid());
}

void TestLuauTileAlgorithm::runtimeErrorYieldsEmptyZones()
{
    using namespace PlasmaZones::TestHelpers;
    const QString path = writeTempScript(m_tmp, QStringLiteral("boom.luau"), QStringLiteral(R"LUA(
        return {
            metadata = { id = "boom", name = "Boom" },
            tile = function(ctx) error("kaboom") end,
        }
    )LUA"));
    QVERIFY(!path.isEmpty());

    LuauTileAlgorithm algo(path, m_watchdog);
    QVERIFY(algo.isValid()); // loads fine; the error only happens when tile() runs

    TilingParams p;
    p.windowCount = 3;
    p.screenGeometry = QRect(0, 0, 1920, 1080);
    p.outerGaps = EdgeGaps::uniform(0);

    // A runtime error inside tile() degrades to no zones, never a crash.
    QVERIFY(algo.calculateZones(p).isEmpty());
}

void TestLuauTileAlgorithm::zoneCountCappedAtMaxZones()
{
    using namespace PlasmaZones::TestHelpers;
    using namespace AutotileDefaults;
    // tile() returns far more entries than MaxZones; calculateZones must cap the
    // result at MaxZones rather than returning an unbounded list.
    const QString path = writeTempScript(m_tmp, QStringLiteral("toomany.luau"), QStringLiteral(R"LUA(
        return {
            metadata = { id = "toomany", name = "Too Many" },
            tile = function(ctx)
                local zones = {}
                for i = 1, 300 do
                    zones[i] = { x = 0, y = 0, width = 10, height = 10 }
                end
                return zones
            end,
        }
    )LUA"));
    QVERIFY(!path.isEmpty());

    LuauTileAlgorithm algo(path, m_watchdog);
    QVERIFY(algo.isValid());

    TilingParams p;
    p.windowCount = 300;
    p.screenGeometry = QRect(0, 0, 1920, 1080);
    p.outerGaps = EdgeGaps::uniform(0);

    QCOMPARE(algo.calculateZones(p).size(), MaxZones);
}

void TestLuauTileAlgorithm::oversizedScriptRejected()
{
    using namespace PlasmaZones::TestHelpers;
    // A script larger than the 1 MiB cap must be rejected (invalid), not loaded.
    QString content = QStringLiteral("-- ");
    content += QString(1100000, QLatin1Char('x')); // padding comment > 1 MiB
    content +=
        QStringLiteral("\nreturn { metadata = { id = \"big\", name = \"Big\" }, tile = function() return {} end }\n");
    const QString path = writeTempScript(m_tmp, QStringLiteral("big.luau"), content);
    QVERIFY(!path.isEmpty());

    LuauTileAlgorithm algo(path, m_watchdog);
    QVERIFY(!algo.isValid());
}

void TestLuauTileAlgorithm::enumAndBoolCustomParamsParsed()
{
    using namespace PlasmaZones::TestHelpers;
    const QString path = writeTempScript(m_tmp, QStringLiteral("cptypes.luau"), QStringLiteral(R"LUA(
        return {
            metadata = {
                id = "cptypes", name = "CP Types",
                customParams = {
                    { name = "mode", type = "enum", default = "a", options = { "a", "b", "c" } },
                    { name = "flag", type = "bool", default = true },
                },
            },
            tile = function(ctx) return {} end,
        }
    )LUA"));
    QVERIFY(!path.isEmpty());

    LuauTileAlgorithm algo(path, m_watchdog);
    QVERIFY(algo.isValid());
    const QVariantList defs = algo.customParamDefList();
    QCOMPARE(defs.size(), 2);

    QVariantMap byName0 = defs.at(0).toMap();
    QVariantMap byName1 = defs.at(1).toMap();
    const QVariantMap& enumDef =
        byName0.value(QStringLiteral("name")).toString() == QLatin1String("mode") ? byName0 : byName1;
    const QVariantMap& boolDef = (&enumDef == &byName0) ? byName1 : byName0;

    QCOMPARE(enumDef.value(QStringLiteral("type")).toString(), QStringLiteral("enum"));
    QCOMPARE(enumDef.value(QStringLiteral("enumOptions")).toStringList(),
             (QStringList{QStringLiteral("a"), QStringLiteral("b"), QStringLiteral("c")}));
    QCOMPARE(boolDef.value(QStringLiteral("type")).toString(), QStringLiteral("bool"));
    // bool params carry neither number bounds nor enum options.
    QVERIFY(!boolDef.contains(QStringLiteral("minValue")));
    QVERIFY(!boolDef.contains(QStringLiteral("enumOptions")));
}

void TestLuauTileAlgorithm::onWindowRemovedHookRuns()
{
    using namespace PlasmaZones::TestHelpers;
    // Self-contained module: onWindowAdded increments, onWindowRemoved decrements
    // a module-local counter; tile() returns `counter` plain rects (no pluau needed).
    const QString path = writeTempScript(m_tmp, QStringLiteral("hooks.luau"), QStringLiteral(R"LUA(
        local n = 0
        return {
            metadata = { id = "hooks", name = "Hooks" },
            onWindowAdded = function(state, index) n = n + 1 end,
            onWindowRemoved = function(state, index) n = n - 1 end,
            tile = function(ctx)
                local zones = {}
                for i = 1, math.max(1, n) do zones[i] = { x = 0, y = 0, width = 10, height = 10 } end
                return zones
            end,
        }
    )LUA"));
    QVERIFY(!path.isEmpty());
    LuauTileAlgorithm algo(path, m_watchdog);
    QVERIFY(algo.isValid());
    QVERIFY(algo.supportsLifecycleHooks());

    TilingState state(QStringLiteral("screen-1"));
    state.addWindow(QStringLiteral("a"));
    state.addWindow(QStringLiteral("b"));
    state.addWindow(QStringLiteral("c"));

    TilingParams p;
    p.windowCount = 4;
    p.screenGeometry = QRect(0, 0, 1920, 1080);
    p.state = &state; // no split tree → tile() returns `counter` zones
    p.innerGap = 8;

    algo.onWindowAdded(&state, 0);
    algo.onWindowAdded(&state, 1);
    algo.onWindowAdded(&state, 2); // counter == 3
    QCOMPARE(algo.calculateZones(p).size(), 3); // sanity: three adds persisted

    algo.onWindowRemoved(&state, 2); // counter == 2 — proves the remove hook ran
    QCOMPARE(algo.calculateZones(p).size(), 2);
}

void TestLuauTileAlgorithm::nonFiniteOverrideRejected()
{
    using namespace PlasmaZones::TestHelpers;
    using namespace AutotileDefaults;
    // A defaultSplitRatio override function returning a non-finite value (0/0)
    // must not poison geometry: std::clamp(NaN) returns NaN, so the resolver
    // falls back to a finite default.
    const QString path = writeTempScript(m_tmp, QStringLiteral("nan.luau"), QStringLiteral(R"LUA(
        return {
            metadata = { id = "nan", name = "NaN" },
            defaultSplitRatio = function() return 0 / 0 end,
            tile = function(ctx) return {} end,
        }
    )LUA"));
    QVERIFY(!path.isEmpty());

    LuauTileAlgorithm algo(path, m_watchdog);
    QVERIFY(algo.isValid());
    const qreal ratio = algo.defaultSplitRatio();
    QVERIFY(std::isfinite(ratio));
    QVERIFY(ratio >= MinSplitRatio && ratio <= MaxSplitRatio);
}

void TestLuauTileAlgorithm::scriptedMetadataFlagsExposed()
{
    using namespace PlasmaZones::TestHelpers;
    const QString path = writeTempScript(m_tmp, QStringLiteral("flags.luau"), QStringLiteral(R"LUA(
        return {
            metadata = {
                id = "flags", name = "Flags",
                producesOverlappingZones = true,
                centerLayout = true,
                supportsMinSizes = false,
                zoneNumberDisplay = "all",
            },
            tile = function(ctx) return {} end,
        }
    )LUA"));
    QVERIFY(!path.isEmpty());

    LuauTileAlgorithm algo(path, m_watchdog);
    QVERIFY(algo.isValid());
    QVERIFY(algo.producesOverlappingZones());
    QVERIFY(algo.centerLayout());
    QVERIFY(!algo.supportsMinSizes());
    QCOMPARE(algo.zoneNumberDisplay(), QStringLiteral("all"));
}

void TestLuauTileAlgorithm::zoneNumberDisplayInvalidFallsBack()
{
    using namespace PlasmaZones::TestHelpers;
    // A valid token round-trips; an unrecognised token falls back to the
    // renderer-decides default (never surfaces the bogus string).
    const QString validPath = writeTempScript(m_tmp, QStringLiteral("znd-valid.luau"), QStringLiteral(R"LUA(
        return { metadata = { id = "znd-valid", name = "ZND", zoneNumberDisplay = "last" },
                 tile = function(ctx) return {} end }
    )LUA"));
    const QString badPath = writeTempScript(m_tmp, QStringLiteral("znd-bad.luau"), QStringLiteral(R"LUA(
        return { metadata = { id = "znd-bad", name = "ZND", zoneNumberDisplay = "bogus" },
                 tile = function(ctx) return {} end }
    )LUA"));
    QVERIFY(!validPath.isEmpty() && !badPath.isEmpty());

    LuauTileAlgorithm valid(validPath, m_watchdog);
    LuauTileAlgorithm bad(badPath, m_watchdog);
    QVERIFY(valid.isValid() && bad.isValid());
    QCOMPARE(valid.zoneNumberDisplay(), QStringLiteral("last"));
    QVERIFY(bad.zoneNumberDisplay() != QStringLiteral("bogus"));
}

void TestLuauTileAlgorithm::metadataInfiniteSplitRatioClamped()
{
    using namespace PlasmaZones::TestHelpers;
    using namespace AutotileDefaults;
    // An infinite metadata-field split ratio must clamp to a finite, in-range
    // value (std::clamp(inf, lo, hi) == hi).
    const QString path = writeTempScript(m_tmp, QStringLiteral("inf.luau"), QStringLiteral(R"LUA(
        return { metadata = { id = "inf", name = "Inf", defaultSplitRatio = 1 / 0 },
                 tile = function(ctx) return {} end }
    )LUA"));
    QVERIFY(!path.isEmpty());

    LuauTileAlgorithm algo(path, m_watchdog);
    QVERIFY(algo.isValid());
    QVERIFY(std::isfinite(algo.defaultSplitRatio()));
    QVERIFY(algo.defaultSplitRatio() >= MinSplitRatio && algo.defaultSplitRatio() <= MaxSplitRatio);
}

void TestLuauTileAlgorithm::erroringOverrideFallsBack()
{
    using namespace PlasmaZones::TestHelpers;
    using namespace AutotileDefaults;
    // An override function that errors at load must fall back to the default,
    // not leave the cached accessor in a bad state.
    const QString path = writeTempScript(m_tmp, QStringLiteral("errfn.luau"), QStringLiteral(R"LUA(
        return {
            metadata = { id = "errfn", name = "ErrFn" },
            defaultSplitRatio = function() error("boom") end,
            tile = function(ctx) return {} end,
        }
    )LUA"));
    QVERIFY(!path.isEmpty());

    LuauTileAlgorithm algo(path, m_watchdog);
    QVERIFY(algo.isValid());
    QVERIFY(std::isfinite(algo.defaultSplitRatio()));
    QVERIFY(algo.defaultSplitRatio() >= MinSplitRatio && algo.defaultSplitRatio() <= MaxSplitRatio);
}

void TestLuauTileAlgorithm::customParamValueReachesTile()
{
    using namespace PlasmaZones::TestHelpers;
    // End-to-end: a declared custom param's runtime value reaches tile() via
    // ctx.custom and drives the zone count, distinct from windowCount.
    const QString path = writeTempScript(m_tmp, QStringLiteral("custom.luau"), QStringLiteral(R"LUA(
        return {
            metadata = {
                id = "custom", name = "Custom",
                customParams = { { name = "cols", type = "number", min = 1, max = 8, default = 3 } },
            },
            tile = function(ctx)
                local n = math.floor((ctx.custom and ctx.custom.cols) or 1)
                local zones = {}
                for i = 1, n do zones[i] = { x = 0, y = 0, width = 10, height = 10 } end
                return zones
            end,
        }
    )LUA"));
    QVERIFY(!path.isEmpty());

    LuauTileAlgorithm algo(path, m_watchdog);
    QVERIFY(algo.isValid());
    QVERIFY(algo.supportsCustomParams());

    TilingParams p;
    p.windowCount = 5; // intentionally != the custom-param-driven count
    p.screenGeometry = QRect(0, 0, 1920, 1080);
    p.outerGaps = EdgeGaps::uniform(0);
    p.customParams.insert(QStringLiteral("cols"), 4);

    // tile() returns `cols` (4) zones, proving ctx.custom.cols flowed through.
    QCOMPARE(algo.calculateZones(p).size(), 4);
}

// The shared-engine ctor is how the loader hosts all trusted bundled scripts in
// one VM. Verify two different modules sharing one engine each compute their own
// correct zones (no cross-contamination), that destroying one (which releases
// only its module from the shared engine) leaves the other working, and that the
// shared engine outlives both algorithms via their shared_ptr copies.
void TestLuauTileAlgorithm::sharedEngineHostsIndependentModules()
{
    QString err;
    std::shared_ptr<PhosphorScripting::LuauEngine> shared = LuauTileAlgorithm::createSandboxedEngine(m_watchdog, &err);
    QVERIFY2(shared != nullptr, qPrintable(err));

    TilingState state(QStringLiteral("screen-1"));
    state.setSplitRatio(0.6);
    TilingParams p;
    p.windowCount = 5;
    p.screenGeometry = QRect(0, 0, 1920, 1080);
    p.state = &state;
    p.innerGap = 8;
    p.outerGaps = EdgeGaps::uniform(0);

    {
        // Two distinct modules in the SAME shared engine.
        auto masterStack = std::make_unique<LuauTileAlgorithm>(scriptPath(QStringLiteral("master-stack.luau")), shared,
                                                               m_watchdog, nullptr);
        QVERIFY(masterStack->isValid());

        TilingState memState(QStringLiteral("screen-2"));
        auto memTree = std::make_unique<SplitTree>();
        memTree->insertAtEnd(QStringLiteral("w1"));
        memTree->insertAtEnd(QStringLiteral("w2"));
        memTree->insertAtEnd(QStringLiteral("w3"));
        memState.setSplitTree(std::move(memTree));

        auto memoryHooks = std::make_unique<LuauTileAlgorithm>(scriptPath(QStringLiteral("memory-hooks.luau")), shared,
                                                               m_watchdog, nullptr);
        QVERIFY(memoryHooks->isValid());

        // Each module computes its own result through the shared VM, uncontaminated
        // by the other: master-stack → one zone per window (5); memory-hooks → one
        // zone per split-tree leaf (3).
        QCOMPARE(masterStack->calculateZones(p).size(), 5);

        TilingParams memP;
        memP.windowCount = 5; // != leafCount, to prove the tree marshalled
        memP.screenGeometry = QRect(0, 0, 1920, 1080);
        memP.state = &memState;
        memP.innerGap = 8;
        QCOMPARE(memoryHooks->calculateZones(memP).size(), 3);

        // Destroying one algorithm releases only ITS module from the shared engine
        // (dtor → releaseModule); the other must keep working on the same engine.
        memoryHooks.reset();
        QCOMPARE(masterStack->calculateZones(p).size(), 5);
    }

    // Both algorithms destroyed; the shared engine is still alive via our local
    // shared_ptr, and a fresh module can still be loaded into it.
    QVERIFY(shared->isValid());
    LuauTileAlgorithm late(scriptPath(QStringLiteral("master-stack.luau")), shared, m_watchdog, nullptr);
    QVERIFY(late.isValid());
    QCOMPARE(late.calculateZones(p).size(), 5);
}

QTEST_MAIN(TestLuauTileAlgorithm)
#include "test_luau_tile_algorithm.moc"
