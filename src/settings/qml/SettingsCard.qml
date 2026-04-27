// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import org.phosphor.animation

/**
 * @brief Styled settings card with hover effects and optional collapse.
 *
 * Features:
 *   - Subtle border highlight on hover (accent color)
 *   - Gentle upward translate on hover (-1px)
 *   - Optional collapsible content (set collapsible: true)
 *   - Smooth 200ms transitions throughout
 *
 * Usage:
 *   SettingsCard {
 *       headerText: i18n("Appearance")
 *       collapsible: true
 *       contentItem: Kirigami.FormLayout { ... }
 *   }
 *
 * Or with a custom header (same as Kirigami.Card):
 *   SettingsCard {
 *       header: MyCustomHeader { }
 *       contentItem: ColumnLayout { ... }
 *   }
 */
Item {
    id: root

    // ── Public API ──────────────────────────────────────────────────────
    // Simple header: just provide a title string
    property string headerText: ""
    // Custom header: provide any Item (overrides headerText)
    property Item header: null
    // Content (same as Kirigami.Card)
    property Item contentItem: null
    // Collapse
    property bool collapsible: false
    property bool collapsed: false
    // Header enable toggle
    property bool showToggle: false
    property bool toggleChecked: false

    signal toggleClicked(bool checked)

    onCollapsedChanged: {
        if (collapsed) {
            expandAnim.stop();
            collapseAnim.start();
        } else {
            collapseAnim.stop();
            expandAnim.start();
        }
    }
    Layout.fillWidth: true
    implicitHeight: cardBg.height
    implicitWidth: cardBg.width
    // Reparent contentItem into our content area with top padding
    onContentItemChanged: {
        if (contentItem) {
            contentItem.parent = contentColumn;
            contentItem.y = Kirigami.Units.largeSpacing;
            contentItem.width = Qt.binding(function() {
                return contentColumn.width;
            });
        }
    }
    // Handle custom header reparenting
    onHeaderChanged: {
        if (header) {
            header.parent = headerLoader;
            header.width = Qt.binding(function() {
                return headerLoader.width;
            });
            headerLoader.sourceComponent = null;
        }
    }

    HoverHandler {
        id: hoverHandler
    }

    Rectangle {
        id: cardBg

        width: root.width
        height: headerArea.height + contentClip.height
        radius: Kirigami.Units.smallSpacing * 1.5
        // Slightly elevated from page background
        color: Qt.rgba(Kirigami.Theme.textColor.r, Kirigami.Theme.textColor.g, Kirigami.Theme.textColor.b, 0.03)
        border.width: Math.round(Kirigami.Units.devicePixelRatio)
        border.color: {
            if (!root.enabled)
                return Qt.rgba(Kirigami.Theme.textColor.r, Kirigami.Theme.textColor.g, Kirigami.Theme.textColor.b, 0.04);

            if (hoverHandler.hovered)
                return Qt.rgba(Kirigami.Theme.highlightColor.r, Kirigami.Theme.highlightColor.g, Kirigami.Theme.highlightColor.b, 0.4);

            return Qt.rgba(Kirigami.Theme.textColor.r, Kirigami.Theme.textColor.g, Kirigami.Theme.textColor.b, 0.08);
        }

        // ── Header ─────────────────────────────────────────────────────
        Rectangle {
            id: headerArea

            width: parent.width
            height: headerLoader.height
            visible: root.headerText.length > 0 || root.header !== null
            color: Qt.rgba(Kirigami.Theme.textColor.r, Kirigami.Theme.textColor.g, Kirigami.Theme.textColor.b, 0.03)
            radius: cardBg.radius

            // Square off the bottom corners since content is below
            Rectangle {
                anchors.bottom: parent.bottom
                width: parent.width
                height: parent.radius
                color: parent.color
            }

            // Click to collapse/expand
            MouseArea {
                z: -1
                anchors.fill: parent
                enabled: root.collapsible
                cursorShape: root.collapsible ? Qt.PointingHandCursor : Qt.ArrowCursor
                onClicked: root.collapsed = !root.collapsed
            }

            // Default header (Heading from headerText)
            Loader {
                id: headerLoader

                width: parent.width
                sourceComponent: root.header !== null ? null : defaultHeaderComponent
            }

            Component {
                id: defaultHeaderComponent

                RowLayout {
                    width: parent ? parent.width : 0
                    spacing: 0

                    Kirigami.Heading {
                        text: root.headerText
                        level: 3
                        padding: Kirigami.Units.smallSpacing
                        leftPadding: Kirigami.Units.smallSpacing
                        Layout.fillWidth: true
                    }

                    // Header enable toggle
                    SettingsSwitch {
                        visible: root.showToggle
                        checked: root.toggleChecked
                        accessibleName: root.headerText
                        Layout.rightMargin: Kirigami.Units.smallSpacing
                        onToggled: function(newValue) {
                            root.toggleClicked(newValue);
                        }
                    }

                    // Collapse chevron
                    Kirigami.Icon {
                        visible: root.collapsible
                        source: "arrow-down"
                        Layout.preferredWidth: Kirigami.Units.iconSizes.small
                        Layout.preferredHeight: Kirigami.Units.iconSizes.small
                        Layout.rightMargin: Kirigami.Units.largeSpacing
                        rotation: root.collapsed ? -90 : 0
                        opacity: 0.5

                        Behavior on rotation {
                            PhosphorMotionAnimation {
                                profile: "widget.hover"
                                durationOverride: 200
                            }

                        }

                    }

                }

            }

        }

        // ── Separator ──────────────────────────────────────────────────
        Rectangle {
            id: headerSep

            anchors.top: headerArea.bottom
            width: parent.width
            height: headerArea.visible ? 1 : 0
            color: Qt.rgba(Kirigami.Theme.textColor.r, Kirigami.Theme.textColor.g, Kirigami.Theme.textColor.b, 0.08)
        }

        // ── Content (clipped for collapse animation) ───────────────────
        Item {
            id: contentClip

            anchors.top: headerSep.bottom
            width: parent.width
            height: contentColumn.implicitHeight
            clip: true
            opacity: root.showToggle && !root.toggleChecked ? 0.5 : 1
            enabled: root.showToggle ? root.toggleChecked : true

            Item {
                id: contentColumn

                width: parent.width
                implicitHeight: root.contentItem ? root.contentItem.implicitHeight + Kirigami.Units.largeSpacing * 2 : 0
            }

            SequentialAnimation {
                id: collapseAnim

                PhosphorMotionAnimation {
                    target: contentClip
                    properties: "opacity"
                    to: 0
                    profile: "widget.fade"
                }

                PhosphorMotionAnimation {
                    target: contentClip
                    properties: "height"
                    to: 0
                    profile: "widget.accordion"
                    durationOverride: 200
                }

            }

            SequentialAnimation {
                id: expandAnim

                PhosphorMotionAnimation {
                    target: contentClip
                    properties: "height"
                    to: contentColumn.implicitHeight
                    profile: "widget.accordion"
                    durationOverride: 200
                }

                PhosphorMotionAnimation {
                    target: contentClip
                    properties: "opacity"
                    to: root.showToggle && !root.toggleChecked ? 0.5 : 1
                    profile: "widget.fade"
                }

                ScriptAction {
                    script: {
                        contentClip.height = Qt.binding(function() {
                            return contentColumn.implicitHeight;
                        });
                        contentClip.opacity = Qt.binding(function() {
                            return root.showToggle && !root.toggleChecked ? 0.5 : 1;
                        });
                    }
                }

            }

            Behavior on opacity {
                PhosphorMotionAnimation {
                    profile: "widget.hover"
                    durationOverride: 200
                }

            }

        }

        Behavior on border.color {
            PhosphorMotionAnimation {
                profile: "widget.hover"
                durationOverride: 200
            }

        }

    }

    // Subtle lift on hover
    transform: Translate {
        y: hoverHandler.hovered && root.enabled ? -1 : 0

        Behavior on y {
            PhosphorMotionAnimation {
                profile: "widget.hover"
                durationOverride: 200
            }

        }

    }

}
