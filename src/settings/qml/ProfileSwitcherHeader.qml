// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import org.kde.kirigami as Kirigami

/**
 * @brief Sidebar-header profile switcher: the active profile's identicon mark
 *        plus a ProfileComboBox to activate another profile from anywhere.
 *
 * Selecting a profile STAGES it into the Save footer (applies on Save, reverts
 * on Discard), mirroring the per-row Activate on the Profiles page. Collapses
 * to zero height until at least one profile exists. Declares `compact` so the
 * Sidebar keeps the slot alive in the icon-only rail, where the name field
 * hides and only the mark remains.
 *
 * Hosted through Sidebar.headerContent by Main.qml, which watches `popupOpen`
 * to fold the dropdown into the window's nav-shortcut suppression.
 */
Item {
    id: profileHeader

    /// True while the switcher's dropdown is open. The hosting window folds
    /// this into its nav-shortcut suppression; the flag lives here because
    /// this file cannot see the window's ids.
    readonly property bool popupOpen: profileCombo.popup.visible

    // Declaring `compact` opts this slot into staying visible in the
    // icon-only rail (the Sidebar hides unaware consumers there) and
    // makes it receive the live value. In the rail there is no room for
    // the name field, so only the mark is shown — see switcherRow.
    property bool compact: false

    readonly property var profilesBridge: settingsController.profilesPage ? settingsController.profilesPage.bridge : null
    property var profileRows: profilesBridge ? profilesBridge.availableProfiles() : []
    readonly property int activeIndex: {
        for (let i = 0; i < profileRows.length; ++i) {
            if (profileRows[i].active)
                return i;
        }
        return -1;
    }
    readonly property var activeRow: activeIndex >= 0 ? profileRows[activeIndex] : null

    // Zero-height when there are no profiles, so the band disappears.
    implicitHeight: profileRows.length > 0 ? switcherRow.implicitHeight + Kirigami.Units.smallSpacing * 2 : 0

    Connections {
        function onProfilesChanged() {
            profileHeader.profileRows = profileHeader.profilesBridge ? profileHeader.profilesBridge.availableProfiles() : [];
        }

        target: profileHeader.profilesBridge
    }

    // A settings edit doesn't fire profilesChanged but can flip the
    // active profile's `modified` state — re-read so the marker updates.
    // Debounced: settingsChanged fires per property change (a slider
    // drag emits a burst), and each re-read walks every profile file.
    Timer {
        id: settingsEditRefresh

        interval: 250
        onTriggered: profileHeader.profileRows = profileHeader.profilesBridge ? profileHeader.profilesBridge.availableProfiles() : []
    }

    Connections {
        function onSettingsChanged() {
            settingsEditRefresh.restart();
        }

        target: appSettings
    }

    // Anchored rather than a RowLayout: the mark sits BESIDE the field
    // when expanded but CENTRED OVER it in the compact rail, and only
    // anchors can express both. The mark is never inside the field —
    // the Desktop style neither honours leftPadding for the text nor
    // tolerates a replaced contentItem (it calls positionToRectangle on
    // that item), so an in-field mark lands on top of the name.
    Item {
        id: switcherRow

        visible: profileHeader.profileRows.length > 0
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.verticalCenter: parent.verticalCenter
        anchors.leftMargin: Kirigami.Units.smallSpacing
        anchors.rightMargin: Kirigami.Units.smallSpacing
        implicitHeight: profileCombo.implicitHeight

        Item {
            id: markSlot

            visible: profileHeader.activeRow !== null
            anchors.verticalCenter: parent.verticalCenter
            // Beside the field when expanded; centred over it in the rail.
            // Positioned with `x`, NOT by swapping anchors: assigning
            // `undefined` to an anchor does not reliably clear it, so
            // toggling compact left both `left` and `horizontalCenter`
            // live, and Qt sized the item to satisfy both (left 0 +
            // centre w/2 ⇒ width = row width) — anchors beat an explicit
            // `width`, so the mark stretched across the whole row.
            // Expanded: sit at the same inset as the page icons below.
            // SidebarRow pads its content by largeSpacing on top of the
            // list's own smallSpacing inset, so matching that puts this
            // mark on the sidebar's icon column instead of hard against
            // the edge. Compact centres, exactly as SidebarRow does when
            // it drops leftPadding to zero for the rail.
            x: profileHeader.compact ? Math.round((parent.width - width) / 2) : Kirigami.Units.largeSpacing
            width: Kirigami.Units.iconSizes.smallMedium
            height: Kirigami.Units.iconSizes.smallMedium
            // Non-interactive on purpose: with no input handlers, clicks
            // fall through to the ComboBox beneath, so tapping the mark
            // in the rail opens the profile list.
            z: 1

            ProfileSignature {
                anchors.fill: parent
                signature: profileHeader.activeRow ? profileHeader.activeRow.signature : ""
            }

            // Modified badge — the settings have moved on from the
            // profile this mark represents.
            Rectangle {
                visible: profileHeader.activeRow !== null && profileHeader.activeRow.modified
                anchors.right: parent.right
                anchors.top: parent.top
                anchors.rightMargin: -1
                anchors.topMargin: -1
                width: Kirigami.Units.smallSpacing * 1.5
                height: width
                radius: width / 2
                color: Kirigami.Theme.neutralTextColor
                border.width: 1
                border.color: Kirigami.Theme.backgroundColor
            }
        }

        ProfileComboBox {
            id: profileCombo

            anchors.left: parent.left
            anchors.right: parent.right
            anchors.leftMargin: profileHeader.compact ? 0 : markSlot.x + markSlot.width + Kirigami.Units.smallSpacing
            anchors.verticalCenter: parent.verticalCenter
            // In the rail the field itself is hidden but stays laid out
            // and interactive, so the mark on top of it still opens a
            // correctly-anchored popup. opacity (not visible) because an
            // invisible item takes no input.
            opacity: profileHeader.compact ? 0 : 1
            model: profileHeader.profileRows
            // Seeded imperatively, NOT bound: QQC2 ComboBox writes
            // currentIndex on every user pick, severing a plain binding —
            // the trap the other two ProfileComboBox consumers document.
            // Re-sync on the two events that can move the selection out from
            // under the combo: the active profile changing, and the model
            // being reassigned (which resets currentIndex).
            function syncToActive() {
                currentIndex = profileHeader.activeIndex;
            }

            Component.onCompleted: syncToActive()
            onModelChanged: syncToActive()

            Connections {
                function onActiveIndexChanged() {
                    profileCombo.syncToActive();
                }

                target: profileHeader
            }

            displayText: profileHeader.activeRow ? profileHeader.activeRow.name : i18n("No profile")
            Accessible.name: i18n("Active profile")
            onActivated: function (index) {
                const row = profileHeader.profileRows[index];
                if (row && profileHeader.profilesBridge)
                    profileHeader.profilesBridge.activateProfile(row.id);
            }
        }
    }
}
