// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_wta_screen_changed.cpp
 * @brief WindowTrackingAdaptor::windowScreenChanged coverage: the
 *        stored-screen comparison that decides between keeping the snap and
 *        unsnapping, the empty-screen bail, the "screen_changed"
 *        windowStateChanged emission, and, with per-screen stores, what an
 *        output move releases on the output left. Also the activation-side
 *        reads of a window's screen: the snap re-home backstop, the last-used
 *        update, and the focused screen the shortcuts act on.
 *
 * Why this surface earns a suite of its own: it is the arm a compositor-side
 * defect fires straight into. The effect's endDrag ApplySnap branch calls
 * cancelInteractiveMoveResize() to end KWin's interactive move before writing
 * the zone rect, and that cancel REVERTS the window to its drag-start rect —
 * the source monitor on a cross-screen drop. KWin emits outputChanged
 * synchronously from the revert, and with the drag already stopped (the
 * endDrag reply is async) nothing in the effect held it back, so the daemon
 * received windowScreenChanged naming the SOURCE screen moments after
 * commitSnap had stored the TARGET. This adaptor then did exactly what the
 * contract below says it should — read the mismatch as the user moving the
 * window off its zone and unsnap — which surfaced to users as a drop that
 * lands at the zone rect with no snap state behind it (reported 2026-08-31,
 * PZ 3.4.3, dual-head).
 *
 * The daemon behaviour was never wrong; the input was. These cases pin the
 * contract from both sides so the compositor-side guard has something
 * explicit to be correct against: a report naming the ASSIGNED screen must
 * leave the snap intact, and only a genuine divergence may unsnap.
 */

#include "wta_convenience_fixture.h"
#include "helpers/StubPlacementEngine.h"
#include "helpers/VirtualScreenTestHelpers.h"

#include <PhosphorIdentity/VirtualScreenId.h>
#include <PhosphorScreens/VirtualScreen.h>
#include <QScopeGuard>

class TestWtaScreenChanged : public QObject, protected WtaConvenienceFixture
{
    Q_OBJECT

private Q_SLOTS:
    void init()
    {
        initFixture();
    }
    void cleanup()
    {
        cleanupFixture();
    }

    // ── The keep-snap arm ────────────────────────────────────────────────
    //
    // The commit stores the target screen; a screen report naming that same
    // screen is the daemon's own placement being observed back, not a user
    // move. This is the arm the effect's ApplySnap guard exists to land on:
    // whatever KWin says mid-apply, what reaches here must either be
    // suppressed outright or name the screen the window was snapped to.
    void testWindowScreenChanged_reportForAssignedScreenKeepsTheSnap()
    {
        const QString windowId = QStringLiteral("brave-browser|screen-keep");
        const QString target = QStringLiteral("HDMI-1");

        m_snapEngine->commitSnap(windowId, m_zoneIds[0], target);
        QCOMPARE(m_wta->service()->screenForWindow(windowId), target);

        m_wta->windowScreenChanged(windowId, target);

        QCOMPARE(m_wta->service()->zoneForWindow(windowId), m_zoneIds[0]);
        QCOMPARE(m_wta->service()->screenForWindow(windowId), target);
    }

    // ── The unsnap arm ───────────────────────────────────────────────────
    //
    // The genuine user gesture this arm is FOR ("Move to Screen" on a snapped
    // window): the window leaves the monitor holding its zone, so the zone
    // assignment must not follow it.
    void testWindowScreenChanged_reportForADifferentScreenUnsnaps()
    {
        const QString windowId = QStringLiteral("brave-browser|screen-move");
        const QString assigned = QStringLiteral("HDMI-1");
        const QString elsewhere = QStringLiteral("DP-1");

        m_snapEngine->commitSnap(windowId, m_zoneIds[0], assigned);
        QVERIFY(!m_wta->service()->zoneForWindow(windowId).isEmpty());

        m_wta->windowScreenChanged(windowId, elsewhere);

        QVERIFY2(m_wta->service()->zoneForWindow(windowId).isEmpty(),
                 "a report naming a screen other than the assigned one must drop the zone assignment");
    }

    // The unsnap arm's wire half: subscribers are told the window is no
    // longer snapped, tagged "screen_changed", and carrying the screen the
    // decision was actually made against rather than the stored one.
    void testWindowScreenChanged_unsnapEmitsScreenChangedWithTheResolvedScreen()
    {
        const QString windowId = QStringLiteral("brave-browser|screen-emit");
        const QString elsewhere = QStringLiteral("DP-1");

        m_snapEngine->commitSnap(windowId, m_zoneIds[0], QStringLiteral("HDMI-1"));

        QSignalSpy spy(m_wta, &WindowTrackingAdaptor::windowStateChanged);
        m_wta->windowScreenChanged(windowId, elsewhere);

        bool found = false;
        for (int i = 0; i < spy.count(); ++i) {
            const auto state = spy.at(i).at(1).value<PhosphorProtocol::WindowStateEntry>();
            if (state.changeType != QLatin1String("screen_changed")) {
                continue;
            }
            QCOMPARE(spy.at(i).at(0).toString(), windowId);
            QCOMPARE(state.screenId, elsewhere);
            QCOMPARE(state.zoneId, QString());
            QVERIFY(!state.isFloating);
            found = true;
            break;
        }
        QVERIFY2(found, "the screen-change unsnap must publish a screen_changed entry");
    }

    // ── The empty-screen bail ────────────────────────────────────────────
    //
    // An empty id is "no tracking", not a screen. Letting it through would
    // store an empty screen downstream, and the live screen arrives on the
    // next callback anyway — so a snapped window must be left alone.
    void testWindowScreenChanged_emptyScreenIsIgnored()
    {
        const QString windowId = QStringLiteral("brave-browser|screen-empty");
        const QString assigned = QStringLiteral("HDMI-1");

        m_snapEngine->commitSnap(windowId, m_zoneIds[0], assigned);

        m_wta->windowScreenChanged(windowId, QString());

        QCOMPARE(m_wta->service()->zoneForWindow(windowId), m_zoneIds[0]);
        QCOMPARE(m_wta->service()->screenForWindow(windowId), assigned);
    }

    // ── The virtual-screen arm ───────────────────────────────────────────
    //
    // KWin reports PHYSICAL output names, so on a subdivided monitor the
    // assigned screen ("HDMI-1/vs:0") never equals the reported one
    // ("HDMI-1") and a naive compare unsnaps every window on every report.
    // The physical-parent check is what stops that, and it is the arm the
    // cross-screen drop lands on for any user running virtual screens — the
    // same configuration the effect-side guard was written for.
    void testWindowScreenChanged_physicalReportForAVirtualAssignedScreenKeepsTheSnap()
    {
        const QString windowId = QStringLiteral("brave-browser|screen-virtual");
        const QString assigned = QStringLiteral("HDMI-1/vs:0");

        m_snapEngine->commitSnap(windowId, m_zoneIds[0], assigned);
        QCOMPARE(m_wta->service()->screenForWindow(windowId), assigned);

        m_wta->windowScreenChanged(windowId, QStringLiteral("HDMI-1"));

        QCOMPARE(m_wta->service()->zoneForWindow(windowId), m_zoneIds[0]);
        QCOMPARE(m_wta->service()->screenForWindow(windowId), assigned);
    }

    // The other half of that arm: a physical report naming a DIFFERENT
    // monitor than the virtual screen's parent is a genuine move and must
    // still unsnap, so the check above cannot be read as "virtual assignments
    // never unsnap".
    void testWindowScreenChanged_physicalReportForAnotherMonitorUnsnapsAVirtualAssignment()
    {
        const QString windowId = QStringLiteral("brave-browser|screen-virtual-move");

        m_snapEngine->commitSnap(windowId, m_zoneIds[0], QStringLiteral("HDMI-1/vs:0"));
        QVERIFY(!m_wta->service()->zoneForWindow(windowId).isEmpty());

        m_wta->windowScreenChanged(windowId, QStringLiteral("DP-1"));

        QVERIFY2(m_wta->service()->zoneForWindow(windowId).isEmpty(),
                 "a report from a different physical monitor must drop a virtual-screen assignment");
    }

    // The same move with a real ScreenManager, which the shared fixture lacks.
    // The physical report is resolved to a virtual screen through the window's
    // FRAME. Resolving it through the zone instead measured the zone on the
    // stored screen, landed back on the stored virtual screen, and kept the
    // snap of a window that had changed monitors.
    void testWindowScreenChanged_physicalReportResolvesTheVirtualScreenFromTheFrame()
    {
        const QString hdmi = QStringLiteral("HDMI-1");
        const QString dp = QStringLiteral("DP-1");
        PhosphorScreens::FakePhysicalScreenSource fake;
        fake.addScreen(hdmi, QRect(0, 0, 1920, 1080), hdmi);
        fake.addScreen(dp, QRect(1920, 0, 1920, 1080), dp);
        PhosphorScreens::ScreenManager screenMgr(
            PhosphorScreens::ScreenManagerConfig{.physicalScreenSource = &fake, .useGeometrySensors = false});
        screenMgr.start();
        QVERIFY(screenMgr.setVirtualScreenConfig(hdmi, PlasmaZones::TestHelpers::makeSplitConfig(hdmi)));
        QVERIFY(screenMgr.setVirtualScreenConfig(dp, PlasmaZones::TestHelpers::makeSplitConfig(dp)));
        const QString hdmiLeft = PhosphorIdentity::VirtualScreenId::make(hdmi, 0);
        const QString dpRight = PhosphorIdentity::VirtualScreenId::make(dp, 1);
        QCOMPARE(screenMgr.effectiveScreenAt(QPoint(3300, 400)), dpRight);

        QObject parent;
        auto* wta = new WindowTrackingAdaptor(m_layoutManager, m_zoneDetector, &screenMgr, m_settings, nullptr, nullptr,
                                              &parent);
        auto* snap = new SnapEngine(m_layoutManager, wta->service(), m_zoneDetector, nullptr, nullptr);
        snap->setEngineSettings(m_settings);
        wta->service()->setSnapState(snap->snapState());
        wta->service()->setSnapEngine(snap);
        wta->setEngines(snap, nullptr, nullptr);
        const auto teardown = qScopeGuard([wta, snap] {
            wta->service()->setSnapEngine(nullptr);
            wta->service()->setSnapState(nullptr);
            delete snap;
        });

        const QString windowId = QStringLiteral("brave-browser|screen-frame");
        snap->commitSnap(windowId, m_zoneIds[0], hdmiLeft);
        QCOMPARE(wta->service()->screenForWindow(windowId), hdmiLeft);
        wta->setFrameGeometry(windowId, 3100, 200, 400, 300); // in DP-1's right half

        QSignalSpy stateSpy(wta, &WindowTrackingAdaptor::windowStateChanged);
        wta->windowScreenChanged(windowId, dp);

        QVERIFY2(wta->service()->zoneForWindow(windowId).isEmpty(), "a window that changed monitors must unsnap");
        QCOMPARE(stateSpy.count(), 1);
        const auto entry = stateSpy.first().at(1).value<PhosphorProtocol::WindowStateEntry>();
        QCOMPARE(entry.changeType, QStringLiteral("screen_changed"));
        QCOMPARE(entry.screenId, dpRight);
    }

    // Repeated reports for the assigned screen stay inert. The effect can
    // legitimately emit more than one per apply (the synchronous frame change
    // and an async follow-up from an X11 size constraint), so idempotence
    // here is what keeps a second one from undoing the first's no-op.
    void testWindowScreenChanged_repeatedAssignedScreenReportsStayInert()
    {
        const QString windowId = QStringLiteral("brave-browser|screen-repeat");
        const QString target = QStringLiteral("HDMI-1");

        m_snapEngine->commitSnap(windowId, m_zoneIds[0], target);

        for (int i = 0; i < 3; ++i) {
            m_wta->windowScreenChanged(windowId, target);
            QVERIFY2(!m_wta->service()->zoneForWindow(windowId).isEmpty(),
                     "no repeat of an assigned-screen report may drop the snap");
        }
        QCOMPARE(m_wta->service()->screenForWindow(windowId), target);
    }

    // ── Output moves with per-screen stores ──────────────────────────────

    // An output move of a window snapped on TWO desktops releases every
    // membership it held on the old output, not only the desktop in view:
    // the other desktop's zone would otherwise re-apply on the next switch
    // and drag the window back across monitors (seen live on two outputs).
    void testScreenChanged_releasesEveryMembershipOnTheOldOutput()
    {
        installPerScreenResolver();

        const QString w = QStringLiteral("app|two-desktops");
        const QString other = QStringLiteral("DP-2");
        m_snapEngine->setCurrentDesktopForScreen(m_screenId, 1);
        m_wta->service()->assignWindowToZone(w, m_zoneIds[0], m_screenId, 1);
        m_snapEngine->setCurrentDesktopForScreen(m_screenId, 2);
        m_snapEngine->stateForWindowOnScreen(w, m_screenId, 2)->assignWindowToZone(w, m_zoneIds[1], m_screenId, 2);
        m_snapEngine->setCurrentDesktopForScreen(m_screenId, 1);
        m_snapEngine->setCurrentDesktopForScreen(other, 1);
        m_wta->service()->placementStore().record(*m_snapEngine->capturePlacement(w));

        m_wta->windowScreenChanged(w, other);

        auto* onOne = static_cast<PhosphorSnapEngine::SnapState*>(m_snapEngine->stateForScreen(m_screenId));
        QVERIFY(onOne->zonesForWindow(w).isEmpty());
        m_snapEngine->setCurrentDesktopForScreen(m_screenId, 2);
        auto* onTwo = static_cast<PhosphorSnapEngine::SnapState*>(m_snapEngine->stateForScreen(m_screenId));
        QVERIFY2(onTwo->zonesForWindow(w).isEmpty(), "the other desktop's zone on the old output must go");
        QVERIFY(!m_snapEngine->holdsWindowInState(w, onTwo));
        // The window ends free and untracked by snap: nothing on the new
        // output either, until the user snaps it there.
        auto* onOther = static_cast<PhosphorSnapEngine::SnapState*>(m_snapEngine->stateForScreen(other));
        QVERIFY(!onOther || !m_snapEngine->holdsWindowInState(w, onOther));
        QVERIFY(!m_snapEngine->isWindowTracked(w));
        // The record's snap slot went with it (another monitor), so neither a
        // restart nor a resnap brings the old output's zones back.
        const auto rec = m_wta->service()->placementStore().peekExact(w);
        QVERIFY(rec.has_value());
        QCOMPARE(rec->slotFor(PhosphorEngine::WindowPlacement::snapEngineId()).state,
                 QString(PhosphorEngine::WindowPlacement::stateReleased()));
        m_wta->service()->setSnapState(m_snapEngine->snapState());
    }

    // A zone the window holds on a BACKGROUND desktop of the output it left
    // counts as snapped, though the desktop in view holds it with no zone.
    // Reading the view alone took it for a free window and kept every
    // membership on the old output: switching to that desktop re-applied the
    // zone and dragged the window back across monitors.
    void testScreenChanged_releasesAZoneHeldOnABackgroundDesktop()
    {
        installPerScreenResolver();
        const auto restore = qScopeGuard([this] {
            m_wta->service()->setSnapState(m_snapEngine->snapState());
        });
        const QString w = QStringLiteral("app|background-zone");
        const QString other = QStringLiteral("DP-2");
        m_snapEngine->setCurrentDesktopForScreen(m_screenId, 1);
        m_wta->service()->assignWindowToZone(w, m_zoneIds[0], m_screenId, 1);
        m_snapEngine->setCurrentDesktopForScreen(m_screenId, 2);
        QVERIFY(m_snapEngine->stateForWindowOnScreen(w, m_screenId, 2)); // adopted there, no zone
        m_snapEngine->setCurrentDesktopForScreen(other, 1);
        m_wta->service()->placementStore().record(*m_snapEngine->capturePlacement(w));
        QVERIFY(m_wta->service()->zoneForWindow(w).isEmpty()); // nothing in view

        m_wta->windowScreenChanged(w, other);

        QVERIFY2(!m_snapEngine->isWindowTracked(w), "no membership may stay on the output the window left");
        m_snapEngine->setCurrentDesktopForScreen(m_screenId, 1);
        auto* onOne = static_cast<PhosphorSnapEngine::SnapState*>(m_snapEngine->stateForScreen(m_screenId));
        QVERIFY(onOne->zonesForWindow(w).isEmpty());
        const auto rec = m_wta->service()->placementStore().peekExact(w);
        QVERIFY(rec.has_value());
        QCOMPARE(rec->slotFor(PhosphorEngine::WindowPlacement::snapEngineId()).state,
                 QString(PhosphorEngine::WindowPlacement::stateReleased()));
    }

    // A floating window KWin moves to another output ends floating there with
    // no home on the output it left, and the move is announced: the float
    // relay dedups on the float bit alone, so without the windowStateChanged
    // entry no subscriber heard of it.
    void testScreenChanged_floatingWindowForgetsItsHomeAndIsAnnounced()
    {
        installPerScreenResolver();
        const auto restore = qScopeGuard([this] {
            m_wta->service()->setSnapState(m_snapEngine->snapState());
        });
        const QString w = QStringLiteral("app|floated-move");
        const QString other = QStringLiteral("DP-2");
        m_snapEngine->setCurrentDesktopForScreen(m_screenId, 1);
        m_snapEngine->setCurrentDesktopForScreen(other, 1);
        m_wta->service()->assignWindowToZone(w, m_zoneIds[0], m_screenId, 1);
        m_snapEngine->setWindowFloat(w, true, m_screenId);
        QVERIFY(m_snapEngine->isFloating(w));
        QCOMPARE(m_wta->service()->preFloatScreen(w), m_screenId);

        QSignalSpy stateSpy(m_wta, &WindowTrackingAdaptor::windowStateChanged);
        m_wta->windowScreenChanged(w, other);

        QVERIFY(m_snapEngine->isFloating(w));
        QCOMPARE(m_snapEngine->screenForTrackedWindow(w), other);
        QVERIFY2(m_wta->service()->preFloatZones(w).isEmpty(), "the home on the output left must be forgotten");
        QCOMPARE(stateSpy.count(), 1);
        const auto entry = stateSpy.first().at(1).value<PhosphorProtocol::WindowStateEntry>();
        QCOMPARE(entry.changeType, QStringLiteral("screen_changed"));
        QCOMPARE(entry.screenId, other);
        QVERIFY(entry.isFloating);
    }

    // A floating window moved onto a screen autotile runs is handed to
    // autotile as a float there, not left in snap (F850).
    void testScreenChanged_floatingWindowOntoAutotileIsHandedOverFloating()
    {
        installPerScreenResolver();
        StubPlacementEngine autotile;
        autotile.activeScreens.insert(QStringLiteral("DP-2"));
        m_wta->setEngines(m_snapEngine, &autotile, nullptr);
        const auto restore = qScopeGuard([this] {
            m_wta->setEngines(m_snapEngine, nullptr, nullptr);
            m_wta->service()->setSnapState(m_snapEngine->snapState());
        });
        const QString w = QStringLiteral("app|float-to-autotile");
        m_snapEngine->setCurrentDesktopForScreen(m_screenId, 1);
        m_wta->service()->assignWindowToZone(w, m_zoneIds[0], m_screenId, 1);
        m_snapEngine->setWindowFloat(w, true, m_screenId);

        m_wta->windowScreenChanged(w, QStringLiteral("DP-2"));

        QCOMPARE(autotile.received.size(), 1);
        QCOMPARE(autotile.received.first().toScreenId, QStringLiteral("DP-2"));
        QVERIFY(autotile.received.first().wasFloating);
        QVERIFY(!m_snapEngine->isWindowTracked(w));
    }

    // A free, non-floating window has nothing to move, and a floating one
    // reported on the screen it is already on stays put: no announcement
    // either way (F850).
    void testScreenChanged_freeWindowAndSameScreenFloatAreInert()
    {
        installPerScreenResolver();
        const auto restore = qScopeGuard([this] {
            m_wta->service()->setSnapState(m_snapEngine->snapState());
        });
        const QString floating = QStringLiteral("app|float-same-screen");
        m_snapEngine->setCurrentDesktopForScreen(m_screenId, 1);
        m_wta->service()->assignWindowToZone(floating, m_zoneIds[0], m_screenId, 1);
        m_snapEngine->setWindowFloat(floating, true, m_screenId);

        QSignalSpy stateSpy(m_wta, &WindowTrackingAdaptor::windowStateChanged);
        m_wta->windowScreenChanged(QStringLiteral("app|free"), QStringLiteral("DP-2"));
        m_wta->windowScreenChanged(floating, m_screenId);

        QCOMPARE(stateSpy.count(), 0);
        QVERIFY(m_snapEngine->isFloating(floating));
        QCOMPARE(m_snapEngine->screenForTrackedWindow(floating), m_screenId);
    }

    // The unsnap clears the last-used zone of the screen the window left, and
    // only that one: the zone was unassigned there. Another screen running the
    // same layout remembers its own last-used zone, which this move did not
    // touch.
    void testScreenChanged_clearsTheLastUsedOfTheScreenLeftOnly()
    {
        installPerScreenResolver();
        const auto restore = qScopeGuard([this] {
            m_wta->service()->setSnapState(m_snapEngine->snapState());
        });
        const QString w = QStringLiteral("app|lastused-move");
        const QString other = QStringLiteral("DP-2");
        m_snapEngine->setCurrentDesktopForScreen(m_screenId, 1);
        m_snapEngine->setCurrentDesktopForScreen(other, 1);
        m_wta->service()->assignWindowToZone(w, m_zoneIds[0], m_screenId, 1);
        auto* onA = static_cast<PhosphorSnapEngine::SnapState*>(m_snapEngine->stateForScreen(m_screenId));
        onA->restoreLastUsedZone(m_zoneIds[0], m_screenId, QString(), 1);
        PhosphorSnapEngine::SnapState* onB = m_snapEngine->stateForWindowOnScreen(QStringLiteral("app|on-b"), other);
        QVERIFY(onB && onB != onA);
        onB->restoreLastUsedZone(m_zoneIds[0], other, QString(), 1);

        m_wta->windowScreenChanged(w, other);

        QVERIFY(m_wta->service()->zoneForWindow(w).isEmpty());
        QVERIFY(onA->lastUsedZoneId().isEmpty());
        QCOMPARE(onB->lastUsedZoneId(), m_zoneIds[0]);
    }

    // A crossing between virtual screens of ONE monitor unsnaps like any
    // move, but keeps the record's snap slot for now: only a change of
    // monitor releases it.
    void testScreenChanged_virtualScreenCrossingKeepsTheRecordSlot()
    {
        installPerScreenResolver();
        const auto restore = qScopeGuard([this] {
            m_wta->service()->setSnapState(m_snapEngine->snapState());
        });
        const QString w = QStringLiteral("app|vs-cross");
        const QString vs0 = QStringLiteral("DP-1/vs:0");
        const QString vs1 = QStringLiteral("DP-1/vs:1");
        m_snapEngine->setCurrentDesktopForScreen(vs0, 1);
        m_snapEngine->setCurrentDesktopForScreen(vs1, 1);
        m_wta->service()->assignWindowToZone(w, m_zoneIds[0], vs0, 1);
        m_wta->service()->placementStore().record(*m_snapEngine->capturePlacement(w));

        m_wta->windowScreenChanged(w, vs1);

        QVERIFY(m_wta->service()->zoneForWindow(w).isEmpty());
        const auto rec = m_wta->service()->placementStore().peekExact(w);
        QVERIFY(rec.has_value());
        QCOMPARE(rec->slotFor(PhosphorEngine::WindowPlacement::snapEngineId()).state,
                 QString(PhosphorEngine::WindowPlacement::stateSnapped()));
    }

    // ── Activation and the focused screen ────────────────────────────────

    // A snap float activated on another monitor is re-homed there: the
    // backstop for a move that sent no screen report.
    void testActivation_rehomesAFloatOntoTheActivationMonitor()
    {
        installPerScreenResolver();
        const auto restore = qScopeGuard([this] {
            m_wta->service()->setSnapState(m_snapEngine->snapState());
        });
        const QString w = QStringLiteral("app|float-rehome");
        const QString other = QStringLiteral("DP-2");
        m_snapEngine->setCurrentDesktopForScreen(m_screenId, 1);
        m_snapEngine->setCurrentDesktopForScreen(other, 1);
        m_wta->service()->assignWindowToZone(w, m_zoneIds[0], m_screenId, 1);
        m_snapEngine->setWindowFloat(w, true, m_screenId);

        m_wta->windowActivated(w, other);

        QVERIFY(m_snapEngine->isFloating(w));
        QCOMPARE(m_snapEngine->screenForTrackedWindow(w), other);
    }

    // An activation reported with the physical id of the monitor whose
    // virtual screen the float lives on names the same place: no re-home
    // (F854). Offscreen screens carry no EDID, so the connector-name / EDID
    // form of one monitor cannot be built here; this is the id-form pair the
    // effect does send, before its virtual screen configs load.
    void testActivation_physicalIdOfTheFloatsMonitorDoesNotRehome()
    {
        installPerScreenResolver();
        const auto restore = qScopeGuard([this] {
            m_wta->service()->setSnapState(m_snapEngine->snapState());
        });
        const QString w = QStringLiteral("app|float-vs-form");
        const QString vs0 = QStringLiteral("DP-1/vs:0");
        m_snapEngine->setCurrentDesktopForScreen(vs0, 1);
        m_snapEngine->setCurrentDesktopForScreen(m_screenId, 1);
        m_wta->service()->assignWindowToZone(w, m_zoneIds[0], vs0, 1);
        m_snapEngine->setWindowFloat(w, true, vs0);
        QCOMPARE(m_snapEngine->screenForTrackedWindow(w), vs0);

        m_wta->windowActivated(w, m_screenId);

        QVERIFY(m_snapEngine->isFloating(w));
        QCOMPARE(m_snapEngine->screenForTrackedWindow(w), vs0);
    }

    // A zone held on a background desktop of the window's monitor keeps the
    // backstop off: the migrate would leave that zone behind (F259).
    void testActivation_backgroundDesktopZoneBlocksTheRehome()
    {
        installPerScreenResolver();
        const auto restore = qScopeGuard([this] {
            m_wta->service()->setSnapState(m_snapEngine->snapState());
        });
        const QString w = QStringLiteral("app|activate-background-zone");
        const QString other = QStringLiteral("DP-2");
        m_snapEngine->setCurrentDesktopForScreen(m_screenId, 1);
        m_wta->service()->assignWindowToZone(w, m_zoneIds[0], m_screenId, 1);
        m_snapEngine->setCurrentDesktopForScreen(m_screenId, 2);
        PhosphorSnapEngine::SnapState* onTwo = m_snapEngine->stateForWindowOnScreen(w, m_screenId, 2);
        QVERIFY(onTwo); // adopted there, no zone
        onTwo->recordResidence(w, m_screenId, 2); // living there free
        m_snapEngine->setCurrentDesktopForScreen(other, 1);
        QVERIFY(m_snapEngine->zoneForWindow(w).isEmpty()); // nothing in view
        QCOMPARE(m_snapEngine->screenForTrackedWindow(w), m_screenId);

        m_wta->windowActivated(w, other);

        m_snapEngine->setCurrentDesktopForScreen(m_screenId, 1);
        auto* onOne = static_cast<PhosphorSnapEngine::SnapState*>(m_snapEngine->stateForScreen(m_screenId));
        QCOMPARE(onOne->zonesForWindow(w), QStringList{m_zoneIds[0]});
    }

    // An activation on a screen snap does not run leaves snap's float where
    // it is: a tiling engine owns the window there (F679).
    void testActivation_onATilingScreenDoesNotRehome()
    {
        installPerScreenResolver();
        const QString other = QStringLiteral("DP-2");
        m_snapEngine->setLiveModeResolver([other](const QString& screenId) {
            return screenId == other ? PhosphorZones::AssignmentEntry::Mode::Autotile
                                     : PhosphorZones::AssignmentEntry::Mode::Snapping;
        });
        const auto restore = qScopeGuard([this] {
            m_snapEngine->setLiveModeResolver({});
            m_wta->service()->setSnapState(m_snapEngine->snapState());
        });
        const QString w = QStringLiteral("app|activate-tiling");
        m_snapEngine->setCurrentDesktopForScreen(m_screenId, 1);
        m_snapEngine->setCurrentDesktopForScreen(other, 1);
        m_wta->service()->assignWindowToZone(w, m_zoneIds[0], m_screenId, 1);
        m_snapEngine->setWindowFloat(w, true, m_screenId);

        m_wta->windowActivated(w, other);

        QCOMPARE(m_snapEngine->screenForTrackedWindow(w), m_screenId);
    }

    // Focusing a snapped window records its zone as the last used on the
    // screen it is snapped on, and on no other screen it is activated on
    // (F222).
    void testActivation_lastUsedZoneOnlyWhereTheWindowIsSnapped()
    {
        installPerScreenResolver();
        const auto restore = qScopeGuard([this] {
            m_wta->service()->setSnapState(m_snapEngine->snapState());
        });
        m_settings->setMoveNewWindowsToLastZone(true);
        const QString w = QStringLiteral("app|activate-lastused");
        const QString other = QStringLiteral("DP-2");
        m_snapEngine->setCurrentDesktopForScreen(m_screenId, 1);
        m_snapEngine->setCurrentDesktopForScreen(other, 1);
        m_wta->service()->assignWindowToZone(w, m_zoneIds[0], m_screenId, 1);
        PhosphorSnapEngine::SnapState* onB = m_snapEngine->stateForWindowOnScreen(QStringLiteral("app|on-b"), other);
        auto* onA = static_cast<PhosphorSnapEngine::SnapState*>(m_snapEngine->stateForScreen(m_screenId));
        QVERIFY(onB && onB != onA);

        m_wta->windowActivated(w, other);
        QVERIFY2(onB->lastUsedZoneId().isEmpty(), "a zone snapped on another monitor is not the last used here");

        m_wta->windowActivated(w, m_screenId);
        QCOMPARE(onA->lastUsedZoneId(), m_zoneIds[0]);
    }

    // A window the restore facade snaps into the last-used zone stays marked
    // auto-snapped, so focusing it does not count as the user using that
    // zone. The mark used to be parked on the global holder before the
    // commit, whose first placement evicted it (F385).
    void testFacadeRestoreKeepsTheAutoSnappedMark()
    {
        installPerScreenResolver();
        const auto restore = qScopeGuard([this] {
            m_wta->service()->setSnapState(m_snapEngine->snapState());
        });
        m_settings->setMoveNewWindowsToLastZone(true);
        m_layoutManager->assignLayout(m_screenId, m_layoutManager->currentVirtualDesktop(), QString(), m_testLayout);
        m_snapEngine->setCurrentDesktopForScreen(m_screenId, m_layoutManager->currentVirtualDesktop());
        m_snapEngine->commitSnap(QStringLiteral("app|placed-by-user"), m_zoneIds[1], m_screenId);
        m_wta->service()->setUserSnappedClasses({QStringLiteral("app")});
        const QString w = QStringLiteral("app|restored-last-zone");

        int x = 0, y = 0, width = 0, height = 0;
        bool shouldSnap = false;
        m_snapAdaptor->snapToLastZone(w, m_screenId, false, x, y, width, height, shouldSnap);

        QVERIFY(shouldSnap);
        QCOMPARE(m_snapEngine->zoneForWindow(w), m_zoneIds[1]);
        QVERIFY2(m_wta->service()->isAutoSnapped(w), "a facade restore must stay marked auto-snapped");
    }

    // The focused window's screen report repoints snap's focused screen
    // too, the one its float verb falls back on (F28).
    void testActiveWindowScreenChanged_repointsSnapsFocusedScreen()
    {
        const QString w = QStringLiteral("app|focus-follow");
        m_wta->windowActivated(w, m_screenId);
        QCOMPARE(m_snapEngine->lastActiveScreenId(), m_screenId);

        m_wta->activeWindowScreenChanged(w, QStringLiteral("DP-2"));

        QCOMPARE(m_snapEngine->lastActiveScreenId(), QStringLiteral("DP-2"));
    }

    // Snap's memory of a window on a screen snap no longer runs does not
    // answer where the focused window is (F29).
    void testLastActiveScreen_ignoresSnapMemoryOnAScreenSnapDoesNotRun()
    {
        const QString w = QStringLiteral("app|memory-elsewhere");
        m_snapEngine->setCurrentDesktopForScreen(m_screenId, 1);
        m_wta->service()->assignWindowToZone(w, m_zoneIds[0], m_screenId, 1);
        m_wta->windowActivated(w, m_screenId);
        m_wta->activeWindowScreenChanged(w, QStringLiteral("DP-2"));
        QCOMPARE(m_wta->lastActiveScreenName(), m_screenId); // snap still runs it

        m_snapEngine->setLiveModeResolver([this](const QString& screenId) {
            return screenId == m_screenId ? PhosphorZones::AssignmentEntry::Mode::Autotile
                                          : PhosphorZones::AssignmentEntry::Mode::Snapping;
        });
        const auto restore = qScopeGuard([this] {
            m_snapEngine->setLiveModeResolver({});
        });

        QCOMPARE(m_wta->lastActiveScreenName(), QStringLiteral("DP-2"));
    }

    // A tiling engine's tracking in another desktop's context does not
    // answer where the focused window is, only its hold in view (F216).
    void testLastActiveScreen_readsTheTilingHoldInView()
    {
        StubPlacementEngine autotile;
        m_wta->setEngines(m_snapEngine, &autotile, nullptr);
        const auto restore = qScopeGuard([this] {
            m_wta->setEngines(m_snapEngine, nullptr, nullptr);
        });
        const QString w = QStringLiteral("app|tile-elsewhere");
        autotile.trackedElsewhere.insert(w);
        autotile.elsewhereScreen.insert(w, QStringLiteral("DP-3"));
        m_wta->windowActivated(w, QStringLiteral("DP-2"));

        QCOMPARE(m_wta->lastActiveScreenName(), QStringLiteral("DP-2"));

        autotile.heldScreen.insert(w, QStringLiteral("DP-3"));
        QCOMPARE(m_wta->lastActiveScreenName(), QStringLiteral("DP-3"));
    }
};

QTEST_MAIN(TestWtaScreenChanged)
#include "test_wta_screen_changed.moc"
