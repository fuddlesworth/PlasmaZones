// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_window_identity.cpp
 * @brief Unit tests for PhosphorIdentity::WindowId::extractAppId() — the composite-key parser.
 *
 * The effect produces window ids in the form "appId|instanceId" (frozen at
 * first observation). Every daemon map keys off that composite. Services
 * that need the first-seen app class parse it with PhosphorIdentity::WindowId::extractAppId();
 * services that need the LIVE app class (Electron/CEF apps that mutate
 * their WM_CLASS mid-session) query WindowTrackingService::currentAppIdFor
 * or AutotileEngine::currentAppIdFor, both of which hit WindowRegistry.
 *
 * The two semantics coexist intentionally: the stable composite lets
 * daemon maps ignore class mutations (so a rule bound to a zone doesn't
 * get reassigned mid-session), and the live registry lookup lets new
 * rule decisions see the current class.
 *
 * This file pins extractAppId()'s behavior on composites (split before the
 * first '|') and on bare strings (passthrough) so the fallback stays
 * well-defined for the WindowTrackingService::currentAppIdFor() and
 * AutotileEngine::currentAppIdFor() call paths that walk this helper when
 * no registry is attached.
 */

#include <QTest>
#include <QString>
#include <QHash>

#include "../../../src/core/utils.h"

#include <PhosphorIdentity/WindowId.h>

using PhosphorIdentity::WindowId::appIdMatches;
using PhosphorIdentity::WindowId::buildCompositeId;
using PhosphorIdentity::WindowId::deriveShortName;
using PhosphorIdentity::WindowId::extractAppId;
using PhosphorIdentity::WindowId::extractInstanceId;

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
        QString windowId = QStringLiteral("|uuid-1234");
        QString appId = extractAppId(windowId);

        QCOMPARE(appId, QString());
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
        QString windowId = QStringLiteral("|uuid-12345");
        QString appId = extractAppId(windowId);

        QCOMPARE(appId, QString());
    }

    // =====================================================================
    // buildCompositeId — symmetry with extractAppId / extractInstanceId
    // =====================================================================

    void testBuildCompositeId_roundtrip()
    {
        const QString appId = QStringLiteral("org.kde.konsole");
        const QString instanceId = QStringLiteral("a1b2c3d4-e5f6-7890-abcd-ef1234567890");
        const QString composite = buildCompositeId(appId, instanceId);

        QCOMPARE(composite, appId + QLatin1Char('|') + instanceId);
        QCOMPARE(extractAppId(composite), appId);
        QCOMPARE(extractInstanceId(composite), instanceId);
    }

    void testBuildCompositeId_emptyInstance_yieldsBareAppId()
    {
        // Symmetric with extractAppId's passthrough on bare strings — an empty
        // instance id must not leave a dangling separator, otherwise the
        // round-trip extractInstanceId(buildCompositeId(a, "")) would return
        // an empty string that subsequent matchers treat as a valid instance.
        const QString composite = buildCompositeId(QStringLiteral("firefox"), QString());
        QCOMPARE(composite, QStringLiteral("firefox"));
        QCOMPARE(extractAppId(composite), QStringLiteral("firefox"));
    }

    void testBuildCompositeId_emptyAppId_preservesSeparator()
    {
        // An empty appId with a non-empty instance id must still produce a
        // composite that extractInstanceId can recover — otherwise a window
        // that briefly has no app class can't be disambiguated.
        const QString composite = buildCompositeId(QString(), QStringLiteral("uuid-1"));
        QCOMPARE(composite, QStringLiteral("|uuid-1"));
        QCOMPARE(extractInstanceId(composite), QStringLiteral("uuid-1"));
    }

    // =====================================================================
    // appIdMatches — segment-aware pattern matching used by Layout::matchAppRule
    //
    // The semantics here are subtle enough that the inline header comment
    // is the spec; these tests pin it so future refactors can't quietly
    // widen or narrow the match.
    // =====================================================================

    void testAppIdMatches_exactIgnoresCase()
    {
        QVERIFY(appIdMatches(QStringLiteral("firefox"), QStringLiteral("firefox")));
        QVERIFY(appIdMatches(QStringLiteral("Firefox"), QStringLiteral("firefox")));
        QVERIFY(appIdMatches(QStringLiteral("firefox"), QStringLiteral("FIREFOX")));
    }

    void testAppIdMatches_trailingDotSegment()
    {
        // Reverse-DNS appId matched by bare last-segment pattern.
        QVERIFY(appIdMatches(QStringLiteral("org.mozilla.firefox"), QStringLiteral("firefox")));
        // And the reverse direction: bare appId matched by reverse-DNS pattern.
        QVERIFY(appIdMatches(QStringLiteral("firefox"), QStringLiteral("org.mozilla.firefox")));
    }

    void testAppIdMatches_rejectsShortPrefixCollision()
    {
        // The header's key invariant: 4-char "fire" must not match any form
        // of "firefox". Asymmetric gating (prefix candidate ≥ 5 chars) is
        // what enforces this.
        QVERIFY(!appIdMatches(QStringLiteral("fire"), QStringLiteral("firefox")));
        QVERIFY(!appIdMatches(QStringLiteral("firefox"), QStringLiteral("fire")));
        QVERIFY(!appIdMatches(QStringLiteral("org.mozilla.firefox"), QStringLiteral("fire")));
        QVERIFY(!appIdMatches(QStringLiteral("fire"), QStringLiteral("org.mozilla.firefox")));
    }

    void testAppIdMatches_lastSegmentPrefix()
    {
        // "systemsettings" is a 14-char prefix candidate, so it passes the
        // ≥5 gate and matches "org.kde.systemsettings5" via its last segment.
        QVERIFY(appIdMatches(QStringLiteral("org.kde.systemsettings5"), QStringLiteral("systemsettings")));
        // Reverse direction — bare appId prefixes the pattern's last segment.
        QVERIFY(appIdMatches(QStringLiteral("systemsettings"), QStringLiteral("org.kde.systemsettings5")));
    }

    void testAppIdMatches_emptyInputsReturnFalse()
    {
        QVERIFY(!appIdMatches(QString(), QStringLiteral("firefox")));
        QVERIFY(!appIdMatches(QStringLiteral("firefox"), QString()));
        QVERIFY(!appIdMatches(QString(), QString()));
    }

    void testAppIdMatches_noCrossSegmentMatch()
    {
        // "mozilla" is a middle segment, not a trailing one, so it must not
        // match — prevents rules scoped to "firefox" from catching "thunderbird"
        // just because both are under "org.mozilla".
        QVERIFY(!appIdMatches(QStringLiteral("org.mozilla.firefox"), QStringLiteral("mozilla")));
    }

    // =====================================================================
    // deriveShortName — icon / display-name helper
    // =====================================================================

    void testDeriveShortName_reverseDns()
    {
        QCOMPARE(deriveShortName(QStringLiteral("org.kde.dolphin")), QStringLiteral("dolphin"));
        QCOMPARE(deriveShortName(QStringLiteral("com.example.app")), QStringLiteral("app"));
    }

    void testDeriveShortName_bareName()
    {
        QCOMPARE(deriveShortName(QStringLiteral("firefox")), QStringLiteral("firefox"));
    }

    void testDeriveShortName_trailingDotStrippedBeforeSegmentExtraction()
    {
        // Documented behaviour: trailing dots are chopped first so the
        // segment lookup doesn't degenerate into returning the full string.
        QCOMPARE(deriveShortName(QStringLiteral("org.kde.foo.")), QStringLiteral("foo"));
    }

    void testDeriveShortName_emptyAndDotOnly()
    {
        QCOMPARE(deriveShortName(QString()), QString());
        QCOMPARE(deriveShortName(QStringLiteral("....")), QString());
    }
};

QTEST_MAIN(TestWindowIdentity)
#include "test_window_identity.moc"
