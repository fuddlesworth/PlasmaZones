// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_autotile_state.cpp
 * @brief Unit tests for WindowId utilities and AutotileStateHelpers
 *
 * Tests WindowId::extractAppId, WindowId::deriveShortName,
 * AutotileStateHelpers::cleanupClosedWindowState and removeFromOtherScreens,
 * and the BorderState ↔ DecorationDefaults default-drift tripwire.
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
        const QString sibling = QStringLiteral("other|7");
        const QString screenId = QStringLiteral("screen-0");
        const QString staleScreen = QStringLiteral("screen-stale");

        // Set up BorderState with the window in TWO screen buckets — the
        // cross-screen-stale scenario cleanup defends against (window
        // crossed screens before closing). The stale bucket also holds a
        // sibling so we can verify the bucket survives while the closed
        // window is scrubbed from it.
        PhosphorCompositor::BorderState border;
        PhosphorCompositor::AutotileStateHelpers::addTiledOnScreen(border, screenId, windowId);
        PhosphorCompositor::AutotileStateHelpers::addTiledOnScreen(border, staleScreen, windowId);
        PhosphorCompositor::AutotileStateHelpers::addTiledOnScreen(border, staleScreen, sibling);

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
        // Same cross-screen-stale shape for the geometry store: the closed
        // window's entry in the old screen's bucket plus a sibling entry
        // that must survive the sweep.
        preAutotileGeometries[staleScreen].insert(windowId, QRectF(0.2, 0.2, 0.4, 0.4));
        preAutotileGeometries[staleScreen].insert(sibling, QRectF(0.3, 0.3, 0.3, 0.3));

        PhosphorCompositor::AutotileStateHelpers::AutotileWindowState state{
            notifiedWindows,      notifiedWindowScreens,   minimizeFloatedWindows, autotileTargetZones,
            centeredWaylandZones, monocleMaximizedWindows, preAutotileGeometries};

        // Perform cleanup
        PhosphorCompositor::AutotileStateHelpers::cleanupClosedWindowState(windowId, border, state);

        // Verify all maps no longer contain the window
        QVERIFY(!PhosphorCompositor::AutotileStateHelpers::isTiledWindow(border, windowId));
        QVERIFY(!notifiedWindows.contains(windowId));
        QVERIFY(!notifiedWindowScreens.contains(windowId));
        QVERIFY(!minimizeFloatedWindows.contains(windowId));
        QVERIFY(!autotileTargetZones.contains(windowId));
        QVERIFY(!centeredWaylandZones.contains(windowId));
        QVERIFY(!monocleMaximizedWindows.contains(windowId));

        // Cross-screen sweep: the window is scrubbed from EVERY bucket, the
        // now-empty owning bucket is erased, the stale bucket survives with
        // only the sibling, and the sibling's entries are untouched.
        QVERIFY(!border.tiledWindowsByScreen.contains(screenId));
        QVERIFY(!border.tiledWindowsByScreen.value(staleScreen).contains(windowId));
        QVERIFY(border.tiledWindowsByScreen.value(staleScreen).contains(sibling));
        QVERIFY(!preAutotileGeometries.contains(screenId));
        QVERIFY(!preAutotileGeometries.value(staleScreen).contains(windowId));
        QVERIFY(preAutotileGeometries.value(staleScreen).contains(sibling));
    }

    // =================================================================
    // AutotileStateHelpers: removeFromOtherScreens
    // =================================================================

    void testRemoveFromOtherScreens()
    {
        const QString windowId = QStringLiteral("app|1");
        const QString other = QStringLiteral("other|1");
        const QString keep = QStringLiteral("screen-keep");
        const QString shared = QStringLiteral("screen-shared");
        const QString solo = QStringLiteral("screen-solo");

        PhosphorCompositor::BorderState border;
        PhosphorCompositor::AutotileStateHelpers::addTiledOnScreen(border, keep, windowId);
        PhosphorCompositor::AutotileStateHelpers::addTiledOnScreen(border, shared, windowId);
        PhosphorCompositor::AutotileStateHelpers::addTiledOnScreen(border, shared, other);
        PhosphorCompositor::AutotileStateHelpers::addTiledOnScreen(border, solo, windowId);

        PhosphorCompositor::AutotileStateHelpers::removeFromOtherScreens(border, windowId, keep);

        // Window survives only on the keep screen; the sibling that still
        // holds another window survives; the window-only sibling bucket is
        // erased entirely.
        QVERIFY(border.tiledWindowsByScreen.value(keep).contains(windowId));
        QVERIFY(!border.tiledWindowsByScreen.value(shared).contains(windowId));
        QVERIFY(border.tiledWindowsByScreen.value(shared).contains(other));
        QVERIFY(!border.tiledWindowsByScreen.contains(solo));
    }

    // =================================================================
    // BorderState: defaults drift tripwire
    // =================================================================

    void testBorderStateDefaultsMatchDecorationDefaults()
    {
        // BorderState now carries only the mode title-bar-hide flag (border
        // appearance is resolved from window rules). The flag must still match
        // its DecorationDefaults counterpart so the mode's pre-load title-bar
        // state can't drift from what the daemon would persist.
        const PhosphorCompositor::BorderState border;
        QCOMPARE(border.hideTitleBars, PhosphorCompositor::DecorationDefaults::HideTitleBars);
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
