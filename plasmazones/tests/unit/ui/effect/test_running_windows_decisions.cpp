// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_running_windows_decisions.cpp
 * @brief The rows the KWin effect offers the rule editor's window picker
 *        (plasmazoneseffect/runningwindowsdecisions.h).
 */

#include <QTest>

#include "plasmazoneseffect/runningwindowsdecisions.h"

using namespace PlasmaZones::RunningWindowsDecisions;

class TestRunningWindowsDecisions : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    // Each value is spelled the way the rule field it fills compares it. The
    // raw class is KWin's "resourceName resourceClass" untouched, because the
    // WindowClass field compares that whole string; the canonical token alone
    // could never satisfy an Equals leaf. FAILS if rawWindowClass is
    // normalized.
    void makeRow_spellsEachValueForItsField()
    {
        const Row row = makeRow(QStringLiteral("konsole org.kde.konsole"), QStringLiteral("Shell — Konsole"),
                                QStringLiteral("org.kde.konsole"));
        QCOMPARE(row.rawWindowClass, QStringLiteral("konsole org.kde.konsole"));
        QCOMPARE(row.windowClass, QStringLiteral("org.kde.konsole"));
        QCOMPARE(row.appName, QStringLiteral("konsole"));
        QCOMPARE(row.caption, QStringLiteral("Shell — Konsole"));
        QCOMPARE(row.desktopFile, QStringLiteral("org.kde.konsole"));
    }

    // A classless window (KWin reports a single space) keeps no class, but
    // a desktop file still gives it an app name, as the AppId field reads it.
    void makeRow_classlessWindow()
    {
        const Row bare = makeRow(QStringLiteral(" "), QStringLiteral("Gamescope"), QString());
        QVERIFY(bare.windowClass.isEmpty());
        QVERIFY(bare.rawWindowClass.isEmpty());
        QVERIFY(bare.appName.isEmpty());

        const Row withDesktopFile =
            makeRow(QStringLiteral(" "), QStringLiteral("Game"), QStringLiteral("org.example.Game"));
        QVERIFY(withDesktopFile.windowClass.isEmpty());
        QCOMPARE(withDesktopFile.appName, QStringLiteral("game"));
    }

    // Classed windows are listed once per class (the first fed, the topmost),
    // classless ones once per caption, and a classless window without a
    // caption is left out. The two keys are kept apart: a caption equal to a
    // class token does not shadow it. FAILS if classless windows are dropped
    // or all collapse onto one row.
    void rowFilter_keysClassedAndClasslessApart()
    {
        RowFilter filter;
        QVERIFY(filter.admit(makeRow(QStringLiteral("firefox firefox"), QStringLiteral("Top"), QString())));
        QVERIFY(!filter.admit(makeRow(QStringLiteral("firefox firefox"), QStringLiteral("Below"), QString())));
        QVERIFY(filter.admit(makeRow(QStringLiteral(" "), QStringLiteral("Game A"), QString())));
        QVERIFY(filter.admit(makeRow(QStringLiteral(" "), QStringLiteral("Game B"), QString())));
        QVERIFY(!filter.admit(makeRow(QStringLiteral(" "), QStringLiteral("Game A"), QString())));
        QVERIFY(!filter.admit(makeRow(QStringLiteral(" "), QString(), QString())));
        QVERIFY(filter.admit(makeRow(QStringLiteral(" "), QStringLiteral("firefox"), QString())));
    }

    // The serialized list never crosses the cap: a row that would is left out
    // and a later smaller row still fits. "[]" and the separating commas count.
    void payloadBudget_leavesOutRowsPastTheCap()
    {
        PayloadBudget budget(10);
        QVERIFY(budget.take(4)); // [xxxx]       = 6
        QVERIFY(!budget.take(4)); // [xxxx,xxxx] = 11 > 10
        QVERIFY(budget.take(3)); // [xxxx,xxx]  = 10
        QVERIFY(!budget.take(0));
    }
};

QTEST_GUILESS_MAIN(TestRunningWindowsDecisions)
#include "test_running_windows_decisions.moc"
