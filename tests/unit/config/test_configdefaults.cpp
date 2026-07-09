// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_configdefaults.cpp
 * @brief Unit tests for the ConfigDefaults class (config/configdefaults.h)
 *
 * Tests verify that ConfigDefaults accessors return the expected values and
 * that all numeric defaults fall within their declared min/max bounds.
 */

#include <QTest>

#include "../../../src/config/configdefaults.h"
#include "../../../src/core/settings_interfaces.h" // ZoneSelectorConfig struct tripwire

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
        // Activation. Enum-valued settings derive their bounds from the enum
        // (not magic literals) so the tripwire tracks the enum if it grows.
        QVERIFY(ConfigDefaults::zoneSpanModifier() >= static_cast<int>(DragModifier::Disabled));
        QVERIFY(ConfigDefaults::zoneSpanModifier() <= static_cast<int>(DragModifier::CtrlAltMeta));

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
        QVERIFY(ConfigDefaults::innerGap() >= ConfigDefaults::innerGapMin());
        QVERIFY(ConfigDefaults::innerGap() <= ConfigDefaults::innerGapMax());
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
        QVERIFY(ConfigDefaults::snappingStickyWindowHandling()
                >= static_cast<int>(StickyWindowHandling::TreatAsNormal));
        QVERIFY(ConfigDefaults::snappingStickyWindowHandling() <= static_cast<int>(StickyWindowHandling::IgnoreAll));
        QVERIFY(ConfigDefaults::minimumWindowWidth() >= ConfigDefaults::minimumWindowWidthMin());
        QVERIFY(ConfigDefaults::minimumWindowWidth() <= ConfigDefaults::minimumWindowWidthMax());
        QVERIFY(ConfigDefaults::minimumWindowHeight() >= ConfigDefaults::minimumWindowHeightMin());
        QVERIFY(ConfigDefaults::minimumWindowHeight() <= ConfigDefaults::minimumWindowHeightMax());

        // Decoration window filtering
        QVERIFY(ConfigDefaults::decorationMinimumWindowWidth() >= ConfigDefaults::decorationMinimumWindowWidthMin());
        QVERIFY(ConfigDefaults::decorationMinimumWindowWidth() <= ConfigDefaults::decorationMinimumWindowWidthMax());
        QVERIFY(ConfigDefaults::decorationMinimumWindowHeight() >= ConfigDefaults::decorationMinimumWindowHeightMin());
        QVERIFY(ConfigDefaults::decorationMinimumWindowHeight() <= ConfigDefaults::decorationMinimumWindowHeightMax());

        // PhosphorZones::Zone Selector
        QVERIFY(ConfigDefaults::triggerDistance() >= ConfigDefaults::triggerDistanceMin());
        QVERIFY(ConfigDefaults::triggerDistance() <= ConfigDefaults::triggerDistanceMax());
        QVERIFY(ConfigDefaults::position() >= static_cast<int>(ZoneSelectorPosition::TopLeft));
        QVERIFY(ConfigDefaults::position() <= static_cast<int>(ZoneSelectorPosition::BottomRight));
        QVERIFY(ConfigDefaults::layoutMode() >= static_cast<int>(ZoneSelectorLayoutMode::Grid));
        QVERIFY(ConfigDefaults::layoutMode() <= static_cast<int>(ZoneSelectorLayoutMode::Vertical));
        QVERIFY(ConfigDefaults::sizeMode() >= static_cast<int>(ZoneSelectorSizeMode::Auto));
        QVERIFY(ConfigDefaults::sizeMode() <= static_cast<int>(ZoneSelectorSizeMode::Manual));
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

        // Inner/outer gaps are unified with snapping (tested above as innerGap/outerGap).
        QVERIFY(ConfigDefaults::autotileMaxWindows() >= ConfigDefaults::autotileMaxWindowsMin());
        QVERIFY(ConfigDefaults::autotileMaxWindows() <= ConfigDefaults::autotileMaxWindowsMax());
        QVERIFY(ConfigDefaults::autotileInsertPosition() >= ConfigDefaults::autotileInsertPositionMin());
        QVERIFY(ConfigDefaults::autotileInsertPosition() <= ConfigDefaults::autotileInsertPositionMax());

        // Animations
        QVERIFY(ConfigDefaults::animationDuration() >= ConfigDefaults::animationDurationMin());
        QVERIFY(ConfigDefaults::animationDuration() <= ConfigDefaults::animationDurationMax());
        QVERIFY(ConfigDefaults::animationMinDistance() >= ConfigDefaults::animationMinDistanceMin());
        QVERIFY(ConfigDefaults::animationMinDistance() <= ConfigDefaults::animationMinDistanceMax());
        QVERIFY(ConfigDefaults::animationSequenceMode() >= ConfigDefaults::animationSequenceModeMin());
        QVERIFY(ConfigDefaults::animationSequenceMode() <= ConfigDefaults::animationSequenceModeMax());
        QVERIFY(ConfigDefaults::animationStaggerInterval() >= ConfigDefaults::animationStaggerIntervalMin());
        QVERIFY(ConfigDefaults::animationStaggerInterval() <= ConfigDefaults::animationStaggerIntervalMax());

        // Window decoration focus cross-fade
        QVERIFY(ConfigDefaults::focusFadeDuration() >= ConfigDefaults::focusFadeDurationMin());
        QVERIFY(ConfigDefaults::focusFadeDuration() <= ConfigDefaults::focusFadeDurationMax());
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

    /**
     * Zone span toggle mode (#563) is opt-in: the default must be false so the
     * span modifier keeps its hold-to-span behaviour unless the user enables it.
     */
    void testZoneSpanToggleMode_default_isFalse()
    {
        QCOMPARE(ConfigDefaults::zoneSpanToggleMode(), false);
    }

    /**
     * ZoneSelectorConfig's in-struct fallback defaults duplicate the
     * ConfigDefaults accessors by value (core cannot include the config layer
     * to delegate). This tripwire keeps a retuned ConfigDefault from silently
     * diverging from the struct fallback the overlay falls back to.
     */
    void testZoneSelectorConfigStructDefaultsMatchAccessors()
    {
        const ZoneSelectorConfig cfg;
        QCOMPARE(cfg.position, ConfigDefaults::position());
        QCOMPARE(cfg.layoutMode, ConfigDefaults::layoutMode());
        QCOMPARE(cfg.sizeMode, ConfigDefaults::sizeMode());
        QCOMPARE(cfg.maxRows, ConfigDefaults::maxRows());
        QCOMPARE(cfg.previewWidth, ConfigDefaults::previewWidth());
        QCOMPARE(cfg.previewHeight, ConfigDefaults::previewHeight());
        QCOMPARE(cfg.previewLockAspect, ConfigDefaults::previewLockAspect());
        QCOMPARE(cfg.gridColumns, ConfigDefaults::gridColumns());
        QCOMPARE(cfg.triggerDistance, ConfigDefaults::triggerDistance());
    }
};

QTEST_GUILESS_MAIN(TestConfigDefaults)
#include "test_configdefaults.moc"
