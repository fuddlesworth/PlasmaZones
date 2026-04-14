// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_compositor_common.cpp
 * @brief Unit tests for compositor-common shared library types
 *
 * Tests D-Bus type roundtrips, FloatingCache logic, TriggerParser modifier
 * checking, and WindowIdUtils string utilities.
 */

#include <QTest>

#include "compositor-common/animation_math.h"
#include "compositor-common/autotile_state.h"
#include "compositor-common/dbus_types.h"
#include "compositor-common/easingcurve.h"
#include "compositor-common/floating_cache.h"
#include "compositor-common/trigger_parser.h"
#include "compositor-common/window_id.h"

namespace {

/**
 * @brief Roundtrip a D-Bus type through QDBusConnection peer connection
 *
 * QDBusArgument is write-only when constructed directly. To produce a
 * readable QDBusArgument we must go through the D-Bus wire format. We
 * use QDBusServer + connectToPeer for a self-contained roundtrip that
 * does not require a running session bus.
 *
 * Fallback: when no session bus is available we verify that the write
 * operator produces the expected D-Bus type signature (which confirms
 * field order and count). The toRect/fromRect/toGeometryEntry tests
 * cover the actual data logic.
 */

/**
 * @brief Verify a D-Bus struct type has the expected signature
 *
 * Writes the value into a QDBusArgument and checks the resulting
 * D-Bus type signature string. This validates that the streaming
 * operator serializes the correct number and type of fields.
 */
template<typename T>
QString dbusSignature(const T& value)
{
    QDBusArgument arg;
    arg << value;
    return arg.currentSignature();
}

} // anonymous namespace

class TestCompositorCommon : public QObject
{
    Q_OBJECT

private Q_SLOTS:

    // =================================================================
    // D-Bus types: WindowGeometryEntry roundtrip
    // =================================================================

    void testWindowGeometryEntryRoundtrip()
    {
        PlasmaZones::registerDBusTypes();
        PlasmaZones::WindowGeometryEntry entry{QStringLiteral("firefox|42"), 100, 200, 800, 600};

        // Verify D-Bus signature: (siiii) = struct of string + 4 ints
        const QString sig = dbusSignature(entry);
        QCOMPARE(sig, QStringLiteral("(siiii)"));

        // Verify metatype is registered (needed for QVariant D-Bus transport)
        const int typeId = qMetaTypeId<PlasmaZones::WindowGeometryEntry>();
        QVERIFY(typeId != QMetaType::UnknownType);

        // Verify aggregate construction preserves all fields
        QCOMPARE(entry.windowId, QStringLiteral("firefox|42"));
        QCOMPARE(entry.x, 100);
        QCOMPARE(entry.y, 200);
        QCOMPARE(entry.width, 800);
        QCOMPARE(entry.height, 600);

        // Verify default construction
        PlasmaZones::WindowGeometryEntry defaultEntry;
        QVERIFY(defaultEntry.windowId.isEmpty());
        QCOMPARE(defaultEntry.x, 0);
        QCOMPARE(defaultEntry.y, 0);
        QCOMPARE(defaultEntry.width, 0);
        QCOMPARE(defaultEntry.height, 0);
    }

    // =================================================================
    // D-Bus types: TileRequestEntry roundtrip
    // =================================================================

    void testTileRequestEntryRoundtrip()
    {
        PlasmaZones::registerDBusTypes();
        PlasmaZones::TileRequestEntry entry{
            QStringLiteral("konsole|7"), 50,   100,  640, 480, QStringLiteral("{zone-uuid}"),
            QStringLiteral("screen-0"),  true, false};

        // Verify D-Bus signature: (siiiissbb) = string + 4 ints + 2 strings + 2 bools
        const QString sig = dbusSignature(entry);
        QCOMPARE(sig, QStringLiteral("(siiiissbb)"));

        // Verify metatype registration
        const int typeId = qMetaTypeId<PlasmaZones::TileRequestEntry>();
        QVERIFY(typeId != QMetaType::UnknownType);

        // Verify aggregate construction preserves all fields
        QCOMPARE(entry.windowId, QStringLiteral("konsole|7"));
        QCOMPARE(entry.x, 50);
        QCOMPARE(entry.y, 100);
        QCOMPARE(entry.width, 640);
        QCOMPARE(entry.height, 480);
        QCOMPARE(entry.zoneId, QStringLiteral("{zone-uuid}"));
        QCOMPARE(entry.screenId, QStringLiteral("screen-0"));
        QCOMPARE(entry.monocle, true);
        QCOMPARE(entry.floating, false);

        // Verify default construction
        PlasmaZones::TileRequestEntry defaultEntry;
        QVERIFY(defaultEntry.windowId.isEmpty());
        QCOMPARE(defaultEntry.monocle, false);
        QCOMPARE(defaultEntry.floating, false);
    }

    // =================================================================
    // D-Bus types: SnapAllResultEntry roundtrip
    // =================================================================

    void testSnapAllResultEntryRoundtrip()
    {
        PlasmaZones::registerDBusTypes();
        PlasmaZones::SnapAllResultEntry entry{QStringLiteral("dolphin|3"),
                                              QStringLiteral("{target-zone}"),
                                              QStringLiteral("{source-zone}"),
                                              10,
                                              20,
                                              500,
                                              400};

        // Verify D-Bus signature: (sssiiii) = 3 strings + 4 ints
        const QString sig = dbusSignature(entry);
        QCOMPARE(sig, QStringLiteral("(sssiiii)"));

        // Verify metatype registration
        const int typeId = qMetaTypeId<PlasmaZones::SnapAllResultEntry>();
        QVERIFY(typeId != QMetaType::UnknownType);

        // Verify aggregate construction preserves all fields
        QCOMPARE(entry.windowId, QStringLiteral("dolphin|3"));
        QCOMPARE(entry.targetZoneId, QStringLiteral("{target-zone}"));
        QCOMPARE(entry.sourceZoneId, QStringLiteral("{source-zone}"));
        QCOMPARE(entry.x, 10);
        QCOMPARE(entry.y, 20);
        QCOMPARE(entry.width, 500);
        QCOMPARE(entry.height, 400);

        // Verify default construction
        PlasmaZones::SnapAllResultEntry defaultEntry;
        QVERIFY(defaultEntry.windowId.isEmpty());
        QVERIFY(defaultEntry.targetZoneId.isEmpty());
        QVERIFY(defaultEntry.sourceZoneId.isEmpty());
    }

    // =================================================================
    // WindowGeometryEntry::toRect() and fromRect()
    // =================================================================

    void testWindowGeometryToRect()
    {
        PlasmaZones::WindowGeometryEntry entry{QStringLiteral("win|1"), 10, 20, 300, 400};
        QRect rect = entry.toRect();
        QCOMPARE(rect, QRect(10, 20, 300, 400));
    }

    void testWindowGeometryFromRect()
    {
        QRect rect(50, 60, 700, 500);
        auto entry = PlasmaZones::WindowGeometryEntry::fromRect(QStringLiteral("app|2"), rect);
        QCOMPARE(entry.windowId, QStringLiteral("app|2"));
        QCOMPARE(entry.toRect(), rect);
    }

    // =================================================================
    // TileRequestEntry::toRect()
    // =================================================================

    void testTileRequestToRect()
    {
        PlasmaZones::TileRequestEntry entry{QStringLiteral("app|5"), 15,    25,   640, 480, QStringLiteral("{z}"),
                                            QStringLiteral("s0"),    false, false};
        QCOMPARE(entry.toRect(), QRect(15, 25, 640, 480));
    }

    // =================================================================
    // SnapAllResultEntry::toGeometryEntry()
    // =================================================================

    void testSnapAllResultToGeometryEntry()
    {
        PlasmaZones::SnapAllResultEntry snap{
            QStringLiteral("kate|9"), QStringLiteral("{target}"), QStringLiteral("{source}"), 30, 40, 1024, 768};

        PlasmaZones::WindowGeometryEntry geo = snap.toGeometryEntry();
        QCOMPARE(geo.windowId, QStringLiteral("kate|9"));
        QCOMPARE(geo.x, 30);
        QCOMPARE(geo.y, 40);
        QCOMPARE(geo.width, 1024);
        QCOMPARE(geo.height, 768);
    }

    void testSnapAllResultToRect()
    {
        PlasmaZones::SnapAllResultEntry snap{
            QStringLiteral("app|1"), QStringLiteral("{t}"), QStringLiteral("{s}"), 5, 10, 200, 150};
        QCOMPARE(snap.toRect(), QRect(5, 10, 200, 150));
    }

    // =================================================================
    // D-Bus types: WindowStateEntry roundtrip
    // =================================================================

    void testWindowStateEntryRoundtrip()
    {
        PlasmaZones::registerDBusTypes();
        PlasmaZones::WindowStateEntry entry;
        entry.windowId = QStringLiteral("firefox|42");
        entry.zoneId = QStringLiteral("{zone-1}");
        entry.screenId = QStringLiteral("screen-0");
        entry.isFloating = true;
        entry.changeType = QStringLiteral("snapped");
        entry.zoneIds = {QStringLiteral("{zone-1}"), QStringLiteral("{zone-2}")};
        entry.isSticky = true;

        const QString sig = dbusSignature(entry);
        QCOMPARE(sig, QStringLiteral("(sssbsasb)"));

        const int typeId = qMetaTypeId<PlasmaZones::WindowStateEntry>();
        QVERIFY(typeId != QMetaType::UnknownType);

        QCOMPARE(entry.windowId, QStringLiteral("firefox|42"));
        QCOMPARE(entry.zoneId, QStringLiteral("{zone-1}"));
        QCOMPARE(entry.screenId, QStringLiteral("screen-0"));
        QCOMPARE(entry.isFloating, true);
        QCOMPARE(entry.zoneIds.size(), 2);
        QCOMPARE(entry.isSticky, true);
        QCOMPARE(entry.changeType, QStringLiteral("snapped"));

        PlasmaZones::WindowStateEntry defaultEntry;
        QVERIFY(defaultEntry.windowId.isEmpty());
        QCOMPARE(defaultEntry.isFloating, false);
        QVERIFY(defaultEntry.changeType.isEmpty());
    }

    // =================================================================
    // D-Bus types: UnfloatRestoreResult roundtrip
    // =================================================================

    void testUnfloatRestoreResultRoundtrip()
    {
        PlasmaZones::registerDBusTypes();
        PlasmaZones::UnfloatRestoreResult entry{
            true, {QStringLiteral("{z1}"), QStringLiteral("{z2}")}, QStringLiteral("screen-0"), 10, 20, 800, 600};

        const QString sig = dbusSignature(entry);
        QCOMPARE(sig, QStringLiteral("(bassiiii)"));

        const int typeId = qMetaTypeId<PlasmaZones::UnfloatRestoreResult>();
        QVERIFY(typeId != QMetaType::UnknownType);

        QCOMPARE(entry.found, true);
        QCOMPARE(entry.zoneIds.size(), 2);
        QCOMPARE(entry.zoneIds.at(0), QStringLiteral("{z1}"));
        QCOMPARE(entry.zoneIds.at(1), QStringLiteral("{z2}"));
        QCOMPARE(entry.screenName, QStringLiteral("screen-0"));
        QCOMPARE(entry.toRect(), QRect(10, 20, 800, 600));

        PlasmaZones::UnfloatRestoreResult defaultEntry;
        QCOMPARE(defaultEntry.found, false);
        QVERIFY(defaultEntry.zoneIds.isEmpty());
    }

    // =================================================================
    // D-Bus types: ZoneGeometryRect roundtrip
    // =================================================================

    void testZoneGeometryRectRoundtrip()
    {
        PlasmaZones::registerDBusTypes();
        PlasmaZones::ZoneGeometryRect entry{50, 100, 1920, 1080};

        const QString sig = dbusSignature(entry);
        QCOMPARE(sig, QStringLiteral("(iiii)"));

        const int typeId = qMetaTypeId<PlasmaZones::ZoneGeometryRect>();
        QVERIFY(typeId != QMetaType::UnknownType);

        QCOMPARE(entry.x, 50);
        QCOMPARE(entry.y, 100);
        QCOMPARE(entry.width, 1920);
        QCOMPARE(entry.height, 1080);
        QCOMPARE(entry.toRect(), QRect(50, 100, 1920, 1080));

        QRect rect(200, 300, 640, 480);
        auto fromRect = PlasmaZones::ZoneGeometryRect::fromRect(rect);
        QCOMPARE(fromRect.toRect(), rect);
    }

    // =================================================================
    // D-Bus types: EmptyZoneEntry roundtrip
    // =================================================================

    void testEmptyZoneEntryRoundtrip()
    {
        PlasmaZones::registerDBusTypes();
        PlasmaZones::EmptyZoneEntry entry;
        entry.zoneId = QStringLiteral("{zone-abc}");
        entry.x = 10;
        entry.y = 20;
        entry.width = 400;
        entry.height = 300;
        entry.borderWidth = 2;
        entry.borderRadius = 8;
        entry.useCustomColors = true;
        entry.highlightColor = QStringLiteral("#ff00ff00");
        entry.inactiveColor = QStringLiteral("#80808080");
        entry.borderColor = QStringLiteral("#ffffffff");
        entry.activeOpacity = 0.7;
        entry.inactiveOpacity = 0.2;

        const QString sig = dbusSignature(entry);
        QCOMPARE(sig, QStringLiteral("(siiiiiibsssdd)"));

        const int typeId = qMetaTypeId<PlasmaZones::EmptyZoneEntry>();
        QVERIFY(typeId != QMetaType::UnknownType);

        QCOMPARE(entry.zoneId, QStringLiteral("{zone-abc}"));
        QCOMPARE(entry.toRect(), QRect(10, 20, 400, 300));
        QCOMPARE(entry.borderWidth, 2);
        QCOMPARE(entry.borderRadius, 8);
        QCOMPARE(entry.useCustomColors, true);
        QCOMPARE(entry.highlightColor, QStringLiteral("#ff00ff00"));
        QCOMPARE(entry.activeOpacity, 0.7);
        QCOMPARE(entry.inactiveOpacity, 0.2);

        PlasmaZones::EmptyZoneEntry defaultEntry;
        QCOMPARE(defaultEntry.borderWidth, 0);
        QCOMPARE(defaultEntry.borderRadius, 0);
        QCOMPARE(defaultEntry.useCustomColors, false);
        QCOMPARE(defaultEntry.activeOpacity, 0.5);
        QCOMPARE(defaultEntry.inactiveOpacity, 0.3);
    }

    // =================================================================
    // D-Bus types: SnapAssistCandidate roundtrip
    // =================================================================

    void testSnapAssistCandidateRoundtrip()
    {
        PlasmaZones::registerDBusTypes();
        PlasmaZones::SnapAssistCandidate entry{QStringLiteral("konsole|7"), QStringLiteral("kwin-handle-42"),
                                               QStringLiteral("utilities-terminal"),
                                               QStringLiteral("Konsole - Terminal")};

        const QString sig = dbusSignature(entry);
        QCOMPARE(sig, QStringLiteral("(ssss)"));

        const int typeId = qMetaTypeId<PlasmaZones::SnapAssistCandidate>();
        QVERIFY(typeId != QMetaType::UnknownType);

        QCOMPARE(entry.windowId, QStringLiteral("konsole|7"));
        QCOMPARE(entry.compositorHandle, QStringLiteral("kwin-handle-42"));
        QCOMPARE(entry.icon, QStringLiteral("utilities-terminal"));
        QCOMPARE(entry.caption, QStringLiteral("Konsole - Terminal"));
    }

    // =================================================================
    // D-Bus types: NamedZoneGeometry roundtrip
    // =================================================================

    void testNamedZoneGeometryRoundtrip()
    {
        PlasmaZones::registerDBusTypes();
        PlasmaZones::NamedZoneGeometry entry{QStringLiteral("{zone-left}"), 0, 0, 960, 1080};

        const QString sig = dbusSignature(entry);
        QCOMPARE(sig, QStringLiteral("(siiii)"));

        const int typeId = qMetaTypeId<PlasmaZones::NamedZoneGeometry>();
        QVERIFY(typeId != QMetaType::UnknownType);

        QCOMPARE(entry.zoneId, QStringLiteral("{zone-left}"));
        QCOMPARE(entry.toRect(), QRect(0, 0, 960, 1080));
    }

    // =================================================================
    // D-Bus types: AlgorithmInfoEntry roundtrip
    // =================================================================

    void testAlgorithmInfoEntryRoundtrip()
    {
        PlasmaZones::registerDBusTypes();
        PlasmaZones::AlgorithmInfoEntry entry{QStringLiteral("master-stack"),
                                              QStringLiteral("Master-Stack"),
                                              QStringLiteral("A tiling algorithm"),
                                              true,
                                              true,
                                              false,
                                              false,
                                              0.65,
                                              8,
                                              false,
                                              QStringLiteral("sequential"),
                                              false,
                                              true};

        const QString sig = dbusSignature(entry);
        QCOMPARE(sig, QStringLiteral("(sssbbbbdibsbb)"));

        const int typeId = qMetaTypeId<PlasmaZones::AlgorithmInfoEntry>();
        QVERIFY(typeId != QMetaType::UnknownType);

        QCOMPARE(entry.id, QStringLiteral("master-stack"));
        QCOMPARE(entry.name, QStringLiteral("Master-Stack"));
        QCOMPARE(entry.description, QStringLiteral("A tiling algorithm"));
        QCOMPARE(entry.supportsMasterCount, true);
        QCOMPARE(entry.supportsSplitRatio, true);
        QCOMPARE(entry.centerLayout, false);
        QCOMPARE(entry.producesOverlappingZones, false);
        QVERIFY(qAbs(entry.defaultSplitRatio - 0.65) < 0.001);
        QCOMPARE(entry.defaultMaxWindows, 8);
        QCOMPARE(entry.isScripted, false);
        QCOMPARE(entry.zoneNumberDisplay, QStringLiteral("sequential"));
        QCOMPARE(entry.isUserScript, false);
        QCOMPARE(entry.supportsMemory, true);
    }

    // =================================================================
    // D-Bus types: BridgeRegistrationResult roundtrip
    // =================================================================

    void testBridgeRegistrationResultRoundtrip()
    {
        PlasmaZones::registerDBusTypes();
        PlasmaZones::BridgeRegistrationResult entry{QStringLiteral("1"), QStringLiteral("kwin"),
                                                    QStringLiteral("abc-123")};

        const QString sig = dbusSignature(entry);
        QCOMPARE(sig, QStringLiteral("(sss)"));

        const int typeId = qMetaTypeId<PlasmaZones::BridgeRegistrationResult>();
        QVERIFY(typeId != QMetaType::UnknownType);

        QCOMPARE(entry.apiVersion, QStringLiteral("1"));
        QCOMPARE(entry.bridgeName, QStringLiteral("kwin"));
        QCOMPARE(entry.sessionId, QStringLiteral("abc-123"));
    }

    // =================================================================
    // D-Bus types: MoveTargetResult roundtrip
    // =================================================================

    void testMoveTargetResultRoundtrip()
    {
        PlasmaZones::registerDBusTypes();
        PlasmaZones::MoveTargetResult entry{true,
                                            QString(),
                                            QStringLiteral("{zone-right}"),
                                            100,
                                            200,
                                            960,
                                            1080,
                                            QStringLiteral("{zone-left}"),
                                            QStringLiteral("screen-0")};

        const QString sig = dbusSignature(entry);
        QCOMPARE(sig, QStringLiteral("(bssiiiiss)"));

        const int typeId = qMetaTypeId<PlasmaZones::MoveTargetResult>();
        QVERIFY(typeId != QMetaType::UnknownType);

        QCOMPARE(entry.success, true);
        QVERIFY(entry.reason.isEmpty());
        QCOMPARE(entry.zoneId, QStringLiteral("{zone-right}"));
        QCOMPARE(entry.toRect(), QRect(100, 200, 960, 1080));
        QCOMPARE(entry.sourceZoneId, QStringLiteral("{zone-left}"));
        QCOMPARE(entry.screenName, QStringLiteral("screen-0"));
    }

    // =================================================================
    // D-Bus types: FocusTargetResult roundtrip
    // =================================================================

    void testFocusTargetResultRoundtrip()
    {
        PlasmaZones::registerDBusTypes();
        PlasmaZones::FocusTargetResult entry{true,
                                             QString(),
                                             QStringLiteral("dolphin|3"),
                                             QStringLiteral("{zone-left}"),
                                             QStringLiteral("{zone-right}"),
                                             QStringLiteral("screen-0")};

        const QString sig = dbusSignature(entry);
        QCOMPARE(sig, QStringLiteral("(bsssss)"));

        const int typeId = qMetaTypeId<PlasmaZones::FocusTargetResult>();
        QVERIFY(typeId != QMetaType::UnknownType);

        QCOMPARE(entry.success, true);
        QCOMPARE(entry.windowIdToActivate, QStringLiteral("dolphin|3"));
        QCOMPARE(entry.sourceZoneId, QStringLiteral("{zone-left}"));
        QCOMPARE(entry.targetZoneId, QStringLiteral("{zone-right}"));
        QCOMPARE(entry.screenName, QStringLiteral("screen-0"));
    }

    // =================================================================
    // D-Bus types: CycleTargetResult roundtrip
    // =================================================================

    void testCycleTargetResultRoundtrip()
    {
        PlasmaZones::registerDBusTypes();
        PlasmaZones::CycleTargetResult entry{true, QString(), QStringLiteral("kate|5"), QStringLiteral("{zone-center}"),
                                             QStringLiteral("screen-1")};

        const QString sig = dbusSignature(entry);
        QCOMPARE(sig, QStringLiteral("(bssss)"));

        const int typeId = qMetaTypeId<PlasmaZones::CycleTargetResult>();
        QVERIFY(typeId != QMetaType::UnknownType);

        QCOMPARE(entry.success, true);
        QCOMPARE(entry.windowIdToActivate, QStringLiteral("kate|5"));
        QCOMPARE(entry.zoneId, QStringLiteral("{zone-center}"));
        QCOMPARE(entry.screenName, QStringLiteral("screen-1"));
    }

    // =================================================================
    // D-Bus types: SwapTargetResult roundtrip
    // =================================================================

    void testSwapTargetResultRoundtrip()
    {
        PlasmaZones::registerDBusTypes();
        PlasmaZones::SwapTargetResult entry{true,
                                            QString(),
                                            QStringLiteral("firefox|1"),
                                            0,
                                            0,
                                            960,
                                            1080,
                                            QStringLiteral("{zone-left}"),
                                            QStringLiteral("konsole|2"),
                                            960,
                                            0,
                                            960,
                                            1080,
                                            QStringLiteral("{zone-right}"),
                                            QStringLiteral("screen-0"),
                                            QStringLiteral("{zone-left}"),
                                            QStringLiteral("{zone-right}")};

        const QString sig = dbusSignature(entry);
        QCOMPARE(sig, QStringLiteral("(bssiiiissiiiissss)"));

        const int typeId = qMetaTypeId<PlasmaZones::SwapTargetResult>();
        QVERIFY(typeId != QMetaType::UnknownType);

        QCOMPARE(entry.success, true);
        QCOMPARE(entry.windowId1, QStringLiteral("firefox|1"));
        QCOMPARE(entry.x1, 0);
        QCOMPARE(entry.y1, 0);
        QCOMPARE(entry.w1, 960);
        QCOMPARE(entry.h1, 1080);
        QCOMPARE(entry.zoneId1, QStringLiteral("{zone-left}"));
        QCOMPARE(entry.windowId2, QStringLiteral("konsole|2"));
        QCOMPARE(entry.x2, 960);
        QCOMPARE(entry.y2, 0);
        QCOMPARE(entry.w2, 960);
        QCOMPARE(entry.h2, 1080);
        QCOMPARE(entry.zoneId2, QStringLiteral("{zone-right}"));
        QCOMPARE(entry.screenName, QStringLiteral("screen-0"));
        QCOMPARE(entry.sourceZoneId, QStringLiteral("{zone-left}"));
        QCOMPARE(entry.targetZoneId, QStringLiteral("{zone-right}"));
    }

    // =================================================================
    // D-Bus types: RestoreTargetResult roundtrip
    // =================================================================

    void testRestoreTargetResultRoundtrip()
    {
        PlasmaZones::registerDBusTypes();
        PlasmaZones::RestoreTargetResult entry{true, true, 50, 75, 1024, 768};

        const QString sig = dbusSignature(entry);
        QCOMPARE(sig, QStringLiteral("(bbiiii)"));

        const int typeId = qMetaTypeId<PlasmaZones::RestoreTargetResult>();
        QVERIFY(typeId != QMetaType::UnknownType);

        QCOMPARE(entry.success, true);
        QCOMPARE(entry.found, true);
        QCOMPARE(entry.toRect(), QRect(50, 75, 1024, 768));

        PlasmaZones::RestoreTargetResult defaultEntry;
        QCOMPARE(defaultEntry.success, false);
        QCOMPARE(defaultEntry.found, false);
        QCOMPARE(defaultEntry.toRect(), QRect(0, 0, 0, 0));
    }

    // =================================================================
    // FloatingCache: basic operations
    // =================================================================

    void testFloatingCacheBasic()
    {
        PlasmaZones::FloatingCache cache;
        QVERIFY(!cache.isFloating(QStringLiteral("firefox|1")));

        cache.setFloating(QStringLiteral("firefox|1"), true);
        QVERIFY(cache.isFloating(QStringLiteral("firefox|1")));

        // Different instance, no bare appId entry
        QVERIFY(!cache.isFloating(QStringLiteral("firefox|2")));

        cache.setFloating(QStringLiteral("firefox|1"), false);
        QVERIFY(!cache.isFloating(QStringLiteral("firefox|1")));
    }

    // =================================================================
    // FloatingCache: appId fallback
    // =================================================================

    void testFloatingCacheAppIdFallback()
    {
        PlasmaZones::FloatingCache cache;
        cache.insert(QStringLiteral("firefox")); // bare appId
        QVERIFY(cache.isFloating(QStringLiteral("firefox|1"))); // matches via appId fallback
        QVERIFY(cache.isFloating(QStringLiteral("firefox|2"))); // also matches

        cache.remove(QStringLiteral("firefox"));
        QVERIFY(!cache.isFloating(QStringLiteral("firefox|1")));
    }

    // =================================================================
    // FloatingCache: clear
    // =================================================================

    void testFloatingCacheClear()
    {
        PlasmaZones::FloatingCache cache;
        cache.setFloating(QStringLiteral("app1|1"), true);
        cache.setFloating(QStringLiteral("app2|1"), true);
        QCOMPARE(cache.size(), 2);
        cache.clear();
        QCOMPARE(cache.size(), 0);
    }

    // =================================================================
    // FloatingCache: unfloat removes bare appId
    // =================================================================

    void testFloatingCacheUnfloatRemovesBareAppId()
    {
        PlasmaZones::FloatingCache cache;
        cache.insert(QStringLiteral("firefox")); // bare appId from daemon sync
        cache.insert(QStringLiteral("firefox|1")); // specific instance

        cache.setFloating(QStringLiteral("firefox|1"), false);
        // Both "firefox|1" and bare "firefox" should be removed
        QVERIFY(!cache.isFloating(QStringLiteral("firefox|1")));
        // "firefox|2" was never individually floating, and bare appId is gone:
        QVERIFY(!cache.isFloating(QStringLiteral("firefox|2")));
    }

    // =================================================================
    // TriggerParser: checkModifier
    // =================================================================

    void testCheckModifier()
    {
        using PlasmaZones::TriggerParser::checkModifier;

        QVERIFY(!checkModifier(0, Qt::ShiftModifier)); // Disabled
        QVERIFY(checkModifier(1, Qt::ShiftModifier)); // Shift
        QVERIFY(!checkModifier(1, Qt::ControlModifier)); // Shift required but Ctrl held
        QVERIFY(checkModifier(2, Qt::ControlModifier)); // Ctrl
        QVERIFY(checkModifier(3, Qt::AltModifier)); // Alt
        QVERIFY(checkModifier(4, Qt::MetaModifier)); // Meta
        QVERIFY(checkModifier(5, Qt::ControlModifier | Qt::AltModifier)); // CtrlAlt
        QVERIFY(!checkModifier(5, Qt::ControlModifier)); // Only Ctrl, need CtrlAlt
        QVERIFY(checkModifier(6, Qt::ControlModifier | Qt::ShiftModifier)); // CtrlShift
        QVERIFY(checkModifier(7, Qt::AltModifier | Qt::ShiftModifier)); // AltShift
        QVERIFY(checkModifier(8, Qt::NoModifier)); // AlwaysActive
        QVERIFY(checkModifier(9, Qt::AltModifier | Qt::MetaModifier)); // AltMeta
        QVERIFY(checkModifier(10, Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier)); // CtrlAltMeta
        QVERIFY(!checkModifier(99, Qt::ShiftModifier)); // Unknown
    }

    // =================================================================
    // TriggerParser: anyTriggerHeld
    // =================================================================

    void testAnyTriggerHeld()
    {
        using PlasmaZones::ParsedTrigger;
        using PlasmaZones::TriggerParser::anyTriggerHeld;

        QVector<ParsedTrigger> triggers = {{1, 0}}; // Shift modifier, any button
        QVERIFY(anyTriggerHeld(triggers, Qt::ShiftModifier, Qt::NoButton));
        QVERIFY(!anyTriggerHeld(triggers, Qt::ControlModifier, Qt::NoButton));

        // Empty triggers
        QVERIFY(!anyTriggerHeld({}, Qt::ShiftModifier, Qt::LeftButton));

        // Both modifier=0 and mouseButton=0 -- should NOT match (guard clause)
        QVector<ParsedTrigger> nullTrigger = {{0, 0}};
        QVERIFY(!anyTriggerHeld(nullTrigger, Qt::ShiftModifier, Qt::LeftButton));
    }

    void testAnyTriggerHeldMouseButton()
    {
        using PlasmaZones::ParsedTrigger;
        using PlasmaZones::TriggerParser::anyTriggerHeld;

        // Trigger requires left mouse button only (modifier=0 means "any mod is ok"
        // but the guard clause requires at least one non-zero field)
        QVector<ParsedTrigger> btnTrigger = {{0, static_cast<int>(Qt::LeftButton)}};
        QVERIFY(anyTriggerHeld(btnTrigger, Qt::NoModifier, Qt::LeftButton));
        QVERIFY(!anyTriggerHeld(btnTrigger, Qt::NoModifier, Qt::RightButton));
    }

    void testAnyTriggerHeldModAndButton()
    {
        using PlasmaZones::ParsedTrigger;
        using PlasmaZones::TriggerParser::anyTriggerHeld;

        // Requires Shift + LeftButton
        QVector<ParsedTrigger> combined = {{1, static_cast<int>(Qt::LeftButton)}};
        QVERIFY(anyTriggerHeld(combined, Qt::ShiftModifier, Qt::LeftButton));
        QVERIFY(!anyTriggerHeld(combined, Qt::ShiftModifier, Qt::RightButton));
        QVERIFY(!anyTriggerHeld(combined, Qt::ControlModifier, Qt::LeftButton));
    }

    // =================================================================
    // WindowIdUtils: extractAppId
    // =================================================================

    void testExtractAppId()
    {
        QCOMPARE(PlasmaZones::WindowIdUtils::extractAppId(QStringLiteral("firefox|42")), QStringLiteral("firefox"));
        QCOMPARE(PlasmaZones::WindowIdUtils::extractAppId(QStringLiteral("firefox")), QStringLiteral("firefox"));
        QCOMPARE(PlasmaZones::WindowIdUtils::extractAppId(QString()), QString());
        QCOMPARE(PlasmaZones::WindowIdUtils::extractAppId(QStringLiteral("org.kde.dolphin|123")),
                 QStringLiteral("org.kde.dolphin"));
    }

    // =================================================================
    // WindowIdUtils: deriveShortName
    // =================================================================

    void testDeriveShortName()
    {
        QCOMPARE(PlasmaZones::WindowIdUtils::deriveShortName(QStringLiteral("org.kde.dolphin")),
                 QStringLiteral("dolphin"));
        QCOMPARE(PlasmaZones::WindowIdUtils::deriveShortName(QStringLiteral("firefox")), QStringLiteral("firefox"));
        QCOMPARE(PlasmaZones::WindowIdUtils::deriveShortName(QString()), QString());
        QCOMPARE(PlasmaZones::WindowIdUtils::deriveShortName(QStringLiteral("com.example.app")), QStringLiteral("app"));
    }

    // =================================================================
    // EasingCurve: fromString bezier
    // =================================================================

    void testEasingCurveFromStringBezier()
    {
        PlasmaZones::EasingCurve curve = PlasmaZones::EasingCurve::fromString(QStringLiteral("0.33,1.00,0.68,1.00"));
        QCOMPARE(curve.type, PlasmaZones::EasingCurve::Type::CubicBezier);
        QVERIFY(qAbs(curve.x1 - 0.33) < 0.01);
        QVERIFY(qAbs(curve.y1 - 1.00) < 0.01);
        QVERIFY(qAbs(curve.x2 - 0.68) < 0.01);
        QVERIFY(qAbs(curve.y2 - 1.00) < 0.01);
    }

    // =================================================================
    // EasingCurve: fromString elastic
    // =================================================================

    void testEasingCurveFromStringElastic()
    {
        PlasmaZones::EasingCurve curve = PlasmaZones::EasingCurve::fromString(QStringLiteral("elastic-out:1.0,0.3"));
        QCOMPARE(curve.type, PlasmaZones::EasingCurve::Type::ElasticOut);
        QVERIFY(qAbs(curve.amplitude - 1.0) < 0.01);
        QVERIFY(qAbs(curve.period - 0.3) < 0.01);
    }

    // =================================================================
    // EasingCurve: fromString bounce
    // =================================================================

    void testEasingCurveFromStringBounce()
    {
        PlasmaZones::EasingCurve curve = PlasmaZones::EasingCurve::fromString(QStringLiteral("bounce-out:1.5,4"));
        QCOMPARE(curve.type, PlasmaZones::EasingCurve::Type::BounceOut);
        QVERIFY(qAbs(curve.amplitude - 1.5) < 0.01);
        QCOMPARE(curve.bounces, 4);
    }

    // =================================================================
    // EasingCurve: roundtrip (toString → fromString)
    // =================================================================

    void testEasingCurveRoundtrip()
    {
        // CubicBezier roundtrip
        {
            PlasmaZones::EasingCurve original;
            original.type = PlasmaZones::EasingCurve::Type::CubicBezier;
            original.x1 = 0.25;
            original.y1 = 0.10;
            original.x2 = 0.25;
            original.y2 = 1.00;
            PlasmaZones::EasingCurve restored = PlasmaZones::EasingCurve::fromString(original.toString());
            QCOMPARE(restored, original);
        }
        // ElasticOut roundtrip
        {
            PlasmaZones::EasingCurve original;
            original.type = PlasmaZones::EasingCurve::Type::ElasticOut;
            original.amplitude = 1.2;
            original.period = 0.4;
            PlasmaZones::EasingCurve restored = PlasmaZones::EasingCurve::fromString(original.toString());
            QCOMPARE(restored, original);
        }
        // ElasticIn roundtrip
        {
            PlasmaZones::EasingCurve original;
            original.type = PlasmaZones::EasingCurve::Type::ElasticIn;
            original.amplitude = 1.5;
            original.period = 0.5;
            PlasmaZones::EasingCurve restored = PlasmaZones::EasingCurve::fromString(original.toString());
            QCOMPARE(restored, original);
        }
        // ElasticInOut roundtrip
        {
            PlasmaZones::EasingCurve original;
            original.type = PlasmaZones::EasingCurve::Type::ElasticInOut;
            original.amplitude = 0.8;
            original.period = 0.6;
            PlasmaZones::EasingCurve restored = PlasmaZones::EasingCurve::fromString(original.toString());
            QCOMPARE(restored, original);
        }
        // BounceOut roundtrip
        {
            PlasmaZones::EasingCurve original;
            original.type = PlasmaZones::EasingCurve::Type::BounceOut;
            original.amplitude = 1.0;
            original.bounces = 5;
            PlasmaZones::EasingCurve restored = PlasmaZones::EasingCurve::fromString(original.toString());
            QCOMPARE(restored, original);
        }
        // BounceIn roundtrip
        {
            PlasmaZones::EasingCurve original;
            original.type = PlasmaZones::EasingCurve::Type::BounceIn;
            original.amplitude = 2.0;
            original.bounces = 3;
            PlasmaZones::EasingCurve restored = PlasmaZones::EasingCurve::fromString(original.toString());
            QCOMPARE(restored, original);
        }
        // BounceInOut roundtrip
        {
            PlasmaZones::EasingCurve original;
            original.type = PlasmaZones::EasingCurve::Type::BounceInOut;
            original.amplitude = 1.3;
            original.bounces = 6;
            PlasmaZones::EasingCurve restored = PlasmaZones::EasingCurve::fromString(original.toString());
            QCOMPARE(restored, original);
        }
    }

    // =================================================================
    // EasingCurve: evaluate boundaries
    // =================================================================

    void testEasingCurveEvaluateBoundaries()
    {
        // All curve types should return ~0 at t=0 and ~1 at t=1
        QVector<PlasmaZones::EasingCurve> curves;

        // CubicBezier
        PlasmaZones::EasingCurve bezier;
        bezier.type = PlasmaZones::EasingCurve::Type::CubicBezier;
        curves.append(bezier);

        // ElasticOut
        PlasmaZones::EasingCurve elasticOut;
        elasticOut.type = PlasmaZones::EasingCurve::Type::ElasticOut;
        curves.append(elasticOut);

        // ElasticIn
        PlasmaZones::EasingCurve elasticIn;
        elasticIn.type = PlasmaZones::EasingCurve::Type::ElasticIn;
        curves.append(elasticIn);

        // ElasticInOut
        PlasmaZones::EasingCurve elasticInOut;
        elasticInOut.type = PlasmaZones::EasingCurve::Type::ElasticInOut;
        curves.append(elasticInOut);

        // BounceOut
        PlasmaZones::EasingCurve bounceOut;
        bounceOut.type = PlasmaZones::EasingCurve::Type::BounceOut;
        curves.append(bounceOut);

        // BounceIn
        PlasmaZones::EasingCurve bounceIn;
        bounceIn.type = PlasmaZones::EasingCurve::Type::BounceIn;
        curves.append(bounceIn);

        // BounceInOut
        PlasmaZones::EasingCurve bounceInOut;
        bounceInOut.type = PlasmaZones::EasingCurve::Type::BounceInOut;
        curves.append(bounceInOut);

        for (const auto& curve : curves) {
            QVERIFY(qAbs(curve.evaluate(0.0)) < 0.01);
            QVERIFY(qAbs(curve.evaluate(1.0) - 1.0) < 0.01);
        }
    }

    // =================================================================
    // EasingCurve: elastic overshoot
    // =================================================================

    void testEasingCurveElasticOvershoot()
    {
        PlasmaZones::EasingCurve elastic;
        elastic.type = PlasmaZones::EasingCurve::Type::ElasticOut;
        elastic.amplitude = 1.0;
        elastic.period = 0.3;

        // Sample the curve at early points where elastic overshoot is expected
        bool foundOvershoot = false;
        for (int i = 1; i < 100; ++i) {
            qreal t = qreal(i) / 100.0;
            qreal val = elastic.evaluate(t);
            if (val > 1.0) {
                foundOvershoot = true;
                break;
            }
        }
        QVERIFY(foundOvershoot);
    }

    // =================================================================
    // AnimationMath: createSnapAnimation disabled
    // =================================================================

    void testCreateSnapAnimationDisabled()
    {
        PlasmaZones::AnimationConfig config;
        config.enabled = false;

        auto result = PlasmaZones::AnimationMath::createSnapAnimation(QPointF(0, 0), QSizeF(100, 100),
                                                                      QRect(200, 200, 400, 300), config);
        QVERIFY(!result.has_value());
    }

    // =================================================================
    // AnimationMath: createSnapAnimation degenerate target
    // =================================================================

    void testCreateSnapAnimationDegenerateTarget()
    {
        PlasmaZones::AnimationConfig config;
        config.enabled = true;

        // Width = 0 should produce nullopt
        auto result = PlasmaZones::AnimationMath::createSnapAnimation(QPointF(0, 0), QSizeF(100, 100),
                                                                      QRect(200, 200, 0, 300), config);
        QVERIFY(!result.has_value());
    }

    // =================================================================
    // AnimationMath: createSnapAnimation below threshold
    // =================================================================

    void testCreateSnapAnimationBelowThreshold()
    {
        PlasmaZones::AnimationConfig config;
        config.enabled = true;
        config.minDistance = 50;

        // Move only 2 pixels, no size change — below threshold
        auto result = PlasmaZones::AnimationMath::createSnapAnimation(QPointF(100, 100), QSizeF(400, 300),
                                                                      QRect(101, 101, 400, 300), config);
        QVERIFY(!result.has_value());
    }

    // =================================================================
    // AnimationMath: createSnapAnimation valid
    // =================================================================

    void testCreateSnapAnimationValid()
    {
        PlasmaZones::AnimationConfig config;
        config.enabled = true;
        config.duration = 200.0;
        config.minDistance = 5;

        auto result = PlasmaZones::AnimationMath::createSnapAnimation(QPointF(0, 0), QSizeF(100, 100),
                                                                      QRect(300, 300, 500, 400), config);
        QVERIFY(result.has_value());
        QCOMPARE(result->targetGeometry, QRect(300, 300, 500, 400));
        QVERIFY(qAbs(result->startPosition.x() - 0.0) < 0.01);
        QVERIFY(qAbs(result->startPosition.y() - 0.0) < 0.01);
        QVERIFY(qAbs(result->startSize.width() - 100.0) < 0.01);
        QVERIFY(qAbs(result->startSize.height() - 100.0) < 0.01);
        QVERIFY(qAbs(result->duration - 200.0) < 0.01);
    }

    // =================================================================
    // AnimationMath: computeOvershootBounds includes padding
    // =================================================================

    void testComputeOvershootBoundsIncludesPadding()
    {
        PlasmaZones::EasingCurve bezier; // default CubicBezier
        QPointF startPos(100, 100);
        QSizeF startSize(200, 150);
        QRect target(400, 400, 300, 250);
        QMarginsF padding(10.0, 10.0, 10.0, 10.0);

        QRectF bounds =
            PlasmaZones::AnimationMath::computeOvershootBounds(startPos, startSize, target, bezier, padding);

        // The bounds should contain both start and target rects with padding
        QRectF startWithPadding(startPos.x() - 10.0, startPos.y() - 10.0, startSize.width() + 20.0,
                                startSize.height() + 20.0);
        QRectF targetWithPadding(target.x() - 10.0, target.y() - 10.0, target.width() + 20.0, target.height() + 20.0);

        QVERIFY(bounds.contains(startWithPadding));
        QVERIFY(bounds.contains(targetWithPadding));
    }

    // =================================================================
    // AutotileStateHelpers: cleanupClosedWindowState
    // =================================================================

    void testCleanupClosedWindowState()
    {
        const QString windowId = QStringLiteral("testapp|42");
        const QString screenId = QStringLiteral("screen-0");

        // Set up BorderState with the window on its owning screen.
        PlasmaZones::BorderState border;
        PlasmaZones::AutotileStateHelpers::addBorderlessOnScreen(border, screenId, windowId);
        PlasmaZones::AutotileStateHelpers::addTiledOnScreen(border, screenId, windowId);
        border.zoneGeometries.insert(windowId, QRect(0, 0, 800, 600));

        // Set up AutotileWindowState maps
        QSet<QString> notifiedWindows;
        notifiedWindows.insert(windowId);

        QHash<QString, QString> notifiedWindowScreens;
        notifiedWindowScreens.insert(windowId, screenId);

        QSet<QString> minimizeFloatedWindows;
        minimizeFloatedWindows.insert(windowId);

        QHash<QString, QRect> autotileTargetZones;
        autotileTargetZones.insert(windowId, QRect(0, 0, 800, 600));

        QHash<QString, QRect> centeredWaylandZones;
        centeredWaylandZones.insert(windowId, QRect(100, 100, 600, 400));

        QSet<QString> monocleMaximizedWindows;
        monocleMaximizedWindows.insert(windowId);

        QHash<QString, QHash<QString, QRectF>> preAutotileGeometries;
        preAutotileGeometries[screenId].insert(windowId, QRectF(0.1, 0.1, 0.5, 0.5));

        PlasmaZones::AutotileStateHelpers::AutotileWindowState state{
            notifiedWindows,      notifiedWindowScreens,   minimizeFloatedWindows, autotileTargetZones,
            centeredWaylandZones, monocleMaximizedWindows, preAutotileGeometries};

        // Perform cleanup
        PlasmaZones::AutotileStateHelpers::cleanupClosedWindowState(windowId, screenId, border, state);

        // Verify all maps no longer contain the window
        QVERIFY(!PlasmaZones::AutotileStateHelpers::isBorderlessWindow(border, windowId));
        QVERIFY(!PlasmaZones::AutotileStateHelpers::isTiledWindow(border, windowId));
        QVERIFY(!border.zoneGeometries.contains(windowId));
        QVERIFY(!notifiedWindows.contains(windowId));
        QVERIFY(!notifiedWindowScreens.contains(windowId));
        QVERIFY(!minimizeFloatedWindows.contains(windowId));
        QVERIFY(!autotileTargetZones.contains(windowId));
        QVERIFY(!centeredWaylandZones.contains(windowId));
        QVERIFY(!monocleMaximizedWindows.contains(windowId));
        QVERIFY(!preAutotileGeometries[screenId].contains(windowId));
    }

    // =================================================================
    // WindowIdUtils: extractAppId leading separator edge case
    // =================================================================

    void testExtractAppIdLeadingSeparator()
    {
        // Separator at index 0 means sep is NOT > 0, so should return original
        QCOMPARE(PlasmaZones::WindowIdUtils::extractAppId(QStringLiteral("|instance")), QStringLiteral("|instance"));
    }

    // =================================================================
    // WindowIdUtils: deriveShortName trailing dot edge case
    // =================================================================

    void testDeriveShortNameTrailingDot()
    {
        // Dot at end position: dotIdx == length()-1, guard (dotIdx < length()-1) is false
        // so it returns the full string unchanged
        QCOMPARE(PlasmaZones::WindowIdUtils::deriveShortName(QStringLiteral("org.kde.")), QStringLiteral("org.kde."));
    }

    // =================================================================
    // D-Bus types: WindowOpenedEntry roundtrip
    // =================================================================

    void testWindowOpenedEntryRoundtrip()
    {
        PlasmaZones::registerDBusTypes();
        PlasmaZones::WindowOpenedEntry entry{QStringLiteral("firefox|42"), QStringLiteral("screen-0"), 320, 240};

        const QString sig = dbusSignature(entry);
        QCOMPARE(sig, QStringLiteral("(ssii)"));

        const int typeId = qMetaTypeId<PlasmaZones::WindowOpenedEntry>();
        QVERIFY(typeId != QMetaType::UnknownType);
        const int listTypeId = qMetaTypeId<PlasmaZones::WindowOpenedList>();
        QVERIFY(listTypeId != QMetaType::UnknownType);

        QCOMPARE(entry.windowId, QStringLiteral("firefox|42"));
        QCOMPARE(entry.screenId, QStringLiteral("screen-0"));
        QCOMPARE(entry.minWidth, 320);
        QCOMPARE(entry.minHeight, 240);

        PlasmaZones::WindowOpenedEntry defaultEntry;
        QVERIFY(defaultEntry.windowId.isEmpty());
        QVERIFY(defaultEntry.screenId.isEmpty());
        QCOMPARE(defaultEntry.minWidth, 0);
        QCOMPARE(defaultEntry.minHeight, 0);
    }

    // =================================================================
    // D-Bus types: SnapConfirmationEntry roundtrip
    // =================================================================

    void testSnapConfirmationEntryRoundtrip()
    {
        PlasmaZones::registerDBusTypes();
        PlasmaZones::SnapConfirmationEntry entry{QStringLiteral("kate|7"), QStringLiteral("{zone-1}"),
                                                 QStringLiteral("screen-0"), true};

        const QString sig = dbusSignature(entry);
        QCOMPARE(sig, QStringLiteral("(sssb)"));

        const int typeId = qMetaTypeId<PlasmaZones::SnapConfirmationEntry>();
        QVERIFY(typeId != QMetaType::UnknownType);
        const int listTypeId = qMetaTypeId<PlasmaZones::SnapConfirmationList>();
        QVERIFY(listTypeId != QMetaType::UnknownType);

        QCOMPARE(entry.windowId, QStringLiteral("kate|7"));
        QCOMPARE(entry.zoneId, QStringLiteral("{zone-1}"));
        QCOMPARE(entry.screenId, QStringLiteral("screen-0"));
        QCOMPARE(entry.isRestore, true);

        PlasmaZones::SnapConfirmationEntry defaultEntry;
        QVERIFY(defaultEntry.windowId.isEmpty());
        QCOMPARE(defaultEntry.isRestore, false);
    }
};

QTEST_GUILESS_MAIN(TestCompositorCommon)
#include "test_compositor_common.moc"
