// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorWorkspaces/VirtualDesktopManager.h>

#include <QSignalSpy>
#include <QTest>

using PhosphorWorkspaces::VirtualDesktopManager;

/// The per-output desktop map. Driven entirely by plain calls (the effect's
/// reports arrive as updateScreenDesktop), so no KWin and no D-Bus: the
/// manager is never init()ed here and its KWin half stays dormant.
class TestVirtualDesktopManager : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void renameScreen_movesTheRowAndReports();
    void renameScreen_existingTargetWinsAndStaysQuiet();
    void renameScreen_ignoresUnknownAndDegenerateIds();
};

void TestVirtualDesktopManager::renameScreen_movesTheRowAndReports()
{
    VirtualDesktopManager vdm;
    const QString oldId = QStringLiteral("DP-2");
    const QString newId = QStringLiteral("DP-2/DELL-U2415");

    vdm.updateScreenDesktop(oldId, 3);
    QVERIFY(vdm.hasScreenDesktopReport(oldId));

    QSignalSpy spy(&vdm, &VirtualDesktopManager::screenDesktopChanged);
    vdm.renameScreen(oldId, newId);

    // Without the re-key the row stays under the dead id: the live id has no
    // report and currentDesktopForScreen falls back to the GLOBAL desktop (1),
    // which is the whole defect.
    QVERIFY(!vdm.hasScreenDesktopReport(oldId));
    QVERIFY(vdm.hasScreenDesktopReport(newId));
    QCOMPARE(vdm.currentDesktopForScreen(newId), 3);
    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.first().at(0).toString(), newId);
    QCOMPARE(spy.first().at(1).toInt(), 3);
}

void TestVirtualDesktopManager::renameScreen_existingTargetWinsAndStaysQuiet()
{
    VirtualDesktopManager vdm;
    const QString oldId = QStringLiteral("DP-2");
    const QString newId = QStringLiteral("DP-2/DELL-U2415");

    vdm.updateScreenDesktop(oldId, 3);
    vdm.updateScreenDesktop(newId, 5); // a report already made under the live id

    QSignalSpy spy(&vdm, &VirtualDesktopManager::screenDesktopChanged);
    vdm.renameScreen(oldId, newId);

    QVERIFY(!vdm.hasScreenDesktopReport(oldId)); // the stale row is dropped
    QCOMPARE(vdm.currentDesktopForScreen(newId), 5); // the fresher report stands
    QCOMPARE(spy.count(), 0); // no value changed, so nothing is emitted
}

void TestVirtualDesktopManager::renameScreen_ignoresUnknownAndDegenerateIds()
{
    VirtualDesktopManager vdm;
    vdm.updateScreenDesktop(QStringLiteral("DP-2"), 4);

    QSignalSpy spy(&vdm, &VirtualDesktopManager::screenDesktopChanged);
    vdm.renameScreen(QStringLiteral("HDMI-A-1"), QStringLiteral("HDMI-A-1/X")); // nothing recorded
    vdm.renameScreen(QStringLiteral("DP-2"), QStringLiteral("DP-2")); // same id
    vdm.renameScreen(QString(), QStringLiteral("DP-2"));
    vdm.renameScreen(QStringLiteral("DP-2"), QString());

    QCOMPARE(spy.count(), 0);
    QVERIFY(!vdm.hasScreenDesktopReport(QStringLiteral("HDMI-A-1/X")));
    QCOMPARE(vdm.currentDesktopForScreen(QStringLiteral("DP-2")), 4);
}

QTEST_MAIN(TestVirtualDesktopManager)
#include "test_virtualdesktopmanager.moc"
