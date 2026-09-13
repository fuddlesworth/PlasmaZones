// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import Phosphor.Lock
import Phosphor.Service.Mpris
import Phosphor.Service.UPower
import Phosphor.Shell
import Phosphor.Theme

// Services are shared by every output. The visual library accepts injected
// models so the same components can be exercised without locking a session.
Item {
    id: root
    required property var coordinator
    property int notificationCount: 0
    LockController {
        id: authController
        lock: root.coordinator.lock
    }
    UPowerHost {
        id: batteryHost
    }
    LockKeyboard {
        id: keyboardState
    }
    Loader {
        id: mediaFeed
        active: Appearance.lockMedia && authController.surfacesWanted
        sourceComponent: Item {
            MprisHost {
                id: mpris
            }
            readonly property MprisPlayer player: mpris.playerCount > 0 ? mpris.playerAt(0) : null
        }
    }
    PerScreen {
        model: PhosphorShell.screens
        delegate: LockSurface {
            id: surface
            required property var phosphorScreen
            property string name: ""
            property int index: 0
            property bool isPrimary: false
            screen: surface.phosphorScreen
            visible: authController.surfacesWanted
            LockScreen {
                anchors.fill: parent
                controller: authController
                decoration: ShellChrome.decorationComponent
                userName: root.coordinator.lock.userName
                keyboard: keyboardState
                battery: batteryHost
                session: root.coordinator.session
                notificationCount: root.notificationCount
                player: mediaFeed.item ? mediaFeed.item.player : null
            }
        }
    }
}
