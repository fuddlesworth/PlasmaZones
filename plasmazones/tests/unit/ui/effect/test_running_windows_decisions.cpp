// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_running_windows_decisions.cpp
 * @brief The rows the KWin effect offers the rule editor's window picker
 *        (plasmazoneseffect/runningwindowsdecisions.h).
 */

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTest>

#include "plasmazoneseffect/runningwindowsdecisions.h"

using namespace PlasmaZones::RunningWindowsDecisions;

namespace {
/// The effect's use of the filter for a row the budget takes.
bool keep(RowFilter& filter, const Row& row)
{
    if (!filter.admits(row)) {
        return false;
    }
    filter.record(row);
    return true;
}
} // namespace

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
    // classless ones once per caption, or once per desktop file when they
    // have no caption, and a classless window with neither is left out. The
    // keys are kept apart: a caption equal to a class token does not shadow
    // it. FAILS if classless windows are dropped or all collapse onto one row.
    void rowFilter_keysClassedAndClasslessApart()
    {
        RowFilter filter;
        QVERIFY(keep(filter, makeRow(QStringLiteral("firefox firefox"), QStringLiteral("Top"), QString())));
        QVERIFY(!keep(filter, makeRow(QStringLiteral("firefox firefox"), QStringLiteral("Below"), QString())));
        QVERIFY(keep(filter, makeRow(QStringLiteral(" "), QStringLiteral("Game A"), QString())));
        QVERIFY(keep(filter, makeRow(QStringLiteral(" "), QStringLiteral("Game B"), QString())));
        QVERIFY(!keep(filter, makeRow(QStringLiteral(" "), QStringLiteral("Game A"), QString())));
        QVERIFY(!keep(filter, makeRow(QStringLiteral(" "), QString(), QString())));
        QVERIFY(keep(filter, makeRow(QStringLiteral(" "), QStringLiteral("firefox"), QString())));
        QVERIFY(keep(filter, makeRow(QStringLiteral(" "), QString(), QStringLiteral("org.example.Game"))));
        QVERIFY(!keep(filter, makeRow(QStringLiteral(" "), QString(), QStringLiteral("org.example.Game"))));
    }

    // A row that is checked but not recorded (the budget refused it) leaves
    // its key free for the next row of its group. FAILS if admits() records.
    void rowFilter_admitsDoesNotRecord()
    {
        RowFilter filter;
        const Row top = makeRow(QStringLiteral("steam steam"), QStringLiteral("A very long title"), QString());
        QVERIFY(filter.admits(top));
        const Row below = makeRow(QStringLiteral("steam steam"), QStringLiteral("Steam"), QString());
        QVERIFY(filter.admits(below));
        filter.record(below);
        QVERIFY(!filter.admits(top));
    }

    // The keys the settings app reads, and a budget fed each row's compact
    // size totals exactly the compact array the daemon measures. FAILS if a
    // key is renamed or the budget's bracket and comma count drifts.
    void toJson_keysAndBudgetMatchTheWire()
    {
        const Row konsole = makeRow(QStringLiteral("konsole org.kde.konsole"), QStringLiteral("Shell"),
                                    QStringLiteral("org.kde.konsole"));
        const QJsonObject obj = toJson(konsole);
        QCOMPARE(obj.value(QLatin1String("windowClass")).toString(), konsole.windowClass);
        QCOMPARE(obj.value(QLatin1String("rawWindowClass")).toString(), konsole.rawWindowClass);
        QCOMPARE(obj.value(QLatin1String("appName")).toString(), konsole.appName);
        QCOMPARE(obj.value(QLatin1String("caption")).toString(), konsole.caption);
        QCOMPARE(obj.value(QLatin1String("desktopFile")).toString(), konsole.desktopFile);
        QCOMPARE(obj.size(), 5);

        const QList<Row> rows{konsole, makeRow(QStringLiteral(" "), QStringLiteral("Gämescope ✓"), QString()),
                              makeRow(QStringLiteral("steam steam"), QStringLiteral("Steam"), QString())};
        QJsonArray array;
        qsizetype rowBytes = 0;
        for (const Row& row : rows) {
            const QJsonObject rowObj = toJson(row);
            rowBytes += QJsonDocument(rowObj).toJson(QJsonDocument::Compact).size();
            array.append(rowObj);
        }
        const qsizetype arrayBytes = QJsonDocument(array).toJson(QJsonDocument::Compact).size();
        QCOMPARE(arrayBytes, rowBytes + 2 + (rows.size() - 1));
        PayloadBudget exact(arrayBytes);
        for (const Row& row : rows) {
            QVERIFY(exact.take(QJsonDocument(toJson(row)).toJson(QJsonDocument::Compact).size()));
        }
        QVERIFY(!exact.take(0));
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
