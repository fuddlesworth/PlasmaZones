// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include <QTest>
#include <QCoreApplication>
#include <QSignalSpy>

#include <PhosphorTileEngine/AutotileEngine.h>
#include "../helpers/AutotileTestHelpers.h"
#include <PhosphorTileEngine/AutotileConfig.h>
#include <PhosphorTiles/TilingState.h>
#include <PhosphorTiles/AlgorithmRegistry.h>
#include "core/constants.h"

using namespace PlasmaZones;
using namespace PhosphorTileEngine;

/**
 * @brief AutotileEngine tests for windowMinSizeUpdated behavior
 */
class TestAutotileEngineMinSize : public QObject
{
    Q_OBJECT

private Q_SLOTS:

    void initTestCase()
    {
        PlasmaZones::TestHelpers::testRegistry();
    }

    void testWindowMinSizeUpdated_validWindow()
    {
        AutotileEngine engine(nullptr, nullptr, nullptr, PlasmaZones::TestHelpers::testRegistry());
        const QString screenName = QStringLiteral("TestScreen");
        const QString windowId = QStringLiteral("win-minsize-1");

        QSet<QString> screens{screenName};
        engine.setAutotileScreens(screens);
        engine.windowOpened(windowId, screenName, 100, 50);
        QCoreApplication::processEvents();

        QSignalSpy tilingSpy(&engine, &PhosphorEngineApi::PlacementEngineBase::placementChanged);
        engine.windowMinSizeUpdated(windowId, 200, 100);
        QCoreApplication::processEvents();

        QCOMPARE(tilingSpy.count(), 1);
    }

    void testWindowMinSizeUpdated_noOpSameValue()
    {
        AutotileEngine engine(nullptr, nullptr, nullptr, PlasmaZones::TestHelpers::testRegistry());
        const QString screenName = QStringLiteral("TestScreen");
        const QString windowId = QStringLiteral("win-minsize-2");

        QSet<QString> screens{screenName};
        engine.setAutotileScreens(screens);
        engine.windowOpened(windowId, screenName, 100, 50);
        QCoreApplication::processEvents();

        QSignalSpy tilingSpy(&engine, &PhosphorEngineApi::PlacementEngineBase::placementChanged);
        engine.windowMinSizeUpdated(windowId, 100, 50);
        QCoreApplication::processEvents();

        QCOMPARE(tilingSpy.count(), 0);
    }

    void testWindowMinSizeUpdated_unknownWindow()
    {
        AutotileEngine engine(nullptr, nullptr, nullptr, PlasmaZones::TestHelpers::testRegistry());
        const QString screenName = QStringLiteral("TestScreen");

        QSet<QString> screens{screenName};
        engine.setAutotileScreens(screens);

        engine.windowMinSizeUpdated(QStringLiteral("nonexistent-win"), 100, 50);
        QCoreApplication::processEvents();
    }

    void testWindowMinSizeUpdated_negativeValues()
    {
        AutotileEngine engine(nullptr, nullptr, nullptr, PlasmaZones::TestHelpers::testRegistry());
        const QString screenName = QStringLiteral("TestScreen");
        const QString windowId = QStringLiteral("win-minsize-neg");

        QSet<QString> screens{screenName};
        engine.setAutotileScreens(screens);
        engine.windowOpened(windowId, screenName, 100, 50);
        QCoreApplication::processEvents();

        engine.windowMinSizeUpdated(windowId, -10, -20);
        QCoreApplication::processEvents();
    }

    void testWindowMinSizeUpdated_zeroRemovesEntry()
    {
        AutotileEngine engine(nullptr, nullptr, nullptr, PlasmaZones::TestHelpers::testRegistry());
        const QString screenName = QStringLiteral("TestScreen");
        const QString windowId = QStringLiteral("win-minsize-zero");

        QSet<QString> screens{screenName};
        engine.setAutotileScreens(screens);
        engine.windowOpened(windowId, screenName, 100, 50);
        QCoreApplication::processEvents();

        QSignalSpy tilingSpy(&engine, &PhosphorEngineApi::PlacementEngineBase::placementChanged);
        engine.windowMinSizeUpdated(windowId, 0, 0);
        QCoreApplication::processEvents();

        QCOMPARE(tilingSpy.count(), 1);
    }
};

QTEST_MAIN(TestAutotileEngineMinSize)
#include "test_autotile_engine_minsize.moc"
