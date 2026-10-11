// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

// The scroll engine's evacuee park: a strip on an output that disconnects is
// parked as a stash the removed-screen prune and the saves leave alone, an
// untouched window KWin brings back re-enters its old column through an
// exact-id claim, and a window touched while away never claims it.

#include <PhosphorScrollEngine/ScrollEngine.h>
#include <PhosphorScrollEngine/ScrollState.h>

#include "scrollstriptestutils.h"

#include <QCoreApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QtTest>

using namespace PhosphorScrollEngine;
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
};

QTEST_MAIN(TestScrollEngineEvacuee)
#include "test_scrollengine_evacuee.moc"
