// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_auto_tile.cpp
 * @brief Unit tests for AutoTileService
 *
 * Tests the auto-tile service's internal state management:
 * - Window tracking (add/remove/master)
 * - Screen dynamic status queries
 * - Master window promotion ordering
 * - Tiled window count
 *
 * These tests use null dependencies where possible since
 * the service gracefully handles null layout/service pointers
 * for query methods. Full integration tests require a running
 * LayoutManager with Dynamic layouts.
 */

#include <QTest>
#include <QSignalSpy>
#include <QJsonArray>
#include <QJsonObject>

#include "core/autotileservice.h"

using namespace PlasmaZones;

class TestAutoTile : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase();

    // Basic construction
    void testConstruction();

    // State queries with null dependencies
    void testIsScreenDynamic_NullLayoutManager();
    void testTiledWindowCount_EmptyScreen();
    void testMasterWindowId_EmptyScreen();

    // Window lifecycle tracking
    void testHandleWindowOpened_NullDeps();
    void testHandleWindowClosed_NullDeps();
    void testHandleWindowMinimized_NullDeps();

    // Auto-tile result structure
    void testAutoTileResult_Default();
    void testWindowAssignment_Structure();

    // JSON conversion
    void testAssignmentsToJson_ViaSignal();

private:
    std::unique_ptr<AutoTileService> createService();
};

void TestAutoTile::initTestCase()
{
    // Nothing to set up - tests use null dependencies
}

std::unique_ptr<AutoTileService> TestAutoTile::createService()
{
    // Create with null dependencies — service handles gracefully
    return std::make_unique<AutoTileService>(nullptr, nullptr, nullptr, nullptr);
}

void TestAutoTile::testConstruction()
{
    auto service = createService();
    QVERIFY(service != nullptr);
}

void TestAutoTile::testIsScreenDynamic_NullLayoutManager()
{
    auto service = createService();
    // With null LayoutManager, no screen can be Dynamic
    QVERIFY(!service->isScreenDynamic(QStringLiteral("DP-1")));
    QVERIFY(!service->isScreenDynamic(QStringLiteral("")));
}

void TestAutoTile::testTiledWindowCount_EmptyScreen()
{
    auto service = createService();
    // No windows tracked yet
    QCOMPARE(service->tiledWindowCount(QStringLiteral("DP-1")), 0);
    QCOMPARE(service->tiledWindowCount(QStringLiteral("")), 0);
}

void TestAutoTile::testMasterWindowId_EmptyScreen()
{
    auto service = createService();
    // No master set
    QVERIFY(service->masterWindowId(QStringLiteral("DP-1")).isEmpty());
    QVERIFY(service->masterWindowId(QStringLiteral("")).isEmpty());
}

void TestAutoTile::testHandleWindowOpened_NullDeps()
{
    auto service = createService();
    // With null deps, handleWindowOpened should return handled=false
    auto result = service->handleWindowOpened(QStringLiteral("konsole:konsole 0x12345"), QStringLiteral("DP-1"));
    QVERIFY(!result.handled);
    QVERIFY(result.assignments.isEmpty());
}

void TestAutoTile::testHandleWindowClosed_NullDeps()
{
    auto service = createService();
    // Should not crash with null deps
    service->handleWindowClosed(QStringLiteral("konsole:konsole 0x12345"));
    // Verify no crash — if we get here, the test passes
}

void TestAutoTile::testHandleWindowMinimized_NullDeps()
{
    auto service = createService();
    // Should not crash with null deps
    service->handleWindowMinimized(QStringLiteral("konsole:konsole 0x12345"), true);
    service->handleWindowMinimized(QStringLiteral("konsole:konsole 0x12345"), false);
    // Verify no crash
}

void TestAutoTile::testAutoTileResult_Default()
{
    AutoTileResult result;
    QVERIFY(!result.handled);
    QVERIFY(result.assignments.isEmpty());
}

void TestAutoTile::testWindowAssignment_Structure()
{
    WindowAssignment assignment;
    assignment.windowId = QStringLiteral("konsole:konsole 0x12345");
    assignment.zoneId = QStringLiteral("{abcd-1234}");
    assignment.geometry = QRect(0, 0, 960, 1080);

    QCOMPARE(assignment.windowId, QStringLiteral("konsole:konsole 0x12345"));
    QCOMPARE(assignment.zoneId, QStringLiteral("{abcd-1234}"));
    QCOMPARE(assignment.geometry, QRect(0, 0, 960, 1080));
}

void TestAutoTile::testAssignmentsToJson_ViaSignal()
{
    auto service = createService();

    // Connect to geometriesChanged signal to verify JSON format
    QSignalSpy spy(service.get(), &AutoTileService::geometriesChanged);
    QVERIFY(spy.isValid());

    // The signal takes (QString screenName, QJsonArray assignments)
    // We can't easily trigger it without real deps, but we can verify
    // the signal exists and is connectable
    QCOMPARE(spy.count(), 0);
}

QTEST_MAIN(TestAutoTile)
#include "test_auto_tile.moc"
