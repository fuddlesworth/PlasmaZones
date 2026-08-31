// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_wta_screen_changed.cpp
 * @brief WindowTrackingAdaptor::windowScreenChanged coverage for SNAPPED
 *        windows: the stored-screen comparison that decides between keeping
 *        the snap and unsnapping, the empty-screen bail, and the
 *        "screen_changed" windowStateChanged emission.
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
};

QTEST_MAIN(TestWtaScreenChanged)
#include "test_wta_screen_changed.moc"
