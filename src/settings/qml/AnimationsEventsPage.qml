// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

/**
 * @brief Animations > Events — unified page with 5 collapsible category groups.
 *
 * Replaces the 7 per-category sub-pages (Window, Zone, OSD, Panel, Cursor,
 * Widget, Workspace) with a single scrollable view. Each category is a
 * SettingsCard whose collapsible content holds AnimationEventCard children.
 */
Flickable {
    id: root

    contentHeight: contentColumn.implicitHeight
    clip: true

    ColumnLayout {
        id: contentColumn

        width: parent.width
        spacing: Kirigami.Units.largeSpacing

        // ── Category 1: Windows ────────────────────────────────────────
        SettingsCard {
            headerText: i18n("Windows")
            collapsible: true
            collapsed: true

            contentItem: ColumnLayout {
                spacing: Kirigami.Units.smallSpacing

                AnimationEventCard {
                    Layout.fillWidth: true
                    eventPath: "window"
                    eventLabel: i18n("All Window Events")
                    isParentNode: true
                }

                AnimationEventCard {
                    Layout.fillWidth: true
                    eventPath: "window.open"
                    eventLabel: i18n("Open")
                }

                AnimationEventCard {
                    Layout.fillWidth: true
                    eventPath: "window.close"
                    eventLabel: i18n("Close")
                }

                AnimationEventCard {
                    Layout.fillWidth: true
                    eventPath: "window.minimize"
                    eventLabel: i18n("Minimize")
                }

                AnimationEventCard {
                    Layout.fillWidth: true
                    eventPath: "window.unminimize"
                    eventLabel: i18n("Unminimize")
                }

                AnimationEventCard {
                    Layout.fillWidth: true
                    eventPath: "window.maximize"
                    eventLabel: i18n("Maximize")
                }

                AnimationEventCard {
                    Layout.fillWidth: true
                    eventPath: "window.unmaximize"
                    eventLabel: i18n("Unmaximize")
                }

                AnimationEventCard {
                    Layout.fillWidth: true
                    eventPath: "window.move"
                    eventLabel: i18n("Move")
                }

                AnimationEventCard {
                    Layout.fillWidth: true
                    eventPath: "window.resize"
                    eventLabel: i18n("Resize")
                }

                AnimationEventCard {
                    Layout.fillWidth: true
                    eventPath: "window.focus"
                    eventLabel: i18n("Focus")
                }
            }
        }

        // ── Category 2: Zones ──────────────────────────────────────────
        SettingsCard {
            headerText: i18n("Zones")
            collapsible: true
            collapsed: true

            contentItem: ColumnLayout {
                spacing: Kirigami.Units.smallSpacing

                AnimationEventCard {
                    Layout.fillWidth: true
                    eventPath: "zone"
                    eventLabel: i18n("All Zone Events")
                    isParentNode: true
                }

                AnimationEventCard {
                    Layout.fillWidth: true
                    eventPath: "zone.snapIn"
                    eventLabel: i18n("Snap In")
                }

                AnimationEventCard {
                    Layout.fillWidth: true
                    eventPath: "zone.snapOut"
                    eventLabel: i18n("Snap Out")
                }

                AnimationEventCard {
                    Layout.fillWidth: true
                    eventPath: "zone.snapResize"
                    eventLabel: i18n("Snap Resize")
                }

                AnimationEventCard {
                    Layout.fillWidth: true
                    eventPath: "zone.highlight"
                    eventLabel: i18n("Highlight")
                }

                AnimationEventCard {
                    Layout.fillWidth: true
                    eventPath: "zone.layoutSwitchIn"
                    eventLabel: i18n("Layout Switch")
                }
            }
        }

        // ── Category 3: Overlays ───────────────────────────────────────
        SettingsCard {
            headerText: i18n("Overlays")
            collapsible: true
            collapsed: true

            contentItem: ColumnLayout {
                spacing: Kirigami.Units.smallSpacing

                AnimationEventCard {
                    Layout.fillWidth: true
                    eventPath: "osd"
                    eventLabel: i18n("All Overlay Events")
                    isParentNode: true
                }

                AnimationEventCard {
                    Layout.fillWidth: true
                    eventPath: "osd.show"
                    eventLabel: i18n("Show")
                }

                AnimationEventCard {
                    Layout.fillWidth: true
                    eventPath: "osd.hide"
                    eventLabel: i18n("Hide")
                }

                AnimationEventCard {
                    Layout.fillWidth: true
                    eventPath: "osd.pop"
                    eventLabel: i18n("Pop (scale-in)")
                }

                AnimationEventCard {
                    Layout.fillWidth: true
                    eventPath: "osd.dim"
                    eventLabel: i18n("Dim")
                }

                AnimationEventCard {
                    Layout.fillWidth: true
                    eventPath: "panel.popup.zoneSelector.show"
                    eventLabel: i18n("Zone Selector — Show")
                }

                AnimationEventCard {
                    Layout.fillWidth: true
                    eventPath: "panel.popup.zoneSelector.hide"
                    eventLabel: i18n("Zone Selector — Hide")
                }

                AnimationEventCard {
                    Layout.fillWidth: true
                    eventPath: "panel.popup.layoutPicker.show"
                    eventLabel: i18n("Layout Picker — Show")
                }

                AnimationEventCard {
                    Layout.fillWidth: true
                    eventPath: "panel.popup.layoutPicker.hide"
                    eventLabel: i18n("Layout Picker — Hide")
                }

                AnimationEventCard {
                    Layout.fillWidth: true
                    eventPath: "panel.popup.layoutPicker.popIn"
                    eventLabel: i18n("Layout Picker — Pop In (scale)")
                }

                AnimationEventCard {
                    Layout.fillWidth: true
                    eventPath: "panel.popup.snapAssist.show"
                    eventLabel: i18n("Snap Assist — Show")
                }

                AnimationEventCard {
                    Layout.fillWidth: true
                    eventPath: "panel.slideIn"
                    eventLabel: i18n("Slide In")
                }

                AnimationEventCard {
                    Layout.fillWidth: true
                    eventPath: "panel.slideOut"
                    eventLabel: i18n("Slide Out")
                }
            }
        }

        // ── Category 4: Workspaces ─────────────────────────────────────
        SettingsCard {
            headerText: i18n("Workspaces")
            collapsible: true
            collapsed: true

            contentItem: ColumnLayout {
                spacing: Kirigami.Units.smallSpacing

                AnimationEventCard {
                    Layout.fillWidth: true
                    eventPath: "workspace"
                    eventLabel: i18n("All Workspace Events")
                    isParentNode: true
                }

                AnimationEventCard {
                    Layout.fillWidth: true
                    eventPath: "workspace.switchIn"
                    eventLabel: i18n("Switch In")
                }

                AnimationEventCard {
                    Layout.fillWidth: true
                    eventPath: "workspace.switchOut"
                    eventLabel: i18n("Switch Out")
                }

                AnimationEventCard {
                    Layout.fillWidth: true
                    eventPath: "workspace.overview"
                    eventLabel: i18n("Overview")
                }
            }
        }

        // ── Category 5: Widgets ────────────────────────────────────────
        SettingsCard {
            headerText: i18n("Widgets")
            collapsible: true
            collapsed: true

            contentItem: ColumnLayout {
                spacing: Kirigami.Units.smallSpacing

                AnimationEventCard {
                    Layout.fillWidth: true
                    eventPath: "widget"
                    eventLabel: i18n("All Widget Events")
                    isParentNode: true
                }

                AnimationEventCard {
                    Layout.fillWidth: true
                    eventPath: "widget.hover"
                    eventLabel: i18n("Hover")
                }

                AnimationEventCard {
                    Layout.fillWidth: true
                    eventPath: "widget.press"
                    eventLabel: i18n("Press")
                }

                AnimationEventCard {
                    Layout.fillWidth: true
                    eventPath: "widget.toggle"
                    eventLabel: i18n("Toggle (knob slide)")
                }

                AnimationEventCard {
                    Layout.fillWidth: true
                    eventPath: "widget.badge"
                    eventLabel: i18n("Badge (overshoot)")
                }

                AnimationEventCard {
                    Layout.fillWidth: true
                    eventPath: "widget.tint"
                    eventLabel: i18n("Tint (background)")
                }

                AnimationEventCard {
                    Layout.fillWidth: true
                    eventPath: "widget.dim"
                    eventLabel: i18n("Dimension change")
                }

                AnimationEventCard {
                    Layout.fillWidth: true
                    eventPath: "widget.fade"
                    eventLabel: i18n("Fade (opacity)")
                }

                AnimationEventCard {
                    Layout.fillWidth: true
                    eventPath: "widget.reorder"
                    eventLabel: i18n("Reorder")
                }

                AnimationEventCard {
                    Layout.fillWidth: true
                    eventPath: "widget.accordion"
                    eventLabel: i18n("Accordion (expand)")
                }

                AnimationEventCard {
                    Layout.fillWidth: true
                    eventPath: "widget.progress"
                    eventLabel: i18n("Progress")
                }

                AnimationEventCard {
                    Layout.fillWidth: true
                    eventPath: "cursor.hover"
                    eventLabel: i18n("Cursor Hover")
                }

                AnimationEventCard {
                    Layout.fillWidth: true
                    eventPath: "cursor.click"
                    eventLabel: i18n("Cursor Click")
                }
            }
        }
    }
}
