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

#include "helpers/IsolatedConfigGuard.h"
#include "helpers/LayoutRegistryTestHelpers.h"
#include "helpers/StubSettings.h"
#include "helpers/StubZoneDetector.h"

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

    // Seed a placement record whose snap slot has @p state + @p zoneIds, as a
    // prior daemon session's capture would have persisted it.
    void seedSnapSlotRecord(SnapEngine& engine, const QString& windowId, const QString& state,
                            const QStringList& zoneIds, const QString& screenId = QStringLiteral("DP-1"))
    {
        PhosphorEngine::WindowPlacement p;
        p.windowId = windowId;
        p.appId = m_wts->currentAppIdFor(windowId);
        p.screenId = screenId;
        PhosphorEngine::EngineSlot slot;
        slot.state = state;
        slot.zoneIds = zoneIds;
        p.engines.insert(engine.engineId(), slot);
        QVERIFY(m_wts->placementStore().record(p));
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
            return PlacementDirective{{2, 3}};
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
            return PlacementDirective{{1}};
        });

        const PhosphorEngine::SnapResult result =
            engine.resolveWindowRestore(QStringLiteral("fresh|win"), QStringLiteral("DP-1"), /*sticky*/ false);

        QVERIFY2(result.shouldSnap, "a SnapToZone rule must snap a fresh window with no stored record");
        QCOMPARE(result.zoneIds.size(), 1);
        m_wts->setSnapState(nullptr);
    }

    // A RouteToScreen directive pins the placement to a different monitor than the
    // one the window opened on: the snap must resolve on the ROUTED screen, so
    // result.screenId is the target (the apply path then moves the window there).
    void testResolveWindowRestore_routeToScreen_resolvesOnTargetScreen()
    {
        SnapEngine engine(m_layoutManager, m_wts, nullptr, nullptr, nullptr);
        engine.setEngineSettings(m_settings);
        m_wts->setSnapState(engine.snapState());

        installLayout(3);
        // Window opens on DP-1 but the rule routes it to DP-2 and snaps zone 1.
        engine.setPlacementZonesResolver([](const QString&, const QString&) {
            return PlacementDirective{{1}, QStringLiteral("DP-2")};
        });

        const PhosphorEngine::SnapResult result =
            engine.resolveWindowRestore(QStringLiteral("fresh|win"), QStringLiteral("DP-1"), /*sticky*/ false);

        QVERIFY2(result.shouldSnap, "RouteToScreen + SnapToZone must snap the window");
        QCOMPARE(result.zoneIds.size(), 1);
        QCOMPARE(result.screenId, QStringLiteral("DP-2"));
        m_wts->setSnapState(nullptr);
    }

    // A RouteToDesktop directive snaps the window into its zone on the DESTINATION
    // desktop's layout and stamps that desktop on the result, so the commit records
    // the assignment on the desktop the window is being moved to (not the one it
    // momentarily opened on).
    void testResolveWindowRestore_routeToDesktop_stampsResultDesktop()
    {
        SnapEngine engine(m_layoutManager, m_wts, nullptr, nullptr, nullptr);
        engine.setEngineSettings(m_settings);
        m_wts->setSnapState(engine.snapState());

        installLayout(3);
        // Snap zone 1, and route to virtual desktop 2.
        engine.setPlacementZonesResolver([](const QString&, const QString&) {
            PlacementDirective d;
            d.zoneOrdinals = {1};
            d.targetDesktop = 2;
            return d;
        });

        const PhosphorEngine::SnapResult result =
            engine.resolveWindowRestore(QStringLiteral("fresh|win"), QStringLiteral("DP-1"), /*sticky*/ false);

        QVERIFY2(result.shouldSnap, "SnapToZone + RouteToDesktop must still snap the window");
        QCOMPARE(result.zoneIds.size(), 1);
        QCOMPARE(result.virtualDesktop, 2);
        m_wts->setSnapState(nullptr);
    }

    // Un-floating a window into a matched SnapToZone + RouteToDesktop rule must
    // COMMIT the zone assignment on the routed desktop, not the current one. The
    // rule resolves the zones against the destination desktop's layout, so a
    // commit that dropped the routed desktop would record those zones under the
    // current desktop — a cross-desktop assignment mismatch. Mirrors the open
    // path (resolveWindowRestore → commitSnap forwards result.virtualDesktop).
    void testUnfloatToZone_routeToDesktop_commitsAssignmentOnRoutedDesktop()
    {
        SnapEngine engine(m_layoutManager, m_wts, nullptr, nullptr, nullptr);
        engine.setEngineSettings(m_settings);
        m_wts->setSnapState(engine.snapState());

        installLayout(3);
        // Snap zone 1, and route to virtual desktop 2.
        engine.setPlacementZonesResolver([](const QString&, const QString&) {
            PlacementDirective d;
            d.zoneOrdinals = {1};
            d.targetDesktop = 2;
            return d;
        });

        // Mark the window floating so the public toggle takes the un-float path,
        // which routes through the private unfloatToZone (the code under test).
        engine.snapState()->setFloating(QStringLiteral("fresh|win"), true);
        engine.toggleWindowFloat(QStringLiteral("fresh|win"), QStringLiteral("DP-1"));

        QVERIFY2(!engine.snapState()->zoneForWindow(QStringLiteral("fresh|win")).isEmpty(),
                 "un-floating into a SnapToZone + RouteToDesktop rule must assign the window to its rule zone");
        QCOMPARE(engine.snapState()->desktopForWindow(QStringLiteral("fresh|win")), 2);
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
            return PlacementDirective{};
        });

        const PhosphorEngine::SnapResult result =
            engine.resolveWindowRestore(QStringLiteral("nomatch|win"), QStringLiteral("DP-1"), /*sticky*/ false);

        QVERIFY2(!result.shouldSnap, "no matching rule → the placement rule must not snap the window");
        m_wts->setSnapState(nullptr);
    }

    // ═══════════════════════════════════════════════════════════════════════
    // resolveUnfloatGeometry: placement-record fallback (daemon-restart path)
    //
    // The in-memory pre-float capture dies with the daemon. The persisted
    // placement record's snap slot survives: a floating capture carries the
    // pre-float zones, a stale snapped capture (daemon died before the float
    // persisted) carries the zones the window occupied before it floated.
    // Without the fallback, unfloating after a restart dead-ends ("no
    // pre-float zone, keeping floating") with no way out.
    // ═══════════════════════════════════════════════════════════════════════

    void testResolveUnfloat_noLiveCapture_fallsBackToRecordFloatingSlot()
    {
        SnapEngine engine(m_layoutManager, m_wts, nullptr, nullptr, nullptr);
        engine.setEngineSettings(m_settings);
        m_wts->setSnapState(engine.snapState());

        auto* layout = installLayout(2);
        const QString homeZone = layout->zones().first()->id().toString();
        const QString w = QStringLiteral("app|restarted");

        // Live state after a daemon restart: the window is floating, but the
        // in-memory pre-float capture is gone.
        engine.snapState()->setFloatingOnScreen(w, QStringLiteral("DP-1"), 0);
        QVERIFY(m_wts->preFloatZones(w).isEmpty());
        seedSnapSlotRecord(engine, w, QString(PhosphorEngine::WindowPlacement::stateFloating()), {homeZone});

        const PhosphorEngine::UnfloatResult r = engine.resolveUnfloatGeometry(w, QStringLiteral("DP-1"));
        QVERIFY2(r.found, "record's floating slot carries the pre-float zones — unfloat must find them");
        QCOMPARE(r.zoneIds, QStringList{homeZone});
        QVERIFY(r.geometry.isValid());
        m_wts->setSnapState(nullptr);
    }

    void testResolveUnfloat_noLiveCapture_fallsBackToRecordSnappedSlot()
    {
        SnapEngine engine(m_layoutManager, m_wts, nullptr, nullptr, nullptr);
        engine.setEngineSettings(m_settings);
        m_wts->setSnapState(engine.snapState());

        auto* layout = installLayout(2);
        const QString homeZone = layout->zones().last()->id().toString();
        const QString w = QStringLiteral("app|stalerec");

        engine.snapState()->setFloatingOnScreen(w, QStringLiteral("DP-1"), 0);
        seedSnapSlotRecord(engine, w, QString(PhosphorEngine::WindowPlacement::stateSnapped()), {homeZone});

        const PhosphorEngine::UnfloatResult r = engine.resolveUnfloatGeometry(w, QStringLiteral("DP-1"));
        QVERIFY2(r.found, "a stale snapped slot still names the window's home zone — unfloat must use it");
        QCOMPARE(r.zoneIds, QStringList{homeZone});
        QVERIFY(r.geometry.isValid());
        m_wts->setSnapState(nullptr);
    }

    // The record's screenId is only a HINT: when it names a screen that no
    // longer exists (monitor unplugged since the record was captured),
    // resolveUnfloatScreen must reject it and degrade to the caller's live
    // fallback screen — the restore still succeeds, on the fallback screen,
    // instead of resolving against the missing monitor or failing outright.
    void testResolveUnfloat_recordScreenGone_degradesToFallbackScreen()
    {
        SnapEngine engine(m_layoutManager, m_wts, nullptr, nullptr, nullptr);
        engine.setEngineSettings(m_settings);
        m_wts->setSnapState(engine.snapState());

        auto* layout = installLayout(2);
        const QString homeZone = layout->zones().first()->id().toString();
        const QString w = QStringLiteral("app|unplugged");

        engine.snapState()->setFloatingOnScreen(w, QStringLiteral("DP-1"), 0);
        QVERIFY(m_wts->preFloatZones(w).isEmpty());
        // Record captured on a screen no environment can have (a plausible
        // name like DP-2 would resolve as a REAL monitor when the test binary
        // is run directly on a developer's session instead of under ctest's
        // offscreen QPA).
        seedSnapSlotRecord(engine, w, QString(PhosphorEngine::WindowPlacement::stateSnapped()), {homeZone},
                           QStringLiteral("PZTEST-UNPLUGGED-1"));

        const PhosphorEngine::UnfloatResult r = engine.resolveUnfloatGeometry(w, QStringLiteral("DP-1"));
        QVERIFY2(r.found, "a record naming a missing screen must still restore via the fallback screen");
        QCOMPARE(r.zoneIds, QStringList{homeZone});
        QCOMPARE(r.screenId, QStringLiteral("DP-1"));
        QVERIFY(r.geometry.isValid());
        m_wts->setSnapState(nullptr);
    }

    // Composed end-to-end join of the two resolvers through the public toggle:
    // no live capture, no record, fallback setting ON → toggleWindowFloat's
    // unfloat path falls from resolveUnfloatGeometry (not-found) through to
    // resolveFallbackUnfloatGeometry and actually snaps the window into the
    // fallback zone. Pins the caller-side chaining that the per-resolver unit
    // tests above cover only in isolation.
    void testToggleUnfloat_noCaptureNoRecord_snapsToFallbackZone()
    {
        SnapEngine engine(m_layoutManager, m_wts, nullptr, nullptr, nullptr);
        engine.setEngineSettings(m_settings);
        m_wts->setSnapState(engine.snapState());

        auto* layout = installLayout(2);
        const QString firstZone = layout->zones().first()->id().toString();
        const QString w = QStringLiteral("app|composed");

        engine.snapState()->setFloatingOnScreen(w, QStringLiteral("DP-1"), 0);
        m_settings->setSnapUnfloatFallbackToZone(true);

        engine.toggleWindowFloat(w, QStringLiteral("DP-1"));

        QVERIFY2(!engine.snapState()->isFloating(w),
                 "with the fallback setting on, the unfloat toggle must leave the floating state");
        QCOMPARE(engine.snapState()->zoneForWindow(w), firstZone);
        m_wts->setSnapState(nullptr);
    }

    void testResolveUnfloat_noLiveCapture_noRecord_staysNotFound()
    {
        SnapEngine engine(m_layoutManager, m_wts, nullptr, nullptr, nullptr);
        engine.setEngineSettings(m_settings);
        m_wts->setSnapState(engine.snapState());

        installLayout(2);
        engine.snapState()->setFloatingOnScreen(QStringLiteral("app|norec"), QStringLiteral("DP-1"), 0);

        const PhosphorEngine::UnfloatResult r =
            engine.resolveUnfloatGeometry(QStringLiteral("app|norec"), QStringLiteral("DP-1"));
        QVERIFY2(!r.found, "no live capture and no record → not-found (caller falls to the fallback-zone path)");
        m_wts->setSnapState(nullptr);
    }

    // The record fallback consumes EXACT-windowId records only. A same-app
    // SIBLING's record (different instance, same appId FIFO bucket) must never
    // supply the unfloat target — without the accept predicate, peek's
    // appId-FIFO branch would hand this record-less window the sibling's home
    // zone and unfloat-snap it there (cross-window zone bleed).
    void testResolveUnfloat_noLiveCaptureNoOwnRecord_ignoresAppIdSibling()
    {
        SnapEngine engine(m_layoutManager, m_wts, nullptr, nullptr, nullptr);
        engine.setEngineSettings(m_settings);
        m_wts->setSnapState(engine.snapState());

        auto* layout = installLayout(2);
        const QString siblingZone = layout->zones().first()->id().toString();

        // The sibling instance persisted a snapped record; the window under
        // test is floating with NO record of its own and no live capture.
        seedSnapSlotRecord(engine, QStringLiteral("app|sibling"),
                           QString(PhosphorEngine::WindowPlacement::stateSnapped()), {siblingZone});
        engine.snapState()->setFloatingOnScreen(QStringLiteral("app|recordless"), QStringLiteral("DP-1"), 0);

        const PhosphorEngine::UnfloatResult r =
            engine.resolveUnfloatGeometry(QStringLiteral("app|recordless"), QStringLiteral("DP-1"));
        QVERIFY2(!r.found, "a same-app sibling's record must not supply the unfloat target");
        m_wts->setSnapState(nullptr);
    }
};

QTEST_MAIN(TestSnapUnfloatFallback)
#include "test_snap_unfloat_fallback.moc"
