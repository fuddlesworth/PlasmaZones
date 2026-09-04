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
    // Meta+Ctrl families in configdefaults.h. Chords that leave the Meta+Alt
    // anchor do so for a stated reason at their own accessor: the height verbs
    // that add Ctrl to their width twin's chord, and the equalize pair, which
    // sits on the LETTER of Retile's Meta+Ctrl+T because all three re-flow the
    // strip (see scrollingEqualizeColumnWidthsShortcut).
    //
    // The sizing verbs come in width/height PAIRS that do the same thing on
    // the two axes, and each pair spells the axis in its chord, but not all by
    // the same device. The default device is Ctrl: Meta+Alt+<key> sizes the
    // column along the strip and Meta+Ctrl+Alt+<key> sizes the window across
    // it (maximize F, grow into empty space E, and E's Shift'd minimize).
    // Three pairs predate that device and keep their own spelling: the adjust
    // pair uses two mnemonic letters (W width, H height, each with its Shift'd
    // opposite), the preset cycles use one key with Shift for the height axis
    // (PgUp/PgDown plain for width, Shift'd for height), and the equalize pair
    // shares Retile's T outside the Meta+Alt family entirely. Each accessor
    // states which case it is, and a NEW sizing pair should use the Ctrl
    // device. Note Equalize Window Heights lands on Meta+Ctrl+Alt+T for the
    // T-letter reason and not as an instance of the Ctrl device — its own
    // accessor spells that out, including why the chord is NOT the height twin
    // of Meta+Alt+T.
    //
    // NOTE: the Meta+Alt family is SHARED with the layouts pair (Meta+Alt+[ ]),
    // the cheatsheet (Meta+Alt+/), cycle-in-zone (Meta+Alt+, .), the
    // quick-layout digit slots (Meta+Alt+<digit>, 1..QuickLayoutSlotCount) and
    // every other Meta+Alt default in configdefaults.h (X, Space, Return,
    // Escape and the Shift+Arrow move family). The Meta+Ctrl+Alt space the
    // height and equalize verbs use is shared just as widely: swapWindow owns
    // Meta+Ctrl+Alt+Arrows (configdefaults.h), and the virtual-screen verbs own
    // Meta+Ctrl+Alt+Shift+Arrows and Meta+Ctrl+Alt+[ ]
    // (configdefaults_screens.h).
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
    // The table above covers the Meta+Alt anchor ONLY. The Meta+Ctrl+Alt and
    // Meta+Ctrl+Shift chords used by the height and equalize verbs are
    // UNSURVEYED against stock Plasma — nothing here says they are free, only
    // that nobody has checked.
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
        // The resize family splits by verb, symmetric across both axes:
        // ADJUST pairs live on mnemonic letters (W/Shift+W width, H/Shift+H
        // height, per the letter+Shift convention), and the preset CYCLES
        // page on PgUp/PgDown — plain for the width axis, Shift for the
        // height axis. NOT R (niri's preset-width mnemonic): Spectacle owns
        // the entire Meta-modified R family — see the externally-owned table
        // in the section banner — and Meta+Alt+R was a live collision with
        // its screen recording.
        return QStringLiteral("Meta+Alt+PgUp");
    }
    static QString scrollingCycleColumnWidthBackShortcut()
    {
        return QStringLiteral("Meta+Alt+PgDown");
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
    static QString scrollingMaximizeToEdgesShortcut()
    {
        // M for maximize, niri's Mod+M with this scheme's Meta+Alt prefix.
        return QStringLiteral("Meta+Alt+M");
    }
    static QString scrollingExpandColumnShortcut()
    {
        return QStringLiteral("Meta+Alt+E");
    }
    static QString scrollingCycleWindowHeightShortcut()
    {
        // Shift tier of the width cycle's PgUp/PgDown — see
        // scrollingCycleColumnWidthShortcut for the resize family's split.
        return QStringLiteral("Meta+Alt+Shift+PgUp");
    }
    static QString scrollingCycleWindowHeightBackShortcut()
    {
        return QStringLiteral("Meta+Alt+Shift+PgDown");
    }
    static QString scrollingIncreaseWindowHeightShortcut()
    {
        // NOT Meta+Alt+= / Meta+Alt+- : "=" is shifted on several non-US
        // layouts, and a chord whose spelling needs Shift can never fire on
        // Wayland (see toggleCheatsheetShortcut; same trap as the width twin,
        // scrollingIncreaseColumnWidthShortcut). H for height, the adjust twin
        // of W/Shift+W on the width axis.
        return QStringLiteral("Meta+Alt+H");
    }
    static QString scrollingDecreaseWindowHeightShortcut()
    {
        return QStringLiteral("Meta+Alt+Shift+H");
    }
    static QString scrollingMaximizeWindowHeightShortcut()
    {
        // Ctrl added to the width verb's chord: Meta+Alt+<key> sizes the
        // column along the strip, Meta+Ctrl+Alt+<key> sizes the window across
        // it. THREE height verbs are spelled this way (this one, Grow Window
        // into Empty Space, and Minimize Window Height); the rest of the
        // height family splits from its width twin by letter or by Shift
        // instead (see the banner). F is Maximize Column's letter
        // (scrollingMaximizeColumnShortcut).
        return QStringLiteral("Meta+Ctrl+Alt+F");
    }
    static QString scrollingExpandWindowShortcut()
    {
        // The Ctrl twin of Grow Column into Empty Space's E, per the family
        // rule in scrollingMaximizeWindowHeightShortcut.
        return QStringLiteral("Meta+Ctrl+Alt+E");
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
        // viewport, so they belong on one letter (as does Equalize Window
        // Heights on the Alt twin — see scrollingEqualizeWindowHeightsShortcut).
        // Meta+Ctrl+Shift+T is not in the externally-owned table, which covers
        // the Meta+Alt anchor only and so clears nothing here.
        return QStringLiteral("Meta+Ctrl+Shift+T");
    }
    static QString scrollingMinimizeColumnWidthShortcut()
    {
        // Shift twin of E (grow into empty space): plain E gives the focused
        // column every spare pixel on screen, Shift+E takes it to the smallest
        // width preset. Opposed ends of the same axis, with Shift reading as
        // the opposed end the way it does on W, H, I, V and Y, and no new
        // letter consumed. "Smallest preset" rather than "narrowest": the verb
        // writes the smallest entry of the preset vocabulary, and only falls
        // back to the engine floor when that list is empty.
        return QStringLiteral("Meta+Alt+Shift+E");
    }
    static QString scrollingEqualizeWindowHeightsShortcut()
    {
        // The Alt twin of Retile's Meta+Ctrl+T, beside Equalize Column
        // Widths' Shift twin (see scrollingEqualizeColumnWidthsShortcut):
        // all three re-flow the strip's sizes, so they share the letter. It
        // does NOT follow the Meta+Ctrl+Alt+<width letter> rule its three
        // height siblings use: that rule would put it on the width twin's key,
        // and Equalize Column Widths has no Meta+Alt key to borrow. Read the T
        // here as Retile's letter, NOT as the Ctrl twin of Meta+Alt+T — that
        // chord is Toggle Column Tabbed
        // (scrollingToggleColumnTabbedShortcut), which has nothing to do with
        // heights and is the one place the family rule reads as a promise it
        // does not make.
        // NOT Meta+Alt+0 — that read as a tenth quick-layout digit beside
        // Meta+Alt+1-9.
        return QStringLiteral("Meta+Ctrl+Alt+T");
    }
    static QString scrollingMinimizeWindowHeightShortcut()
    {
        // Shift added to Grow Window into Empty Space's chord, mirroring
        // exactly what Minimize Column Width does to Grow Column into Empty
        // Space on the width axis.
        return QStringLiteral("Meta+Ctrl+Alt+Shift+E");
    }
};

} // namespace PlasmaZones
