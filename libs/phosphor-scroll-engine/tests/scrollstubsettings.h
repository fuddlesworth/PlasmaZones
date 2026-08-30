// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

// The shared IScrollSettings stub for the per-screen suites.
//
// Every field is public and seeded with the value that keeps the layout maths
// out of the way (zero gaps, no smart gaps, no minimum-size clamping, never
// center), so a resolved rect is a clean function of the ONE value a case
// drives. Two suites read it: the default-channel tests
// (test_scrollengine_perscreen) and the behaviour-toggle tests
// (test_scrollengine_behaviour), which layer per-screen override maps over
// exactly these globals — a second copy of the stub would let the two suites'
// notion of "the configured default" drift apart, which is the one thing
// those tests are comparing against.

#include <PhosphorEngine/EngineTypes.h>
#include <PhosphorScrollEngine/IScrollSettings.h>
#include <PhosphorScrollEngine/ScrollTypes.h>

#include <QObject>
#include <QStringList>

namespace ScrollTestUtils {

class StubScrollSettings : public QObject, public PhosphorEngine::IScrollSettings
{
    Q_OBJECT
    Q_INTERFACES(PhosphorEngine::IScrollSettings)

public:
    using QObject::QObject;

    int widthKind = static_cast<int>(PhosphorScrollEngine::DefaultWidthKind::Proportion);
    qreal widthValue = 0.25;
    int widthPresetIndex = 0;
    int heightKind = static_cast<int>(PhosphorScrollEngine::DefaultHeightKind::Auto);
    qreal heightValue = 0.0;
    int heightPresetIndex = 0;
    QStringList widthPresets{QStringLiteral("0.25"), QStringLiteral("0.5"), QStringLiteral("0.75")};
    QStringList heightPresets{QStringLiteral("0.25"), QStringLiteral("0.5"), QStringLiteral("0.75")};

    // The behaviour globals the per-screen override keys layer over. Seeded
    // to the inert answer, so a case that flips one on a single screen is
    // asserting the OVERRIDE and not the global.
    bool focusNewWindows = true;
    /// The GLOBAL strip-axis intent (config tri-state: 0 auto / 1 horizontal /
    /// 2 vertical — deliberately NOT the protocol enum's numbering). Seeded to
    /// Auto so the harness's transposed work area decides the axis, which is
    /// what every case that does not name an axis expects. A case driving it
    /// must call engine->refreshConfigFromSettings() afterwards: the engine
    /// caches the read at refresh, not per relayout.
    int stripAxis = 0;
    bool respectMinimumSize = false;
    bool centerShortColumns = false;
    bool smartGaps = false;
    bool alwaysCenterSingleColumn = false;
    bool cropStraddlers = false;
    int centerFocused = static_cast<int>(PhosphorScrollEngine::CenterFocusedColumn::Never);
    int stickyHandling = static_cast<int>(PhosphorEngine::StickyWindowHandling::TreatAsNormal);
    int insertPosition = static_cast<int>(PhosphorScrollEngine::ScrollInsertPosition::RightOfActive);
    int defaultColumnDisplay = static_cast<int>(PhosphorScrollEngine::ColumnDisplay::Normal);
    /// Uniform outer gap. Non-zero only where a case needs smart gaps to have
    /// something to zero — at 0 the setting is unobservable, since a layout
    /// that ignored it entirely would produce the same rects.
    int outerGap = 0;
    int innerGap = 0;

    // Edge auto-scroll (DragScroll). Seeded to the interface defaults so a
    // suite that never touches them behaves exactly like the no-settings
    // fixtures; a case driving one must call refreshConfigFromSettings()
    // afterwards, same as stripAxis.
    /// Close-settle reflow hold. 0 (the interface default) keeps the
    /// historical immediate reflow; the closehold suite drives it non-zero.
    /// A case setting it must call refreshConfigFromSettings() afterwards,
    /// same as stripAxis.
    int closeReflowDelayMs = 0;

    bool dragScrollEnabled = PhosphorEngine::IScrollSettings::kDragScrollEnabledDefault;
    int dragScrollTriggerWidth = PhosphorEngine::IScrollSettings::kDragScrollTriggerWidthDefault;
    int dragScrollDelayMs = PhosphorEngine::IScrollSettings::kDragScrollDelayMsDefault;
    int dragScrollMaxSpeed = PhosphorEngine::IScrollSettings::kDragScrollMaxSpeedDefault;

    int scrollingInnerGap() const override
    {
        return innerGap;
    }
    bool scrollingUsePerSideOuterGap() const override
    {
        return false;
    }
    int scrollingOuterGap() const override
    {
        return outerGap;
    }
    int scrollingOuterGapTop() const override
    {
        return outerGap;
    }
    int scrollingOuterGapBottom() const override
    {
        return outerGap;
    }
    int scrollingOuterGapLeft() const override
    {
        return outerGap;
    }
    int scrollingOuterGapRight() const override
    {
        return outerGap;
    }
    bool scrollingFocusNewWindows() const override
    {
        return focusNewWindows;
    }
    int scrollingStripAxis() const override
    {
        return stripAxis;
    }
    int scrollingStickyWindowHandling() const override
    {
        return stickyHandling;
    }
    bool scrollingRespectMinimumSize() const override
    {
        return respectMinimumSize;
    }
    bool scrollingCenterShortColumns() const override
    {
        return centerShortColumns;
    }
    bool scrollingSmartGaps() const override
    {
        return smartGaps;
    }
    int scrollingCenterFocusedColumn() const override
    {
        return centerFocused;
    }
    bool scrollingAlwaysCenterSingleColumn() const override
    {
        return alwaysCenterSingleColumn;
    }
    bool scrollingCropStraddlers() const override
    {
        return cropStraddlers;
    }
    int scrollingCloseReflowDelayMs() const override
    {
        return closeReflowDelayMs;
    }
    bool scrollingDragScrollEnabled() const override
    {
        return dragScrollEnabled;
    }
    int scrollingDragScrollTriggerWidth() const override
    {
        return dragScrollTriggerWidth;
    }
    int scrollingDragScrollDelayMs() const override
    {
        return dragScrollDelayMs;
    }
    int scrollingDragScrollMaxSpeed() const override
    {
        return dragScrollMaxSpeed;
    }
    int scrollingDefaultColumnWidthKind() const override
    {
        return widthKind;
    }
    qreal scrollingDefaultColumnWidthValue() const override
    {
        return widthValue;
    }
    int scrollingDefaultColumnWidthPresetIndex() const override
    {
        return widthPresetIndex;
    }
    int scrollingDefaultWindowHeightKind() const override
    {
        return heightKind;
    }
    qreal scrollingDefaultWindowHeightValue() const override
    {
        return heightValue;
    }
    int scrollingDefaultWindowHeightPresetIndex() const override
    {
        return heightPresetIndex;
    }
    int scrollingInsertPosition() const override
    {
        return insertPosition;
    }
    int scrollingDefaultColumnDisplay() const override
    {
        return defaultColumnDisplay;
    }
    QStringList scrollingPresetColumnWidths() const override
    {
        return widthPresets;
    }
    QStringList scrollingPresetWindowHeights() const override
    {
        return heightPresets;
    }

    // Tab-indicator geometry. Public fields so a case can drive the indicator
    // the way it drives the width/height trios above; the seeds are the
    // shipped defaults, so an untouched stub behaves like a fresh config.
    bool tabIndicatorEnabled = true;
    bool tabIndicatorHideWhenSingleTab = false;
    bool tabIndicatorPlaceWithinColumn = false;
    int tabIndicatorGap = 5;
    int tabIndicatorWidth = 4;
    qreal tabIndicatorLengthProportion = 0.5;
    int tabIndicatorPosition = static_cast<int>(PhosphorScrollEngine::TabIndicatorPosition::Left);

    bool scrollingTabIndicatorEnabled() const override
    {
        return tabIndicatorEnabled;
    }
    bool scrollingTabIndicatorHideWhenSingleTab() const override
    {
        return tabIndicatorHideWhenSingleTab;
    }
    bool scrollingTabIndicatorPlaceWithinColumn() const override
    {
        return tabIndicatorPlaceWithinColumn;
    }
    int scrollingTabIndicatorGap() const override
    {
        return tabIndicatorGap;
    }
    int scrollingTabIndicatorWidth() const override
    {
        return tabIndicatorWidth;
    }
    qreal scrollingTabIndicatorLengthProportion() const override
    {
        return tabIndicatorLengthProportion;
    }
    int scrollingTabIndicatorPosition() const override
    {
        return tabIndicatorPosition;
    }
};

} // namespace ScrollTestUtils
