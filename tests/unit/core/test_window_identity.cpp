// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_window_identity.cpp
 * @brief Unit tests for Utils::extractAppId() — the legacy composite parser.
 *
 * IMPORTANT: the runtime wire format is no longer "appId|uuid" composite —
 * the kwin-effect bridge sends opaque instance ids (bare UUIDs).
 * Utils::extractAppId() is preserved as a fallback inside
 * WindowTrackingService::currentAppIdFor() for unit tests and code paths
 * that don't have a WindowRegistry attached — on a bare instance id the
 * helper is a passthrough (no '|' separator to split on).
 *
 * This file validates the parser's behavior on both legacy composites and
 * bare instance ids so that the compat fallback stays well-defined. It does
 * NOT describe how production looks up app class for a window — for that,
 * see WindowTrackingService::currentAppIdFor() and
 * PlasmaZonesEffect::appIdForInstance(), which query the live
 * WindowRegistry.
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
    // Wire format: bare instance id passthrough
    //
    // These cases pin the parser's behavior on the production wire format
    // (no '|' separator). The helper must return the input unchanged so the
    // WTS fallback in unit tests can still return something sensible for
    // lookup-by-string comparisons in the legacy compat path.
    // =====================================================================

    void testExtractAppId_bareInstanceId_returnsUnchanged()
    {
        // This is what getWindowId() emits in production: a bare KWin UUID.
        // Parsing it must not throw away data — the helper is a passthrough
        // when there's no separator.
        const QString instanceId = QStringLiteral("cef1ba31-3316-4f05-84f5-ef627674b504");
        QCOMPARE(extractAppId(instanceId), instanceId);
    }

    void testExtractAppId_legacyComposite_stillParses()
    {
        // Legacy callers (disk fixtures from old config files, etc.) may
        // still carry composite strings. The helper continues to accept
        // them so migration paths don't silently corrupt data.
        const QString composite = QStringLiteral("org.kde.kate|old-uuid-55555");
        QCOMPARE(extractAppId(composite), QStringLiteral("org.kde.kate"));
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
