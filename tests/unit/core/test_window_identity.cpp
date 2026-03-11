// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_window_identity.cpp
 * @brief Unit tests for window identity extraction and collision detection
 *
 * Bug context: Windows were auto-snapping to wrong zones because extractAppId()
 * creates non-unique identifiers for windows of the same application class.
 * This is now BY DESIGN -- the daemon uses consumption queues to handle
 * multiple instances of the same app.
 *
 * Window ID format: "appId|internalUuid"
 * App ID format: "appId" (UUID stripped)
 *
 * Example collision (by design):
 *   Window A: "org.kde.konsole|uuid1" -> appId: "org.kde.konsole"
 *   Window B: "org.kde.konsole|uuid2" -> appId: "org.kde.konsole"
 *   Both windows share the same appId; consumption queues handle disambiguation.
 *
 * This test suite validates:
 * 1. extractAppId() behavior for various window ID formats
 * 2. Detection of same-class window collisions (by design)
 * 3. Edge cases in window ID parsing
 */

#include <QTest>
#include <QString>
#include <QHash>

#include "../../../src/core/utils.h"

using PlasmaZones::Utils::extractAppId;

class TestWindowIdentity : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    // =====================================================================
    // Basic extractAppId() tests
    // =====================================================================

    void testExtractAppId_normalFormat()
    {
        // Standard window ID format: appId|uuid
        QString windowId = QStringLiteral("org.kde.konsole|a1b2c3d4-e5f6-7890-abcd-ef1234567890");
        QString appId = extractAppId(windowId);

        QCOMPARE(appId, QStringLiteral("org.kde.konsole"));
    }

    void testExtractAppId_stripsUuid()
    {
        // Verify UUID is stripped
        QString windowId = QStringLiteral("org.kde.dolphin|a1b2c3d4-e5f6-7890-abcd-ef1234567890");
        QString appId = extractAppId(windowId);

        QVERIFY(!appId.contains(QStringLiteral("a1b2c3d4")));
        QCOMPARE(appId, QStringLiteral("org.kde.dolphin"));
    }

    void testExtractAppId_emptyInput()
    {
        QString appId = extractAppId(QString());
        QVERIFY(appId.isEmpty());
    }

    void testExtractAppId_noPipe()
    {
        // Window ID with no pipe should return as-is
        QString windowId = QStringLiteral("simpleWindowId");
        QString appId = extractAppId(windowId);

        QCOMPARE(appId, windowId);
    }

    void testExtractAppId_pipeAtEnd()
    {
        // Window ID with pipe but no UUID part
        QString windowId = QStringLiteral("org.kde.konsole|");
        QString appId = extractAppId(windowId);

        QCOMPARE(appId, QStringLiteral("org.kde.konsole"));
    }

    void testExtractAppId_multipleDotsInAppId()
    {
        // App ID with complex reverse-DNS
        QString windowId = QStringLiteral("com.company.product.app|uuid-1234");
        QString appId = extractAppId(windowId);

        QCOMPARE(appId, QStringLiteral("com.company.product.app"));
    }

    void testExtractAppId_pipeAtStart()
    {
        // Edge case: pipe at position 0 -- should return empty (sep == 0, not > 0)
        QString windowId = QStringLiteral("|uuid-1234");
        QString appId = extractAppId(windowId);

        // extractAppId returns original string when sep is not > 0
        QCOMPARE(appId, windowId);
    }

    // =====================================================================
    // Window identity collision (by design -- handled by consumption queues)
    // =====================================================================

    void testSameClassWindowsProduceIdenticalAppIds()
    {
        // BY DESIGN: Two Konsole windows have identical app IDs
        QString konsole1 = QStringLiteral("org.kde.konsole|uuid-11111");
        QString konsole2 = QStringLiteral("org.kde.konsole|uuid-22222");

        QString appId1 = extractAppId(konsole1);
        QString appId2 = extractAppId(konsole2);

        // Same-class windows share the same appId -- consumption queues handle this
        QEXPECT_FAIL("", "By design: same-class windows share appId, handled by consumption queues", Continue);
        QVERIFY(appId1 != appId2);
        QCOMPARE(appId1, QStringLiteral("org.kde.konsole"));
    }

    void testCollisionCountWithMultipleSameClassWindows()
    {
        // Simulate 5 instances of the same application
        QStringList windowIds = {
            QStringLiteral("org.kde.konsole|uuid-11111"), QStringLiteral("org.kde.konsole|uuid-22222"),
            QStringLiteral("org.kde.konsole|uuid-33333"), QStringLiteral("org.kde.konsole|uuid-44444"),
            QStringLiteral("org.kde.konsole|uuid-55555")};

        QHash<QString, int> appIdCounts;
        for (const QString& windowId : windowIds) {
            QString appId = extractAppId(windowId);
            appIdCounts[appId]++;
        }

        // BY DESIGN: All 5 windows map to the same app ID
        QEXPECT_FAIL("", "By design: all same-class windows share one appId, handled by consumption queues", Continue);
        QVERIFY(appIdCounts.size() == 5);
        QCOMPARE(appIdCounts.value(QStringLiteral("org.kde.konsole")), 5);
    }

    void testDifferentClassWindowsHaveUniqueAppIds()
    {
        // Different applications should have unique app IDs
        QString konsole = QStringLiteral("org.kde.konsole|uuid-11111");
        QString dolphin = QStringLiteral("org.kde.dolphin|uuid-22222");
        QString kate = QStringLiteral("org.kde.kate|uuid-33333");

        QString appKonsole = extractAppId(konsole);
        QString appDolphin = extractAppId(dolphin);
        QString appKate = extractAppId(kate);

        QVERIFY(appKonsole != appDolphin);
        QVERIFY(appDolphin != appKate);
        QVERIFY(appKonsole != appKate);
    }

    // =====================================================================
    // Session Persistence Collision Simulation
    // =====================================================================

    void testSessionRestoreCollisionScenario()
    {
        // Simulate session restore with same-class windows
        // Session 1: User had Konsole window in Zone A
        QHash<QString, QStringList> persistedAssignments;
        QString session1Window = QStringLiteral("org.kde.konsole|uuid-11111");
        QString zone1 = QStringLiteral("zone-uuid-a");

        // Save appId -> zone mapping
        QString appId = extractAppId(session1Window);
        persistedAssignments[appId] = QStringList{zone1};

        // Session 2: New Konsole window opens (different UUID, never was snapped)
        QString session2Window = QStringLiteral("org.kde.konsole|uuid-22222");
        QString newAppId = extractAppId(session2Window);

        // By design: New window matches the old session's appId
        // Consumption queues in the daemon handle this correctly
        QEXPECT_FAIL("", "By design: same appId, consumption queues handle disambiguation", Continue);
        QVERIFY(!persistedAssignments.contains(newAppId));

        // This is expected -- the daemon uses consumption queues to handle this
        QString assignedZone = persistedAssignments.value(newAppId).value(0);
        QCOMPARE(assignedZone, zone1);
    }

    void testMultipleSessionRestoreConfusion()
    {
        // Simulate: User had 3 Konsole windows in zones A, B, C (session 1)
        // With consumption queues, all 3 would be stored as a list under one appId
        QHash<QString, QStringList> persistedAssignments;

        // All 3 windows have the same appId - only LAST one is stored in this simple hash!
        QString konsole1 = QStringLiteral("org.kde.konsole|uuid-11111");
        QString konsole2 = QStringLiteral("org.kde.konsole|uuid-22222");
        QString konsole3 = QStringLiteral("org.kde.konsole|uuid-33333");

        QString appId1 = extractAppId(konsole1);
        QString appId2 = extractAppId(konsole2);
        QString appId3 = extractAppId(konsole3);

        // Simulate saving: last write wins (in a simple hash; real daemon uses QList)
        persistedAssignments[appId1] = QStringList{QStringLiteral("zone-a")};
        persistedAssignments[appId2] = QStringList{QStringLiteral("zone-b")}; // Overwrites zone-a!
        persistedAssignments[appId3] = QStringList{QStringLiteral("zone-c")}; // Overwrites zone-b!

        // Expected: Only one assignment survives in simple hash (daemon uses list)
        QEXPECT_FAIL("", "By design: same appId in simple hash, daemon uses consumption queues with QList", Continue);
        QVERIFY(persistedAssignments.size() == 3);
        QCOMPARE(persistedAssignments.value(QStringLiteral("org.kde.konsole")).value(0), QStringLiteral("zone-c"));
    }

    // =====================================================================
    // Edge Cases in Window ID Format
    // =====================================================================

    void testWindowIdWithShortUuid()
    {
        // Short UUID (not a full UUID, but still valid format)
        QString windowId = QStringLiteral("com.example.app|12345");
        QString appId = extractAppId(windowId);

        QCOMPARE(appId, QStringLiteral("com.example.app"));
    }

    void testWindowIdWithMultiplePipes()
    {
        // Window ID with multiple pipes (only first one is used)
        QString windowId = QStringLiteral("com.company.app|resource|99999");
        QString appId = extractAppId(windowId);

        QCOMPARE(appId, QStringLiteral("com.company.app"));
    }

    void testWindowIdWithFullUuid()
    {
        // Full UUID format
        QString windowId = QStringLiteral("org.kde.app|a1b2c3d4-e5f6-7890-abcd-ef1234567890");
        QString appId = extractAppId(windowId);

        QCOMPARE(appId, QStringLiteral("org.kde.app"));
    }

    void testWindowIdWithHyphenatedAppId()
    {
        // App ID containing hyphens
        QString windowId = QStringLiteral("my-cool-app|uuid-12345");
        QString appId = extractAppId(windowId);

        QCOMPARE(appId, QStringLiteral("my-cool-app"));
    }

    void testWindowIdWithEmptyAppId()
    {
        // Empty app ID part (pipe at start)
        QString windowId = QStringLiteral("|uuid-12345");
        QString appId = extractAppId(windowId);

        // pipe at position 0 means sep is not > 0, returns original
        QCOMPARE(appId, windowId);
    }
};

QTEST_MAIN(TestWindowIdentity)
#include "test_window_identity.moc"
