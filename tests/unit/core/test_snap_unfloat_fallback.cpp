// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

// ON-success coverage for SnapEngine::resolveFallbackUnfloatGeometry (the
// unfloatFallbackToZone feature). The sibling test_snap_engine.cpp is
// QTEST_GUILESS_MAIN, where QGuiApplication::primaryScreen() is null so
// WindowTrackingService::zoneGeometry() always returns an invalid QRect — the
// resolver therefore can never produce a found result there (it can only
// exercise the OFF gate and the headless not-found fallthrough).
//
// This file uses QTEST_MAIN so a QGuiApplication exists; under the offscreen QPA
// platform (wired for every test in tests/unit/CMakeLists.txt) primaryScreen()
// is a real screen with valid geometry, so zoneGeometry() resolves and the
// resolver's full chain — opt-in gate → screen resolution → layout lookup →
// zone selection (last-used → first-empty → first-zone) → geometry — can be
// asserted end to end.

#include <QTest>
#include <QSignalSpy>

#include <memory>

#include <PhosphorSnapEngine/SnapEngine.h>
#include <PhosphorSnapEngine/SnapState.h>
#include <PhosphorPlacement/WindowTrackingService.h>
#include <PhosphorEngine/WindowPlacement.h>
#include <PhosphorEngine/EngineTypes.h>
#include <PhosphorZones/Layout.h>
#include <PhosphorZones/LayoutRegistry.h>
#include <PhosphorZones/Zone.h>

#include "../helpers/IsolatedConfigGuard.h"
#include "../helpers/LayoutRegistryTestHelpers.h"
#include "../helpers/StubSettings.h"
#include "../helpers/StubZoneDetector.h"

using namespace PlasmaZones;
using namespace PhosphorSnapEngine;
using PlasmaZones::TestHelpers::IsolatedConfigGuard;

class TestSnapUnfloatFallback : public QObject
{
    Q_OBJECT

private:
    std::unique_ptr<IsolatedConfigGuard> m_guard;
    PhosphorZones::LayoutRegistry* m_layoutManager = nullptr;
    StubSettings* m_settings = nullptr;
    PlasmaZones::StubZoneDetector* m_zoneDetector = nullptr;
    PhosphorPlacement::WindowTrackingService* m_wts = nullptr;

    // Register a fresh equal-split layout of zoneCount zones as the active layout
    // and return it, so callers can assert on specific zone ids. (Engine/SnapState
    // wiring is done per-test, not here.)
    PhosphorZones::Layout* installLayout(int zoneCount)
    {
        auto* layout = createTestLayout(zoneCount, m_layoutManager);
        m_layoutManager->addLayout(layout);
        m_layoutManager->setActiveLayout(layout);
        return layout;
    }

private Q_SLOTS:
    void init()
    {
        m_guard = std::make_unique<IsolatedConfigGuard>();
        m_layoutManager = PlasmaZones::TestHelpers::makeLayoutRegistry(QStringLiteral("plasmazones/layouts"));
        m_settings = new StubSettings(nullptr);
        m_zoneDetector = new PlasmaZones::StubZoneDetector(nullptr);
        // screenManager == nullptr: zoneGeometry() then resolves against
        // QGuiApplication::primaryScreen() (valid under the offscreen QPA), which
        // is exactly what makes the on-success geometry path reachable here.
        m_wts = new PhosphorPlacement::WindowTrackingService(m_layoutManager, m_zoneDetector, nullptr, nullptr);
    }

    void cleanup()
    {
        m_wts->setSnapState(nullptr);
        delete m_wts;
        delete m_zoneDetector;
        delete m_settings;
        delete m_layoutManager;
        m_guard.reset();
    }

    // Sanity: the fixture really does resolve valid zone geometry. If this fails,
    // the offscreen primary screen is unavailable and the on-success assertions
    // below would be meaningless rather than wrong — so pin it explicitly.
    void testFixture_zoneGeometryIsValid()
    {
        auto* layout = installLayout(2);
        const QString zoneId = layout->zones().first()->id().toString();
        QVERIFY2(m_wts->zoneGeometry(zoneId, QStringLiteral("DP-1")).isValid(),
                 "offscreen primary screen must yield valid zone geometry for the on-success tests");
    }

    // ON + a layout but no last-used zone and no occupancy → the resolver selects
    // the first EMPTY zone (lowest zone number) and returns a found result with
    // valid geometry on the requested screen.
    void testFallback_on_noLastUsed_selectsFirstEmptyZone()
    {
        SnapEngine engine(m_layoutManager, m_wts, nullptr, nullptr, nullptr);
        engine.setEngineSettings(m_settings);
        m_wts->setSnapState(engine.snapState());

        auto* layout = installLayout(2);
        const QString firstZone = layout->zones().first()->id().toString();

        engine.snapState()->setFloatingOnScreen(QStringLiteral("app|w"), QStringLiteral("DP-1"), 0);
        m_settings->setSnapUnfloatFallbackToZone(true);

        const PhosphorEngine::UnfloatResult r =
            engine.resolveFallbackUnfloatGeometry(QStringLiteral("app|w"), QStringLiteral("DP-1"));

        QVERIFY2(r.found, "on + valid geometry → fallback target found");
        QCOMPARE(r.zoneIds, QStringList{firstZone});
        QCOMPARE(r.screenId, QStringLiteral("DP-1"));
        QVERIFY(r.geometry.isValid());
        m_wts->setSnapState(nullptr);
    }

    // ON + a recorded last-used zone that exists in this screen's layout → the
    // resolver prefers the last-used zone over the first-empty fallback.
    void testFallback_on_prefersLastUsedZone()
    {
        SnapEngine engine(m_layoutManager, m_wts, nullptr, nullptr, nullptr);
        engine.setEngineSettings(m_settings);
        m_wts->setSnapState(engine.snapState());

        auto* layout = installLayout(2);
        const QString secondZone = layout->zones().at(1)->id().toString();

        // Record zone 2 as last-used; first-empty would otherwise pick zone 1.
        engine.snapState()->updateLastUsedZone(secondZone, QStringLiteral("DP-1"), QStringLiteral("app"), 0);
        engine.snapState()->setFloatingOnScreen(QStringLiteral("app|w"), QStringLiteral("DP-1"), 0);
        m_settings->setSnapUnfloatFallbackToZone(true);

        const PhosphorEngine::UnfloatResult r =
            engine.resolveFallbackUnfloatGeometry(QStringLiteral("app|w"), QStringLiteral("DP-1"));

        QVERIFY(r.found);
        QCOMPARE(r.zoneIds, QStringList{secondZone});
        m_wts->setSnapState(nullptr);
    }

    // ON + a recorded last-used zone that belongs to a DIFFERENT layout (not the one
    // resolved for this screen) → the last-used tier is rejected (its zone resolves
    // geometry on this screen, but layout->zoneById fails) and the resolver falls
    // through to the first-empty zone of THIS screen's layout. Pins the layout-scoping
    // guard.
    void testFallback_on_lastUsedFromOtherLayout_fallsThroughToFirstEmpty()
    {
        SnapEngine engine(m_layoutManager, m_wts, nullptr, nullptr, nullptr);
        engine.setEngineSettings(m_settings);
        m_wts->setSnapState(engine.snapState());

        // `layout` is installed FIRST, so it is the registry's default
        // (resolveLayoutForScreen → defaultLayout() → m_layouts.first() when no
        // per-screen assignment or default-id provider is wired) and therefore the
        // layout resolved for DP-1.
        auto* layout = installLayout(2);
        const QString firstZone = layout->zones().first()->id().toString();

        // A second layout registered AFTER it (so not the default / not DP-1's
        // resolved layout). Its zones still resolve geometry (findZoneInAllLayouts
        // spans every registered layout), so a last-used zone from it must be
        // rejected by the zoneById scoping, not by an invalid geometry.
        auto* otherLayout = createTestLayout(2, m_layoutManager);
        m_layoutManager->addLayout(otherLayout);
        const QString foreignZone = otherLayout->zones().first()->id().toString();

        engine.snapState()->updateLastUsedZone(foreignZone, QStringLiteral("DP-1"), QStringLiteral("app"), 0);
        engine.snapState()->setFloatingOnScreen(QStringLiteral("app|w"), QStringLiteral("DP-1"), 0);
        m_settings->setSnapUnfloatFallbackToZone(true);

        const PhosphorEngine::UnfloatResult r =
            engine.resolveFallbackUnfloatGeometry(QStringLiteral("app|w"), QStringLiteral("DP-1"));

        QVERIFY(r.found);
        QVERIFY2(r.zoneIds != QStringList{foreignZone}, "a last-used zone from another layout must not be selected");
        QCOMPARE(r.zoneIds, QStringList{firstZone});
        m_wts->setSnapState(nullptr);
    }

    // ON + every zone occupied and no last-used → first-empty yields nothing, so
    // the resolver falls back to the FIRST zone in the layout (occupancy is fine:
    // snapping stacks multiple windows per zone).
    void testFallback_on_allZonesOccupied_fallsBackToFirstZone()
    {
        SnapEngine engine(m_layoutManager, m_wts, nullptr, nullptr, nullptr);
        engine.setEngineSettings(m_settings);
        m_wts->setSnapState(engine.snapState());

        auto* layout = installLayout(2);
        const QString firstZone = layout->zones().first()->id().toString();
        const QString secondZone = layout->zones().at(1)->id().toString();

        // Occupy both zones with non-floating windows so findEmptyZoneInLayout
        // returns nothing (buildOccupiedZoneSet skips floating windows).
        m_wts->assignWindowToZone(QStringLiteral("app|a"), firstZone, QStringLiteral("DP-1"), 0);
        m_wts->assignWindowToZone(QStringLiteral("app|b"), secondZone, QStringLiteral("DP-1"), 0);
        engine.snapState()->setFloatingOnScreen(QStringLiteral("app|w"), QStringLiteral("DP-1"), 0);
        m_settings->setSnapUnfloatFallbackToZone(true);

        const PhosphorEngine::UnfloatResult r =
            engine.resolveFallbackUnfloatGeometry(QStringLiteral("app|w"), QStringLiteral("DP-1"));

        QVERIFY2(r.found, "all-occupied still resolves to the first zone (stacking)");
        QCOMPARE(r.zoneIds, QStringList{firstZone});
        QVERIFY(r.geometry.isValid());
        m_wts->setSnapState(nullptr);
    }

    // OFF gate beats valid geometry: even though zoneGeometry would resolve here,
    // the setting being off short-circuits the resolver to not-found. This is the
    // GUI complement to the guiless onButHeadless test — together they pin that
    // the result is driven by the gate, not merely by geometry availability.
    void testFallback_off_returnsNotFoundDespiteValidGeometry()
    {
        SnapEngine engine(m_layoutManager, m_wts, nullptr, nullptr, nullptr);
        engine.setEngineSettings(m_settings);
        m_wts->setSnapState(engine.snapState());

        installLayout(2);
        engine.snapState()->setFloatingOnScreen(QStringLiteral("app|w"), QStringLiteral("DP-1"), 0);

        m_settings->setSnapUnfloatFallbackToZone(false);
        QVERIFY2(!engine.resolveFallbackUnfloatGeometry(QStringLiteral("app|w"), QStringLiteral("DP-1")).found,
                 "off → not-found even though zoneGeometry is valid in this GUI fixture");
        m_wts->setSnapState(nullptr);
    }

    // ── SnapToZone placement-rule precedence ────────────────────────────────
    // A matched SnapToZone rule (the daemon-injected placement-zones resolver) is
    // the highest-priority restore: it overrides a remembered placement record on
    // open. The store still re-binds the record FIRST, so the window's float-back
    // geometry survives the override (a later Meta+F floats it to its remembered
    // free position, not the zone rect). Lives in this GUI fixture because the
    // rule's snap only resolves with valid zone geometry.
    void testResolveWindowRestore_placementRule_beatsFloatedRecord_preservesFreeGeo()
    {
        SnapEngine engine(m_layoutManager, m_wts, nullptr, nullptr, nullptr);
        engine.setEngineSettings(m_settings);
        m_wts->setSnapState(engine.snapState());

        installLayout(3);
        engine.setPlacementZonesResolver([](const QString&, const QString&) {
            return QList<int>{2, 3};
        });

        // A remembered FLOATED record on DP-1 carrying the window's free position.
        const QRect floatGeo(120, 80, 800, 600);
        PhosphorEngine::WindowPlacement rec;
        rec.windowId = QStringLiteral("app|orig");
        rec.appId = QStringLiteral("app");
        rec.screenId = QStringLiteral("DP-1");
        PhosphorEngine::EngineSlot slot;
        slot.state = PhosphorEngine::WindowPlacement::stateFloating();
        rec.engines.insert(PhosphorEngine::WindowPlacement::snapEngineId(), slot);
        rec.freeGeometryByScreen.insert(QStringLiteral("DP-1"), floatGeo);
        m_wts->placementStore().record(rec);

        // KWin reopens the window with a new uuid on the same screen.
        const PhosphorEngine::SnapResult result =
            engine.resolveWindowRestore(QStringLiteral("app|new"), QStringLiteral("DP-1"), /*sticky*/ false);

        // The rule wins over the stored floated record: snap to the two rule zones.
        QVERIFY2(result.shouldSnap, "a SnapToZone rule must override a remembered floated record");
        QCOMPARE(result.zoneIds.size(), 2);

        // The record was re-bound to the live id with its float-back geometry
        // intact, so a later float returns to the remembered free position — NOT
        // the zone rect the rule just snapped to.
        QVERIFY2(m_wts->placementStore().contains(QStringLiteral("app|new"), QStringLiteral("app")),
                 "the placement record must be re-bound to the live window id");
        const auto rebound = m_wts->placementStore().peek(QStringLiteral("app|new"), QStringLiteral("app"));
        QVERIFY(rebound.has_value());
        QCOMPARE(rebound->freeGeometryFor(QStringLiteral("DP-1")), floatGeo);
        m_wts->setSnapState(nullptr);
    }

    // A SnapToZone rule snaps a fresh window that has no stored record at all
    // (nothing to inherit, so no float-back to preserve).
    void testResolveWindowRestore_placementRule_noRecord_snaps()
    {
        SnapEngine engine(m_layoutManager, m_wts, nullptr, nullptr, nullptr);
        engine.setEngineSettings(m_settings);
        m_wts->setSnapState(engine.snapState());

        installLayout(3);
        engine.setPlacementZonesResolver([](const QString&, const QString&) {
            return QList<int>{1};
        });

        const PhosphorEngine::SnapResult result =
            engine.resolveWindowRestore(QStringLiteral("fresh|win"), QStringLiteral("DP-1"), /*sticky*/ false);

        QVERIFY2(result.shouldSnap, "a SnapToZone rule must snap a fresh window with no stored record");
        QCOMPARE(result.zoneIds.size(), 1);
        m_wts->setSnapState(nullptr);
    }

    // An empty resolver result (no matching rule) must NOT snap — the window
    // falls through to the normal restore/float path.
    void testResolveWindowRestore_placementRule_emptyResolver_doesNotSnap()
    {
        SnapEngine engine(m_layoutManager, m_wts, nullptr, nullptr, nullptr);
        engine.setEngineSettings(m_settings);
        m_wts->setSnapState(engine.snapState());

        installLayout(3);
        engine.setPlacementZonesResolver([](const QString&, const QString&) {
            return QList<int>{};
        });

        const PhosphorEngine::SnapResult result =
            engine.resolveWindowRestore(QStringLiteral("nomatch|win"), QStringLiteral("DP-1"), /*sticky*/ false);

        QVERIFY2(!result.shouldSnap, "no matching rule → the placement rule must not snap the window");
        m_wts->setSnapState(nullptr);
    }
};

QTEST_MAIN(TestSnapUnfloatFallback)
#include "test_snap_unfloat_fallback.moc"
