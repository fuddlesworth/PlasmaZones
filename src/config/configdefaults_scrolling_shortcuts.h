// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "configdefaults_scrolling_behavior.h"

namespace PlasmaZones {

// Chain link 8: the Shortcuts.Scrolling chord defaults. Split out of
// configdefaults_scrolling.h to keep that file inside the size ceiling; the
// engine's own Scrolling defaults stay there (link 6) and its behaviour
// tunables in configdefaults_scrolling_behavior.h (link 7). Every accessor here
// reaches call sites through the ConfigDefaults leaf as before, so no consumer
// changes.
class ConfigDefaultsScrollingShortcuts : public ConfigDefaultsScrollingBehavior
{
public:
    // ═══════════════════════════════════════════════════════════════════════════
    // Scrolling Shortcuts
    //
    // Anchored on Meta+Alt to stay clear of stock Plasma and the Meta+Shift /
    // Meta+Ctrl families in configdefaults.h. NOTE: the Meta+Alt family is
    // SHARED with the layouts pair (Meta+Alt+[ ]), the cheatsheet (Meta+Alt+/),
    // cycle-in-zone (Meta+Alt+, .), and the quick-layout digit slots
    // (Meta+Alt+<digit>, 1..QuickLayoutSlotCount) —
    // KGlobalAccel routes one action per chord, so every default here must be
    // unique across the WHOLE inheritance chain (test_scrolling_settings pins
    // this). Shift+symbol spellings are forbidden: see
    // toggleCheatsheetShortcut() — KWin consumes Shift in the keysym
    // translation on Wayland and the chord can never fire.
    // The GENERIC directional focus/move/swap chords stay shared with the
    // other modes; the scroll-specific column vocabulary gets its own chords,
    // and the niri-parity focus variants (edge-stop, wrap) plus the one-way
    // float verbs have their own keys that SHIP UNBOUND — see each accessor's
    // rationale.
    //
    // EXTERNALLY OWNED CHORDS the internal-uniqueness test can NOT catch:
    // KGlobalAccel silently gives a chord to whichever action registered
    // first, so a default colliding with a stock KDE app just never fires
    // for one of the two. Known occupied on a stock Plasma 6 install:
    //   - Spectacle owns the whole R family: Meta+R and Meta+Shift+R
    //     (region recording), Meta+Ctrl+R (window recording), Meta+Alt+R
    //     (screen recording). R is unusable with any Meta-based modifier.
    //   - Plasma owns Meta+Alt+K and Meta+Alt+L (keyboard layouts),
    //     Meta+Alt+S (screen reader), Meta+Alt+P (panel focus), and
    //     Meta+Alt+Arrows (KWin switch-window).
    //   - KWin core owns Meta+Alt+wheel (desktop switch; axis registrations
    //     that lose the race are silently dropped).
    // Check a candidate against this table AND a live session
    // (~/.config/kglobalshortcutsrc plus kglobalaccel's component list)
    // before shipping a new default.
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
        // Letters only for this family's chords: punctuation like ; ' =
        // is shifted (or absent) on many non-US layouts, and a chord whose
        // spelling needs Shift on the user's layout can never fire on
        // Wayland (see toggleCheatsheetShortcut). I as in "into the
        // column"; Shift+I is the opposite direction. Shift+letter is safe
        // — only Shift+SYMBOL spellings are forbidden. (The autotile
        // master-count pair keeps the KDE-wide Meta+Ctrl+= / - idiom as a
        // documented exception — see autotileIncMasterCountShortcut.)
        return QStringLiteral("Meta+Alt+I");
    }
    static QString scrollingExpelWindowShortcut()
    {
        return QStringLiteral("Meta+Alt+Shift+I");
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
    static QString scrollingToggleWindowedFullscreenShortcut()
    {
        // Shares the F letter with Meta+Alt+F (maximize column) because both
        // are fullscreen-adjacent presentation toggles, and Shift+F was the
        // free spelling on that letter. NOT an opposed pair in the
        // letter+Shift convention's sense (see
        // scrollingCycleColumnWidthShortcut) — windowed fullscreen never
        // resizes the window; it flips the client's fullscreen presentation
        // while the tile keeps its column slot.
        return QStringLiteral("Meta+Alt+Shift+F");
    }
    static QString scrollingCycleColumnWidthShortcut()
    {
        // The letter pairs in this family follow one convention: a mnemonic
        // letter for the primary action and Shift on the same letter for the
        // opposed one (I/Shift+I consume/expel, W/Shift+W widen/narrow,
        // D/Shift+D and H/Shift+H cycle forward/back). D as in the column's
        // Dimensions, H is height. NOT R (niri's preset-width mnemonic):
        // Spectacle owns the entire Meta-modified R family — see the
        // externally-owned table in the section banner — and Meta+Alt+R was
        // a live collision with its screen recording.
        return QStringLiteral("Meta+Alt+D");
    }
    static QString scrollingCycleColumnWidthBackShortcut()
    {
        return QStringLiteral("Meta+Alt+Shift+D");
    }
    static QString scrollingIncreaseColumnWidthShortcut()
    {
        // NOT Meta+Alt+= / Meta+Alt+- : "=" is a shifted key on several
        // non-US layouts (dead chord on Wayland, same trap as the consume
        // pair above). W for width, Shift+W for the opposite direction.
        return QStringLiteral("Meta+Alt+W");
    }
    static QString scrollingDecreaseColumnWidthShortcut()
    {
        return QStringLiteral("Meta+Alt+Shift+W");
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
        // NOT Meta+Alt+Shift+D: that slot is the width cycle's reverse
        // (see scrollingCycleColumnWidthShortcut for the letter+Shift
        // convention).
        return QStringLiteral("Meta+Alt+H");
    }
    static QString scrollingCycleWindowHeightBackShortcut()
    {
        return QStringLiteral("Meta+Alt+Shift+H");
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
    static QString scrollingCenterVisibleColumnsShortcut()
    {
        // Shift twin of centerColumn's C: the whole-span variant of the same
        // centering idea, no new letter consumed from the shrinking pool.
        return QStringLiteral("Meta+Alt+Shift+C");
    }
    static QString scrollingFocusWindowTopShortcut()
    {
        // V for vertical: the within-column axis. Shift+V is the opposed
        // end, per the family's letter+Shift convention.
        return QStringLiteral("Meta+Alt+V");
    }
    static QString scrollingFocusWindowBottomShortcut()
    {
        return QStringLiteral("Meta+Alt+Shift+V");
    }
    static QString scrollingFocusColumnLeftShortcut()
    {
        // Unbound by default: the generic focus chords already walk columns
        // and cross monitors at the strip edge. These plain variants exist
        // for users who want niri's exact stop-at-the-edge behaviour; a
        // default would spend two letters from the shrinking Meta+Alt pool
        // on a behaviour nothing defaults to. NOTE an unbound entry never
        // registers with KGlobalAccel (the registry skips empty sequences),
        // so it does NOT appear in the system Shortcuts KCM — binding one
        // means writing its Shortcuts.Scrolling key in
        // ~/.config/plasmazones/config.json or over the settings D-Bus
        // surface; the daemon rebinds live on the settings change.
        return QString();
    }
    static QString scrollingFocusColumnRightShortcut()
    {
        return QString();
    }
    static QString scrollingFocusColumnLeftOrLastShortcut()
    {
        // Unbound like the plain pair above, and for the same reason: the
        // wrap variants are alternatives a user binds INSTEAD of the
        // monitor-crossing default.
        return QString();
    }
    static QString scrollingFocusColumnRightOrFirstShortcut()
    {
        return QString();
    }
    static QString scrollingMoveToFloatingShortcut()
    {
        // Unbound: Meta+F already toggles float, and the explicit one-way
        // verbs are for users scripting a deterministic direction.
        return QString();
    }
    static QString scrollingMoveToTilingShortcut()
    {
        return QString();
    }
    static QString scrollingViewPageBackShortcut()
    {
        // The view pan splits by input: the STEP is the wheel's (Meta+Shift+
        // wheel, an effect-side axis shortcut mirroring Meta+wheel's column
        // focus), and the PAGE is the keyboard's, the way PgUp/PgDn page
        // where the wheel scrolls lines. A keypress is one deliberate act
        // and can carry a whole viewport; a wheel notch is one of a stream
        // and would overshoot. No keyboard step pair, for the same reason
        // Meta+wheel's column focus has none: the wheel is a different input
        // with a different feel, not a duplicate of the keys. Y is a free
        // letter in the Meta+Alt pool (see the banner's externally-owned
        // table); back and forward are the letter+Shift pair per the
        // family's convention, and "back" is toward the strip's start
        // whichever way the strip runs.
        return QStringLiteral("Meta+Alt+Y");
    }
    static QString scrollingViewPageForwardShortcut()
    {
        return QStringLiteral("Meta+Alt+Shift+Y");
    }
    static QString scrollingEqualizeColumnWidthsShortcut()
    {
        // The Shift twin of Retile's Meta+Ctrl+T, deliberately outside the
        // Meta+Alt family: both verbs re-flow the strip's widths, one back
        // to the layout's defaults and the other to equal shares of the
        // viewport, so they belong on one letter. Meta+Ctrl+Shift+T is not
        // in the externally-owned table.
        return QStringLiteral("Meta+Ctrl+Shift+T");
    }
    static QString scrollingMinimizeColumnWidthShortcut()
    {
        // Shift twin of E (grow into empty space): plain E gives the focused
        // column every spare pixel on screen, Shift+E takes it to its
        // narrowest. Opposed ends of the same axis, per the family's
        // letter+Shift convention, and no new letter consumed.
        return QStringLiteral("Meta+Alt+Shift+E");
    }
};

} // namespace PlasmaZones
