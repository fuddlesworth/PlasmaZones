// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_tiling_state.cpp
 * @brief Unit tests for WindowId utilities and TilingStateHelpers
 *
 * Tests WindowId::extractAppId, WindowId::deriveShortName, and
 * TilingStateHelpers::cleanupClosedWindowState and removeFromOtherScreens.
 */

#include <QTest>

#include <PhosphorCompositor/TilingState.h>
#include <PhosphorIdentity/WindowId.h>

class TestTilingState : public QObject
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
    // TilingStateHelpers: cleanupClosedWindowState
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
        PhosphorCompositor::TilingStateHelpers::addTiledOnScreen(border, screenId, windowId);
        PhosphorCompositor::TilingStateHelpers::addTiledOnScreen(border, staleScreen, windowId);
        PhosphorCompositor::TilingStateHelpers::addTiledOnScreen(border, staleScreen, sibling);

        // Set up TilingWindowState maps
        QSet<QString> notifiedWindows;
        notifiedWindows.insert(windowId);

        QHash<QString, QString> notifiedWindowScreens;
        notifiedWindowScreens.insert(windowId, screenId);

        QSet<QString> minimizeFloatedWindows;
        minimizeFloatedWindows.insert(windowId);

        QHash<QString, QRect> tileTargetZones;
        tileTargetZones.insert(windowId, QRect(0, 0, 800, 600));

        QHash<QString, QRect> centeredWaylandZones;
        centeredWaylandZones.insert(windowId, QRect(100, 100, 600, 400));

        QSet<QString> monocleMaximizedWindows;
        monocleMaximizedWindows.insert(windowId);

        QHash<QString, QHash<QString, QRectF>> preTileGeometries;
        preTileGeometries[screenId].insert(windowId, QRectF(0.1, 0.1, 0.5, 0.5));
        // Same cross-screen-stale shape for the geometry store: the closed
        // window's entry in the old screen's bucket plus a sibling entry
        // that must survive the sweep.
        preTileGeometries[staleScreen].insert(windowId, QRectF(0.2, 0.2, 0.4, 0.4));
        preTileGeometries[staleScreen].insert(sibling, QRectF(0.3, 0.3, 0.3, 0.3));

        PhosphorCompositor::TilingStateHelpers::TilingWindowState state{
            notifiedWindows,      notifiedWindowScreens,   minimizeFloatedWindows, tileTargetZones,
            centeredWaylandZones, monocleMaximizedWindows, preTileGeometries};

        // Perform cleanup
        PhosphorCompositor::TilingStateHelpers::cleanupClosedWindowState(windowId, border, state);

        // Verify all maps no longer contain the window
        QVERIFY(!PhosphorCompositor::TilingStateHelpers::isTiledWindow(border, windowId));
        QVERIFY(!notifiedWindows.contains(windowId));
        QVERIFY(!notifiedWindowScreens.contains(windowId));
        QVERIFY(!minimizeFloatedWindows.contains(windowId));
        QVERIFY(!tileTargetZones.contains(windowId));
        QVERIFY(!centeredWaylandZones.contains(windowId));
        QVERIFY(!monocleMaximizedWindows.contains(windowId));

        // Cross-screen sweep: the window is scrubbed from EVERY bucket, the
        // now-empty owning bucket is erased, the stale bucket survives with
        // only the sibling, and the sibling's entries are untouched.
        QVERIFY(!border.tiledWindowsByScreen.contains(screenId));
        QVERIFY(!border.tiledWindowsByScreen.value(staleScreen).contains(windowId));
        QVERIFY(border.tiledWindowsByScreen.value(staleScreen).contains(sibling));
        QVERIFY(!preTileGeometries.contains(screenId));
        QVERIFY(!preTileGeometries.value(staleScreen).contains(windowId));
        QVERIFY(preTileGeometries.value(staleScreen).contains(sibling));
    }

    // =================================================================
    // TilingStateHelpers: removeFromOtherScreens
    // =================================================================

    void testRemoveFromOtherScreens()
    {
        const QString windowId = QStringLiteral("app|1");
        const QString other = QStringLiteral("other|1");
        const QString keep = QStringLiteral("screen-keep");
        const QString shared = QStringLiteral("screen-shared");
        const QString solo = QStringLiteral("screen-solo");

        PhosphorCompositor::BorderState border;
        PhosphorCompositor::TilingStateHelpers::addTiledOnScreen(border, keep, windowId);
        PhosphorCompositor::TilingStateHelpers::addTiledOnScreen(border, shared, windowId);
        PhosphorCompositor::TilingStateHelpers::addTiledOnScreen(border, shared, other);
        PhosphorCompositor::TilingStateHelpers::addTiledOnScreen(border, solo, windowId);

        PhosphorCompositor::TilingStateHelpers::removeFromOtherScreens(border, windowId, keep);

        // Window survives only on the keep screen; the sibling that still
        // holds another window survives; the window-only sibling bucket is
        // erased entirely.
        QVERIFY(border.tiledWindowsByScreen.value(keep).contains(windowId));
        QVERIFY(!border.tiledWindowsByScreen.value(shared).contains(windowId));
        QVERIFY(border.tiledWindowsByScreen.value(shared).contains(other));
        QVERIFY(!border.tiledWindowsByScreen.contains(solo));
    }

    // Boundary: cleaning up a window absent from every map must be a pure
    // no-op — a regression that erased whole buckets on a miss would pass
    // the positive-path test above unnoticed.
    void testCleanupUnknownWindow_isNoOp()
    {
        const QString a = QStringLiteral("app|a");
        const QString b = QStringLiteral("app|b");
        PhosphorCompositor::BorderState border;
        PhosphorCompositor::TilingStateHelpers::addTiledOnScreen(border, QStringLiteral("s1"), a);
        PhosphorCompositor::TilingStateHelpers::addTiledOnScreen(border, QStringLiteral("s2"), b);

        QSet<QString> notifiedWindows{a, b};
        QHash<QString, QString> notifiedWindowScreens{{a, QStringLiteral("s1")}, {b, QStringLiteral("s2")}};
        QSet<QString> minimizeFloatedWindows;
        QHash<QString, QRect> tileTargetZones;
        QHash<QString, QRect> centeredWaylandZones;
        QSet<QString> monocleMaximizedWindows;
        QHash<QString, QHash<QString, QRectF>> preTileGeometries;
        PhosphorCompositor::TilingStateHelpers::TilingWindowState state{
            notifiedWindows,      notifiedWindowScreens,   minimizeFloatedWindows, tileTargetZones,
            centeredWaylandZones, monocleMaximizedWindows, preTileGeometries};

        PhosphorCompositor::TilingStateHelpers::cleanupClosedWindowState(QStringLiteral("app|unknown"), border, state);

        QVERIFY(border.tiledWindowsByScreen.value(QStringLiteral("s1")).contains(a));
        QVERIFY(border.tiledWindowsByScreen.value(QStringLiteral("s2")).contains(b));
        QCOMPARE(notifiedWindows.size(), 2);
        QCOMPARE(notifiedWindowScreens.size(), 2);
    }

    // Boundary: an EMPTY keepScreen (no screen id resolved at the call
    // site) strips the window from every bucket — pinned so the "invalid
    // input at a system boundary" shape has a declared outcome instead of
    // an accidental one.
    void testRemoveFromOtherScreens_emptyKeepStripsEverywhere()
    {
        const QString windowId = QStringLiteral("app|1");
        const QString other = QStringLiteral("other|1");
        PhosphorCompositor::BorderState border;
        PhosphorCompositor::TilingStateHelpers::addTiledOnScreen(border, QStringLiteral("s1"), windowId);
        PhosphorCompositor::TilingStateHelpers::addTiledOnScreen(border, QStringLiteral("s2"), windowId);
        PhosphorCompositor::TilingStateHelpers::addTiledOnScreen(border, QStringLiteral("s2"), other);

        PhosphorCompositor::TilingStateHelpers::removeFromOtherScreens(border, windowId, QString());

        QVERIFY(!PhosphorCompositor::TilingStateHelpers::isTiledWindow(border, windowId));
        QVERIFY(border.tiledWindowsByScreen.value(QStringLiteral("s2")).contains(other));
        QVERIFY(!border.tiledWindowsByScreen.contains(QStringLiteral("s1")));
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

QTEST_GUILESS_MAIN(TestTilingState)
#include "test_tiling_state.moc"
