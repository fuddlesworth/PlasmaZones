// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "configdefaults_screens.h"

namespace PlasmaZones {

// Chain link 6: the scrolling engine's Scrolling defaults and the
// Shortcuts.Scrolling chord defaults. Split out of configdefaults.h to keep
// that file inside the size ceiling; every accessor here reaches call sites
// through the ConfigDefaults leaf as before, so no consumer changes.
class ConfigDefaultsScrolling : public ConfigDefaultsScreens
{
public:
    // ═══════════════════════════════════════════════════════════════════════════
    // Scrolling (Scrolling)
    // ═══════════════════════════════════════════════════════════════════════════

    /// Master switch for the scrolling placement mode, the peer of
    /// snappingEnabled and autotileEnabled. Off, a Scrolling context
    /// downgrades to Snapping in the daemon's derive pass and the mode
    /// toggle skips the mode.
    static bool scrollingEnabled()
    {
        return true;
    }

    /// CenterFocusedColumn wire values. Named so the predicate below and any
    /// settings-layer branch read against the vocabulary instead of raw ints,
    /// matching the width-kind accessors further down. The schema declares the
    /// same three values through the engine's own enumerators, and
    /// settingsschema_scrolling.cpp static_asserts the two spellings agree.
    static constexpr int scrollingCenterFocusedColumnNever()
    {
        return 0;
    }
    static constexpr int scrollingCenterFocusedColumnAlways()
    {
        return 1;
    }
    static constexpr int scrollingCenterFocusedColumnOnOverflow()
    {
        return 2;
    }
    /// CenterFocusedColumn: 0 = never, 1 = always, 2 = on overflow.
    static constexpr int scrollingCenterFocusedColumn()
    {
        return scrollingCenterFocusedColumnNever();
    }
    /// Closed-set validity check — the SAME enumerator list the schema's
    /// validIntOr closed set uses, so the D-Bus registry guard and the
    /// schema validator cannot drift (a range check would silently accept
    /// any hole a future enum leaves).
    static constexpr bool isValidScrollingCenterFocusedColumn(int v)
    {
        return v == scrollingCenterFocusedColumnNever() || v == scrollingCenterFocusedColumnAlways()
            || v == scrollingCenterFocusedColumnOnOverflow();
    }
    static constexpr bool scrollingAlwaysCenterSingleColumn()
    {
        return false;
    }
    /// Width-kind wire values (0 = proportion, 1 = fixed px, 2 = client
    /// decides). Named so the settings layer's kind-aware branches read
    /// against the vocabulary instead of raw ints; the LGPL engine keeps
    /// its own interpretation of the same values (IScrollSettings docs).
    static constexpr int scrollingWidthKindProportion()
    {
        return 0;
    }
    static constexpr int scrollingWidthKindFixed()
    {
        return 1;
    }
    static constexpr int scrollingWidthKindClientDecides()
    {
        return 2;
    }
    /// Preset kind, appended as 3 (2 is taken by ClientDecides; the engine's
    /// DefaultWidthKind::Preset carries the same value and the schema
    /// static_asserts the pair).
    static constexpr int scrollingWidthKindPreset()
    {
        return 3;
    }
    /// Default column width kind: 0 = proportion, 1 = fixed px,
    /// 2 = client decides, 3 = preset index.
    static constexpr int scrollingDefaultColumnWidthKind()
    {
        return scrollingWidthKindProportion();
    }
    /// Closed-set validity check (see isValidScrollingCenterFocusedColumn).
    static constexpr bool isValidScrollingWidthKind(int v)
    {
        return v == scrollingWidthKindProportion() || v == scrollingWidthKindFixed()
            || v == scrollingWidthKindClientDecides() || v == scrollingWidthKindPreset();
    }
    /// Preset index the Preset width kind opens columns at, clamped by the
    /// schema into [0, presetIndexMax] and by the engine against the live
    /// preset list.
    static constexpr int scrollingDefaultColumnWidthPresetIndex()
    {
        return 0;
    }
    /// Schema-level ceiling for stored preset indices: the preset-list
    /// canonicalizer caps lists at 16 entries (settingsschema_p.h
    /// kMaxPresetEntries), so 15 is the largest index that can ever
    /// resolve. The engine re-clamps against the actual list length.
    static constexpr int scrollingPresetIndexMax()
    {
        return 15;
    }
    /// Value paired with the kind: a proportion in (0, 1] or a pixel width.
    static constexpr qreal scrollingDefaultColumnWidthValue()
    {
        return 0.5;
    }
    static constexpr qreal scrollingDefaultColumnWidthValueMin()
    {
        return 0.05;
    }
    /// Proportion-kind ceiling (100% of the work area). The QML slider and
    /// the schema's proportion-list canonicalizer bound against the same
    /// value conceptually; this accessor is the C++ home for it.
    static constexpr qreal scrollingDefaultColumnWidthProportionMax()
    {
        return 1.0;
    }
    /// Fixed-kind pixel floor. Enforced in two places, because the shared
    /// value key means the schema's clampDouble has to span both kinds and
    /// cannot enforce either: the hand-written setter (which the D-Bus
    /// registry routes through), and Settings::normalizeScrollingColumnWidthValue,
    /// called from load() and from applyConfigOverlayStaged. Between them
    /// those two cover every way a value reaches the store: a hand edit, a
    /// config import, the Discard reload, and profile staging. The QML SpinBox reads
    /// its bounds from here via SettingsController::scrollingWidthConstants().
    /// The engine's qMax(1, …) keeps any bypass value renderable.
    static constexpr qreal scrollingDefaultColumnWidthFixedMin()
    {
        return 100.0;
    }
    /// Fixed-kind pixel ceiling. Also the schema clampDouble's upper bound
    /// (deliberately: the shared value key spans both kinds, so the schema
    /// clamp uses the WIDER fixed range and the kind-aware setter owns the
    /// real per-kind bounds).
    static constexpr qreal scrollingDefaultColumnWidthFixedMax()
    {
        return 10000.0;
    }
    /// Pixel width seeded when the kind flips to Fixed while a proportion
    /// is stored (the shared value key serves both kinds).
    static constexpr qreal scrollingDefaultColumnWidthFixedPx()
    {
        return 800.0;
    }
    /// Editing granularity for the two width controls, shipped to QML through
    /// SettingsController::scrollingWidthConstants(). The proportion step is a
    /// work-area fraction (5% per slider notch); the fixed step is whole
    /// pixels. Both are doubles so the pair travels through one QVariantMap
    /// shape alongside the min/max accessors above.
    static constexpr qreal scrollingDefaultColumnWidthProportionStep()
    {
        return 0.05;
    }
    static constexpr qreal scrollingDefaultColumnWidthFixedStep()
    {
        return 10.0;
    }
    /// ColumnDisplay wire values (0 = normal, 1 = tabbed). Named for the same
    /// reason as the CenterFocusedColumn values above.
    static constexpr int scrollingColumnDisplayNormal()
    {
        return 0;
    }
    static constexpr int scrollingColumnDisplayTabbed()
    {
        return 1;
    }
    /// ColumnDisplay new columns open in: 0 = normal, 1 = tabbed.
    static constexpr int scrollingDefaultColumnDisplay()
    {
        return scrollingColumnDisplayNormal();
    }
    /// Closed-set validity check (see isValidScrollingCenterFocusedColumn).
    static constexpr bool isValidScrollingColumnDisplay(int v)
    {
        return v == scrollingColumnDisplayNormal() || v == scrollingColumnDisplayTabbed();
    }
    /// Preset proportion lists, comma-joined decimals (the niri defaults).
    /// KEEP IN SYNC with the engine's hard-coded fallback in
    /// ScrollEngine (engine_core.cpp refreshConfigFromSettings) — same
    /// {1/3, 1/2, 2/3} intent spelled twice because the LGPL engine cannot
    /// include this GPL header.
    static QString scrollingPresetColumnWidths()
    {
        return QStringLiteral("0.333,0.5,0.667");
    }
    static QString scrollingPresetWindowHeights()
    {
        return QStringLiteral("0.333,0.5,0.667");
    }
    /// Tab-indicator strip over tabbed columns. Off, the indicator hides and
    /// stays hidden until re-enabled (tabbed columns still work; they just
    /// carry no on-screen pill).
    static constexpr bool scrollingTabStripEnabled()
    {
        return true;
    }
    /// Meta+wheel column focus in the KWin effect. Off, the axis chords are
    /// genuinely released back to the compositor (KWin's zoom effect can
    /// reclaim Meta+wheel). Inverted flips the scroll direction.
    static constexpr bool scrollingWheelFocusEnabled()
    {
        return true;
    }
    static constexpr bool scrollingWheelFocusInverted()
    {
        return false;
    }
    /// DefaultWindowHeightKind wire values — the engine's WindowHeight::Kind
    /// vocabulary 1:1 (0 = auto split, 1 = fixed px, 2 = preset index); the
    /// schema static_asserts the pair via DefaultHeightKind.
    static constexpr int scrollingHeightKindAuto()
    {
        return 0;
    }
    static constexpr int scrollingHeightKindFixed()
    {
        return 1;
    }
    static constexpr int scrollingHeightKindPreset()
    {
        return 2;
    }
    /// Default window height kind for fresh tiles: auto (even split).
    static constexpr int scrollingDefaultWindowHeightKind()
    {
        return scrollingHeightKindAuto();
    }
    /// Closed-set validity check (see isValidScrollingCenterFocusedColumn).
    static constexpr bool isValidScrollingHeightKind(int v)
    {
        return v == scrollingHeightKindAuto() || v == scrollingHeightKindFixed() || v == scrollingHeightKindPreset();
    }
    /// Fixed-kind pixel height for fresh tiles, plus its range. Only read
    /// under the Fixed kind; Auto and Preset ignore it. The relayout
    /// renormalizes fixed heights into the column budget, so an oversized
    /// value degrades gracefully rather than overflowing.
    static constexpr qreal scrollingDefaultWindowHeightValue()
    {
        return 600.0;
    }
    static constexpr qreal scrollingDefaultWindowHeightMin()
    {
        return 100.0;
    }
    static constexpr qreal scrollingDefaultWindowHeightMax()
    {
        return 10000.0;
    }
    /// Preset index the Preset height kind seeds tiles at (see
    /// scrollingPresetIndexMax for the ceiling rationale).
    static constexpr int scrollingDefaultWindowHeightPresetIndex()
    {
        return 0;
    }

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
    static bool scrollingFocusFollowsMouse()
    {
        return false;
    }
    /// StickyWindowHandling wire values, the shared PhosphorEngine enum's
    /// spelling (0 = treat as normal, 1 = restore only, 2 = ignore all).
    /// Named for the same reason as the CenterFocusedColumn values above;
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
    /// Percent of the work-area extent one increase/decrease shortcut press
    /// moves a column width or window height. Daemon-side only: the engine
    /// receives an already-computed delta, so these never enter
    /// IScrollSettings. Distinct from the DefaultColumnWidth*Step editor
    /// granularity accessors above, which size QML control notches.
    static constexpr int scrollingColumnWidthStepPercent()
    {
        return 10;
    }
    static constexpr int scrollingWindowHeightStepPercent()
    {
        return 10;
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
    // Scrolling Shortcuts
    //
    // Anchored on Meta+Alt to stay clear of stock Plasma and the Meta+Shift /
    // Meta+Ctrl families in configdefaults.h. NOTE: the Meta+Alt family is
    // SHARED with the layouts pair (Meta+Alt+[ ]), the cheatsheet (Meta+Alt+/),
    // cycle-in-zone (Meta+Alt+, .), and the quick-layout digits (Meta+Alt+1-9) —
    // KGlobalAccel routes one action per chord, so every default here must be
    // unique across the WHOLE inheritance chain (test_scrolling_settings pins
    // this). Shift+symbol spellings are forbidden: see
    // toggleCheatsheetShortcut() — KWin consumes Shift in the keysym
    // translation on Wayland and the chord can never fire.
    // Directional focus/move/swap reuse the existing generic navigation
    // shortcuts; only the scroll-specific column vocabulary gets its own
    // chords.
    // ═══════════════════════════════════════════════════════════════════════════

    static QString scrollingFocusColumnFirstShortcut()
    {
        return QStringLiteral("Meta+Alt+Home");
    }
    static QString scrollingFocusColumnLastShortcut()
    {
        return QStringLiteral("Meta+Alt+End");
    }
    static QString scrollingMoveColumnToFirstShortcut()
    {
        return QStringLiteral("Meta+Alt+Shift+Home");
    }
    static QString scrollingMoveColumnToLastShortcut()
    {
        return QStringLiteral("Meta+Alt+Shift+End");
    }
    static QString scrollingConsumeWindowShortcut()
    {
        // Meta+Alt+, and Meta+Alt+. belong to cycle-in-zone; the semicolon
        // pair is the nearest free punctuation.
        return QStringLiteral("Meta+Alt+;");
    }
    static QString scrollingExpelWindowShortcut()
    {
        return QStringLiteral("Meta+Alt+'");
    }
    static QString scrollingConsumeOrExpelLeftShortcut()
    {
        // Meta+Alt+[ and Meta+Alt+] belong to the layouts pair.
        return QStringLiteral("Meta+Alt+U");
    }
    static QString scrollingConsumeOrExpelRightShortcut()
    {
        return QStringLiteral("Meta+Alt+O");
    }
    static QString scrollingCenterColumnShortcut()
    {
        return QStringLiteral("Meta+Alt+C");
    }
    static QString scrollingToggleColumnTabbedShortcut()
    {
        return QStringLiteral("Meta+Alt+T");
    }
    static QString scrollingCycleColumnWidthShortcut()
    {
        return QStringLiteral("Meta+Alt+R");
    }
    static QString scrollingCycleColumnWidthBackShortcut()
    {
        // Deliberately unbound: the reverse cycle is a niche refinement and
        // free chords near Meta+Alt+R are scarce.
        return QString();
    }
    static QString scrollingIncreaseColumnWidthShortcut()
    {
        return QStringLiteral("Meta+Alt+=");
    }
    static QString scrollingDecreaseColumnWidthShortcut()
    {
        return QStringLiteral("Meta+Alt+-");
    }
    static QString scrollingMaximizeColumnShortcut()
    {
        return QStringLiteral("Meta+Alt+F");
    }
    static QString scrollingExpandColumnShortcut()
    {
        return QStringLiteral("Meta+Alt+E");
    }
    static QString scrollingCycleWindowHeightShortcut()
    {
        return QStringLiteral("Meta+Alt+Shift+R");
    }
    static QString scrollingCycleWindowHeightBackShortcut()
    {
        // Deliberately unbound, like scrollingCycleColumnWidthBackShortcut:
        // the reverse cycle is a niche refinement and free chords near
        // Meta+Alt+Shift+R are scarce. Bindable via the Shortcuts KCM.
        return QString();
    }
    static QString scrollingIncreaseWindowHeightShortcut()
    {
        // NOT Meta+Alt+Shift+= — Shift+symbol chords never fire on Wayland
        // (see toggleCheatsheetShortcut). PgUp/PgDown are named keys and
        // pair naturally with the height axis.
        return QStringLiteral("Meta+Alt+PgUp");
    }
    static QString scrollingDecreaseWindowHeightShortcut()
    {
        return QStringLiteral("Meta+Alt+PgDown");
    }
    static QString scrollingResetWindowHeightsShortcut()
    {
        return QStringLiteral("Meta+Alt+0");
    }
};

// Compile-time bound checks for the scrolling defaults that declare min/max
// accessors — same guard the autotile and animation defaults carry at the end
// of configdefaults.h. The width VALUE default is a proportion (the default
// kind is Proportion), so it is checked against the proportion range; the
// pixel seed is checked against the fixed range.
static_assert(ConfigDefaultsScrolling::scrollingDefaultColumnWidthValue()
                      >= ConfigDefaultsScrolling::scrollingDefaultColumnWidthValueMin()
                  && ConfigDefaultsScrolling::scrollingDefaultColumnWidthValue()
                      <= ConfigDefaultsScrolling::scrollingDefaultColumnWidthProportionMax(),
              "ConfigDefaults::scrollingDefaultColumnWidthValue() outside the declared proportion [min, max] range");
static_assert(ConfigDefaultsScrolling::scrollingDefaultColumnWidthFixedPx()
                      >= ConfigDefaultsScrolling::scrollingDefaultColumnWidthFixedMin()
                  && ConfigDefaultsScrolling::scrollingDefaultColumnWidthFixedPx()
                      <= ConfigDefaultsScrolling::scrollingDefaultColumnWidthFixedMax(),
              "ConfigDefaults::scrollingDefaultColumnWidthFixedPx() outside the declared fixed [min, max] range");
// The kind-ish defaults must satisfy their own closed-set predicates.
static_assert(ConfigDefaultsScrolling::isValidScrollingCenterFocusedColumn(
                  ConfigDefaultsScrolling::scrollingCenterFocusedColumn()),
              "ConfigDefaults::scrollingCenterFocusedColumn() is not in its own closed set");
static_assert(
    ConfigDefaultsScrolling::isValidScrollingWidthKind(ConfigDefaultsScrolling::scrollingDefaultColumnWidthKind()),
    "ConfigDefaults::scrollingDefaultColumnWidthKind() is not in its own closed set");
static_assert(
    ConfigDefaultsScrolling::isValidScrollingColumnDisplay(ConfigDefaultsScrolling::scrollingDefaultColumnDisplay()),
    "ConfigDefaults::scrollingDefaultColumnDisplay() is not in its own closed set");
static_assert(ConfigDefaultsScrolling::isValidScrollingStickyWindowHandling(
                  ConfigDefaultsScrolling::scrollingStickyWindowHandling()),
              "ConfigDefaults::scrollingStickyWindowHandling() is not in its own closed set");
static_assert(
    ConfigDefaultsScrolling::isValidScrollingHeightKind(ConfigDefaultsScrolling::scrollingDefaultWindowHeightKind()),
    "ConfigDefaults::scrollingDefaultWindowHeightKind() is not in its own closed set");
static_assert(
    ConfigDefaultsScrolling::isValidScrollingInsertPosition(ConfigDefaultsScrolling::scrollingInsertPosition()),
    "ConfigDefaults::scrollingInsertPosition() is not in its own closed set");
static_assert(ConfigDefaultsScrolling::scrollingDefaultWindowHeightValue()
                      >= ConfigDefaultsScrolling::scrollingDefaultWindowHeightMin()
                  && ConfigDefaultsScrolling::scrollingDefaultWindowHeightValue()
                      <= ConfigDefaultsScrolling::scrollingDefaultWindowHeightMax(),
              "ConfigDefaults::scrollingDefaultWindowHeightValue() outside the declared [min, max] range");
static_assert(ConfigDefaultsScrolling::scrollingDefaultColumnWidthPresetIndex() >= 0
                  && ConfigDefaultsScrolling::scrollingDefaultColumnWidthPresetIndex()
                      <= ConfigDefaultsScrolling::scrollingPresetIndexMax(),
              "ConfigDefaults::scrollingDefaultColumnWidthPresetIndex() outside [0, presetIndexMax]");
static_assert(ConfigDefaultsScrolling::scrollingDefaultWindowHeightPresetIndex() >= 0
                  && ConfigDefaultsScrolling::scrollingDefaultWindowHeightPresetIndex()
                      <= ConfigDefaultsScrolling::scrollingPresetIndexMax(),
              "ConfigDefaults::scrollingDefaultWindowHeightPresetIndex() outside [0, presetIndexMax]");
static_assert(ConfigDefaultsScrolling::scrollingColumnWidthStepPercent()
                      >= ConfigDefaultsScrolling::scrollingStepPercentMin()
                  && ConfigDefaultsScrolling::scrollingColumnWidthStepPercent()
                      <= ConfigDefaultsScrolling::scrollingStepPercentMax(),
              "ConfigDefaults::scrollingColumnWidthStepPercent() outside the declared [min, max] range");
static_assert(ConfigDefaultsScrolling::scrollingWindowHeightStepPercent()
                      >= ConfigDefaultsScrolling::scrollingStepPercentMin()
                  && ConfigDefaultsScrolling::scrollingWindowHeightStepPercent()
                      <= ConfigDefaultsScrolling::scrollingStepPercentMax(),
              "ConfigDefaults::scrollingWindowHeightStepPercent() outside the declared [min, max] range");

} // namespace PlasmaZones
