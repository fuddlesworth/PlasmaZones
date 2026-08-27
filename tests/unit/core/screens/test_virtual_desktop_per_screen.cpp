// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_virtual_desktop_per_screen.cpp
 * @brief Phase 1 of the per-screen virtual desktops work (discussion #648):
 *        the VirtualDesktopManager per-screen current-desktop model.
 *
 * Plasma 6.7's "switch desktops independently for each screen" lets each output
 * sit on its own virtual desktop. The daemon's VirtualDesktopManager gains a
 * screenId -> desktop map (fed by the KWin effect's per-output report) plus
 * currentDesktopForScreen()/perScreenModeActive(). KWin's own D-Bus interface is
 * global-only, so these tests construct the manager WITHOUT a live KWin D-Bus
 * connection (m_useKWinDBus stays false, currentDesktop() == 1) and exercise the
 * per-screen model directly through its public API.
 *
 * These tests exercise the VirtualDesktopManager model in isolation; the daemon
 * wires screenDesktopChanged into its per-screen context and OSD handling
 * downstream (see Daemon::screenDesktopChanged).
 */

#include <QSignalSpy>
#include <QTest>

#include <PhosphorWorkspaces/VirtualDesktopManager.h>

using PhosphorWorkspaces::VirtualDesktopManager;

class TestVirtualDesktopPerScreen : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    // A screen with no per-output desktop on record resolves to the global value.
    void unknownScreen_fallsBackToGlobal()
    {
        VirtualDesktopManager vdm;
        QCOMPARE(vdm.currentDesktopForScreen(QStringLiteral("DP-1")), vdm.currentDesktop());
        QVERIFY(!vdm.perScreenModeActive());
    }

    // Recorded per-screen desktops resolve independently; unknown screens still
    // fall back to the global desktop.
    void updateScreenDesktop_recordsAndResolves()
    {
        VirtualDesktopManager vdm;
        vdm.updateScreenDesktop(QStringLiteral("DP-1"), 3);
        vdm.updateScreenDesktop(QStringLiteral("DP-2"), 5);

        QCOMPARE(vdm.currentDesktopForScreen(QStringLiteral("DP-1")), 3);
        QCOMPARE(vdm.currentDesktopForScreen(QStringLiteral("DP-2")), 5);
        QCOMPARE(vdm.currentDesktopForScreen(QStringLiteral("DP-9")), vdm.currentDesktop());
    }

    // screenDesktopChanged fires only when a screen's desktop actually changes.
    void updateScreenDesktop_emitsOnlyOnChange()
    {
        VirtualDesktopManager vdm;
        QSignalSpy spy(&vdm, &VirtualDesktopManager::screenDesktopChanged);

        vdm.updateScreenDesktop(QStringLiteral("DP-1"), 3);
        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.last().at(0).toString(), QStringLiteral("DP-1"));
        QCOMPARE(spy.last().at(1).toInt(), 3);

        vdm.updateScreenDesktop(QStringLiteral("DP-1"), 3); // same value — no emit
        QCOMPARE(spy.count(), 1);

        vdm.updateScreenDesktop(QStringLiteral("DP-1"), 4); // changed — emit
        QCOMPARE(spy.count(), 2);
        QCOMPARE(spy.last().at(1).toInt(), 4);
    }

    // Empty screen id or a non-positive desktop are rejected silently.
    void updateScreenDesktop_rejectsInvalid()
    {
        VirtualDesktopManager vdm;
        QSignalSpy spy(&vdm, &VirtualDesktopManager::screenDesktopChanged);

        vdm.updateScreenDesktop(QString(), 3);
        vdm.updateScreenDesktop(QStringLiteral("DP-1"), 0);
        vdm.updateScreenDesktop(QStringLiteral("DP-1"), -2);

        QCOMPARE(spy.count(), 0);
        QCOMPARE(vdm.currentDesktopForScreen(QStringLiteral("DP-1")), vdm.currentDesktop());
    }

    // Per-screen mode is active iff two screens are on different desktops, and
    // deactivates when they reconverge.
    void perScreenModeActive_tracksDivergence()
    {
        VirtualDesktopManager vdm;
        QVERIFY(!vdm.perScreenModeActive()); // empty

        vdm.updateScreenDesktop(QStringLiteral("DP-1"), 2);
        QVERIFY(!vdm.perScreenModeActive()); // single screen

        vdm.updateScreenDesktop(QStringLiteral("DP-2"), 2);
        QVERIFY(!vdm.perScreenModeActive()); // two screens, same desktop

        vdm.updateScreenDesktop(QStringLiteral("DP-2"), 5);
        QVERIFY(vdm.perScreenModeActive()); // diverged

        vdm.updateScreenDesktop(QStringLiteral("DP-2"), 2);
        QVERIFY(!vdm.perScreenModeActive()); // reconverged
    }

    // When the desktop count shrinks, per-screen entries above the new count are
    // clamped back to the count (KWin renumbers on removal; the effect re-reports
    // the true value shortly after). Only the affected screen re-emits.
    void desktopCountShrink_clampsPerScreenEntries()
    {
        VirtualDesktopManager vdm;
        // Drive the private count-changed slot directly (no KWin D-Bus in tests).
        QVERIFY(QMetaObject::invokeMethod(&vdm, "onNumberOfDesktopsChanged", Q_ARG(int, 7)));

        vdm.updateScreenDesktop(QStringLiteral("DP-1"), 3);
        vdm.updateScreenDesktop(QStringLiteral("DP-2"), 7);

        QSignalSpy spy(&vdm, &VirtualDesktopManager::screenDesktopChanged);
        // Desktops 5..7 removed -> count 4. The entry on 7 must clamp to 4; the
        // entry on 3 is unaffected.
        QVERIFY(QMetaObject::invokeMethod(&vdm, "onNumberOfDesktopsChanged", Q_ARG(int, 4)));

        QCOMPARE(vdm.currentDesktopForScreen(QStringLiteral("DP-1")), 3);
        QCOMPARE(vdm.currentDesktopForScreen(QStringLiteral("DP-2")), 4);

        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.last().at(0).toString(), QStringLiteral("DP-2"));
        QCOMPARE(spy.last().at(1).toInt(), 4);
    }
    // Dynamic-workspaces id translation: without a KWin id list the accessors
    // answer their documented null values (empty list, empty id, index 0) and
    // never crash — the WorkspaceController leans on exactly these nulls to
    // defer adoption until the first settled reply.
    void desktopIdAccessors_nullWithoutKWin()
    {
        VirtualDesktopManager vdm;
        QVERIFY(vdm.desktopIds().isEmpty());
        QVERIFY(vdm.desktopIdAt(1).isEmpty());
        QVERIFY(vdm.desktopIdAt(0).isEmpty());
        QVERIFY(vdm.desktopIdAt(-3).isEmpty());
        QCOMPARE(vdm.desktopIndexOf(QStringLiteral("{missing}")), 0);
    }

    // The create/remove/rename wrappers are safe no-ops without a KWin D-Bus
    // interface (headless test environment) — no crash, no signal.
    void kwinWrappers_noOpWithoutKWin()
    {
        VirtualDesktopManager vdm;
        QSignalSpy listSpy(&vdm, &VirtualDesktopManager::desktopListChanged);
        vdm.createDesktop(0, QStringLiteral("x"));
        vdm.removeDesktop(QStringLiteral("{id}"));
        vdm.setDesktopName(QStringLiteral("{id}"), QStringLiteral("y"));
        QCOMPARE(listSpy.count(), 0);
    }
};

QTEST_GUILESS_MAIN(TestVirtualDesktopPerScreen)
#include "test_virtual_desktop_per_screen.moc"
