// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_autotile_state.cpp
 * @brief Unit tests for WindowId utilities and AutotileStateHelpers
 *
 * Tests WindowId::extractAppId, WindowId::deriveShortName, and
 * AutotileStateHelpers::cleanupClosedWindowState.
 */

#include <QTest>

#include <PhosphorCompositor/AutotileState.h>
#include <PhosphorIdentity/WindowId.h>

class TestAutotileState : public QObject
{
    Q_OBJECT

private Q_SLOTS:

    // =================================================================
    // WindowId: extractAppId
    // =================================================================

    void testExtractAppId()
    {
        QCOMPARE(::PhosphorIdentity::WindowId::extractAppId(QStringLiteral("firefox|42")), QStringLiteral("firefox"));
        QCOMPARE(::PhosphorIdentity::WindowId::extractAppId(QStringLiteral("firefox")), QStringLiteral("firefox"));
        QCOMPARE(::PhosphorIdentity::WindowId::extractAppId(QString()), QString());
        QCOMPARE(::PhosphorIdentity::WindowId::extractAppId(QStringLiteral("org.kde.dolphin|123")),
                 QStringLiteral("org.kde.dolphin"));
    }

    // =================================================================
    // WindowId: deriveShortName
    // =================================================================

    void testDeriveShortName()
    {
        QCOMPARE(::PhosphorIdentity::WindowId::deriveShortName(QStringLiteral("org.kde.dolphin")),
                 QStringLiteral("dolphin"));
        QCOMPARE(::PhosphorIdentity::WindowId::deriveShortName(QStringLiteral("firefox")), QStringLiteral("firefox"));
        QCOMPARE(::PhosphorIdentity::WindowId::deriveShortName(QString()), QString());
        QCOMPARE(::PhosphorIdentity::WindowId::deriveShortName(QStringLiteral("com.example.app")),
                 QStringLiteral("app"));
    }

    // =================================================================
    // AutotileStateHelpers: cleanupClosedWindowState
    // =================================================================

    void testCleanupClosedWindowState()
    {
        const QString windowId = QStringLiteral("testapp|42");
        const QString screenId = QStringLiteral("screen-0");

        // Set up BorderState with the window on its owning screen.
        PhosphorCompositor::BorderState border;
        PhosphorCompositor::AutotileStateHelpers::addBorderlessOnScreen(border, screenId, windowId);
        PhosphorCompositor::AutotileStateHelpers::addTiledOnScreen(border, screenId, windowId);
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

        PhosphorCompositor::AutotileStateHelpers::AutotileWindowState state{
            notifiedWindows,      notifiedWindowScreens,   minimizeFloatedWindows, autotileTargetZones,
            centeredWaylandZones, monocleMaximizedWindows, preAutotileGeometries};

        // Perform cleanup
        PhosphorCompositor::AutotileStateHelpers::cleanupClosedWindowState(windowId, screenId, border, state);

        // Verify all maps no longer contain the window
        QVERIFY(!PhosphorCompositor::AutotileStateHelpers::isBorderlessWindow(border, windowId));
        QVERIFY(!PhosphorCompositor::AutotileStateHelpers::isTiledWindow(border, windowId));
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
    // WindowId: extractAppId leading separator edge case
    // =================================================================

    void testExtractAppIdLeadingSeparator()
    {
        // Leading separator: empty appId prefix, mirroring extractInstanceId's semantics.
        QCOMPARE(::PhosphorIdentity::WindowId::extractAppId(QStringLiteral("|instance")), QString());
    }

    // =================================================================
    // WindowId: deriveShortName trailing dot edge case
    // =================================================================

    void testDeriveShortNameTrailingDot()
    {
        // Trailing dots are stripped before segment extraction so a typo'd
        // reverse-DNS like "org.kde." normalises to the same short name as
        // "org.kde". A string of nothing-but-dots collapses to empty.
        QCOMPARE(::PhosphorIdentity::WindowId::deriveShortName(QStringLiteral("org.kde.")), QStringLiteral("kde"));
        QCOMPARE(::PhosphorIdentity::WindowId::deriveShortName(QStringLiteral("org.kde...")), QStringLiteral("kde"));
        QCOMPARE(::PhosphorIdentity::WindowId::deriveShortName(QStringLiteral("...")), QString());
    }
};

QTEST_GUILESS_MAIN(TestAutotileState)
#include "test_autotile_state.moc"
