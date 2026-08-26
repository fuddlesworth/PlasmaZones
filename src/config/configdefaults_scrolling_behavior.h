// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "configdefaults_scrolling.h"

namespace PlasmaZones {

// Chain link 7: the Scrolling.Behavior tunables (window handling, focus,
// edge auto-scroll, the step percents) and the Scrolling.ZoneSelector strip
// popup. Split out of configdefaults_scrolling.h (link 6) once that file
// passed the size ceiling; the Shortcuts.Scrolling chord defaults are link 8
// (configdefaults_scrolling_shortcuts.h). Every accessor here reaches call
// sites through the ConfigDefaults leaf as before, so no consumer changes.
class ConfigDefaultsScrollingBehavior : public ConfigDefaultsScrolling
{
public:
    // ═══════════════════════════════════════════════════════════════════════════
    // Scrolling behavior (Scrolling.Behavior)
    //
    // The strip's window-handling and focus knobs, the peers of the
    // Tiling.Behavior and Snapping.Behavior.WindowHandling families. Key
    // NAMES are the shared leaf spellings (FocusNewWindows, StickyWindowHandling,
    // …) under the Scrolling.Behavior group. Defaults deliberately match the
    // autotile canonical so a screen flipped between the two engines starts
    // from identical behavior.
    // ═══════════════════════════════════════════════════════════════════════════

    static constexpr bool scrollingFocusNewWindows()
    {
        return true;
    }

    // ── Edge auto-scroll during a drag re-insert (Scrolling.Behavior.DragScroll) ──
    //
    // niri's dnd-edge-view-scroll defaults, verbatim. Holding a dragged
    // window near either edge of the work area scrolls the strip, so the
    // drop can reach a column that is off screen. Speed ramps linearly from
    // zero at the band's inner edge to the maximum at the work area's edge.
    // The engine holds the same four figures as the IScrollSettings
    // defaults, so a stub that never heard of this schema still scrolls the
    // way niri does. The static_asserts in settings/scrolling.cpp compare the
    // two sides directly rather than against copied literals, so an edit to
    // EITHER file fails the build and there is no manual sync to remember.
    static constexpr bool scrollingDragScrollEnabled()
    {
        return true;
    }
    static constexpr int scrollingDragScrollTriggerWidth()
    {
        return 30;
    }
    static constexpr int scrollingDragScrollTriggerWidthMin()
    {
        return 1;
    }
    /// UI range ceiling, not an engine limit: the engine re-clamps the width
    /// to a third of the work area's main extent at tick time, so values
    /// past that are silently narrowed on small screens either way.
    static constexpr int scrollingDragScrollTriggerWidthMax()
    {
        return 300;
    }
    static constexpr int scrollingDragScrollDelayMs()
    {
        return 100;
    }
    static constexpr int scrollingDragScrollDelayMsMin()
    {
        return 0;
    }
    static constexpr int scrollingDragScrollDelayMsMax()
    {
        return 2000;
    }
    static constexpr int scrollingDragScrollMaxSpeed()
    {
        return 1500;
    }
    /// UI usability floor, not the engine's: the engine floors the speed at
    /// 1 px/s (engine_core clamps independently), but a slider that can
    /// offer a sub-perceptible crawl reads as broken, so the settings range
    /// starts where the motion is visible.
    static constexpr int scrollingDragScrollMaxSpeedMin()
    {
        return 50;
    }
    static constexpr int scrollingDragScrollMaxSpeedMax()
    {
        return 10000;
    }
    /// ScrollInsertPosition wire values (0 = right of active, 1 = left of
    /// active, 2 = first, 3 = last, 4 = into active column); the schema
    /// static_asserts them against the engine enumerators. Right-of-active
    /// must stay 0 so an absent key preserves the historical behavior.
    static constexpr int scrollingInsertRightOfActive()
    {
        return 0;
    }
    static constexpr int scrollingInsertLeftOfActive()
    {
        return 1;
    }
    static constexpr int scrollingInsertFirst()
    {
        return 2;
    }
    static constexpr int scrollingInsertLast()
    {
        return 3;
    }
    static constexpr int scrollingInsertIntoActiveColumn()
    {
        return 4;
    }
    static constexpr int scrollingInsertPosition()
    {
        return scrollingInsertRightOfActive();
    }
    /// Closed-set validity check (see isValidScrollingCenterFocusedColumn).
    static constexpr bool isValidScrollingInsertPosition(int v)
    {
        return v == scrollingInsertRightOfActive() || v == scrollingInsertLeftOfActive() || v == scrollingInsertFirst()
            || v == scrollingInsertLast() || v == scrollingInsertIntoActiveColumn();
    }
    static constexpr bool scrollingFocusFollowsMouse()
    {
        return false;
    }
    /// The furthest focus-follows-mouse may scroll the strip to bring the
    /// window under the pointer into view, as a percent of the viewport's
    /// extent along the strip (niri's `max-scroll-amount`). Above the cap the
    /// pointer is ignored and focus stays put, which keeps a graze along a
    /// column that is mostly off screen from yanking the whole strip.
    ///
    /// 100 is the "no cap" default and the top of the range, not an arbitrary
    /// ceiling: the window under the pointer is by definition at least partly
    /// on screen, so the centering policy never has to move the view a full
    /// viewport to bring it in. 0 is the other end, meaning focus only follows
    /// the pointer onto windows already fully in view.
    static constexpr int scrollingFocusFollowsMouseMaxScroll()
    {
        return 100;
    }
    static constexpr int scrollingFocusFollowsMouseMaxScrollMin()
    {
        return 0;
    }
    static constexpr int scrollingFocusFollowsMouseMaxScrollMax()
    {
        return 100;
    }
    /// StickyWindowHandling wire values, the shared PhosphorEngine enum's
    /// spelling (0 = treat as normal, 1 = restore only, 2 = ignore all).
    /// Named for the same reason as the CenterFocusedColumn values in configdefaults_scrolling.h;
    /// settingsschema_scrolling.cpp static_asserts them against the engine
    /// enumerators.
    static constexpr int scrollingStickyTreatAsNormal()
    {
        return 0;
    }
    static constexpr int scrollingStickyRestoreOnly()
    {
        return 1;
    }
    static constexpr int scrollingStickyIgnoreAll()
    {
        return 2;
    }
    static constexpr int scrollingStickyWindowHandling()
    {
        return scrollingStickyTreatAsNormal();
    }
    /// Closed-set validity check (see isValidScrollingCenterFocusedColumn).
    static constexpr bool isValidScrollingStickyWindowHandling(int v)
    {
        return v == scrollingStickyTreatAsNormal() || v == scrollingStickyRestoreOnly()
            || v == scrollingStickyIgnoreAll();
    }
    static constexpr bool scrollingRespectMinimumSize()
    {
        return true;
    }
    /// Restore the persisted strip snapshot (column order, widths, tab
    /// stacks, focus) when windows reopen after a restart. Gates only the
    /// cross-session restore; in-session mode round-trips always restore.
    static constexpr bool scrollingRestoreStripsOnLogin()
    {
        return true;
    }
    /// Restore a scroll-FLOATED window to its previous position on reopen.
    /// The scroll twin of autotileRestoreFloatedWindowsOnLogin, gating the
    /// engine's geometry move on the floating-reopen branch (the float flag
    /// itself is never gated). The literal matches the autotile canonical's
    /// value (it lives in the leaf configdefaults.h, which this chain link
    /// cannot reference).
    static constexpr bool scrollingRestoreFloatedWindowsOnLogin()
    {
        return true;
    }
    /// Keep a scroll-floated window stacked above the strip. The scroll twin
    /// of autotileKeepFloatingAbove, read by the KWin effect only. A literal
    /// rather than a delegation because the canonical lives in the leaf
    /// configdefaults.h, which this chain link cannot reference; the parity is
    /// pinned by a static_assert in settings/scrolling.cpp.
    static constexpr bool scrollingKeepFloatingAbove()
    {
        return false;
    }
    /// Percent of the work-area extent one increase/decrease shortcut press
    /// moves a column width or window height. Daemon-side only: the engine
    /// receives an already-computed delta, so these never enter
    /// IScrollSettings. Distinct from the DefaultColumnWidth*Step editor
    /// granularity accessors in configdefaults_scrolling.h, which size QML
    /// control notches.
    static constexpr int scrollingColumnWidthStepPercent()
    {
        return 10;
    }
    static constexpr int scrollingWindowHeightStepPercent()
    {
        return 10;
    }
    /// Percent of the work area's extent along the strip one Meta+Shift+wheel
    /// notch (the effect's scrollView D-Bus call) pans the view by, focus
    /// unchanged. The keyboard page pair moves a whole viewport and never
    /// reads this. Shares the sizing pair's [min, max] range. Larger than the
    /// sizing steps' 10 because the subject is the whole viewport rather than
    /// one column: at 10 a press moves a tenth of a screen, which reads as a
    /// nudge, where a quarter reveals a meaningful slice of the next column
    /// without skipping it.
    static constexpr int scrollingViewScrollStepPercent()
    {
        return 25;
    }
    static constexpr int scrollingStepPercentMin()
    {
        return 1;
    }
    static constexpr int scrollingStepPercentMax()
    {
        return 50;
    }

    // ═══════════════════════════════════════════════════════════════════════════
    // Scrolling.ZoneSelector
    //
    // The strip-mode drag selector popup. It mirrors the Snapping.ZoneSelector
    // family minus LayoutMode / GridColumns / MaxRows, because the strip popup
    // is always a single card row along the strip (so it follows the screen's
    // strip axis, but never becomes a grid). The values below hand-duplicate
    // the snapping twins in configdefaults.h (zoneSelectorEnabled(),
    // triggerDistance(), position(), sizeMode(), previewWidth(),
    // previewHeight(), previewLockAspect()): that file is the LEAF of the
    // ConfigDefaults chain and this header is link 7, so it cannot see them.
    // The two families are independent knobs and are free to diverge.
    // The ranges are NOT duplicated — the schema clamps these keys with the
    // shared triggerDistanceMin/Max and previewWidth/Height Min/Max accessors.
    // ═══════════════════════════════════════════════════════════════════════════

    /// On by default, like the snapping selector, and it needs no user
    /// opt-out to stay out of the way: the engine capability gate
    /// (providesDragInsertSelector) keeps it invisible on every screen that is
    /// not running the scrolling engine.
    static constexpr bool scrollingZoneSelectorEnabled()
    {
        return true;
    }
    static constexpr int scrollingZoneSelectorTriggerDistance()
    {
        return 50;
    }
    /// ZoneSelectorPosition::Top.
    static constexpr int scrollingZoneSelectorPosition()
    {
        return 1;
    }
    /// ZoneSelectorSizeMode::Auto.
    static constexpr int scrollingZoneSelectorSizeMode()
    {
        return 0;
    }
    static constexpr int scrollingZoneSelectorPreviewWidth()
    {
        return 180;
    }
    static constexpr int scrollingZoneSelectorPreviewHeight()
    {
        return 101;
    }
    static constexpr bool scrollingZoneSelectorPreviewLockAspect()
    {
        return true;
    }
};

// Compile-time bound and closed-set checks for the defaults above, the same
// guard the rest of the chain carries: a default edited out of its own
// declared range fails the build instead of being silently snapped by the
// schema on first read.
static_assert(ConfigDefaultsScrollingBehavior::isValidScrollingStickyWindowHandling(
                  ConfigDefaultsScrollingBehavior::scrollingStickyWindowHandling()),
              "ConfigDefaults::scrollingStickyWindowHandling() is not in its own closed set");
static_assert(ConfigDefaultsScrollingBehavior::isValidScrollingInsertPosition(
                  ConfigDefaultsScrollingBehavior::scrollingInsertPosition()),
              "ConfigDefaults::scrollingInsertPosition() is not in its own closed set");
static_assert(ConfigDefaultsScrollingBehavior::scrollingColumnWidthStepPercent()
                      >= ConfigDefaultsScrollingBehavior::scrollingStepPercentMin()
                  && ConfigDefaultsScrollingBehavior::scrollingColumnWidthStepPercent()
                      <= ConfigDefaultsScrollingBehavior::scrollingStepPercentMax(),
              "ConfigDefaults::scrollingColumnWidthStepPercent() outside the declared [min, max] range");
static_assert(ConfigDefaultsScrollingBehavior::scrollingWindowHeightStepPercent()
                      >= ConfigDefaultsScrollingBehavior::scrollingStepPercentMin()
                  && ConfigDefaultsScrollingBehavior::scrollingWindowHeightStepPercent()
                      <= ConfigDefaultsScrollingBehavior::scrollingStepPercentMax(),
              "ConfigDefaults::scrollingWindowHeightStepPercent() outside the declared [min, max] range");
static_assert(ConfigDefaultsScrollingBehavior::scrollingViewScrollStepPercent()
                      >= ConfigDefaultsScrollingBehavior::scrollingStepPercentMin()
                  && ConfigDefaultsScrollingBehavior::scrollingViewScrollStepPercent()
                      <= ConfigDefaultsScrollingBehavior::scrollingStepPercentMax(),
              "ConfigDefaults::scrollingViewScrollStepPercent() outside the declared [min, max] range");
// The focus-follows-mouse scroll cap. Worth stating why it needs its own
// guard even though its default EQUALS its maximum: that equality is exactly
// what makes every other check degenerate. The schema clamp, and the schema
// test's validator rows, all compare 100 against 100 today, so lowering the
// maximum alone would leave the default outside its own range with nothing in
// the tree objecting — the schema would simply snap every read down.
static_assert(ConfigDefaultsScrollingBehavior::scrollingFocusFollowsMouseMaxScroll()
                      >= ConfigDefaultsScrollingBehavior::scrollingFocusFollowsMouseMaxScrollMin()
                  && ConfigDefaultsScrollingBehavior::scrollingFocusFollowsMouseMaxScroll()
                      <= ConfigDefaultsScrollingBehavior::scrollingFocusFollowsMouseMaxScrollMax(),
              "ConfigDefaults::scrollingFocusFollowsMouseMaxScroll() outside the declared [min, max] range");
// Edge auto-scroll, same guard as every other ranged default here. Without
// these a retuned default outside its own declared range would not fail the
// build; it would be silently snapped by the schema's clamp on first read.
static_assert(ConfigDefaultsScrollingBehavior::scrollingDragScrollTriggerWidth()
                      >= ConfigDefaultsScrollingBehavior::scrollingDragScrollTriggerWidthMin()
                  && ConfigDefaultsScrollingBehavior::scrollingDragScrollTriggerWidth()
                      <= ConfigDefaultsScrollingBehavior::scrollingDragScrollTriggerWidthMax(),
              "ConfigDefaults::scrollingDragScrollTriggerWidth() outside the declared [min, max] range");
static_assert(ConfigDefaultsScrollingBehavior::scrollingDragScrollDelayMs()
                      >= ConfigDefaultsScrollingBehavior::scrollingDragScrollDelayMsMin()
                  && ConfigDefaultsScrollingBehavior::scrollingDragScrollDelayMs()
                      <= ConfigDefaultsScrollingBehavior::scrollingDragScrollDelayMsMax(),
              "ConfigDefaults::scrollingDragScrollDelayMs() outside the declared [min, max] range");
static_assert(ConfigDefaultsScrollingBehavior::scrollingDragScrollMaxSpeed()
                      >= ConfigDefaultsScrollingBehavior::scrollingDragScrollMaxSpeedMin()
                  && ConfigDefaultsScrollingBehavior::scrollingDragScrollMaxSpeed()
                      <= ConfigDefaultsScrollingBehavior::scrollingDragScrollMaxSpeedMax(),
              "ConfigDefaults::scrollingDragScrollMaxSpeed() outside the declared [min, max] range");

} // namespace PlasmaZones
