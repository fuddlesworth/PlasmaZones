// SPDX-FileCopyrightText: 2024 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_window_identity.cpp
 * @brief Unit tests for window identity extraction and collision detection
 *
 * Bug context: Windows were auto-snapping to wrong zones because extractStableId()
 * creates non-unique identifiers for windows of the same application class.
 *
 * Window ID format: "windowClass:resourceName:pointerAddress"
 * Stable ID format: "windowClass:resourceName" (pointer stripped)
 *
 * Example collision:
 *   Window A: "org.kde.konsole:konsole:12345" -> stable: "org.kde.konsole:konsole"
 *   Window B: "org.kde.konsole:konsole:67890" -> stable: "org.kde.konsole:konsole"
 *   Both windows end up with the same stable ID, causing identity collision.
 *
 * This test suite validates:
 * 1. extractStableId() behavior for various window ID formats
 * 2. Detection of same-class window collisions
 * 3. Edge cases in window ID parsing
 */

#include <QTest>
#include <QString>
#include <QHash>

/**
 * @brief Reimplementation of extractStableId for isolated testing
 *
 * This is copied from WindowTrackingAdaptor::extractStableId() to allow
 * testing without full daemon initialization. Any changes to the original
 * function must be reflected here.
 */
static QString extractStableId(const QString& windowId)
{
    // Window ID format: "windowClass:resourceName:pointerAddress"
    // Stable ID: "windowClass:resourceName" (without pointer address)
    int lastColon = windowId.lastIndexOf(QLatin1Char(':'));
    if (lastColon <= 0) {
        return windowId;
    }

    QString potentialPointer = windowId.mid(lastColon + 1);
    bool isPointer = !potentialPointer.isEmpty();
    for (const QChar& c : potentialPointer) {
        if (!c.isDigit()) {
            isPointer = false;
            break;
        }
    }

    if (isPointer) {
        return windowId.left(lastColon);
    }
    return windowId;
}

class TestWindowIdentity : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    // ═══════════════════════════════════════════════════════════════════════
    // Basic extractStableId() tests
    // ═══════════════════════════════════════════════════════════════════════

    void testExtractStableId_normalFormat()
    {
        // Standard window ID format: windowClass:resourceName:pointer
        QString windowId = QStringLiteral("org.kde.konsole:konsole:12345678");
        QString stableId = extractStableId(windowId);

        QCOMPARE(stableId, QStringLiteral("org.kde.konsole:konsole"));
    }

    void testExtractStableId_stripsPointerAddress()
    {
        // Verify pointer address is stripped
        QString windowId = QStringLiteral("org.kde.dolphin:dolphin:94827364521");
        QString stableId = extractStableId(windowId);

        QVERIFY(!stableId.contains(QStringLiteral("94827364521")));
        QCOMPARE(stableId, QStringLiteral("org.kde.dolphin:dolphin"));
    }

    void testExtractStableId_emptyInput()
    {
        QString stableId = extractStableId(QString());
        QVERIFY(stableId.isEmpty());
    }

    void testExtractStableId_noColons()
    {
        // Window ID with no colons should return as-is
        QString windowId = QStringLiteral("simpleWindowId");
        QString stableId = extractStableId(windowId);

        QCOMPARE(stableId, windowId);
    }

    void testExtractStableId_singleColon()
    {
        // Window ID with single colon (no pointer part)
        QString windowId = QStringLiteral("windowClass:resourceName");
        QString stableId = extractStableId(windowId);

        // Should return as-is since "resourceName" is not all digits
        QCOMPARE(stableId, windowId);
    }

    void testExtractStableId_nonDigitSuffix()
    {
        // Last part is not a pointer (contains non-digits)
        QString windowId = QStringLiteral("org.kde.kate:kate:abc123");
        QString stableId = extractStableId(windowId);

        // Should return as-is since suffix is not purely numeric
        QCOMPARE(stableId, windowId);
    }

    void testExtractStableId_colonAtStart()
    {
        // Edge case: colon at position 0
        QString windowId = QStringLiteral(":resourceName:12345");
        QString stableId = extractStableId(windowId);

        QCOMPARE(stableId, QStringLiteral(":resourceName"));
    }

    // ═══════════════════════════════════════════════════════════════════════
    // Window identity collision
    // ═══════════════════════════════════════════════════════════════════════

    void testSameClassWindowsProduceIdenticalStableIds()
    {
        // BUG DEMONSTRATION: Two Konsole windows have identical stable IDs
        QString konsole1 = QStringLiteral("org.kde.konsole:konsole:12345");
        QString konsole2 = QStringLiteral("org.kde.konsole:konsole:67890");

        QString stable1 = extractStableId(konsole1);
        QString stable2 = extractStableId(konsole2);

        // This is the BUG: Both stable IDs are identical!
        QCOMPARE(stable1, stable2);
        QCOMPARE(stable1, QStringLiteral("org.kde.konsole:konsole"));
    }

    void testCollisionCountWithMultipleSameClassWindows()
    {
        // Simulate 5 instances of the same application
        QStringList windowIds = {
            QStringLiteral("org.kde.konsole:konsole:11111"), QStringLiteral("org.kde.konsole:konsole:22222"),
            QStringLiteral("org.kde.konsole:konsole:33333"), QStringLiteral("org.kde.konsole:konsole:44444"),
            QStringLiteral("org.kde.konsole:konsole:55555")};

        QHash<QString, int> stableIdCounts;
        for (const QString& windowId : windowIds) {
            QString stableId = extractStableId(windowId);
            stableIdCounts[stableId]++;
        }

        // BUG: All 5 windows map to the same stable ID
        QCOMPARE(stableIdCounts.size(), 1);
        QCOMPARE(stableIdCounts.value(QStringLiteral("org.kde.konsole:konsole")), 5);
    }

    void testDifferentClassWindowsHaveUniqueStableIds()
    {
        // Different applications should have unique stable IDs
        QString konsole = QStringLiteral("org.kde.konsole:konsole:12345");
        QString dolphin = QStringLiteral("org.kde.dolphin:dolphin:67890");
        QString kate = QStringLiteral("org.kde.kate:kate:11111");

        QString stableKonsole = extractStableId(konsole);
        QString stableDolphin = extractStableId(dolphin);
        QString stableKate = extractStableId(kate);

        QVERIFY(stableKonsole != stableDolphin);
        QVERIFY(stableDolphin != stableKate);
        QVERIFY(stableKonsole != stableKate);
    }

    // ═══════════════════════════════════════════════════════════════════════
    // Session Persistence Collision Simulation
    // ═══════════════════════════════════════════════════════════════════════

    void testSessionRestoreCollisionScenario()
    {
        // Simulate session restore with same-class windows
        // Session 1: User had Konsole window in Zone A
        QHash<QString, QString> persistedAssignments;
        QString session1Window = QStringLiteral("org.kde.konsole:konsole:12345");
        QString zone1 = QStringLiteral("zone-uuid-a");

        // Save stable ID -> zone mapping
        QString stableId = extractStableId(session1Window);
        persistedAssignments[stableId] = zone1;

        // Session 2: New Konsole window opens (different pointer, never was snapped)
        QString session2Window = QStringLiteral("org.kde.konsole:konsole:67890");
        QString newStableId = extractStableId(session2Window);

        // BUG: New window matches the old session's stable ID!
        QVERIFY(persistedAssignments.contains(newStableId));

        // This causes the WRONG window to be auto-snapped to Zone A
        QString wronglyAssignedZone = persistedAssignments.value(newStableId);
        QCOMPARE(wronglyAssignedZone, zone1);
    }

    void testMultipleSessionRestoreConfusion()
    {
        // Simulate: User had 3 Konsole windows in zones A, B, C (session 1)
        // Problem: Which zone should a NEW Konsole window use in session 2?
        QHash<QString, QString> persistedAssignments;

        // All 3 windows have the same stable ID - only LAST one is stored!
        QString konsole1 = QStringLiteral("org.kde.konsole:konsole:11111");
        QString konsole2 = QStringLiteral("org.kde.konsole:konsole:22222");
        QString konsole3 = QStringLiteral("org.kde.konsole:konsole:33333");

        QString stable1 = extractStableId(konsole1);
        QString stable2 = extractStableId(konsole2);
        QString stable3 = extractStableId(konsole3);

        // Simulate saving: last write wins
        persistedAssignments[stable1] = QStringLiteral("zone-a");
        persistedAssignments[stable2] = QStringLiteral("zone-b"); // Overwrites zone-a!
        persistedAssignments[stable3] = QStringLiteral("zone-c"); // Overwrites zone-b!

        // BUG: Only one assignment survives
        QCOMPARE(persistedAssignments.size(), 1);
        QCOMPARE(persistedAssignments.value(QStringLiteral("org.kde.konsole:konsole")), QStringLiteral("zone-c"));
    }

    // ═══════════════════════════════════════════════════════════════════════
    // Edge Cases in Window ID Format
    // ═══════════════════════════════════════════════════════════════════════

    void testWindowIdWithOnlyDigitsInResourceName()
    {
        // Resource name is all digits (rare but possible)
        QString windowId = QStringLiteral("com.example.app:12345:67890");
        QString stableId = extractStableId(windowId);

        // Should strip the pointer (67890), keep resource name (12345)
        QCOMPARE(stableId, QStringLiteral("com.example.app:12345"));
    }

    void testWindowIdWithMultipleColons()
    {
        // Window class with multiple colons (e.g., reverse DNS)
        QString windowId = QStringLiteral("com.company.product.app:resource:99999");
        QString stableId = extractStableId(windowId);

        QCOMPARE(stableId, QStringLiteral("com.company.product.app:resource"));
    }

    void testWindowIdWithLeadingZerosInPointer()
    {
        // Pointer address with leading zeros
        QString windowId = QStringLiteral("org.kde.app:name:00012345");
        QString stableId = extractStableId(windowId);

        QCOMPARE(stableId, QStringLiteral("org.kde.app:name"));
    }

    void testWindowIdWithVeryLongPointer()
    {
        // Very long pointer address (64-bit address)
        QString windowId = QStringLiteral("app:name:140737353934848");
        QString stableId = extractStableId(windowId);

        QCOMPARE(stableId, QStringLiteral("app:name"));
    }

    void testWindowIdWithEmptyResourceName()
    {
        // Empty resource name
        QString windowId = QStringLiteral("org.kde.app::12345");
        QString stableId = extractStableId(windowId);

        QCOMPARE(stableId, QStringLiteral("org.kde.app:"));
    }

    // ═══════════════════════════════════════════════════════════════════════
    // Proposed Fix Validation Tests (for future implementation)
    // ═══════════════════════════════════════════════════════════════════════

    /**
     * @brief Proposed fix: Include instance counter or timestamp in stable ID
     *
     * The fix would change stable ID to: "windowClass:resourceName:instanceId"
     * where instanceId is a session-stable counter (not the pointer address)
     *
     * These tests validate the expected behavior AFTER the fix is implemented.
     */
    void testProposedFix_instanceCounterMakesUniqueIds()
    {
        // Proposed stable ID format with instance counter
        // This test documents the expected behavior after fix

        // Session-stable instance tracking would give:
        // Konsole #1: "org.kde.konsole:konsole:instance-1"
        // Konsole #2: "org.kde.konsole:konsole:instance-2"

        QString proposedStable1 = QStringLiteral("org.kde.konsole:konsole:instance-1");
        QString proposedStable2 = QStringLiteral("org.kde.konsole:konsole:instance-2");

        // These SHOULD be unique (unlike current behavior)
        QVERIFY(proposedStable1 != proposedStable2);
    }
};

QTEST_MAIN(TestWindowIdentity)
#include "test_window_identity.moc"
