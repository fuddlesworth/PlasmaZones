// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_floating_cache.cpp
 * @brief Unit tests for FloatingCache and TriggerParser
 *
 * Tests FloatingCache set/get/clear/appId-fallback logic and
 * TriggerParser modifier checking and anyTriggerHeld.
 */

#include <QTest>

#include <PhosphorCompositor/FloatingCache.h>
#include <PhosphorCompositor/TriggerParser.h>

class TestFloatingCache : public QObject
{
    Q_OBJECT

private Q_SLOTS:

    // =================================================================
    // FloatingCache: basic operations
    // =================================================================

    void testFloatingCacheBasic()
    {
        PhosphorCompositor::FloatingCache cache;
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
        PhosphorCompositor::FloatingCache cache;
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
        PhosphorCompositor::FloatingCache cache;
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
        PhosphorCompositor::FloatingCache cache;
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
        using PhosphorCompositor::TriggerParser::checkModifier;

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
        using PhosphorCompositor::ParsedTrigger;
        using PhosphorCompositor::TriggerParser::anyTriggerHeld;

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
        using PhosphorCompositor::ParsedTrigger;
        using PhosphorCompositor::TriggerParser::anyTriggerHeld;

        // Trigger requires left mouse button only (modifier=0 means "any mod is ok"
        // but the guard clause requires at least one non-zero field)
        QVector<ParsedTrigger> btnTrigger = {{0, static_cast<int>(Qt::LeftButton)}};
        QVERIFY(anyTriggerHeld(btnTrigger, Qt::NoModifier, Qt::LeftButton));
        QVERIFY(!anyTriggerHeld(btnTrigger, Qt::NoModifier, Qt::RightButton));
    }

    void testAnyTriggerHeldModAndButton()
    {
        using PhosphorCompositor::ParsedTrigger;
        using PhosphorCompositor::TriggerParser::anyTriggerHeld;

        // Requires Shift + LeftButton
        QVector<ParsedTrigger> combined = {{1, static_cast<int>(Qt::LeftButton)}};
        QVERIFY(anyTriggerHeld(combined, Qt::ShiftModifier, Qt::LeftButton));
        QVERIFY(!anyTriggerHeld(combined, Qt::ShiftModifier, Qt::RightButton));
        QVERIFY(!anyTriggerHeld(combined, Qt::ControlModifier, Qt::LeftButton));
    }
};

QTEST_GUILESS_MAIN(TestFloatingCache)
#include "test_floating_cache.moc"
