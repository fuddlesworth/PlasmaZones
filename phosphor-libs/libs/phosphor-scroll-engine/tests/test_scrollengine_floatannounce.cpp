// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

// Which float transitions announce on which channel (F557, F578,
// shell_map_float). setWindowFloat is the tracking adaptor's entry (minimize,
// a drag float, the shell placement map) and announces PASSIVE, so the daemon
// floats the window where it is. The user float verbs (Meta+F, move to
// floating, move to tiling) announce ACTIVE, so the daemon still restores the
// float-back and shows the OSD.

#include <PhosphorScrollEngine/ScrollEngine.h>
#include <PhosphorScrollEngine/ScrollState.h>

#include "scrollstriptestutils.h"

#include <QSignalSpy>
#include <QtTest>

using namespace PhosphorScrollEngine;

using ScrollTestUtils::makeProviderEngine;

namespace {

const QString kScreen = QStringLiteral("S1");
const QString kA = QStringLiteral("app|a");
const QString kB = QStringLiteral("app|b");
const QString kC = QStringLiteral("app|c");

/// Three single-tile columns a, b, c with b focused.
ScrollEngine* threeColumns(QObject* owner)
{
    ScrollEngine* engine = makeProviderEngine(owner, {kScreen});
    for (const QString& id : {kA, kB, kC}) {
        engine->windowOpened(id, kScreen, 0, 0);
    }
    engine->windowFocused(kB, kScreen);
    QCoreApplication::processEvents();
    return engine;
}

ScrollState* stateFor(ScrollEngine* engine)
{
    return static_cast<ScrollState*>(engine->stateForScreen(kScreen));
}

} // namespace

class TestScrollEngineFloatAnnounce : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase()
    {
        AX_GUARD_SUITE();
    }

    void setWindowFloatIsPassive()
    {
        QObject owner;
        ScrollEngine* engine = threeColumns(&owner);
        QSignalSpy passive(engine, &PhosphorEngine::PlacementEngineBase::windowFloatingStateSynced);
        QSignalSpy active(engine, &PhosphorEngine::PlacementEngineBase::windowFloatingChanged);

        engine->setWindowFloat(kB, true, kScreen);
        QVERIFY(engine->isWindowFloatingInScroll(kB));
        engine->setWindowFloat(kB, false, kScreen);
        QVERIFY(!engine->isWindowFloatingInScroll(kB));

        QCOMPARE(active.count(), 0);
        QCOMPARE(passive.count(), 2);
        QCOMPARE(passive.at(0).at(0).toString(), kB);
        QCOMPARE(passive.at(0).at(1).toBool(), true);
        QCOMPARE(passive.at(1).at(1).toBool(), false);
        // The slot memory is the same either way: b returns between a and c.
        QCOMPARE(engine->managedWindowOrder(kScreen), (QStringList{kA, kB, kC}));
    }

    void toggleWindowFloatIsActive()
    {
        QObject owner;
        ScrollEngine* engine = threeColumns(&owner);
        QSignalSpy passive(engine, &PhosphorEngine::PlacementEngineBase::windowFloatingStateSynced);
        QSignalSpy active(engine, &PhosphorEngine::PlacementEngineBase::windowFloatingChanged);

        engine->toggleWindowFloat(kB, kScreen);
        QVERIFY(engine->isWindowFloatingInScroll(kB));
        engine->toggleWindowFloat(kB, kScreen);
        QVERIFY(!engine->isWindowFloatingInScroll(kB));

        QCOMPARE(passive.count(), 0);
        QCOMPARE(active.count(), 2);
        QCOMPARE(active.at(0).at(1).toBool(), true);
        QCOMPARE(active.at(1).at(1).toBool(), false);
    }

    void moveFocusedToFloatingAndBackIsActive()
    {
        QObject owner;
        ScrollEngine* engine = threeColumns(&owner);
        QSignalSpy passive(engine, &PhosphorEngine::PlacementEngineBase::windowFloatingStateSynced);
        QSignalSpy active(engine, &PhosphorEngine::PlacementEngineBase::windowFloatingChanged);

        engine->moveFocusedToFloating(kScreen);
        QVERIFY(stateFor(engine)->isFloating(kB));
        QVERIFY(stateFor(engine)->floatingHasFocus());
        engine->moveFocusedToTiling(kScreen);
        QVERIFY(!stateFor(engine)->isFloating(kB));

        QCOMPARE(passive.count(), 0);
        QCOMPARE(active.count(), 2);
        QCOMPARE(active.at(0).at(0).toString(), kB);
        QCOMPARE(active.at(0).at(1).toBool(), true);
        QCOMPARE(active.at(1).at(1).toBool(), false);
    }
};

QTEST_GUILESS_MAIN(TestScrollEngineFloatAnnounce)
#include "test_scrollengine_floatannounce.moc"
