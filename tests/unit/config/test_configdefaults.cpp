// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_configdefaults.cpp
 * @brief Unit tests for the ConfigDefaults class (config/configdefaults.h)
 *
 * Tests verify that ConfigDefaults accessors return the expected values and
 * that all numeric defaults fall within their declared min/max bounds.
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
     * Every numeric default must fall within the min/max bounds declared in
     * ConfigDefaults. This prevents drift between default values and their bounds.
     */
    void testAllDefaults_withinKcfgBounds()
    {
        // Activation
        QVERIFY(ConfigDefaults::dragActivationModifier() >= 0);
        QVERIFY(ConfigDefaults::dragActivationModifier() <= 10);
        QVERIFY(ConfigDefaults::dragActivationMouseButton() >= 0);
        QVERIFY(ConfigDefaults::dragActivationMouseButton() <= ConfigDefaults::mouseButtonMax());
        QVERIFY(ConfigDefaults::zoneSpanModifier() >= 0);
        QVERIFY(ConfigDefaults::zoneSpanModifier() <= 10);

        // Display
        QVERIFY(ConfigDefaults::osdStyle() >= ConfigDefaults::osdStyleMin());
        QVERIFY(ConfigDefaults::osdStyle() <= ConfigDefaults::osdStyleMax());

        // Appearance
        QVERIFY(ConfigDefaults::activeOpacity() >= ConfigDefaults::activeOpacityMin());
        QVERIFY(ConfigDefaults::activeOpacity() <= ConfigDefaults::activeOpacityMax());
        QVERIFY(ConfigDefaults::inactiveOpacity() >= ConfigDefaults::inactiveOpacityMin());
        QVERIFY(ConfigDefaults::inactiveOpacity() <= ConfigDefaults::inactiveOpacityMax());
        QVERIFY(ConfigDefaults::borderWidth() >= ConfigDefaults::borderWidthMin());
        QVERIFY(ConfigDefaults::borderWidth() <= ConfigDefaults::borderWidthMax());
        QVERIFY(ConfigDefaults::borderRadius() >= ConfigDefaults::borderRadiusMin());
        QVERIFY(ConfigDefaults::borderRadius() <= ConfigDefaults::borderRadiusMax());
        QVERIFY(ConfigDefaults::labelFontSizeScale() >= ConfigDefaults::labelFontSizeScaleMin());
        QVERIFY(ConfigDefaults::labelFontSizeScale() <= ConfigDefaults::labelFontSizeScaleMax());
        QVERIFY(ConfigDefaults::labelFontWeight() >= ConfigDefaults::labelFontWeightMin());
        QVERIFY(ConfigDefaults::labelFontWeight() <= ConfigDefaults::labelFontWeightMax());

        // Zones
        QVERIFY(ConfigDefaults::zonePadding() >= ConfigDefaults::zonePaddingMin());
        QVERIFY(ConfigDefaults::zonePadding() <= ConfigDefaults::zonePaddingMax());
        QVERIFY(ConfigDefaults::outerGap() >= ConfigDefaults::outerGapMin());
        QVERIFY(ConfigDefaults::outerGap() <= ConfigDefaults::outerGapMax());
        QVERIFY(ConfigDefaults::outerGapTop() >= ConfigDefaults::outerGapTopMin());
        QVERIFY(ConfigDefaults::outerGapTop() <= ConfigDefaults::outerGapTopMax());
        QVERIFY(ConfigDefaults::outerGapBottom() >= ConfigDefaults::outerGapBottomMin());
        QVERIFY(ConfigDefaults::outerGapBottom() <= ConfigDefaults::outerGapBottomMax());
        QVERIFY(ConfigDefaults::outerGapLeft() >= ConfigDefaults::outerGapLeftMin());
        QVERIFY(ConfigDefaults::outerGapLeft() <= ConfigDefaults::outerGapLeftMax());
        QVERIFY(ConfigDefaults::outerGapRight() >= ConfigDefaults::outerGapRightMin());
        QVERIFY(ConfigDefaults::outerGapRight() <= ConfigDefaults::outerGapRightMax());
        QVERIFY(ConfigDefaults::adjacentThreshold() >= ConfigDefaults::adjacentThresholdMin());
        QVERIFY(ConfigDefaults::adjacentThreshold() <= ConfigDefaults::adjacentThresholdMax());
        QVERIFY(ConfigDefaults::pollIntervalMs() >= ConfigDefaults::pollIntervalMsMin());
        QVERIFY(ConfigDefaults::pollIntervalMs() <= ConfigDefaults::pollIntervalMsMax());
        QVERIFY(ConfigDefaults::minimumZoneSizePx() >= ConfigDefaults::minimumZoneSizePxMin());
        QVERIFY(ConfigDefaults::minimumZoneSizePx() <= ConfigDefaults::minimumZoneSizePxMax());
        QVERIFY(ConfigDefaults::minimumZoneDisplaySizePx() >= ConfigDefaults::minimumZoneDisplaySizePxMin());
        QVERIFY(ConfigDefaults::minimumZoneDisplaySizePx() <= ConfigDefaults::minimumZoneDisplaySizePxMax());

        // Behavior
        QVERIFY(ConfigDefaults::stickyWindowHandling() >= 0);
        QVERIFY(ConfigDefaults::stickyWindowHandling() <= 2);
        QVERIFY(ConfigDefaults::minimumWindowWidth() >= ConfigDefaults::minimumWindowWidthMin());
        QVERIFY(ConfigDefaults::minimumWindowWidth() <= ConfigDefaults::minimumWindowWidthMax());
        QVERIFY(ConfigDefaults::minimumWindowHeight() >= ConfigDefaults::minimumWindowHeightMin());
        QVERIFY(ConfigDefaults::minimumWindowHeight() <= ConfigDefaults::minimumWindowHeightMax());

        // Zone Selector
        QVERIFY(ConfigDefaults::triggerDistance() >= ConfigDefaults::triggerDistanceMin());
        QVERIFY(ConfigDefaults::triggerDistance() <= ConfigDefaults::triggerDistanceMax());
        QVERIFY(ConfigDefaults::position() >= 0);
        QVERIFY(ConfigDefaults::position() <= 8);
        QVERIFY(ConfigDefaults::layoutMode() >= 0);
        QVERIFY(ConfigDefaults::layoutMode() <= 2);
        QVERIFY(ConfigDefaults::sizeMode() >= 0);
        QVERIFY(ConfigDefaults::sizeMode() <= 1);
        QVERIFY(ConfigDefaults::maxRows() >= ConfigDefaults::maxRowsMin());
        QVERIFY(ConfigDefaults::maxRows() <= ConfigDefaults::maxRowsMax());
        QVERIFY(ConfigDefaults::previewWidth() >= ConfigDefaults::previewWidthMin());
        QVERIFY(ConfigDefaults::previewWidth() <= ConfigDefaults::previewWidthMax());
        QVERIFY(ConfigDefaults::previewHeight() >= ConfigDefaults::previewHeightMin());
        QVERIFY(ConfigDefaults::previewHeight() <= ConfigDefaults::previewHeightMax());
        QVERIFY(ConfigDefaults::gridColumns() >= ConfigDefaults::gridColumnsMin());
        QVERIFY(ConfigDefaults::gridColumns() <= ConfigDefaults::gridColumnsMax());

        // Shaders
        QVERIFY(ConfigDefaults::shaderFrameRate() >= ConfigDefaults::shaderFrameRateMin());
        QVERIFY(ConfigDefaults::shaderFrameRate() <= ConfigDefaults::shaderFrameRateMax());
        QVERIFY(ConfigDefaults::audioSpectrumBarCount() >= ConfigDefaults::audioSpectrumBarCountMin());
        QVERIFY(ConfigDefaults::audioSpectrumBarCount() <= ConfigDefaults::audioSpectrumBarCountMax());

        // Autotiling
        QVERIFY(ConfigDefaults::autotileSplitRatio() >= ConfigDefaults::autotileSplitRatioMin());
        QVERIFY(ConfigDefaults::autotileSplitRatio() <= ConfigDefaults::autotileSplitRatioMax());
        QVERIFY(ConfigDefaults::autotileMasterCount() >= ConfigDefaults::autotileMasterCountMin());
        QVERIFY(ConfigDefaults::autotileMasterCount() <= ConfigDefaults::autotileMasterCountMax());
        QVERIFY(ConfigDefaults::autotileCenteredMasterSplitRatio()
                >= ConfigDefaults::autotileCenteredMasterSplitRatioMin());
        QVERIFY(ConfigDefaults::autotileCenteredMasterSplitRatio()
                <= ConfigDefaults::autotileCenteredMasterSplitRatioMax());
        QVERIFY(ConfigDefaults::autotileCenteredMasterMasterCount()
                >= ConfigDefaults::autotileCenteredMasterMasterCountMin());
        QVERIFY(ConfigDefaults::autotileCenteredMasterMasterCount()
                <= ConfigDefaults::autotileCenteredMasterMasterCountMax());
        QVERIFY(ConfigDefaults::autotileInnerGap() >= ConfigDefaults::autotileInnerGapMin());
        QVERIFY(ConfigDefaults::autotileInnerGap() <= ConfigDefaults::autotileInnerGapMax());
        QVERIFY(ConfigDefaults::autotileOuterGap() >= ConfigDefaults::autotileOuterGapMin());
        QVERIFY(ConfigDefaults::autotileOuterGap() <= ConfigDefaults::autotileOuterGapMax());
        QVERIFY(ConfigDefaults::autotileMaxWindows() >= ConfigDefaults::autotileMaxWindowsMin());
        QVERIFY(ConfigDefaults::autotileMaxWindows() <= ConfigDefaults::autotileMaxWindowsMax());
        QVERIFY(ConfigDefaults::autotileInsertPosition() >= ConfigDefaults::autotileInsertPositionMin());
        QVERIFY(ConfigDefaults::autotileInsertPosition() <= ConfigDefaults::autotileInsertPositionMax());
        QVERIFY(ConfigDefaults::autotileBorderWidth() >= ConfigDefaults::autotileBorderWidthMin());
        QVERIFY(ConfigDefaults::autotileBorderWidth() <= ConfigDefaults::autotileBorderWidthMax());
        QVERIFY(ConfigDefaults::autotileBorderRadius() >= ConfigDefaults::autotileBorderRadiusMin());
        QVERIFY(ConfigDefaults::autotileBorderRadius() <= ConfigDefaults::autotileBorderRadiusMax());
        QVERIFY(ConfigDefaults::autotileOuterGapTop() >= ConfigDefaults::autotileOuterGapTopMin());
        QVERIFY(ConfigDefaults::autotileOuterGapTop() <= ConfigDefaults::autotileOuterGapTopMax());
        QVERIFY(ConfigDefaults::autotileOuterGapBottom() >= ConfigDefaults::autotileOuterGapBottomMin());
        QVERIFY(ConfigDefaults::autotileOuterGapBottom() <= ConfigDefaults::autotileOuterGapBottomMax());
        QVERIFY(ConfigDefaults::autotileOuterGapLeft() >= ConfigDefaults::autotileOuterGapLeftMin());
        QVERIFY(ConfigDefaults::autotileOuterGapLeft() <= ConfigDefaults::autotileOuterGapLeftMax());
        QVERIFY(ConfigDefaults::autotileOuterGapRight() >= ConfigDefaults::autotileOuterGapRightMin());
        QVERIFY(ConfigDefaults::autotileOuterGapRight() <= ConfigDefaults::autotileOuterGapRightMax());

        // Animations
        QVERIFY(ConfigDefaults::animationDuration() >= ConfigDefaults::animationDurationMin());
        QVERIFY(ConfigDefaults::animationDuration() <= ConfigDefaults::animationDurationMax());
        QVERIFY(ConfigDefaults::animationMinDistance() >= ConfigDefaults::animationMinDistanceMin());
        QVERIFY(ConfigDefaults::animationMinDistance() <= ConfigDefaults::animationMinDistanceMax());
        QVERIFY(ConfigDefaults::animationSequenceMode() >= ConfigDefaults::animationSequenceModeMin());
        QVERIFY(ConfigDefaults::animationSequenceMode() <= ConfigDefaults::animationSequenceModeMax());
        QVERIFY(ConfigDefaults::animationStaggerInterval() >= ConfigDefaults::animationStaggerIntervalMin());
        QVERIFY(ConfigDefaults::animationStaggerInterval() <= ConfigDefaults::animationStaggerIntervalMax());
    }

    /**
     * autotileSplitRatio default must be 0.5.
     */
    void testAutotileSplitRatio_default_is0point5()
    {
        QVERIFY(qFuzzyCompare(ConfigDefaults::autotileSplitRatio(), 0.5));
    }

    /**
     * autotileMasterCount default must be 1.
     */
    void testAutotileMasterCount_default_is1()
    {
        QCOMPARE(ConfigDefaults::autotileMasterCount(), 1);
    }
};

QTEST_MAIN(TestConfigDefaults)
#include "test_configdefaults.moc"
