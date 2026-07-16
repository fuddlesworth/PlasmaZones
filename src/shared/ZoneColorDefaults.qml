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
 * Inactive fills and borders derive from background-family roles, not
 * textColor, so the wallpaper-generated scheme reaches them.
 *
 * Two flavors are provided. The split is panel-hosted vs naked, NOT
 * settings vs overlay process:
 * - `activeZoneColor` / `inactiveZoneColor` / `zoneBorderColor` —
 *   zones drawn NAKED over arbitrary desktop content (ZoneItem,
 *   ZoneOverlayContent, RenderNodeOverlay, SnapAssist highlights);
 *   fills need alpha to keep the desktop visible underneath.
 * - `preview*` — zone cards that sit ON A PANEL (settings previews AND
 *   the PopupFrame-hosted picker/selector/OSD contents), resolved
 *   opaquely against the View color set so the same card renders
 *   identically in every context. Translucent fills on a panel
 *   composited differently per host and read as a different theme.
 *   The two FILL colors are therefore alpha-stripped at this boundary;
 *   the BORDER deliberately carries the pipeline's alpha to match the
 *   live overlay.
 *
 * All consumers keep their own `property color` hooks; this singleton
 * only supplies the default expressions.
 */
QtObject {
    id: root

    /// Strip a pipeline colour's carried alpha — preview FILLS are opaque
    /// by contract (consumers control transparency via `opacity`).
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

    // Non-visual scope pinned to the View color set so the preview
    // defaults resolve against View roles regardless of the caller.
    readonly property Item _viewScope: Item {
        Kirigami.Theme.colorSet: Kirigami.Theme.View
        Kirigami.Theme.inherit: false

        readonly property color highlight: Kirigami.Theme.highlightColor
        readonly property color alternateBackground: Kirigami.Theme.alternateBackgroundColor
        readonly property color separator: Kirigami.ColorUtils.linearInterpolation(Kirigami.Theme.backgroundColor, Kirigami.Theme.textColor, Kirigami.Theme.frameContrast)
    }

    // Effective zone colors from the SETTINGS PIPELINE. Hosts that have a
    // settings object INJECT it here (the settings app assigns
    // `settingsSource = appSettings` from Main.qml; the daemon instead
    // PUSHES pipeline values into its overlay slots, which override these
    // defaults). NOTE: this must be an explicit assignment — a compiled
    // QML module's singleton has no creation-context chain, so it can NOT
    // resolve engine root context properties like `appSettings` by name
    // (tried; it silently fell back and settings previews mismatched the
    // popups). A preview must show what a zone will actually look like:
    // with useSystemColors on, the pipeline follows the theme anyway; with
    // custom colors it shows the user's picks. The theme-role expressions
    // below are the fallback for engines with no injection and no push.
    // Lifetime: the injected object must outlive the engine; if it is
    // deleted externally QML nulls this property and the previews fall
    // back to the theme defaults (or go stale), without crashing.
    property QtObject settingsSource: null

    // Settings-embedded preview defaults (effective settings colors,
    // falling back to View color set roles). Fills are opaque; the border
    // deliberately carries the pipeline's alpha to match the live overlay.
    readonly property color previewActiveZoneColor: root._opaque(root.settingsSource ? root.settingsSource.highlightColor : root._viewScope.highlight)
    readonly property color previewInactiveZoneColor: root._opaque(root.settingsSource ? root.settingsSource.inactiveColor : root._viewScope.alternateBackground)
    readonly property color previewZoneBorderColor: root.settingsSource ? root.settingsSource.borderColor : root._viewScope.separator
}
