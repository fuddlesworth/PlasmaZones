// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

// The scroll engine's evacuee park: a strip on an output that disconnects is
// parked as a stash the removed-screen prune and the saves leave alone, an
// untouched window KWin brings back re-enters its old column through an
// exact-id claim, and a window touched while away never claims it.

#include <PhosphorEngine/DesktopRemap.h>
#include <PhosphorScrollEngine/ScrollEngine.h>
#include <PhosphorScrollEngine/ScrollState.h>

#include "scrollstriptestutils.h"

#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QtTest>

using namespace PhosphorScrollEngine;
using PhosphorEngine::DesktopRemap;
using ScrollTestUtils::makeProviderEngine;

namespace {
const QString kS1 = QStringLiteral("S1");

ScrollState* stateFor(ScrollEngine* engine, const QString& screenId)
{
    return static_cast<ScrollState*>(engine->stateForScreen(screenId));
}

/// Each column's window ids, in strip order.
QList<QStringList> columnsOf(ScrollEngine* engine, const QString& screenId)
{
    QList<QStringList> out;
    if (ScrollState* state = stateFor(engine, screenId)) {
        for (const Column& col : state->strip().columns()) {
            QStringList ids;
            for (const Tile& tile : col.tiles) {
                ids.append(tile.windowId);
            }
            out.append(ids);
        }
    }
    return out;
}

/// The output coming back: the daemon re-admits it to the scrolling set and
/// reports its desktop, both of which the removed-screen prune dropped.
void returnOutput(ScrollEngine* engine)
{
    engine->setActiveScreens({kS1});
    engine->setCurrentDesktopForScreen(kS1, 1);
    QCoreApplication::processEvents();
}
} // namespace

class TestScrollEngineEvacuee : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase()
    {
        AX_GUARD_SUITE();
    }

    // The strip comes back as it was: each window into its own column, in
    // the strip's order, however KWin interleaves the returns.
    void parkedStripComesBackInOrder()
    {
        QObject owner;
        ScrollEngine* engine = makeProviderEngine(&owner, {kS1});
        engine->setCurrentDesktopForScreen(kS1, 1);
        const QStringList ids{QStringLiteral("app|a"), QStringLiteral("app|b"), QStringLiteral("app|c")};
        for (const QString& id : ids) {
            engine->windowOpened(id, kS1, 0, 0);
        }
        QCoreApplication::processEvents();
        const QList<QStringList> before = columnsOf(engine, kS1);
        QCOMPARE(before.size(), 3);

        QStringList parked = engine->parkOutput(kS1);
        std::sort(parked.begin(), parked.end());
        QCOMPARE(parked, ids);
        engine->pruneStatesForRemovedScreen(kS1);
        QVERIFY(!engine->isWindowTracked(ids.at(0)));
        QVERIFY(engine->hasParked(ids.at(0), kS1));
        returnOutput(engine);

        QVERIFY(engine->readoptParked(ids.at(2), kS1, kS1));
        QVERIFY(engine->readoptParked(ids.at(0), kS1, kS1));
        QVERIFY(engine->readoptParked(ids.at(1), kS1, kS1));
        QCoreApplication::processEvents();
        QCOMPARE(columnsOf(engine, kS1), before);
        QVERIFY(!engine->hasParked(ids.at(0), QString()));
    }

    // A parked strip is session memory: the save leaves it out, since a
    // restart loses the park that would claim it.
    void parkedStripIsLeftOutOfTheSave()
    {
        QObject owner;
        ScrollEngine* engine = makeProviderEngine(&owner, {kS1});
        engine->setCurrentDesktopForScreen(kS1, 1);
        engine->windowOpened(QStringLiteral("app|saved"), kS1, 0, 0);
        QCoreApplication::processEvents();
        engine->parkOutput(kS1);
        engine->pruneStatesForRemovedScreen(kS1);

        const QJsonObject blob = engine->serializeStripState();
        QVERIFY2(!QString::fromUtf8(QJsonDocument(blob).toJson()).contains(QStringLiteral("app|saved")),
                 "a parked strip must not be saved");
    }

    // A window touched while away is dropped from the park, and the stash can
    // never hand it its old column: it opens like any other window.
    void droppedWindowNeverClaimsItsColumn()
    {
        QObject owner;
        ScrollEngine* engine = makeProviderEngine(&owner, {kS1});
        engine->setCurrentDesktopForScreen(kS1, 1);
        const QString kept = QStringLiteral("app|kept");
        const QString touched = QStringLiteral("app|touched");
        engine->windowOpened(kept, kS1, 0, 0);
        engine->windowOpened(touched, kS1, 0, 0);
        QCoreApplication::processEvents();
        engine->parkOutput(kS1);
        engine->pruneStatesForRemovedScreen(kS1);

        returnOutput(engine);
        engine->dropParked(touched, QString(), 0, QString());
        QVERIFY(!engine->hasParked(touched, QString()));
        QVERIFY(!engine->readoptParked(touched, kS1, kS1));
        QVERIFY(engine->hasParked(kept, kS1));
        QVERIFY(engine->readoptParked(kept, kS1, kS1));
        QVERIFY(engine->isWindowTracked(kept));
    }

    // A strip parked on a desktop or activity that is then removed is dropped
    // with it: its context never comes into view again, so nothing is owed
    // and nothing is granted (F1024).
    void parkOnARemovedContextIsDropped_data()
    {
        QTest::addColumn<bool>("activity");
        QTest::newRow("activity") << true;
        QTest::newRow("desktop") << false;
    }

    void parkOnARemovedContextIsDropped()
    {
        QFETCH(bool, activity);
        QObject owner;
        ScrollEngine* engine = makeProviderEngine(&owner, {kS1});
        engine->setCurrentActivity(QStringLiteral("act-a"));
        engine->setCurrentDesktopForScreen(kS1, 1);
        const QString window = QStringLiteral("app|away");
        engine->windowOpened(window, kS1, 0, 0);
        QCoreApplication::processEvents();
        engine->parkOutput(kS1);
        engine->pruneStatesForRemovedScreen(kS1);
        QVERIFY(engine->hasParked(window, kS1));

        if (activity) {
            engine->pruneStatesForActivities({QStringLiteral("act-b")});
        } else {
            engine->pruneStatesForDesktop(1);
        }
        QVERIFY(!engine->hasParked(window, QString()));
        returnOutput(engine);
        QVERIFY(!engine->readoptParked(window, kS1, kS1));
    }

    // A window parked floating holds no column in the parked stash, so its
    // drop marks nothing consumed there: the stash still owes both tiles and
    // rebuilds the strip in its order (F1025).
    void droppedFloatLeavesTheStashWhole()
    {
        QObject owner;
        ScrollEngine* engine = makeProviderEngine(&owner, {kS1});
        engine->setCurrentDesktopForScreen(kS1, 1);
        const QString a = QStringLiteral("app|a");
        const QString b = QStringLiteral("app|b");
        const QString floater = QStringLiteral("app|floater");
        engine->windowOpened(a, kS1, 0, 0);
        engine->windowOpened(b, kS1, 0, 0);
        engine->windowOpened(floater, kS1, 0, 0);
        engine->setWindowFloat(floater, true, kS1);
        QCoreApplication::processEvents();
        const QList<QStringList> before = columnsOf(engine, kS1);
        QCOMPARE(before, (QList<QStringList>{QStringList{a}, QStringList{b}}));
        engine->parkOutput(kS1);
        engine->pruneStatesForRemovedScreen(kS1);
        returnOutput(engine);

        engine->dropParked(floater, QString(), 0, QString());
        QVERIFY(engine->readoptParked(b, kS1, kS1));
        QVERIFY(engine->readoptParked(a, kS1, kS1));
        QCoreApplication::processEvents();
        QCOMPARE(columnsOf(engine, kS1), before);
    }

    // A window parked floating comes back floating where KWin put it: it
    // never takes a column on the way, so no strip apply moves it (F1028).
    void parkedFloatComesBackWithoutAColumn()
    {
        QObject owner;
        ScrollEngine* engine = makeProviderEngine(&owner, {kS1});
        engine->setCurrentDesktopForScreen(kS1, 1);
        const QString tile = QStringLiteral("app|tile");
        const QString floater = QStringLiteral("app|floater");
        engine->windowOpened(tile, kS1, 0, 0);
        engine->windowOpened(floater, kS1, 0, 0);
        engine->setWindowFloat(floater, true, kS1);
        QCoreApplication::processEvents();
        engine->parkOutput(kS1);
        engine->pruneStatesForRemovedScreen(kS1);
        returnOutput(engine);
        QVERIFY(engine->readoptParked(tile, kS1, kS1));
        QCoreApplication::processEvents();

        QSignalSpy tiled(engine, &ScrollEngine::windowsTiled);
        QVERIFY(engine->readoptParked(floater, kS1, kS1));
        QCoreApplication::processEvents();
        QVERIFY(engine->isWindowFloatingInScroll(floater));
        QCOMPARE(columnsOf(engine, kS1), QList<QStringList>{QStringList{tile}});
        for (const auto& emission : std::as_const(tiled)) {
            const QJsonArray batch = QJsonDocument::fromJson(emission.at(0).toString().toUtf8()).array();
            for (const QJsonValue& entry : batch) {
                QVERIFY2(entry.toObject().value(QLatin1String("windowId")).toString() != floater,
                         "the float is never laid out as a tile");
            }
        }
    }

    // The same for a context out of view when KWin returns the windows: they
    // are seated into that desktop's strip at once, the float floating, since
    // nothing announces them again when the desktop is shown (F1095, live).
    void outOfViewParkedFloatArrivesWithoutAColumn()
    {
        QObject owner;
        ScrollEngine* engine = makeProviderEngine(&owner, {kS1});
        engine->setCurrentDesktopForScreen(kS1, 2);
        const QString tile = QStringLiteral("app|tile");
        const QString floater = QStringLiteral("app|floater");
        engine->windowOpened(tile, kS1, 0, 0);
        engine->windowOpened(floater, kS1, 0, 0);
        engine->setWindowFloat(floater, true, kS1);
        QCoreApplication::processEvents();
        engine->parkOutput(kS1);
        engine->pruneStatesForRemovedScreen(kS1);
        returnOutput(engine); // desktop 1 in view, the parked strip is on 2
        QSignalSpy tiled(engine, &ScrollEngine::windowsTiled);
        QVERIFY(engine->readoptParked(tile, kS1, kS1));
        QVERIFY(engine->readoptParked(floater, kS1, kS1));
        QVERIFY(engine->isWindowTracked(tile));

        // The desktop is shown, and no open is announced for either window.
        engine->setCurrentDesktopForScreen(kS1, 2);
        QCoreApplication::processEvents();
        QVERIFY(engine->isWindowFloatingInScroll(floater));
        QCOMPARE(columnsOf(engine, kS1), QList<QStringList>{QStringList{tile}});
        for (const auto& emission : std::as_const(tiled)) {
            const QJsonArray batch = QJsonDocument::fromJson(emission.at(0).toString().toUtf8()).array();
            for (const QJsonValue& entry : batch) {
                QVERIFY2(entry.toObject().value(QLatin1String("windowId")).toString() != floater,
                         "the float is never laid out as a tile");
            }
        }
    }

    // A window parked floating that KWin returns before its screen scrolls
    // again is granted with its float verdict: announced once the screen
    // scrolls, it seats floating and takes no column (F1095).
    void floatReturnedBeforeTheScreenScrollsArrivesFloating()
    {
        QObject owner;
        ScrollEngine* engine = makeProviderEngine(&owner, {kS1});
        engine->setCurrentDesktopForScreen(kS1, 1);
        const QString tile = QStringLiteral("app|tile");
        const QString floater = QStringLiteral("app|floater");
        engine->windowOpened(tile, kS1, 0, 0);
        engine->windowOpened(floater, kS1, 0, 0);
        engine->setWindowFloat(floater, true, kS1);
        QCoreApplication::processEvents();
        engine->parkOutput(kS1);
        engine->pruneStatesForRemovedScreen(kS1);
        // Returned while S1 is not a scrolling screen: both are granted.
        QVERIFY(engine->readoptParked(tile, kS1, kS1));
        QVERIFY(engine->readoptParked(floater, kS1, kS1));
        QVERIFY(!engine->isWindowTracked(floater));

        returnOutput(engine);
        QSignalSpy tiled(engine, &ScrollEngine::windowsTiled);
        engine->windowOpened(tile, kS1, 0, 0);
        engine->windowOpened(floater, kS1, 0, 0);
        QCoreApplication::processEvents();
        QVERIFY(engine->isWindowFloatingInScroll(floater));
        QCOMPARE(columnsOf(engine, kS1), QList<QStringList>{QStringList{tile}});
        for (const auto& emission : std::as_const(tiled)) {
            const QJsonArray batch = QJsonDocument::fromJson(emission.at(0).toString().toUtf8()).array();
            for (const QJsonValue& entry : batch) {
                QVERIFY2(entry.toObject().value(QLatin1String("windowId")).toString() != floater,
                         "the float is never laid out as a tile");
            }
        }
    }

    // Once every window of a parked context is granted (returned before its
    // screen scrolls again), the stash stays parked until the tiles arrive:
    // the save still leaves it out (F1097).
    void fullyGrantedStashStaysOutOfTheSave()
    {
        QObject owner;
        ScrollEngine* engine = makeProviderEngine(&owner, {kS1});
        engine->setCurrentDesktopForScreen(kS1, 2);
        const QString a = QStringLiteral("app|a");
        const QString b = QStringLiteral("app|b");
        engine->windowOpened(a, kS1, 0, 0);
        engine->windowOpened(b, kS1, 0, 0);
        QCoreApplication::processEvents();
        engine->parkOutput(kS1);
        engine->pruneStatesForRemovedScreen(kS1);
        // Returned before the screen is scrolling again, so both are granted.
        QVERIFY(engine->readoptParked(a, kS1, kS1));
        QVERIFY(engine->readoptParked(b, kS1, kS1));
        QVERIFY(!engine->hasParked(a, QString()));
        QVERIFY(!engine->isWindowTracked(a));

        const QJsonObject blob = engine->serializeStripState();
        QVERIFY2(!QString::fromUtf8(QJsonDocument(blob).toJson()).contains(a),
                 "a stash still owed to granted tiles must not be saved");
    }

    // A touch on the last window a parked context still owes does not take
    // the stash from tiles already granted: they come back in their order
    // when they arrive (F1097).
    void dropAfterGrantsKeepsTheStashForTheGrantedTiles()
    {
        QObject owner;
        ScrollEngine* engine = makeProviderEngine(&owner, {kS1});
        engine->setCurrentDesktopForScreen(kS1, 2);
        const QString a = QStringLiteral("app|a");
        const QString b = QStringLiteral("app|b");
        const QString c = QStringLiteral("app|c");
        engine->windowOpened(a, kS1, 0, 0);
        engine->windowOpened(b, kS1, 0, 0);
        engine->windowOpened(c, kS1, 0, 0);
        QCoreApplication::processEvents();
        QCOMPARE(columnsOf(engine, kS1), (QList<QStringList>{QStringList{a}, QStringList{b}, QStringList{c}}));
        engine->parkOutput(kS1);
        engine->pruneStatesForRemovedScreen(kS1);
        // Returned before the screen is scrolling again, so a and c are
        // granted; the screen's return to scrolling announces them.
        QVERIFY(engine->readoptParked(a, kS1, kS1));
        QVERIFY(engine->readoptParked(c, kS1, kS1));
        QVERIFY(!engine->isWindowTracked(a));
        engine->dropParked(b, QString(), 0, QString());

        returnOutput(engine);
        engine->setCurrentDesktopForScreen(kS1, 2);
        engine->windowOpened(c, kS1, 0, 0);
        engine->windowOpened(a, kS1, 0, 0);
        QCoreApplication::processEvents();
        QCOMPARE(columnsOf(engine, kS1), (QList<QStringList>{QStringList{a}, QStringList{c}}));
    }

    // A desktop removed while its strip is parked takes the window's parked
    // float verdict with it: parked again later as a tile, it comes back a
    // tile (F1026).
    void removedDesktopParkLeavesNoFloatVerdict()
    {
        QObject owner;
        ScrollEngine* engine = makeProviderEngine(&owner, {kS1});
        engine->setCurrentDesktopForScreen(kS1, 1);
        const QString window = QStringLiteral("app|window");
        engine->windowOpened(window, kS1, 0, 0);
        engine->setWindowFloat(window, true, kS1);
        QCoreApplication::processEvents();
        engine->parkOutput(kS1);
        engine->pruneStatesForRemovedScreen(kS1);
        engine->remapDesktops(DesktopRemap::removal(1, 2));
        QVERIFY(!engine->hasParked(window, QString()));

        returnOutput(engine);
        engine->windowOpened(window, kS1, 0, 0);
        QCoreApplication::processEvents();
        QVERIFY(!engine->isWindowFloatingInScroll(window));
        engine->parkOutput(kS1);
        engine->pruneStatesForRemovedScreen(kS1);
        returnOutput(engine);
        QVERIFY(engine->readoptParked(window, kS1, kS1));
        QCoreApplication::processEvents();
        QVERIFY2(!engine->isWindowFloatingInScroll(window), "the dropped park's float verdict does not linger");
        QCOMPARE(columnsOf(engine, kS1), QList<QStringList>{QStringList{window}});
    }
};

QTEST_MAIN(TestScrollEngineEvacuee)
#include "test_scrollengine_evacuee.moc"
