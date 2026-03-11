// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_configdefaults.cpp
 * @brief Unit tests for the ConfigDefaults class (config/configdefaults.h)
 *
 * Tests verify that ConfigDefaults accessors return the expected values from
 * the .kcfg schema and that all numeric defaults fall within their declared
 * min/max bounds.
 *
 * CMake target (not yet added to CMakeLists.txt):
 *   add_executable(test_configdefaults test_configdefaults.cpp)
 *   target_link_libraries(test_configdefaults PRIVATE Qt6::Test Qt6::Core KF6::ConfigCore plasmazones_core)
 *   add_test(NAME ConfigDefaults COMMAND test_configdefaults)
 *   set_tests_properties(ConfigDefaults PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
 */

#include <QTest>

#include "../../../src/config/configdefaults.h"
#include "../../../src/core/constants.h"

using namespace PlasmaZones;

class TestConfigDefaults : public QObject
{
    Q_OBJECT

private Q_SLOTS:

    /**
     * dismissedUpdateVersion() must return an empty QString for fresh installs.
     * This is hardcoded in ConfigDefaults because KConfigXT does not generate
     * a defaultDismissedUpdateVersionValue() for empty <default></default> strings.
     */
    void testDismissedUpdateVersion_accessor_returnsEmpty()
    {
        QVERIFY2(ConfigDefaults::dismissedUpdateVersion().isEmpty(),
                 "dismissedUpdateVersion default must be empty string");
    }

    /**
     * lastManualLayoutId() must return an empty QString for fresh installs.
     */
    void testLastManualLayoutId_accessor_returnsEmpty()
    {
        QVERIFY2(ConfigDefaults::lastManualLayoutId().isEmpty(), "lastManualLayoutId default must be empty string");
    }

    /**
     * labelFontFamily() must return an empty QString (system default font).
     */
    void testLabelFontFamily_accessor_returnsEmpty()
    {
        QVERIFY2(ConfigDefaults::labelFontFamily().isEmpty(),
                 "labelFontFamily default must be empty string (system default)");
    }

    /**
     * labelFontWeight() must return 700 (CSS Bold), not the legacy Qt5 value of 75.
     */
    void testLabelFontWeight_returns700()
    {
        QCOMPARE(ConfigDefaults::labelFontWeight(), 700);
    }

    /**
     * Every numeric default must fall within the min/max bounds declared in the
     * .kcfg schema. This prevents drift between ConfigDefaults and plasmazones.kcfg.
     *
     * The bounds are taken directly from the .kcfg <min>/<max> elements.
     */
    void testAllDefaults_withinKcfgBounds()
    {
        // Activation
        QVERIFY(ConfigDefaults::dragActivationModifier() >= 0);
        QVERIFY(ConfigDefaults::dragActivationModifier() <= 10);
        QVERIFY(ConfigDefaults::dragActivationMouseButton() >= 0);
        QVERIFY(ConfigDefaults::dragActivationMouseButton() <= 128);
        QVERIFY(ConfigDefaults::zoneSpanModifier() >= 0);
        QVERIFY(ConfigDefaults::zoneSpanModifier() <= 10);

        // Display
        QVERIFY(ConfigDefaults::osdStyle() >= 0);
        QVERIFY(ConfigDefaults::osdStyle() <= 2);

        // Appearance
        QVERIFY(ConfigDefaults::activeOpacity() >= 0.0);
        QVERIFY(ConfigDefaults::activeOpacity() <= 1.0);
        QVERIFY(ConfigDefaults::inactiveOpacity() >= 0.0);
        QVERIFY(ConfigDefaults::inactiveOpacity() <= 1.0);
        QVERIFY(ConfigDefaults::borderWidth() >= 0);
        QVERIFY(ConfigDefaults::borderWidth() <= 10);
        QVERIFY(ConfigDefaults::borderRadius() >= 0);
        QVERIFY(ConfigDefaults::borderRadius() <= 50);
        QVERIFY(ConfigDefaults::labelFontSizeScale() >= 0.25);
        QVERIFY(ConfigDefaults::labelFontSizeScale() <= 3.0);
        QVERIFY(ConfigDefaults::labelFontWeight() >= 100);
        QVERIFY(ConfigDefaults::labelFontWeight() <= 900);

        // Zones
        QVERIFY(ConfigDefaults::zonePadding() >= 0);
        QVERIFY(ConfigDefaults::zonePadding() <= 50);
        QVERIFY(ConfigDefaults::outerGap() >= 0);
        QVERIFY(ConfigDefaults::outerGap() <= 50);
        QVERIFY(ConfigDefaults::outerGapTop() >= 0);
        QVERIFY(ConfigDefaults::outerGapTop() <= 50);
        QVERIFY(ConfigDefaults::outerGapBottom() >= 0);
        QVERIFY(ConfigDefaults::outerGapBottom() <= 50);
        QVERIFY(ConfigDefaults::outerGapLeft() >= 0);
        QVERIFY(ConfigDefaults::outerGapLeft() <= 50);
        QVERIFY(ConfigDefaults::outerGapRight() >= 0);
        QVERIFY(ConfigDefaults::outerGapRight() <= 50);
        QVERIFY(ConfigDefaults::adjacentThreshold() >= 5);
        QVERIFY(ConfigDefaults::adjacentThreshold() <= 100);
        QVERIFY(ConfigDefaults::pollIntervalMs() >= 10);
        QVERIFY(ConfigDefaults::pollIntervalMs() <= 1000);
        QVERIFY(ConfigDefaults::minimumZoneSizePx() >= 50);
        QVERIFY(ConfigDefaults::minimumZoneSizePx() <= 500);
        QVERIFY(ConfigDefaults::minimumZoneDisplaySizePx() >= 1);
        QVERIFY(ConfigDefaults::minimumZoneDisplaySizePx() <= 50);

        // Behavior
        QVERIFY(ConfigDefaults::stickyWindowHandling() >= 0);
        QVERIFY(ConfigDefaults::stickyWindowHandling() <= 2);
        QVERIFY(ConfigDefaults::minimumWindowWidth() >= 0);
        QVERIFY(ConfigDefaults::minimumWindowWidth() <= 2000);
        QVERIFY(ConfigDefaults::minimumWindowHeight() >= 0);
        QVERIFY(ConfigDefaults::minimumWindowHeight() <= 2000);

        // Zone Selector
        QVERIFY(ConfigDefaults::triggerDistance() >= 10);
        QVERIFY(ConfigDefaults::triggerDistance() <= 200);
        QVERIFY(ConfigDefaults::position() >= 0);
        QVERIFY(ConfigDefaults::position() <= 8);
        QVERIFY(ConfigDefaults::layoutMode() >= 0);
        QVERIFY(ConfigDefaults::layoutMode() <= 2);
        QVERIFY(ConfigDefaults::sizeMode() >= 0);
        QVERIFY(ConfigDefaults::sizeMode() <= 1);
        QVERIFY(ConfigDefaults::maxRows() >= 1);
        QVERIFY(ConfigDefaults::maxRows() <= 10);
        QVERIFY(ConfigDefaults::previewWidth() >= 80);
        QVERIFY(ConfigDefaults::previewWidth() <= 400);
        QVERIFY(ConfigDefaults::previewHeight() >= 60);
        QVERIFY(ConfigDefaults::previewHeight() <= 300);
        QVERIFY(ConfigDefaults::gridColumns() >= 1);
        QVERIFY(ConfigDefaults::gridColumns() <= 10);

        // Shaders
        QVERIFY(ConfigDefaults::shaderFrameRate() >= 30);
        QVERIFY(ConfigDefaults::shaderFrameRate() <= 144);
        QVERIFY(ConfigDefaults::audioSpectrumBarCount() >= 16);
        QVERIFY(ConfigDefaults::audioSpectrumBarCount() <= 256);

        // Autotiling
        QVERIFY(ConfigDefaults::autotileSplitRatio() >= 0.1);
        QVERIFY(ConfigDefaults::autotileSplitRatio() <= 0.9);
        QVERIFY(ConfigDefaults::autotileMasterCount() >= 1);
        QVERIFY(ConfigDefaults::autotileMasterCount() <= 5);
        QVERIFY(ConfigDefaults::autotileCenteredMasterSplitRatio() >= 0.1);
        QVERIFY(ConfigDefaults::autotileCenteredMasterSplitRatio() <= 0.9);
        QVERIFY(ConfigDefaults::autotileCenteredMasterMasterCount() >= 1);
        QVERIFY(ConfigDefaults::autotileCenteredMasterMasterCount() <= 5);
        QVERIFY(ConfigDefaults::autotileInnerGap() >= 0);
        QVERIFY(ConfigDefaults::autotileInnerGap() <= 50);
        QVERIFY(ConfigDefaults::autotileOuterGap() >= 0);
        QVERIFY(ConfigDefaults::autotileOuterGap() <= 50);
        QVERIFY(ConfigDefaults::autotileMaxWindows() >= 1);
        QVERIFY(ConfigDefaults::autotileMaxWindows() <= 12);
        QVERIFY(ConfigDefaults::autotileInsertPosition() >= 0);
        QVERIFY(ConfigDefaults::autotileInsertPosition() <= 2);
        QVERIFY(ConfigDefaults::autotileBorderWidth() >= 0);
        QVERIFY(ConfigDefaults::autotileBorderWidth() <= 10);

        // Animations
        QVERIFY(ConfigDefaults::animationDuration() >= 50);
        QVERIFY(ConfigDefaults::animationDuration() <= 500);
        QVERIFY(ConfigDefaults::animationMinDistance() >= 0);
        QVERIFY(ConfigDefaults::animationMinDistance() <= 200);
        QVERIFY(ConfigDefaults::animationSequenceMode() >= 0);
        QVERIFY(ConfigDefaults::animationSequenceMode() <= 1);
        QVERIFY(ConfigDefaults::animationStaggerInterval() >= 10);
        QVERIFY(ConfigDefaults::animationStaggerInterval() <= 200);

        // Mode Tracking
        QVERIFY(ConfigDefaults::lastTilingMode() >= 0);
        QVERIFY(ConfigDefaults::lastTilingMode() <= 1);
    }

    /**
     * autotileSplitRatio default must be 0.5 (matching the .kcfg <default>0.5</default>).
     */
    void testAutotileSplitRatio_default_is0point5()
    {
        QVERIFY(qFuzzyCompare(ConfigDefaults::autotileSplitRatio(), 0.5));
    }

    /**
     * autotileMasterCount default must be 1 (matching the .kcfg <default>1</default>).
     */
    void testAutotileMasterCount_default_is1()
    {
        QCOMPARE(ConfigDefaults::autotileMasterCount(), 1);
    }
};

QTEST_MAIN(TestConfigDefaults)
#include "test_configdefaults.moc"
