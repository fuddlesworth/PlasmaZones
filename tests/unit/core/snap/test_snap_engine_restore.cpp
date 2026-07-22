// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "helpers/SnapEngineTestFixture.h"

/**
 * @brief SnapEngine restore-path coverage: the snap-to-empty auto-assign gate,
 *        focus-new-windows, resolveWindowRestore predicate gates, capturePlacement,
 *        and resolveFallbackUnfloatGeometry.
 */
class TestSnapEngineRestore : public SnapEngineTestFixture
{
    Q_OBJECT

private Q_SLOTS:

    void testCalculateSnapToEmptyZone_gate_globalOff_perLayoutOff_blocks()
    {
        SnapEngine engine(m_layoutManager, m_wts, nullptr, nullptr, nullptr);
        engine.setEngineSettings(m_settings);
        m_wts->setSnapState(engine.snapState());

        auto* layout = createTestLayout(2, m_layoutManager);
        m_layoutManager->addLayout(layout);
        m_layoutManager->setActiveLayout(layout);

        const QStringList lines =
            runGate(engine, layout, /*perLayoutAuto*/ false, /*globalAuto*/ false, QStringLiteral("DP-1"));

        QVERIFY2(lines.join(QLatin1Char('\n')).contains(QStringLiteral("autoAssign=false")),
                 "gate must short-circuit with the autoAssign=false debug log when both inputs are false");
        m_wts->setSnapState(nullptr);
    }

    void testCalculateSnapToEmptyZone_gate_globalOff_perLayoutOn_passes()
    {
        SnapEngine engine(m_layoutManager, m_wts, nullptr, nullptr, nullptr);
        engine.setEngineSettings(m_settings);
        m_wts->setSnapState(engine.snapState());

        auto* layout = createTestLayout(2, m_layoutManager);
        m_layoutManager->addLayout(layout);
        m_layoutManager->setActiveLayout(layout);

        const QStringList lines =
            runGate(engine, layout, /*perLayoutAuto*/ true, /*globalAuto*/ false, QStringLiteral("DP-1"));

        const QString joined = lines.join(QLatin1Char('\n'));
        QVERIFY2(!joined.contains(QStringLiteral("autoAssign=false")),
                 "per-layout flag alone must pass the gate (no autoAssign=false log)");
        m_wts->setSnapState(nullptr);
    }

    void testCalculateSnapToEmptyZone_gate_globalOn_perLayoutOff_passes()
    {
        // Force-on override (#370): when the global toggle is on, the gate
        // must pass even with the per-layout flag off.
        SnapEngine engine(m_layoutManager, m_wts, nullptr, nullptr, nullptr);
        engine.setEngineSettings(m_settings);
        m_wts->setSnapState(engine.snapState());

        auto* layout = createTestLayout(2, m_layoutManager);
        m_layoutManager->addLayout(layout);
        m_layoutManager->setActiveLayout(layout);

        const QStringList lines =
            runGate(engine, layout, /*perLayoutAuto*/ false, /*globalAuto*/ true, QStringLiteral("DP-1"));

        const QString joined = lines.join(QLatin1Char('\n'));
        QVERIFY2(!joined.contains(QStringLiteral("autoAssign=false")),
                 "global toggle alone must pass the gate (no autoAssign=false log)");
        m_wts->setSnapState(nullptr);
    }

    void testCalculateSnapToEmptyZone_gate_globalOn_perLayoutOn_passes()
    {
        SnapEngine engine(m_layoutManager, m_wts, nullptr, nullptr, nullptr);
        engine.setEngineSettings(m_settings);
        m_wts->setSnapState(engine.snapState());

        auto* layout = createTestLayout(2, m_layoutManager);
        m_layoutManager->addLayout(layout);
        m_layoutManager->setActiveLayout(layout);

        const QStringList lines =
            runGate(engine, layout, /*perLayoutAuto*/ true, /*globalAuto*/ true, QStringLiteral("DP-1"));

        const QString joined = lines.join(QLatin1Char('\n'));
        QVERIFY2(!joined.contains(QStringLiteral("autoAssign=false")),
                 "both inputs true must pass the gate (no autoAssign=false log)");
        m_wts->setSnapState(nullptr);
    }

    // =========================================================================
    // focus-new-windows — commitSnapImpl emits activateWindowRequested only for
    // AutoRestored commits when ISnapSettings::focusNewWindows() is on. Manual
    // (UserInitiated) commits never request focus, regardless of the setting.
    // =========================================================================

    void testFocusNewWindows_autoRestored_emitsWhenEnabled()
    {
        SnapEngine engine(m_layoutManager, m_wts, nullptr, nullptr, nullptr);
        engine.setEngineSettings(m_settings);
        m_wts->setSnapState(engine.snapState());
        m_settings->setSnappingFocusNewWindows(true);

        QSignalSpy spy(&engine, &SnapEngine::activateWindowRequested);
        engine.commitSnap(QStringLiteral("win-focus-1"), QStringLiteral("zone-1"), QStringLiteral("DP-1"),
                          PhosphorEngine::SnapIntent::AutoRestored);

        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.first().first().toString(), QStringLiteral("win-focus-1"));
        m_wts->setSnapState(nullptr);
    }

    void testFocusNewWindows_autoRestored_silentWhenDisabled()
    {
        SnapEngine engine(m_layoutManager, m_wts, nullptr, nullptr, nullptr);
        engine.setEngineSettings(m_settings);
        m_wts->setSnapState(engine.snapState());
        m_settings->setSnappingFocusNewWindows(false);

        QSignalSpy spy(&engine, &SnapEngine::activateWindowRequested);
        engine.commitSnap(QStringLiteral("win-focus-2"), QStringLiteral("zone-1"), QStringLiteral("DP-1"),
                          PhosphorEngine::SnapIntent::AutoRestored);

        QCOMPARE(spy.count(), 0);
        m_wts->setSnapState(nullptr);
    }

    void testFocusNewWindows_userInitiated_neverEmits()
    {
        SnapEngine engine(m_layoutManager, m_wts, nullptr, nullptr, nullptr);
        engine.setEngineSettings(m_settings);
        m_wts->setSnapState(engine.snapState());
        // Even with the setting on, a user-initiated snap (drag, keyboard) must
        // not steal focus — the window is already where the user put it.
        m_settings->setSnappingFocusNewWindows(true);

        QSignalSpy spy(&engine, &SnapEngine::activateWindowRequested);
        engine.commitSnap(QStringLiteral("win-focus-3"), QStringLiteral("zone-1"), QStringLiteral("DP-1"),
                          PhosphorEngine::SnapIntent::UserInitiated);

        QCOMPARE(spy.count(), 0);
        m_wts->setSnapState(nullptr);
    }

    void testFocusNewWindows_multiZone_autoRestored_emitsOnce()
    {
        // A multi-zone auto-restore (zone span) routes through the same
        // commitSnapImpl chokepoint, so it must request focus exactly once.
        SnapEngine engine(m_layoutManager, m_wts, nullptr, nullptr, nullptr);
        engine.setEngineSettings(m_settings);
        m_wts->setSnapState(engine.snapState());
        m_settings->setSnappingFocusNewWindows(true);

        QSignalSpy spy(&engine, &SnapEngine::activateWindowRequested);
        engine.commitMultiZoneSnap(QStringLiteral("win-focus-4"), {QStringLiteral("zone-1"), QStringLiteral("zone-2")},
                                   QStringLiteral("DP-1"), PhosphorEngine::SnapIntent::AutoRestored);

        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.first().first().toString(), QStringLiteral("win-focus-4"));
        m_wts->setSnapState(nullptr);
    }

    // =========================================================================
    // resolveWindowRestore — disabled-context gate (ShouldRestorePredicate,
    // discussion #461 item 7)
    //
    // The daemon injects a predicate that returns false for a screen the user
    // disabled snapping on. resolveWindowRestore must then refuse the restore —
    // and it must be that GATE that refuses, which the log capture asserts via
    // the distinctive debug line. With no predicate the engine behaves as if
    // every context is active (the historical default the rest of this suite
    // relies on).
    // =========================================================================

    void testResolveWindowRestore_disabledContextGate_rejectsDisabledScreen()
    {
        SnapEngine engine(m_layoutManager, m_wts, nullptr, nullptr, nullptr);
        engine.setEngineSettings(m_settings);
        m_wts->setSnapState(engine.snapState());

        // Predicate marks DP-OFF as a disabled context, every other screen active.
        engine.setShouldRestorePredicate([](const QString& screenId) {
            return screenId != QStringLiteral("DP-OFF");
        });

        PhosphorEngine::SnapResult result;
        const QStringList lines =
            captureResolveLogs(engine, QStringLiteral("app|uuid-gate-off"), QStringLiteral("DP-OFF"), &result);

        QVERIFY2(!result.shouldSnap, "a restore onto a disabled context must be refused");
        QVERIFY2(lines.join(QLatin1Char('\n')).contains(QStringLiteral("disabled-context gate rejected restore")),
                 "the disabled-context gate must be the branch that refused the restore");
        m_wts->setSnapState(nullptr);
    }

    void testResolveWindowRestore_disabledContextGate_allowsEnabledScreen()
    {
        SnapEngine engine(m_layoutManager, m_wts, nullptr, nullptr, nullptr);
        engine.setEngineSettings(m_settings);
        m_wts->setSnapState(engine.snapState());

        engine.setShouldRestorePredicate([](const QString& screenId) {
            return screenId != QStringLiteral("DP-OFF");
        });

        // DP-1 is active — the gate must not be the branch that fires. The
        // restore still resolves to noSnap in this guiless fixture (no app
        // rules / pending session entries / ScreenManager); this asserts only
        // that the GATE did not reject it.
        PhosphorEngine::SnapResult result;
        const QStringList lines =
            captureResolveLogs(engine, QStringLiteral("app|uuid-gate-on"), QStringLiteral("DP-1"), &result);

        QVERIFY2(!result.shouldSnap,
                 "guiless fixture has no layout/app-rule/session entry — restore resolves to noSnap");
        // Match the exact top-level gate line, not the broader "disabled-context
        // gate" substring (the appRule/session re-checks log a different
        // phrasing) — so the assertion fails only if the caller-screen gate
        // actually rejected an enabled context.
        QVERIFY2(!lines.join(QLatin1Char('\n')).contains(QStringLiteral("disabled-context gate rejected restore")),
                 "an enabled context must pass the disabled-context gate");
        m_wts->setSnapState(nullptr);
    }

    void testResolveWindowRestore_noPredicate_gateInactive()
    {
        SnapEngine engine(m_layoutManager, m_wts, nullptr, nullptr, nullptr);
        engine.setEngineSettings(m_settings);
        m_wts->setSnapState(engine.snapState());

        // No predicate injected — the engine must behave as if every context
        // is active (the historical default the rest of the suite relies on).
        PhosphorEngine::SnapResult result;
        const QStringList lines =
            captureResolveLogs(engine, QStringLiteral("app|uuid-no-pred"), QStringLiteral("DP-1"), &result);

        QVERIFY2(!result.shouldSnap,
                 "guiless fixture has no layout/app-rule/session entry — restore resolves to noSnap");
        QVERIFY2(!lines.join(QLatin1Char('\n')).contains(QStringLiteral("disabled-context gate rejected restore")),
                 "with no predicate the disabled-context gate must never fire");
        m_wts->setSnapState(nullptr);
    }

    // =========================================================================
    // resolveWindowRestore — managed (snapped-to-zone) restore gate
    // (ManagedRestorePredicate, restoreWindowsToZonesOnLogin)
    //
    // The daemon injects a predicate wired to restoreWindowsToZonesOnLogin. When
    // it returns false a window that was SNAPPED last session must NOT be
    // restored to its recorded zone on reopen — the snapped record is skipped and
    // the window falls through to the normal chain. The distinctive log isolates
    // this gate from a disabled-context veto. With no predicate (or one returning
    // true) the snapped path is taken (it still resolves to noSnap in this guiless
    // fixture because zone geometry can't resolve, asserted via log absence).
    // =========================================================================

    void testResolveWindowRestore_managedRestoreGate_rejectsWhenOff()
    {
        SnapEngine engine(m_layoutManager, m_wts, nullptr, nullptr, nullptr);
        engine.setEngineSettings(m_settings);
        m_wts->setSnapState(engine.snapState());

        // restoreWindowsToZonesOnLogin is OFF.
        engine.setManagedRestorePredicate([](const QString&) {
            return false;
        });

        // A window snapped on DP-1 (its recorded screen, snapping mode).
        PhosphorEngine::WindowPlacement rec;
        rec.windowId = QStringLiteral("app|orig");
        rec.appId = QStringLiteral("app");
        rec.screenId = QStringLiteral("DP-1");
        PhosphorEngine::EngineSlot slot;
        slot.state = PhosphorEngine::WindowPlacement::stateSnapped();
        slot.zoneIds = QStringList{QStringLiteral("z1")};
        rec.engines.insert(PhosphorEngine::WindowPlacement::snapEngineId(), slot);
        m_wts->placementStore().record(rec);

        PhosphorEngine::SnapResult result;
        const QStringList lines =
            captureResolveLogs(engine, QStringLiteral("app|new"), QStringLiteral("DP-1"), &result);

        QVERIFY2(!result.shouldSnap, "with managed restore off, a snapped record must not be re-applied");
        QVERIFY2(lines.join(QLatin1Char('\n')).contains(QStringLiteral("managed-restore gate skipped snapped record")),
                 "the managed-restore gate must be the branch that skipped the snapped record");
        m_wts->setSnapState(nullptr);
    }

    void testResolveWindowRestore_managedRestoreGate_allowsWhenOn()
    {
        SnapEngine engine(m_layoutManager, m_wts, nullptr, nullptr, nullptr);
        engine.setEngineSettings(m_settings);
        m_wts->setSnapState(engine.snapState());

        // Record that the gate actually CONSULTED the predicate. Geometry can't
        // resolve in the guiless fixture, so we can't assert a positive snap —
        // but this flag fails if the managed-restore gate were removed entirely
        // (a full revert would never call the predicate), which a bare
        // log-absence assertion would not catch.
        bool consulted = false;
        engine.setManagedRestorePredicate([&consulted](const QString&) {
            consulted = true;
            return true;
        });

        PhosphorEngine::WindowPlacement rec;
        rec.windowId = QStringLiteral("app|orig");
        rec.appId = QStringLiteral("app");
        rec.screenId = QStringLiteral("DP-1");
        PhosphorEngine::EngineSlot slot;
        slot.state = PhosphorEngine::WindowPlacement::stateSnapped();
        slot.zoneIds = QStringList{QStringLiteral("z1")};
        rec.engines.insert(PhosphorEngine::WindowPlacement::snapEngineId(), slot);
        m_wts->placementStore().record(rec);

        PhosphorEngine::SnapResult result;
        const QStringList lines =
            captureResolveLogs(engine, QStringLiteral("app|new"), QStringLiteral("DP-1"), &result);

        QVERIFY2(consulted, "the snapped-record restore path must consult the managed-restore predicate");
        QVERIFY2(!lines.join(QLatin1Char('\n')).contains(QStringLiteral("managed-restore gate skipped snapped record")),
                 "with managed restore on, the managed-restore gate must not skip the record");
        m_wts->setSnapState(nullptr);
    }

    void testResolveWindowRestore_managedRestoreGate_noPredicateRestores()
    {
        SnapEngine engine(m_layoutManager, m_wts, nullptr, nullptr, nullptr);
        engine.setEngineSettings(m_settings);
        m_wts->setSnapState(engine.snapState());

        // No predicate injected — the engine must restore snapped records
        // unconditionally (the historical default). This guards against a
        // regression that treated a NULL predicate as "block" (skip): the gate
        // must never fire when no predicate is wired.
        PhosphorEngine::WindowPlacement rec;
        rec.windowId = QStringLiteral("app|orig");
        rec.appId = QStringLiteral("app");
        rec.screenId = QStringLiteral("DP-1");
        PhosphorEngine::EngineSlot slot;
        slot.state = PhosphorEngine::WindowPlacement::stateSnapped();
        slot.zoneIds = QStringList{QStringLiteral("z1")};
        rec.engines.insert(PhosphorEngine::WindowPlacement::snapEngineId(), slot);
        m_wts->placementStore().record(rec);

        PhosphorEngine::SnapResult result;
        const QStringList lines =
            captureResolveLogs(engine, QStringLiteral("app|new"), QStringLiteral("DP-1"), &result);

        QVERIFY2(!lines.join(QLatin1Char('\n')).contains(QStringLiteral("managed-restore gate skipped snapped record")),
                 "with no predicate the managed-restore gate must never fire");
        m_wts->setSnapState(nullptr);
    }

    // =========================================================================
    // resolveWindowRestore — open-floating gate (FloatPredicate)
    //
    // The daemon injects a predicate that returns true when a "Float this app"
    // rule matched the opening window. resolveWindowRestore must then mark the
    // window floating and refuse the auto-snap chain, logging the distinctive
    // "floated by rule" line so the test can assert it was THAT branch — not the
    // no-match default-float terminal, which floats the window in this guiless
    // fixture too.
    // =========================================================================

    void testResolveWindowRestore_floatPredicate_floatsMatchedWindow()
    {
        SnapEngine engine(m_layoutManager, m_wts, nullptr, nullptr, nullptr);
        engine.setEngineSettings(m_settings);
        m_wts->setSnapState(engine.snapState());

        engine.setFloatPredicate([](const QString&) {
            return true;
        });

        const QString windowId = QStringLiteral("app|uuid-float-rule");
        QSignalSpy floatSpy(&engine, &SnapEngine::windowFloatingChanged);

        PhosphorEngine::SnapResult result;
        const QStringList lines = captureResolveLogs(engine, windowId, QStringLiteral("DP-1"), &result);

        QVERIFY2(!result.shouldSnap, "a rule-floated window must not auto-snap");
        // The window is floated on DP-1 via the open path, so it lands in that
        // screen's per-key store, not the global-scalar holder — check the engine's
        // store-agnostic float view rather than snapState() (globals).
        QVERIFY2(engine.isFloating(windowId), "the matched window must be marked floating");
        QVERIFY2(lines.join(QLatin1Char('\n')).contains(QStringLiteral("floated by rule")),
                 "the open-floating gate must be the branch that floated the window");
        QCOMPARE(floatSpy.count(), 1);
        QCOMPARE(floatSpy.at(0).at(0).toString(), windowId);
        QCOMPARE(floatSpy.at(0).at(1).toBool(), true);
        m_wts->setSnapState(nullptr);
    }

    void testResolveWindowRestore_floatPredicate_unsetOrFalse_gateInactive()
    {
        SnapEngine engine(m_layoutManager, m_wts, nullptr, nullptr, nullptr);
        engine.setEngineSettings(m_settings);
        m_wts->setSnapState(engine.snapState());

        // Predicate present but returns false — the gate must not fire. (No
        // predicate at all is the same: m_floatPredicate is empty.)
        engine.setFloatPredicate([](const QString&) {
            return false;
        });

        PhosphorEngine::SnapResult result;
        const QStringList lines =
            captureResolveLogs(engine, QStringLiteral("app|uuid-no-float-rule"), QStringLiteral("DP-1"), &result);

        QVERIFY2(!lines.join(QLatin1Char('\n')).contains(QStringLiteral("floated by rule")),
                 "an unmatched window must not be floated by the open-floating gate");
        m_wts->setSnapState(nullptr);
    }

    // =========================================================================
    // resolveWindowRestore — cross-screen ownership gate (multi-monitor login)
    //
    // A window snapped on a SNAP monitor can be reopened by KWin's session
    // restore on a DIFFERENT monitor that happens to be in autotile mode. The
    // opening-screen ownership gate must NOT blindly defer such a window to
    // autotile: its snapped record's RECORDED screen is in snapping mode, so the
    // restore migrates cross-screen back to that monitor (mirrors main, which
    // gated the defer on the saved screen). Conversely, a snapped record whose
    // OWN recorded screen is now autotile-owned must still defer (and must not be
    // consumed), leaving the record for the autotile engine.
    // =========================================================================

    void testResolveWindowRestore_crossScreenSnap_doesNotDeferOnAutotileOpeningScreen()
    {
        SnapEngine engine(m_layoutManager, m_wts, nullptr, nullptr, nullptr);
        engine.setEngineSettings(m_settings);
        m_wts->setSnapState(engine.snapState());

        // DP-2 is an autotile-mode screen; DP-1 stays snapping (the default).
        PhosphorZones::AssignmentEntry autotile;
        autotile.mode = PhosphorZones::AssignmentEntry::Autotile;
        autotile.tilingAlgorithm = QStringLiteral("dwindle");
        m_layoutManager->setAssignmentEntryDirect(QStringLiteral("DP-2"), 0, QString(), autotile);
        QCOMPARE(m_layoutManager->modeForScreen(QStringLiteral("DP-2"), 0, QString()),
                 PhosphorZones::AssignmentEntry::Autotile);

        // A window snapped on the SNAP monitor DP-1 (its recorded screen).
        PhosphorEngine::WindowPlacement rec;
        rec.windowId = QStringLiteral("app|orig");
        rec.appId = QStringLiteral("app");
        rec.screenId = QStringLiteral("DP-1");
        PhosphorEngine::EngineSlot slot;
        slot.state = PhosphorEngine::WindowPlacement::stateSnapped();
        slot.zoneIds = QStringList{QStringLiteral("z1")};
        rec.engines.insert(PhosphorEngine::WindowPlacement::snapEngineId(), slot);
        m_wts->placementStore().record(rec);

        // The session reopens the window (new uuid) on the AUTOTILE monitor DP-2.
        // The opening-screen ownership gate must NOT defer — the snapped record's
        // recorded screen (DP-1) is in snapping mode, so the restore migrates
        // cross-screen. (Geometry can't resolve in this guiless fixture, so we
        // assert via the absence of the defer log rather than result.shouldSnap.)
        PhosphorEngine::SnapResult result;
        const QStringList lines =
            captureResolveLogs(engine, QStringLiteral("app|new"), QStringLiteral("DP-2"), &result);

        QVERIFY2(!lines.join(QLatin1Char('\n')).contains(QStringLiteral("defers to the owning engine")),
                 "a pending cross-screen snap restore must NOT be deferred by the opening-screen ownership gate");
        m_wts->setSnapState(nullptr);
    }

    void testResolveWindowRestore_sameScreenAutotileRecord_defersAndPreservesRecord()
    {
        SnapEngine engine(m_layoutManager, m_wts, nullptr, nullptr, nullptr);
        engine.setEngineSettings(m_settings);
        m_wts->setSnapState(engine.snapState());

        // DP-2 is autotile mode AND the recorded screen of the snapped record
        // (the user switched DP-2 to autotile after snapping there last session).
        PhosphorZones::AssignmentEntry autotile;
        autotile.mode = PhosphorZones::AssignmentEntry::Autotile;
        autotile.tilingAlgorithm = QStringLiteral("dwindle");
        m_layoutManager->setAssignmentEntryDirect(QStringLiteral("DP-2"), 0, QString(), autotile);

        PhosphorEngine::WindowPlacement rec;
        rec.windowId = QStringLiteral("app|orig");
        rec.appId = QStringLiteral("app");
        rec.screenId = QStringLiteral("DP-2"); // recorded on the now-autotile screen
        PhosphorEngine::EngineSlot slot;
        slot.state = PhosphorEngine::WindowPlacement::stateSnapped();
        slot.zoneIds = QStringList{QStringLiteral("z1")};
        rec.engines.insert(PhosphorEngine::WindowPlacement::snapEngineId(), slot);
        m_wts->placementStore().record(rec);

        // Reopen on DP-2. No cross-screen restore is pending (the recorded screen
        // is autotile), so the gate must defer AND must not consume the record —
        // autotile still needs it.
        PhosphorEngine::SnapResult result;
        const QStringList lines =
            captureResolveLogs(engine, QStringLiteral("app|new"), QStringLiteral("DP-2"), &result);

        QVERIFY2(!result.shouldSnap, "a window on an autotile screen with no cross-screen snap restore must not snap");
        QVERIFY2(lines.join(QLatin1Char('\n')).contains(QStringLiteral("defers to the owning engine")),
                 "the opening-screen ownership gate must defer to autotile");
        QVERIFY2(m_wts->placementStore().contains(QStringLiteral("app|orig"), QStringLiteral("app")),
                 "deferring must not consume the snapped record — autotile still needs it");
        m_wts->setSnapState(nullptr);
    }

    // =========================================================================
    // resolveWindowRestore — unsnapped-position restore gate
    // (RestorePositionPredicate)
    //
    // A FREE (never-snapped) or snap-FLOATED window persists its global position
    // keyed by its recorded screen. Float/free position restore is SAME-MONITOR
    // ONLY: a record is eligible only when the window reopens on its recorded
    // screen, and even then the geometry MOVE is gated on the restore-position
    // predicate (the snappingRestoreFloatedWindowsOnLogin setting / RestorePosition
    // rule). A record whose recorded screen differs from the opening screen is
    // NEVER consumed and NEVER moves the window — float restore must not drag a
    // window across monitors: a stale sibling record on another output would
    // otherwise teleport a freshly-launched window, and the wrong-monitor capture
    // that follows would re-cement it into a self-perpetuating jump. KWin's own
    // placement / session restore owns which monitor the window opens on.
    // =========================================================================

    void testResolveWindowRestore_freeCrossScreen_inertEvenWithOptIn()
    {
        SnapEngine engine(m_layoutManager, m_wts, nullptr, nullptr, nullptr);
        engine.setEngineSettings(m_settings);
        m_wts->setSnapState(engine.snapState());
        engine.setRestorePositionPredicate([](const QString&) {
            return true;
        });

        // A genuinely-free window last seen on DP-1 at a recorded global rect.
        const QRect dp1Geo(120, 80, 800, 600);
        PhosphorEngine::WindowPlacement rec;
        rec.windowId = QStringLiteral("app|orig");
        rec.appId = QStringLiteral("app");
        rec.screenId = QStringLiteral("DP-1");
        PhosphorEngine::EngineSlot slot;
        slot.state = PhosphorEngine::WindowPlacement::stateFree();
        rec.engines.insert(PhosphorEngine::WindowPlacement::snapEngineId(), slot);
        rec.freeGeometryByScreen.insert(QStringLiteral("DP-1"), dp1Geo);
        m_wts->placementStore().record(rec);

        QSignalSpy geoSpy(&engine, &PhosphorEngine::PlacementEngineBase::geometryRestoreRequested);

        // KWin reopens the window (new uuid) on DP-2. Float/free restore is
        // same-monitor only: even with the opt-in predicate, a record recorded on
        // DP-1 must NOT move the window to its old monitor, and must NOT be
        // consumed — it stays available for a later open on DP-1.
        const PhosphorEngine::SnapResult result =
            engine.resolveWindowRestore(QStringLiteral("app|new"), QStringLiteral("DP-2"), /*sticky*/ false);

        QVERIFY2(!result.shouldSnap, "a free window is never snapped into a zone");
        QCOMPARE(geoSpy.count(), 0);
        QVERIFY2(m_wts->placementStore().contains(QStringLiteral("app|orig"), QStringLiteral("app")),
                 "a cross-monitor free record is not consumed — float restore never moves across monitors");
        QVERIFY2(!m_wts->placementStore().contains(QStringLiteral("app|new")),
                 "nothing is re-recorded under the live id when the cross-monitor record is left untouched");
        m_wts->setSnapState(nullptr);
    }

    void testResolveWindowRestore_freeCrossScreen_inertWhenPredicateAbsent()
    {
        SnapEngine engine(m_layoutManager, m_wts, nullptr, nullptr, nullptr);
        engine.setEngineSettings(m_settings);
        m_wts->setSnapState(engine.snapState());
        // No restore-position predicate — historical behaviour.

        PhosphorEngine::WindowPlacement rec;
        rec.windowId = QStringLiteral("app|orig");
        rec.appId = QStringLiteral("app");
        rec.screenId = QStringLiteral("DP-1");
        PhosphorEngine::EngineSlot slot;
        slot.state = PhosphorEngine::WindowPlacement::stateFree();
        rec.engines.insert(PhosphorEngine::WindowPlacement::snapEngineId(), slot);
        rec.freeGeometryByScreen.insert(QStringLiteral("DP-1"), QRect(120, 80, 800, 600));
        m_wts->placementStore().record(rec);

        QSignalSpy geoSpy(&engine, &PhosphorEngine::PlacementEngineBase::geometryRestoreRequested);

        const PhosphorEngine::SnapResult result =
            engine.resolveWindowRestore(QStringLiteral("app|new"), QStringLiteral("DP-2"), /*sticky*/ false);

        QVERIFY(!result.shouldSnap);
        QCOMPARE(geoSpy.count(), 0);
        QVERIFY2(m_wts->placementStore().contains(QStringLiteral("app|orig"), QStringLiteral("app")),
                 "without opt-in, a cross-screen free record stays gated on the opening screen and is not consumed");
        m_wts->setSnapState(nullptr);
    }

    void testResolveWindowRestore_floatingCrossScreen_doesNotMoveAcrossMonitors()
    {
        SnapEngine engine(m_layoutManager, m_wts, nullptr, nullptr, nullptr);
        engine.setEngineSettings(m_settings);
        m_wts->setSnapState(engine.snapState());
        engine.setRestorePositionPredicate([](const QString&) {
            return true;
        });

        // A snap-floated window: state floating, carrying its pre-float zone, with
        // a recorded free position on DP-1.
        const QRect dp1Geo(200, 150, 640, 480);
        PhosphorEngine::WindowPlacement rec;
        rec.windowId = QStringLiteral("app|orig");
        rec.appId = QStringLiteral("app");
        rec.screenId = QStringLiteral("DP-1");
        PhosphorEngine::EngineSlot slot;
        slot.state = PhosphorEngine::WindowPlacement::stateFloating();
        slot.zoneIds = QStringList{QStringLiteral("z1")};
        rec.engines.insert(PhosphorEngine::WindowPlacement::snapEngineId(), slot);
        rec.freeGeometryByScreen.insert(QStringLiteral("DP-1"), dp1Geo);
        m_wts->placementStore().record(rec);

        QSignalSpy geoSpy(&engine, &PhosphorEngine::PlacementEngineBase::geometryRestoreRequested);

        // Reopen on DP-2. Same-monitor-only: a floated record recorded on DP-1
        // must not teleport the window to DP-1, and must not be consumed — it stays
        // for a later DP-1 open. (This is the konsole/ghastty wrong-monitor bug:
        // a stale cross-monitor floated record must never drag a fresh window.)
        const PhosphorEngine::SnapResult result =
            engine.resolveWindowRestore(QStringLiteral("app|new"), QStringLiteral("DP-2"), /*sticky*/ false);

        QVERIFY2(!result.shouldSnap, "a floated record never snaps into a zone");
        QVERIFY2(geoSpy.count() == 0, "float restore never moves a window across monitors");
        QVERIFY2(m_wts->placementStore().contains(QStringLiteral("app|orig"), QStringLiteral("app")),
                 "the cross-monitor floating record is left intact for a later DP-1 open");
        QVERIFY2(!m_wts->placementStore().contains(QStringLiteral("app|new")),
                 "nothing is re-recorded under the live id for a rejected cross-monitor record");
        m_wts->setSnapState(nullptr);
    }

    // LEGACY `free` record restored as floating (the retired third state). A `free`
    // slot persisted by an older build is now treated as floating: the merged branch
    // marks it floating (windowFloatingChanged true) AND, with the predicate opted
    // in, re-applies its recorded position on its own screen.
    void testResolveWindowRestore_legacyFreeSameScreen_restoresAsFloatingWhenPredicateOptsIn()
    {
        SnapEngine engine(m_layoutManager, m_wts, nullptr, nullptr, nullptr);
        engine.setEngineSettings(m_settings);
        m_wts->setSnapState(engine.snapState());
        engine.setRestorePositionPredicate([](const QString&) {
            return true;
        });

        const QRect dp1Geo(60, 40, 1024, 768);
        PhosphorEngine::WindowPlacement rec;
        rec.windowId = QStringLiteral("app|orig");
        rec.appId = QStringLiteral("app");
        rec.screenId = QStringLiteral("DP-1");
        PhosphorEngine::EngineSlot slot;
        slot.state = PhosphorEngine::WindowPlacement::stateFree(); // legacy token
        rec.engines.insert(PhosphorEngine::WindowPlacement::snapEngineId(), slot);
        rec.freeGeometryByScreen.insert(QStringLiteral("DP-1"), dp1Geo);
        m_wts->placementStore().record(rec);

        QSignalSpy floatSpy(&engine, &PhosphorEngine::PlacementEngineBase::windowFloatingChanged);
        QSignalSpy geoSpy(&engine, &PhosphorEngine::PlacementEngineBase::geometryRestoreRequested);

        // Reopens on its own recorded screen DP-1.
        const PhosphorEngine::SnapResult result =
            engine.resolveWindowRestore(QStringLiteral("app|new"), QStringLiteral("DP-1"), /*sticky*/ false);

        QVERIFY(!result.shouldSnap);
        // Legacy free now marks floating (the old free branch did NOT).
        QCOMPARE(floatSpy.count(), 1);
        QCOMPARE(floatSpy.takeFirst().at(1).toBool(), true);
        QCOMPARE(geoSpy.count(), 1);
        const QList<QVariant> args = geoSpy.takeFirst();
        QCOMPARE(args.at(1).toRect(), dp1Geo);
        QCOMPARE(args.at(2).toString(), QStringLiteral("DP-1"));
        m_wts->setSnapState(nullptr);
    }

    // The predicate's RETURN VALUE is honored (not merely its presence): a free
    // record reopening on its own screen with the predicate returning false is
    // still eligible (same-screen) and re-recorded under the live id, but its
    // position is NOT re-applied. This is the load-bearing re-record-but-gate-move
    // boundary the free branch documents.
    void testResolveWindowRestore_freeSameScreen_reRecordsButSkipsMoveWhenPredicateDenies()
    {
        SnapEngine engine(m_layoutManager, m_wts, nullptr, nullptr, nullptr);
        engine.setEngineSettings(m_settings);
        m_wts->setSnapState(engine.snapState());
        engine.setRestorePositionPredicate([](const QString&) {
            return false;
        });

        PhosphorEngine::WindowPlacement rec;
        rec.windowId = QStringLiteral("app|orig");
        rec.appId = QStringLiteral("app");
        rec.screenId = QStringLiteral("DP-1");
        PhosphorEngine::EngineSlot slot;
        slot.state = PhosphorEngine::WindowPlacement::stateFree();
        rec.engines.insert(PhosphorEngine::WindowPlacement::snapEngineId(), slot);
        rec.freeGeometryByScreen.insert(QStringLiteral("DP-1"), QRect(60, 40, 1024, 768));
        m_wts->placementStore().record(rec);

        QSignalSpy geoSpy(&engine, &PhosphorEngine::PlacementEngineBase::geometryRestoreRequested);

        const PhosphorEngine::SnapResult result =
            engine.resolveWindowRestore(QStringLiteral("app|new"), QStringLiteral("DP-1"), /*sticky*/ false);

        QVERIFY(!result.shouldSnap);
        // A predicate that denies restore must skip the geometry move.
        QCOMPARE(geoSpy.count(), 0);
        // The record is still rebound to the live id (float-back survives) even
        // though the move is skipped; the stale recorded-uuid entry is gone.
        QVERIFY2(m_wts->placementStore().contains(QStringLiteral("app|new")),
                 "a same-screen free record is re-recorded under the live id even when the move is skipped");
        QVERIFY2(!m_wts->placementStore().contains(QStringLiteral("app|orig")),
                 "the stale recorded-uuid entry is rebound, not left behind");
        m_wts->setSnapState(nullptr);
    }

    // Two-state model: the merged floated-restore branch ALWAYS marks the window
    // floating (windowFloatingChanged true), but the geometry MOVE is now gated on
    // the restore-position predicate for ALL floated windows (the
    // snappingRestoreFloatedWindowsOnLogin setting / RestorePosition rule). When the
    // predicate DENIES, the window comes back floating but stays where the
    // compositor placed it — the move is skipped.
    void testResolveWindowRestore_floatingSameScreen_marksFloatingButSkipsMoveWhenPredicateDenies()
    {
        SnapEngine engine(m_layoutManager, m_wts, nullptr, nullptr, nullptr);
        engine.setEngineSettings(m_settings);
        m_wts->setSnapState(engine.snapState());
        engine.setRestorePositionPredicate([](const QString&) {
            return false;
        });

        const QRect dp1Geo(200, 150, 640, 480);
        PhosphorEngine::WindowPlacement rec;
        rec.windowId = QStringLiteral("app|orig");
        rec.appId = QStringLiteral("app");
        rec.screenId = QStringLiteral("DP-1");
        PhosphorEngine::EngineSlot slot;
        slot.state = PhosphorEngine::WindowPlacement::stateFloating();
        slot.zoneIds = QStringList{QStringLiteral("z1")};
        rec.engines.insert(PhosphorEngine::WindowPlacement::snapEngineId(), slot);
        rec.freeGeometryByScreen.insert(QStringLiteral("DP-1"), dp1Geo);
        m_wts->placementStore().record(rec);

        QSignalSpy floatSpy(&engine, &PhosphorEngine::PlacementEngineBase::windowFloatingChanged);
        QSignalSpy geoSpy(&engine, &PhosphorEngine::PlacementEngineBase::geometryRestoreRequested);

        // Reopens on its own recorded screen DP-1, predicate denying.
        const PhosphorEngine::SnapResult result =
            engine.resolveWindowRestore(QStringLiteral("app|new"), QStringLiteral("DP-1"), /*sticky*/ false);

        QVERIFY(!result.shouldSnap);
        // Marked floating unconditionally...
        QCOMPARE(floatSpy.count(), 1);
        const QList<QVariant> floatArgs = floatSpy.takeFirst();
        QCOMPARE(floatArgs.at(1).toBool(), true);
        QCOMPARE(floatArgs.at(2).toString(), QStringLiteral("DP-1"));
        // ...but the geometry move is gated by the predicate (denied → skipped).
        QVERIFY2(geoSpy.count() == 0, "floated move is gated on the restore-position predicate; denied → no move");
        m_wts->setSnapState(nullptr);
    }

    // Same-monitor-only restore rejects a cross-monitor record outright —
    // regardless of which screen its recorded geometry lives on. Even with the
    // opt-in predicate and geometry captured on a THIRD screen, the record is
    // neither moved nor consumed: no anyFreeGeometry() resurrection, no
    // cross-monitor teleport, and the record is left for a same-monitor open.
    void testResolveWindowRestore_freeCrossScreen_inertRegardlessOfGeometryScreen()
    {
        SnapEngine engine(m_layoutManager, m_wts, nullptr, nullptr, nullptr);
        engine.setEngineSettings(m_settings);
        m_wts->setSnapState(engine.snapState());
        engine.setRestorePositionPredicate([](const QString&) {
            return true;
        });

        PhosphorEngine::WindowPlacement rec;
        rec.windowId = QStringLiteral("app|orig");
        rec.appId = QStringLiteral("app");
        rec.screenId = QStringLiteral("DP-1"); // restoreScreen resolves to DP-1
        PhosphorEngine::EngineSlot slot;
        slot.state = PhosphorEngine::WindowPlacement::stateFree();
        rec.engines.insert(PhosphorEngine::WindowPlacement::snapEngineId(), slot);
        // Geometry recorded on DP-3, NOT on the record's own screen DP-1.
        rec.freeGeometryByScreen.insert(QStringLiteral("DP-3"), QRect(10, 10, 800, 600));
        m_wts->placementStore().record(rec);

        QSignalSpy geoSpy(&engine, &PhosphorEngine::PlacementEngineBase::geometryRestoreRequested);

        // KWin reopens on DP-2 (neither the record's screen nor its geometry's screen).
        const PhosphorEngine::SnapResult result =
            engine.resolveWindowRestore(QStringLiteral("app|new"), QStringLiteral("DP-2"), /*sticky*/ false);

        QVERIFY(!result.shouldSnap);
        QVERIFY2(geoSpy.count() == 0, "a cross-monitor record never moves the window");
        QVERIFY2(m_wts->placementStore().contains(QStringLiteral("app|orig"), QStringLiteral("app")),
                 "a cross-monitor record is not consumed regardless of where its geometry was captured");
        QVERIFY2(!m_wts->placementStore().contains(QStringLiteral("app|new")),
                 "nothing is re-recorded under the live id");
        m_wts->setSnapState(nullptr);
    }

    // =========================================================================
    // Two-state model: snapping has only `snapped` / `floated` — no `free`.
    // =========================================================================

    // capturePlacement of a window that the snap engine does not track at all
    // captures NOTHING.
    // It previously fabricated a FLOATING slot (the untracked window has no
    // effective screen, so the snap-mode gate was bypassed and it reached the
    // state if/else) — and that fabrication overwrote the record's frozen
    // snap-mode memory whenever a capture ran after a cross-engine
    // handoffRelease (autotile adoption), which windowsReleased then read back
    // and "restored" as a phantom float on return to snapping. A TRACKED
    // window that is floating still captures a FLOATING slot — never the
    // retired `free` state.
    void testCapturePlacement_untrackedCapturesNothing_trackedFloatsAsFloating()
    {
        SnapEngine engine(m_layoutManager, m_wts, nullptr, nullptr, nullptr);
        engine.setEngineSettings(m_settings);
        m_wts->setSnapState(engine.snapState());

        QVERIFY2(!engine.capturePlacement(QStringLiteral("app|unmanaged")).has_value(),
                 "an untracked window must capture nothing — fabricating a slot clobbers frozen per-mode memory");

        const QString tracked = QStringLiteral("app|floaty");
        engine.snapState()->setFloatingOnScreen(tracked, QStringLiteral("DP-1"), 0);
        const auto p = engine.capturePlacement(tracked);
        QVERIFY(p.has_value());
        const PhosphorEngine::EngineSlot slot = p->slotFor(PhosphorEngine::WindowPlacement::snapEngineId());
        QCOMPARE(slot.state, QString(PhosphorEngine::WindowPlacement::stateFloating()));
        QVERIFY2(slot.state != PhosphorEngine::WindowPlacement::stateFree(),
                 "the retired `free` state must never be produced");
        m_wts->setSnapState(nullptr);
    }

    // A new window that matches no auto-snap rule on a snap-mode screen defaults to
    // FLOATED (not the retired `free`): resolveWindowRestore marks it floating
    // (windowFloatingChanged true) and returns noSnap. The guiless fixture wires no
    // mode providers, so the unconfigured DP-1 resolves to the default Snapping mode
    // (LayoutRegistry::resolveDefaultAssignmentEntry → default-constructed entry,
    // Mode::Snapping == 0). The window therefore deterministically reaches the
    // snap-mode no-match fallthrough; the distinctive log line pins that path.
    void testResolveWindowRestore_newWindowNoMatch_defaultsToFloating()
    {
        SnapEngine engine(m_layoutManager, m_wts, nullptr, nullptr, nullptr);
        engine.setEngineSettings(m_settings);
        m_wts->setSnapState(engine.snapState());

        auto* layout = createTestLayout(2, m_layoutManager);
        m_layoutManager->addLayout(layout);
        m_layoutManager->setActiveLayout(layout);

        QSignalSpy floatSpy(&engine, &PhosphorEngine::PlacementEngineBase::windowFloatingChanged);

        PhosphorEngine::SnapResult result;
        const QStringList lines =
            captureResolveLogs(engine, QStringLiteral("app|brand-new"), QStringLiteral("DP-1"), &result);
        const QString joined = lines.join(QLatin1Char('\n'));

        QVERIFY(!result.shouldSnap);
        QVERIFY2(joined.contains(QStringLiteral("defaulting to floated")),
                 "a no-match window on a snap-mode screen must default to floated");
        QCOMPARE(floatSpy.count(), 1);
        QCOMPARE(floatSpy.takeFirst().at(1).toBool(), true);
        m_wts->setSnapState(nullptr);
    }

    // The unfloatFallbackToZone setting GATES the fallback: with it off, a window
    // that has no pre-float zone gets no fallback target (resolveFallbackUnfloatGeometry
    // returns not-found), so unfloat keeps it floating. (The on-success geometry path
    // needs a valid zoneGeometry, which this guiless fixture cannot produce — null
    // QGuiApplication::primaryScreen(); it is covered end-to-end in the QTEST_MAIN
    // test_snap_unfloat_fallback.cpp instead.)
    void testResolveFallbackUnfloatGeometry_offReturnsNotFound()
    {
        SnapEngine engine(m_layoutManager, m_wts, nullptr, nullptr, nullptr);
        engine.setEngineSettings(m_settings);
        m_wts->setSnapState(engine.snapState());

        auto* layout = createTestLayout(2, m_layoutManager);
        m_layoutManager->addLayout(layout);
        m_layoutManager->setActiveLayout(layout);

        // A floating window with no pre-float zone, on a known screen.
        engine.snapState()->setFloatingOnScreen(QStringLiteral("app|nofloatzone"), QStringLiteral("DP-1"), 0);

        // Setting OFF (the StubSettings default) → no fallback, regardless of layout.
        m_settings->setSnapUnfloatFallbackToZone(false);
        QVERIFY2(
            !engine.resolveFallbackUnfloatGeometry(QStringLiteral("app|nofloatzone"), QStringLiteral("DP-1")).found,
            "unfloatFallbackToZone off → no fallback target");
        m_wts->setSnapState(nullptr);
    }

    // Companion to the OFF test: with the setting ON, the resolver runs the full
    // chain PAST the opt-in gate — effective-screen resolution, layout lookup, and
    // zone selection (last-used → first-empty → first zone) — and reaches the final
    // geometry gate. Under QTEST_GUILESS_MAIN there is no QScreen, so zoneGeometry()
    // returns an invalid QRect and the result is still not-found. This pins that (a)
    // the post-gate chain executes without crashing on a real layout, and (b) the
    // headless geometry limitation — not a broken gate — is what produces not-found
    // here (the on-success geometry path is covered in test_snap_unfloat_fallback.cpp).
    void testResolveFallbackUnfloatGeometry_onButHeadlessReturnsNotFound()
    {
        SnapEngine engine(m_layoutManager, m_wts, nullptr, nullptr, nullptr);
        engine.setEngineSettings(m_settings);
        m_wts->setSnapState(engine.snapState());

        auto* layout = createTestLayout(2, m_layoutManager);
        m_layoutManager->addLayout(layout);
        m_layoutManager->setActiveLayout(layout);

        engine.snapState()->setFloatingOnScreen(QStringLiteral("app|nofloatzone"), QStringLiteral("DP-1"), 0);

        m_settings->setSnapUnfloatFallbackToZone(true);
        QVERIFY2(
            !engine.resolveFallbackUnfloatGeometry(QStringLiteral("app|nofloatzone"), QStringLiteral("DP-1")).found,
            "unfloatFallbackToZone on, but headless zoneGeometry is invalid → still no fallback target");
        m_wts->setSnapState(nullptr);
    }
};

QTEST_GUILESS_MAIN(TestSnapEngineRestore)
#include "test_snap_engine_restore.moc"
