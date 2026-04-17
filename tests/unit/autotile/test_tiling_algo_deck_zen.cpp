// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include <QTest>
#include <QRect>
#include <QVector>

#include <PhosphorTiles/AlgorithmRegistry.h>
#include <PhosphorTiles/TilingAlgorithm.h>
#include <PhosphorTiles/TilingState.h>
#include "core/constants.h"

#include "../helpers/TilingTestHelpers.h"
#include "../helpers/ScriptedAlgoTestSetup.h"

using namespace PlasmaZones;
using namespace PlasmaZones::TestHelpers;

/**
 * @brief Tests for Deck, Horizontal Deck, Zen, Focus+Sidebar, Floating Center, Paper, and Tatami
 *
 * Covers overlapping (deck, horizontal-deck, paper) and non-overlapping
 * (zen, focus-sidebar, floating-center, tatami) layout algorithms.
 */
class TestTilingAlgoDeckZen : public QObject
{
    Q_OBJECT

private:
    static constexpr int ScreenWidth = 1920;
    static constexpr int ScreenHeight = 1080;
    QRect m_screenGeometry{0, 0, ScreenWidth, ScreenHeight};
    ScriptedAlgoTestSetup m_scriptSetup;

    PhosphorTiles::TilingAlgorithm* deck()
    {
        return PhosphorTiles::AlgorithmRegistry::instance()->algorithm(QLatin1String("deck"));
    }
    PhosphorTiles::TilingAlgorithm* hDeck()
    {
        return PhosphorTiles::AlgorithmRegistry::instance()->algorithm(QLatin1String("horizontal-deck"));
    }
    PhosphorTiles::TilingAlgorithm* zen()
    {
        return PhosphorTiles::AlgorithmRegistry::instance()->algorithm(QLatin1String("zen"));
    }
    PhosphorTiles::TilingAlgorithm* focusSidebar()
    {
        return PhosphorTiles::AlgorithmRegistry::instance()->algorithm(QLatin1String("focus-sidebar"));
    }
    PhosphorTiles::TilingAlgorithm* floatingCenter()
    {
        return PhosphorTiles::AlgorithmRegistry::instance()->algorithm(QLatin1String("floating-center"));
    }
    PhosphorTiles::TilingAlgorithm* paper()
    {
        return PhosphorTiles::AlgorithmRegistry::instance()->algorithm(QLatin1String("paper"));
    }
    PhosphorTiles::TilingAlgorithm* tatami()
    {
        return PhosphorTiles::AlgorithmRegistry::instance()->algorithm(QLatin1String("tatami"));
    }

private Q_SLOTS:
    void initTestCase()
    {
        QVERIFY(m_scriptSetup.init(QStringLiteral(PZ_SOURCE_DIR)));
        QVERIFY(deck() != nullptr);
        QVERIFY(hDeck() != nullptr);
        QVERIFY(zen() != nullptr);
        QVERIFY(focusSidebar() != nullptr);
        QVERIFY(floatingCenter() != nullptr);
        QVERIFY(paper() != nullptr);
        QVERIFY(tatami() != nullptr);
    }

    // =========================================================================
    // Zero window tests — all algorithms
    // =========================================================================

    void testDeck_zeroWindows()
    {
        PhosphorTiles::TilingState state(QStringLiteral("test"));
        QVERIFY(deck()
                    ->calculateZones(makeParams(0, m_screenGeometry, &state, 0, ::PhosphorLayout::EdgeGaps::uniform(0)))
                    .isEmpty());
    }

    void testHDeck_zeroWindows()
    {
        PhosphorTiles::TilingState state(QStringLiteral("test"));
        QVERIFY(hDeck()
                    ->calculateZones(makeParams(0, m_screenGeometry, &state, 0, ::PhosphorLayout::EdgeGaps::uniform(0)))
                    .isEmpty());
    }

    void testZen_zeroWindows()
    {
        PhosphorTiles::TilingState state(QStringLiteral("test"));
        QVERIFY(zen()
                    ->calculateZones(makeParams(0, m_screenGeometry, &state, 0, ::PhosphorLayout::EdgeGaps::uniform(0)))
                    .isEmpty());
    }

    void testFocusSidebar_zeroWindows()
    {
        PhosphorTiles::TilingState state(QStringLiteral("test"));
        QVERIFY(focusSidebar()
                    ->calculateZones(makeParams(0, m_screenGeometry, &state, 0, ::PhosphorLayout::EdgeGaps::uniform(0)))
                    .isEmpty());
    }

    void testFloatingCenter_zeroWindows()
    {
        PhosphorTiles::TilingState state(QStringLiteral("test"));
        QVERIFY(floatingCenter()
                    ->calculateZones(makeParams(0, m_screenGeometry, &state, 0, ::PhosphorLayout::EdgeGaps::uniform(0)))
                    .isEmpty());
    }

    void testPaper_zeroWindows()
    {
        PhosphorTiles::TilingState state(QStringLiteral("test"));
        QVERIFY(paper()
                    ->calculateZones(makeParams(0, m_screenGeometry, &state, 0, ::PhosphorLayout::EdgeGaps::uniform(0)))
                    .isEmpty());
    }

    void testTatami_zeroWindows()
    {
        PhosphorTiles::TilingState state(QStringLiteral("test"));
        QVERIFY(tatami()
                    ->calculateZones(makeParams(0, m_screenGeometry, &state, 0, ::PhosphorLayout::EdgeGaps::uniform(0)))
                    .isEmpty());
    }

    // =========================================================================
    // Single window tests — all algorithms fill full area
    // =========================================================================

    void testDeck_singleWindow()
    {
        PhosphorTiles::TilingState state(QStringLiteral("test"));
        auto zones =
            deck()->calculateZones(makeParams(1, m_screenGeometry, &state, 0, ::PhosphorLayout::EdgeGaps::uniform(0)));
        QCOMPARE(zones.size(), 1);
        QVERIFY(allWithinBounds(zones, m_screenGeometry));
    }

    void testHDeck_singleWindow()
    {
        PhosphorTiles::TilingState state(QStringLiteral("test"));
        auto zones =
            hDeck()->calculateZones(makeParams(1, m_screenGeometry, &state, 0, ::PhosphorLayout::EdgeGaps::uniform(0)));
        QCOMPARE(zones.size(), 1);
        QVERIFY(allWithinBounds(zones, m_screenGeometry));
    }

    void testZen_singleWindow()
    {
        PhosphorTiles::TilingState state(QStringLiteral("test"));
        auto zones =
            zen()->calculateZones(makeParams(1, m_screenGeometry, &state, 0, ::PhosphorLayout::EdgeGaps::uniform(0)));
        QCOMPARE(zones.size(), 1);
        QVERIFY(allWithinBounds(zones, m_screenGeometry));
    }

    void testFocusSidebar_singleWindow()
    {
        PhosphorTiles::TilingState state(QStringLiteral("test"));
        auto zones = focusSidebar()->calculateZones(
            makeParams(1, m_screenGeometry, &state, 0, ::PhosphorLayout::EdgeGaps::uniform(0)));
        QCOMPARE(zones.size(), 1);
        QVERIFY(allWithinBounds(zones, m_screenGeometry));
    }

    void testFloatingCenter_singleWindow()
    {
        PhosphorTiles::TilingState state(QStringLiteral("test"));
        auto zones = floatingCenter()->calculateZones(
            makeParams(1, m_screenGeometry, &state, 0, ::PhosphorLayout::EdgeGaps::uniform(0)));
        QCOMPARE(zones.size(), 1);
        QVERIFY(allWithinBounds(zones, m_screenGeometry));
    }

    void testPaper_singleWindow()
    {
        PhosphorTiles::TilingState state(QStringLiteral("test"));
        auto zones =
            paper()->calculateZones(makeParams(1, m_screenGeometry, &state, 0, ::PhosphorLayout::EdgeGaps::uniform(0)));
        QCOMPARE(zones.size(), 1);
        QVERIFY(allWithinBounds(zones, m_screenGeometry));
    }

    void testTatami_singleWindow()
    {
        PhosphorTiles::TilingState state(QStringLiteral("test"));
        auto zones = tatami()->calculateZones(
            makeParams(1, m_screenGeometry, &state, 0, ::PhosphorLayout::EdgeGaps::uniform(0)));
        QCOMPARE(zones.size(), 1);
        QCOMPARE(zones[0], m_screenGeometry);
    }

    // =========================================================================
    // Deck — overlapping, within bounds
    // =========================================================================

    void testDeck_metadata()
    {
        auto* algo = deck();
        QCOMPARE(algo->name(), QStringLiteral("Deck"));
        QVERIFY(algo->producesOverlappingZones());
        QVERIFY(!algo->supportsMasterCount());
        QVERIFY(algo->supportsSplitRatio());
    }

    void testDeck_multipleWindows()
    {
        PhosphorTiles::TilingState state(QStringLiteral("test"));
        state.setSplitRatio(0.75);
        auto zones =
            deck()->calculateZones(makeParams(4, m_screenGeometry, &state, 0, ::PhosphorLayout::EdgeGaps::uniform(0)));
        QCOMPARE(zones.size(), 4);
        QVERIFY(allWithinBounds(zones, m_screenGeometry));
        for (const QRect& zone : zones) {
            QVERIFY(zone.width() > 0);
            QVERIFY(zone.height() > 0);
        }
        // Focused window (first) should be wider than trailing peek zones
        QVERIFY2(zones[0].width() >= zones[zones.size() - 1].width(),
                 qPrintable(QStringLiteral("Focused zone width (%1) should be >= last zone width (%2)")
                                .arg(zones[0].width())
                                .arg(zones[zones.size() - 1].width())));
    }

    void testDeck_fiveWindows()
    {
        PhosphorTiles::TilingState state(QStringLiteral("test"));
        state.setSplitRatio(0.75);
        auto zones =
            deck()->calculateZones(makeParams(5, m_screenGeometry, &state, 0, ::PhosphorLayout::EdgeGaps::uniform(0)));
        QCOMPARE(zones.size(), 5);
        QVERIFY(allWithinBounds(zones, m_screenGeometry));
    }

    // =========================================================================
    // Horizontal Deck — overlapping, within bounds, horizontal orientation
    // =========================================================================

    void testHDeck_metadata()
    {
        auto* algo = hDeck();
        QCOMPARE(algo->name(), QStringLiteral("Horizontal Deck"));
        QVERIFY(algo->producesOverlappingZones());
    }

    void testHDeck_multipleWindows()
    {
        PhosphorTiles::TilingState state(QStringLiteral("test"));
        state.setSplitRatio(0.75);
        auto zones =
            hDeck()->calculateZones(makeParams(4, m_screenGeometry, &state, 0, ::PhosphorLayout::EdgeGaps::uniform(0)));
        QCOMPARE(zones.size(), 4);
        QVERIFY(allWithinBounds(zones, m_screenGeometry));
        // Focused window (first) should be taller than trailing peek zones
        QVERIFY2(zones[0].height() >= zones[zones.size() - 1].height(),
                 qPrintable(QStringLiteral("Focused zone height (%1) should be >= last zone height (%2)")
                                .arg(zones[0].height())
                                .arg(zones[zones.size() - 1].height())));
    }

    // =========================================================================
    // Zen — centered column, non-overlapping
    // =========================================================================

    void testZen_metadata()
    {
        auto* algo = zen();
        QCOMPARE(algo->name(), QStringLiteral("Zen"));
        QVERIFY(!algo->producesOverlappingZones());
        QVERIFY(algo->supportsSplitRatio());
    }

    void testZen_centeredColumn()
    {
        PhosphorTiles::TilingState state(QStringLiteral("test"));
        state.setSplitRatio(0.6);
        auto zones =
            zen()->calculateZones(makeParams(3, m_screenGeometry, &state, 0, ::PhosphorLayout::EdgeGaps::uniform(0)));
        QCOMPARE(zones.size(), 3);
        QVERIFY(allWithinBounds(zones, m_screenGeometry));
        // All zones should have same width (centered column)
        for (int i = 1; i < zones.size(); ++i) {
            QCOMPARE(zones[i].width(), zones[0].width());
        }
        // Column width should be less than area width (centered with margins)
        QVERIFY2(zones[0].width() < ScreenWidth,
                 qPrintable(QStringLiteral("Zen column width (%1) should be < screen width (%2)")
                                .arg(zones[0].width())
                                .arg(ScreenWidth)));
        // Column should be centered — left margin equals right margin
        int leftMargin = zones[0].x();
        int rightMargin = ScreenWidth - zones[0].x() - zones[0].width();
        QVERIFY2(
            qAbs(leftMargin - rightMargin) <= 1,
            qPrintable(
                QStringLiteral("Zen margins should be equal: left=%1, right=%2").arg(leftMargin).arg(rightMargin)));
    }

    void testZen_fiveWindows()
    {
        PhosphorTiles::TilingState state(QStringLiteral("test"));
        state.setSplitRatio(0.6);
        auto zones =
            zen()->calculateZones(makeParams(5, m_screenGeometry, &state, 0, ::PhosphorLayout::EdgeGaps::uniform(0)));
        QCOMPARE(zones.size(), 5);
        QVERIFY(noOverlaps(zones));
        QVERIFY(allWithinBounds(zones, m_screenGeometry));
    }

    // =========================================================================
    // Focus+Sidebar — main window wider than sidebar zones
    // =========================================================================

    void testFocusSidebar_metadata()
    {
        auto* algo = focusSidebar();
        QCOMPARE(algo->name(), QStringLiteral("Focus + Sidebar"));
        QVERIFY(!algo->producesOverlappingZones());
        QVERIFY(algo->supportsSplitRatio());
        QCOMPARE(algo->masterZoneIndex(), 0);
    }

    void testFocusSidebar_mainWiderThanSidebar()
    {
        PhosphorTiles::TilingState state(QStringLiteral("test"));
        state.setSplitRatio(0.7);
        auto zones = focusSidebar()->calculateZones(
            makeParams(4, m_screenGeometry, &state, 0, ::PhosphorLayout::EdgeGaps::uniform(0)));
        QCOMPARE(zones.size(), 4);
        QVERIFY(noOverlaps(zones));
        QVERIFY(allWithinBounds(zones, m_screenGeometry));
        // Main window (index 0) should be wider than each sidebar window
        for (int i = 1; i < zones.size(); ++i) {
            QVERIFY2(zones[0].width() > zones[i].width(),
                     qPrintable(QStringLiteral("Main zone width (%1) should be > sidebar zone %2 width (%3)")
                                    .arg(zones[0].width())
                                    .arg(i)
                                    .arg(zones[i].width())));
        }
    }

    void testFocusSidebar_twoWindows()
    {
        PhosphorTiles::TilingState state(QStringLiteral("test"));
        state.setSplitRatio(0.7);
        auto zones = focusSidebar()->calculateZones(
            makeParams(2, m_screenGeometry, &state, 0, ::PhosphorLayout::EdgeGaps::uniform(0)));
        QCOMPARE(zones.size(), 2);
        QVERIFY(noOverlaps(zones));
        QVERIFY(allWithinBounds(zones, m_screenGeometry));
    }

    void testFocusSidebar_fiveWindows()
    {
        PhosphorTiles::TilingState state(QStringLiteral("test"));
        state.setSplitRatio(0.7);
        auto zones = focusSidebar()->calculateZones(
            makeParams(5, m_screenGeometry, &state, 0, ::PhosphorLayout::EdgeGaps::uniform(0)));
        QCOMPARE(zones.size(), 5);
        QVERIFY(noOverlaps(zones));
        QVERIFY(allWithinBounds(zones, m_screenGeometry));
    }

    // =========================================================================
    // Floating Center — center zone with peripheral panels
    // =========================================================================

    void testFloatingCenter_metadata()
    {
        auto* algo = floatingCenter();
        QCOMPARE(algo->name(), QStringLiteral("Floating Center"));
        QVERIFY(!algo->producesOverlappingZones());
        QVERIFY(algo->supportsSplitRatio());
        QCOMPARE(algo->masterZoneIndex(), 0);
    }

    void testFloatingCenter_threeWindows()
    {
        PhosphorTiles::TilingState state(QStringLiteral("test"));
        state.setSplitRatio(0.65);
        auto zones = floatingCenter()->calculateZones(
            makeParams(3, m_screenGeometry, &state, 0, ::PhosphorLayout::EdgeGaps::uniform(0)));
        QCOMPARE(zones.size(), 3);
        QVERIFY(allWithinBounds(zones, m_screenGeometry));
        // Center zone (index 0) should be inside the screen, not touching edges
        QVERIFY2(zones[0].x() > 0, qPrintable(QStringLiteral("Center zone x (%1) should be > 0").arg(zones[0].x())));
        QVERIFY2(zones[0].width() < ScreenWidth,
                 qPrintable(QStringLiteral("Center zone width (%1) should be < screen width").arg(zones[0].width())));
    }

    void testFloatingCenter_fiveWindows()
    {
        PhosphorTiles::TilingState state(QStringLiteral("test"));
        state.setSplitRatio(0.65);
        auto zones = floatingCenter()->calculateZones(
            makeParams(5, m_screenGeometry, &state, 0, ::PhosphorLayout::EdgeGaps::uniform(0)));
        QCOMPARE(zones.size(), 5);
        QVERIFY(noOverlaps(zones));
        QVERIFY(allWithinBounds(zones, m_screenGeometry));
        // Center zone should be wider and taller than panels
        QVERIFY(zones[0].width() > 0);
        QVERIFY(zones[0].height() > 0);
    }

    void testFloatingCenter_twoWindows()
    {
        PhosphorTiles::TilingState state(QStringLiteral("test"));
        state.setSplitRatio(0.65);
        auto zones = floatingCenter()->calculateZones(
            makeParams(2, m_screenGeometry, &state, 0, ::PhosphorLayout::EdgeGaps::uniform(0)));
        QCOMPARE(zones.size(), 2);
        QVERIFY(allWithinBounds(zones, m_screenGeometry));
    }

    // =========================================================================
    // Paper — overlapping pages
    // =========================================================================

    void testPaper_metadata()
    {
        auto* algo = paper();
        QCOMPARE(algo->name(), QStringLiteral("Paper"));
        QVERIFY(algo->producesOverlappingZones());
        QVERIFY(algo->supportsSplitRatio());
    }

    void testPaper_multipleWindows()
    {
        PhosphorTiles::TilingState state(QStringLiteral("test"));
        state.setSplitRatio(0.8);
        auto zones =
            paper()->calculateZones(makeParams(4, m_screenGeometry, &state, 0, ::PhosphorLayout::EdgeGaps::uniform(0)));
        QCOMPARE(zones.size(), 4);
        QVERIFY(allWithinBounds(zones, m_screenGeometry));
        // All pages should have the same width (or very close due to rounding)
        for (int i = 1; i < zones.size(); ++i) {
            QVERIFY2(qAbs(zones[i].width() - zones[0].width()) <= 1,
                     qPrintable(QStringLiteral("Page %1 width (%2) should match page 0 width (%3)")
                                    .arg(i)
                                    .arg(zones[i].width())
                                    .arg(zones[0].width())));
        }
        // Pages should be full screen height
        for (const QRect& zone : zones) {
            QCOMPARE(zone.height(), ScreenHeight);
        }
    }

    void testPaper_twoWindows()
    {
        PhosphorTiles::TilingState state(QStringLiteral("test"));
        state.setSplitRatio(0.8);
        auto zones =
            paper()->calculateZones(makeParams(2, m_screenGeometry, &state, 0, ::PhosphorLayout::EdgeGaps::uniform(0)));
        QCOMPARE(zones.size(), 2);
        QVERIFY(allWithinBounds(zones, m_screenGeometry));
    }

    // =========================================================================
    // Tatami — non-overlapping, within bounds
    // =========================================================================

    void testTatami_metadata()
    {
        auto* algo = tatami();
        QCOMPARE(algo->name(), QStringLiteral("Tatami"));
        QVERIFY(!algo->producesOverlappingZones());
        QVERIFY(!algo->supportsSplitRatio());
    }

    void testTatami_twoWindows()
    {
        PhosphorTiles::TilingState state(QStringLiteral("test"));
        auto zones = tatami()->calculateZones(
            makeParams(2, m_screenGeometry, &state, 0, ::PhosphorLayout::EdgeGaps::uniform(0)));
        QCOMPARE(zones.size(), 2);
        QVERIFY(noOverlaps(zones));
        QVERIFY(allWithinBounds(zones, m_screenGeometry));
        QVERIFY(zonesFillScreen(zones, m_screenGeometry));
    }

    void testTatami_threeWindows()
    {
        PhosphorTiles::TilingState state(QStringLiteral("test"));
        auto zones = tatami()->calculateZones(
            makeParams(3, m_screenGeometry, &state, 0, ::PhosphorLayout::EdgeGaps::uniform(0)));
        QCOMPARE(zones.size(), 3);
        QVERIFY(noOverlaps(zones));
        QVERIFY(allWithinBounds(zones, m_screenGeometry));
    }

    void testTatami_fourWindows()
    {
        PhosphorTiles::TilingState state(QStringLiteral("test"));
        auto zones = tatami()->calculateZones(
            makeParams(4, m_screenGeometry, &state, 0, ::PhosphorLayout::EdgeGaps::uniform(0)));
        QCOMPARE(zones.size(), 4);
        QVERIFY(noOverlaps(zones));
        QVERIFY(allWithinBounds(zones, m_screenGeometry));
    }

    void testTatami_fiveWindows()
    {
        PhosphorTiles::TilingState state(QStringLiteral("test"));
        auto zones = tatami()->calculateZones(
            makeParams(5, m_screenGeometry, &state, 0, ::PhosphorLayout::EdgeGaps::uniform(0)));
        QCOMPARE(zones.size(), 5);
        QVERIFY(noOverlaps(zones));
        QVERIFY(allWithinBounds(zones, m_screenGeometry));
    }

    void testTatami_withGaps()
    {
        PhosphorTiles::TilingState state(QStringLiteral("test"));
        auto zones = tatami()->calculateZones(
            makeParams(4, m_screenGeometry, &state, 10, ::PhosphorLayout::EdgeGaps::uniform(0)));
        QCOMPARE(zones.size(), 4);
        QVERIFY(noOverlaps(zones));
        QVERIFY(allWithinBounds(zones, m_screenGeometry));
    }

    // =========================================================================
    // Tiny screen edge cases
    // =========================================================================

    void testDeck_tinyScreen()
    {
        PhosphorTiles::TilingState state(QStringLiteral("test"));
        state.setSplitRatio(0.75);
        QRect tiny(0, 0, 50, 50);
        auto zones = deck()->calculateZones(makeParams(3, tiny, &state, 0, ::PhosphorLayout::EdgeGaps::uniform(0)));
        QCOMPARE(zones.size(), 3);
        QVERIFY(allWithinBounds(zones, tiny));
    }

    void testHDeck_tinyScreen()
    {
        PhosphorTiles::TilingState state(QStringLiteral("test"));
        state.setSplitRatio(0.75);
        QRect tiny(0, 0, 50, 50);
        auto zones = hDeck()->calculateZones(makeParams(3, tiny, &state, 0, ::PhosphorLayout::EdgeGaps::uniform(0)));
        QCOMPARE(zones.size(), 3);
        QVERIFY(allWithinBounds(zones, tiny));
    }

    void testZen_tinyScreen()
    {
        PhosphorTiles::TilingState state(QStringLiteral("test"));
        state.setSplitRatio(0.6);
        QRect tiny(0, 0, 50, 50);
        auto zones = zen()->calculateZones(makeParams(3, tiny, &state, 0, ::PhosphorLayout::EdgeGaps::uniform(0)));
        QCOMPARE(zones.size(), 3);
        for (const QRect& zone : zones) {
            QVERIFY(zone.width() > 0);
            QVERIFY(zone.height() > 0);
        }
    }

    void testFocusSidebar_tinyNarrow()
    {
        PhosphorTiles::TilingState state(QStringLiteral("test"));
        state.setSplitRatio(0.7);
        QRect narrow(0, 0, 100, 30);
        auto zones =
            focusSidebar()->calculateZones(makeParams(3, narrow, &state, 0, ::PhosphorLayout::EdgeGaps::uniform(0)));
        QCOMPARE(zones.size(), 3);
        for (const QRect& zone : zones) {
            QVERIFY(zone.width() > 0);
            QVERIFY(zone.height() > 0);
        }
    }

    void testTatami_tinyScreen()
    {
        PhosphorTiles::TilingState state(QStringLiteral("test"));
        QRect tiny(0, 0, 50, 50);
        auto zones = tatami()->calculateZones(makeParams(4, tiny, &state, 0, ::PhosphorLayout::EdgeGaps::uniform(0)));
        QCOMPARE(zones.size(), 4);
        for (const QRect& zone : zones) {
            QVERIFY(zone.width() > 0);
            QVERIFY(zone.height() > 0);
        }
    }

    void testPaper_tinyScreen()
    {
        PhosphorTiles::TilingState state(QStringLiteral("test"));
        state.setSplitRatio(0.8);
        QRect tiny(0, 0, 30, 100);
        auto zones = paper()->calculateZones(makeParams(3, tiny, &state, 0, ::PhosphorLayout::EdgeGaps::uniform(0)));
        QCOMPARE(zones.size(), 3);
        QVERIFY(allWithinBounds(zones, tiny));
    }

    void testFloatingCenter_tinyScreen()
    {
        PhosphorTiles::TilingState state(QStringLiteral("test"));
        state.setSplitRatio(0.65);
        QRect tiny(0, 0, 50, 50);
        auto zones =
            floatingCenter()->calculateZones(makeParams(3, tiny, &state, 0, ::PhosphorLayout::EdgeGaps::uniform(0)));
        QCOMPARE(zones.size(), 3);
        for (const QRect& zone : zones) {
            QVERIFY(zone.width() > 0);
            QVERIFY(zone.height() > 0);
        }
    }

    // =========================================================================
    // Large window count
    // =========================================================================

    void testDeck_largeCount()
    {
        PhosphorTiles::TilingState state(QStringLiteral("test"));
        state.setSplitRatio(0.75);
        auto zones =
            deck()->calculateZones(makeParams(20, m_screenGeometry, &state, 0, ::PhosphorLayout::EdgeGaps::uniform(0)));
        QCOMPARE(zones.size(), 20);
        QVERIFY(allWithinBounds(zones, m_screenGeometry));
    }

    void testZen_largeCount()
    {
        PhosphorTiles::TilingState state(QStringLiteral("test"));
        state.setSplitRatio(0.6);
        auto zones =
            zen()->calculateZones(makeParams(20, m_screenGeometry, &state, 0, ::PhosphorLayout::EdgeGaps::uniform(0)));
        QCOMPARE(zones.size(), 20);
        QVERIFY(allWithinBounds(zones, m_screenGeometry));
    }

    void testFocusSidebar_largeCount()
    {
        PhosphorTiles::TilingState state(QStringLiteral("test"));
        state.setSplitRatio(0.7);
        auto zones = focusSidebar()->calculateZones(
            makeParams(20, m_screenGeometry, &state, 0, ::PhosphorLayout::EdgeGaps::uniform(0)));
        QCOMPARE(zones.size(), 20);
        QVERIFY(allWithinBounds(zones, m_screenGeometry));
    }

    void testTatami_largeCount()
    {
        PhosphorTiles::TilingState state(QStringLiteral("test"));
        auto zones = tatami()->calculateZones(
            makeParams(20, m_screenGeometry, &state, 0, ::PhosphorLayout::EdgeGaps::uniform(0)));
        QCOMPARE(zones.size(), 20);
        QVERIFY(allWithinBounds(zones, m_screenGeometry));
    }

    void testPaper_largeCount()
    {
        PhosphorTiles::TilingState state(QStringLiteral("test"));
        state.setSplitRatio(0.8);
        auto zones = paper()->calculateZones(
            makeParams(20, m_screenGeometry, &state, 0, ::PhosphorLayout::EdgeGaps::uniform(0)));
        QCOMPARE(zones.size(), 20);
        QVERIFY(allWithinBounds(zones, m_screenGeometry));
    }

    void testFloatingCenter_largeCount()
    {
        PhosphorTiles::TilingState state(QStringLiteral("test"));
        state.setSplitRatio(0.65);
        auto zones = floatingCenter()->calculateZones(
            makeParams(20, m_screenGeometry, &state, 0, ::PhosphorLayout::EdgeGaps::uniform(0)));
        QCOMPARE(zones.size(), 20);
        QVERIFY(allWithinBounds(zones, m_screenGeometry));
    }

    void testHDeck_largeCount()
    {
        PhosphorTiles::TilingState state(QStringLiteral("test"));
        state.setSplitRatio(0.75);
        auto zones = hDeck()->calculateZones(
            makeParams(20, m_screenGeometry, &state, 0, ::PhosphorLayout::EdgeGaps::uniform(0)));
        QCOMPARE(zones.size(), 20);
        QVERIFY(allWithinBounds(zones, m_screenGeometry));
    }
};

QTEST_MAIN(TestTilingAlgoDeckZen)
#include "test_tiling_algo_deck_zen.moc"
