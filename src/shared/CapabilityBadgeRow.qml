// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import org.kde.kirigami as Kirigami

/**
 * Autotile capability badges for layout cards.
 *
 * Shows a compact icon per capability an autotile algorithm advertises
 * (persistent split memory, resize reflow, persistent script state). Manual
 * layouts leave every flag false, so the whole row hides and reserves no space.
 *
 * Driven by the serialized LayoutPreview shape (flattened autotile metadata):
 * supportsMemory, reflowsOnResize, supportsScriptState. Shared by the settings
 * layout grid and the overlay layout card so the badge set stays identical.
 */
Row {
    id: root

    property var layoutData: ({})

    spacing: Kirigami.Units.smallSpacing
    // Hidden (and space-free in the enclosing positioner) when the layout
    // advertises no capabilities — i.e. every manual layout.
    visible: layoutData.supportsMemory === true || layoutData.reflowsOnResize === true || layoutData.supportsScriptState === true

    // Shared icon-badge primitive: a small masked symbolic icon with a hover
    // tooltip. Each instance sets only visible/source/color/text.
    component CapabilityBadge: Kirigami.Icon {
        id: badge

        property string tooltipText

        implicitWidth: Kirigami.Units.iconSizes.small
        implicitHeight: Kirigami.Units.iconSizes.small
        // Render as a mask tinted with `color` so every badge recolors
        // uniformly — some symbolic icons (transform-scale, code-context) are
        // not auto-recolored by the icon theme and would otherwise show a
        // default grey, reading as "disabled" next to the recolored memory badge.
        isMask: true
        opacity: 0.7
        ToolTip.delay: Kirigami.Units.toolTipDelay
        ToolTip.visible: badgeHover.hovered && badge.visible
        ToolTip.text: badge.tooltipText

        HoverHandler {
            id: badgeHover
        }
    }

    // Memory indicator for algorithms that persist split state.
    CapabilityBadge {
        visible: root.layoutData.supportsMemory === true
        source: "document-save-symbolic"
        color: Kirigami.Theme.positiveTextColor
        Accessible.name: i18n("Persistent algorithm")
        tooltipText: i18n("Remembers split positions across window changes")
    }

    // Reflow indicator for algorithms that adjust the layout when a tiled
    // window is interactively resized.
    CapabilityBadge {
        visible: root.layoutData.reflowsOnResize === true
        source: "transform-scale-symbolic"
        color: Kirigami.Theme.highlightColor
        Accessible.name: i18n("Reflows")
        tooltipText: i18n("Reflows neighbouring windows when you resize a tiled window")
    }

    // Script-state indicator for scripted algorithms that persist an opaque
    // per-screen state bag across retiles. Distinct colour from the reflow
    // badge so the two are tellable apart when both show.
    CapabilityBadge {
        visible: root.layoutData.supportsScriptState === true
        source: "code-context-symbolic"
        color: Kirigami.Theme.neutralTextColor
        Accessible.name: i18n("Persistent script state")
        tooltipText: i18n("Remembers script-managed layout state across window changes")
    }
}
