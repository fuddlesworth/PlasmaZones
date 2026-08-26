// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

// Singleton registration is driven by the `QT_QML_SINGLETON_TYPE TRUE`
// source property in src/shared/CMakeLists.txt (same pattern as
// CurvePresets.qml in the settings module); the `pragma Singleton` line
// documents intent for readers.
pragma Singleton

import QtQuick
import org.kde.kirigami as Kirigami

/**
 * @brief Single source of truth for the default zone fill/border colors.
 *
 * Replaces the copy-pasted trio (`highlightColor@0.7` active fill,
 * `textColor@0.4` inactive fill, `textColor@0.9` border) that was
 * duplicated across the compositor overlays and the settings previews.
 * Inactive fills stay in the background-family roles so the
 * wallpaper-generated scheme reaches them; the naked border starts from
 * the same family but blends toward textColor at 0.5 for contrast over
 * arbitrary desktop content.
 *
 * Two flavors are provided. The split is panel-hosted vs naked, NOT
 * settings vs overlay process:
 * - `activeZoneColor` / `inactiveZoneColor` / `zoneBorderColor` —
 *   zones drawn NAKED over arbitrary desktop content (ZoneItem,
 *   ZoneOverlayContent, RenderNodeOverlay, SnapAssist highlights);
 *   fills need alpha to keep the desktop visible underneath.
 * - `preview*` — zone cards that sit ON A PANEL (settings previews AND
 *   the PopupFrame-hosted picker/selector/OSD contents), resolved
 *   opaquely (against the ambient theme roles a singleton can read; see
 *   the note above the preview properties) so the same card renders
 *   identically in every context. Translucent fills on a panel
 *   composited differently per host and read as a different theme.
 *   The two FILL colors are therefore alpha-stripped at this boundary;
 *   the BORDER deliberately carries the pipeline's alpha to match the
 *   live overlay. The `tab*` fallbacks below are exempt from the
 *   alpha-stripping too: they are what the compositor's own
 *   resolveTabColor resolves to, and a preview of a tab bar that
 *   dropped their alpha would stop matching the bar it depicts.
 *
 * All consumers keep their own `property color` hooks; this singleton
 * only supplies the default expressions.
 */
QtObject {
    id: root

    /// Strip a pipeline colour's carried alpha — preview FILLS are opaque
    /// by contract (consumers bake their configured fill opacity into the
    /// fill colour's alpha themselves, see ZonePreview's delegate `color`).
    function _opaque(c) {
        return Qt.rgba(c.r, c.g, c.b, 1);
    }

    // Compositor-overlay defaults (over arbitrary desktop content)
    readonly property color activeZoneColor: Qt.alpha(Kirigami.Theme.highlightColor, 0.7)
    readonly property color inactiveZoneColor: Qt.alpha(Kirigami.Theme.backgroundColor, 0.4)
    // Naked overlays need mid-contrast borders — frameContrast (~0.2) is too
    // faint over arbitrary desktop content where the fill is a same-family
    // film; panel-hosted previews keep the softer separator recipe.
    readonly property color zoneBorderColor: Kirigami.ColorUtils.linearInterpolation(Kirigami.Theme.backgroundColor, Kirigami.Theme.textColor, 0.5)

    // The preview fallbacks read Kirigami.Theme ON THIS SINGLETON, not through
    // a child Item pinned to the View colour set. A singleton QtObject is never
    // parented into a scene, and Kirigami's platform theme does not resolve for
    // an Item that belongs to no window: every role on such a child reads
    // black, permanently (measured, not assumed). The attached properties on
    // the singleton itself DO resolve, against the ambient (Window) set. The
    // View pin that the old `_viewScope` Item was meant to provide is
    // therefore not available to a singleton at all. What that costs:
    // highlight (Selection set either way) and text coincide across the
    // Window and View sets, so the tab fallbacks below and their C++ mirror
    // still agree; background and alternateBackground do NOT (View is the
    // lighter pair in Breeze and most schemes), so the preview inactive fill
    // and the preview border now resolve into the Window family rather than
    // View. Accepted as the price of non-black fallbacks; every host with a
    // settings object injects the pipeline colours over these anyway.

    // Effective zone colors from the SETTINGS PIPELINE. Hosts that have a
    // settings object INJECT it here (the settings app assigns
    // `settingsSource = appSettings` from Main.qml; the daemon instead
    // PUSHES pipeline values into its overlay slots, which override these
    // defaults). NOTE: this must be an explicit assignment — a compiled
    // QML module's singleton has no creation-context chain, so it can NOT
    // resolve engine root context properties like `appSettings` by name
    // (tried; it silently fell back and settings previews mismatched the
    // popups). A preview must show what a zone will actually look like:
    // the pipeline's resolved colors follow the palette while a color's
    // stored theme-fallback value is empty, and show the user's picks
    // otherwise. The theme-role expressions below are the fallback for
    // engines with no injection and no push.
    // Lifetime: the injected object must outlive the engine; if it is
    // deleted externally QML nulls this property and the previews fall
    // back to the theme defaults (or go stale), without crashing.
    property QtObject settingsSource: null

    // Settings-embedded preview defaults (effective settings colors,
    // falling back to the ambient theme roles). Fills are opaque; the border
    // deliberately carries the pipeline's alpha to match the live overlay.
    readonly property color previewActiveZoneColor: root._opaque(root.settingsSource ? root.settingsSource.highlightColor : Kirigami.Theme.highlightColor)
    readonly property color previewInactiveZoneColor: root._opaque(root.settingsSource ? root.settingsSource.inactiveColor : Kirigami.Theme.alternateBackgroundColor)
    readonly property color previewZoneBorderColor: root.settingsSource ? root.settingsSource.borderColor : Kirigami.ColorUtils.linearInterpolation(Kirigami.Theme.backgroundColor, Kirigami.Theme.textColor, Kirigami.Theme.frameContrast)

    // Scroll tab-indicator theme fallbacks — what an EMPTY tab colour key
    // resolves to, as the settings page's swatch previews (ScrollingTabsPage)
    // show them. The compositor-drawn pills cannot read this singleton (the
    // KWin effect has no QML engine), so the SAME four fallbacks are
    // MIRRORED BY HAND in resolveTabColor() in
    // kwin-effect/compositor/scrolltabindicatorpainter_raster.cpp, resolved
    // there from KColorScheme's View and Selection sets: active = highlight,
    // urgent = negativeText, inactive bar = text at 0.35, inactive chip =
    // transparent. Change one, change both. The inactive fallback differs
    // per style ON PURPOSE: a chip carries its title, so its resting state
    // is no fill at all; a bar segment has nothing but its fill, so an
    // unfilled one would under-report the tab count.
    readonly property color tabActiveColor: Kirigami.Theme.highlightColor
    readonly property color tabInactiveChipColor: "transparent"
    readonly property color tabInactiveBarColor: Qt.alpha(Kirigami.Theme.textColor, 0.35)
    readonly property color tabUrgentColor: Kirigami.Theme.negativeTextColor
}
