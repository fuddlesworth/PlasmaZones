// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

// Panels drawn in place and fading out as the overview opens (KWin parity).
// Mouse interception consumes every pointer event while the effect runs, so
// a dock cannot receive input here; drawing it lets the open animation
// start from the real screen rather than from a panel-less frame.

import QtQuick
import org.kde.kwin as KWinComponents

Item {
    id: layer

    required property Item root

    KWinComponents.WindowModel {
        id: stackModel
    }

    Repeater {
        model: KWinComponents.WindowFilterModel {
            desktop: layer.root.currentDesktop
            screenName: layer.root.targetScreen ? layer.root.targetScreen.name : ""
            windowModel: stackModel
            windowType: KWinComponents.WindowFilterModel.Dock
        }

        delegate: KWinComponents.WindowThumbnail {
            required property var model
            visible: !model.window.hidden
            wId: model.window.internalId
            x: model.window.x - layer.root.targetScreen.geometry.x
            y: model.window.y - layer.root.targetScreen.geometry.y
            z: model.window.stackingOrder
            width: model.window.width
            height: model.window.height
        }
    }
}
