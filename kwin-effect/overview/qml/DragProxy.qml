// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

// The one item that moves during a drag. Tiles and labels never leave their
// model-bound position (the cell clips its surface and the model is the only
// authority on where a window is), so a press hands this root-level proxy a
// payload and the proxy travels with the pointer instead. It is the Drag
// source the DropAreas see and the item the effect's out-of-screen checks
// receive; on release it springs back to the origin item (KWin's
// returnAnimation) and the next model repositions the real tile.
//
// payload: {kind: "pz-window" | "pz-workspace", windowId, desktopId,
//           screenId, windowCount} where screenId/desktopId name the SOURCE
//           cell and windowCount is how many windows that cell listed at
//           press time (1 means the drop empties it).

import QtQuick
import org.kde.kirigami as Kirigami
import org.kde.kwin as KWinComponents

Item {
    id: proxy

    required property Item root

    property var payload: null
    property Item origin: null
    property point startPos: Qt.point(0, 0)
    readonly property bool dragging: proxy.Drag.active
    // The Drag.source items compare against, and the payload the out-of-screen
    // receiver reads. Keeping both on the proxy means a DropArea (drag.source)
    // and an itemDroppedOutOfScreen handler (item) read the same object.
    readonly property string kind: proxy.payload ? proxy.payload.kind : ""

    visible: false
    z: 100000
    Drag.keys: [proxy.kind]
    Drag.source: proxy
    Drag.proposedAction: Qt.MoveAction
    Drag.supportedActions: Qt.MoveAction
    Drag.dragType: Drag.Internal

    onXChanged: if (proxy.Drag.active) {
        proxy.root.effect.checkItemDraggedOutOfScreen(proxy);
    }
    onYChanged: if (proxy.Drag.active) {
        proxy.root.effect.checkItemDraggedOutOfScreen(proxy);
    }

    KWinComponents.WindowThumbnail {
        anchors.fill: parent
        visible: proxy.kind === "pz-window"
        clip: true
        // A null uuid while no window is being dragged; undefined would
        // log an assignment warning on the QUuid property.
        wId: proxy.payload && proxy.kind === "pz-window" ? proxy.root.effect.windowHandle(proxy.payload.windowId) : "{00000000-0000-0000-0000-000000000000}"
    }

    Rectangle {
        anchors.fill: parent
        visible: proxy.kind === "pz-workspace"
        radius: height / 2
        color: Kirigami.Theme.highlightColor
        opacity: 0.9
        Text {
            anchors.centerIn: parent
            color: Kirigami.Theme.highlightedTextColor
            font: Kirigami.Theme.smallFont
            text: proxy.payload && proxy.payload.label ? proxy.payload.label : ""
        }
    }

    Rectangle {
        anchors.fill: parent
        color: "transparent"
        border.width: Kirigami.Units.smallSpacing / 2
        border.color: Kirigami.Theme.highlightColor
        radius: proxy.kind === "pz-workspace" ? height / 2 : 0
    }

    ParallelAnimation {
        id: returnAnimation
        NumberAnimation {
            id: returnX
            target: proxy
            property: "x"
            duration: proxy.root.effect.animationDuration
            easing.type: Easing.InOutCubic
        }
        NumberAnimation {
            id: returnY
            target: proxy
            property: "y"
            duration: proxy.root.effect.animationDuration
            easing.type: Easing.InOutCubic
        }
        onFinished: proxy.reset()
    }

    // Arm the proxy on press: sized and positioned over the origin item, drag
    // active at once so the DropAreas track the pointer from the first
    // movement. Invisible until the DragHandler crosses the threshold, so a
    // plain click paints nothing.
    function begin(originItem, dragPayload, pressPoint) {
        returnAnimation.stop();
        proxy.origin = originItem;
        proxy.payload = dragPayload;
        const pos = originItem.mapToItem(proxy.parent, 0, 0);
        proxy.startPos = Qt.point(pos.x, pos.y);
        proxy.x = pos.x;
        proxy.y = pos.y;
        proxy.width = originItem.width;
        proxy.height = originItem.height;
        proxy.Drag.hotSpot = Qt.point(pressPoint.x, pressPoint.y);
        proxy.Drag.active = true;
    }

    function show() {
        proxy.visible = true;
        proxy.root.dragActive = true;
    }

    // Follow the pointer: translation is the DragHandler's activeTranslation,
    // measured in scene coordinates, which are this item's parent's.
    function follow(translation) {
        proxy.x = proxy.startPos.x + translation.x;
        proxy.y = proxy.startPos.y + translation.y;
    }

    // A press that never became a drag: disarm without a verb.
    function cancel() {
        proxy.Drag.active = false;
        proxy.reset();
    }

    // Release order follows KWin: Drag.drop() first, and only when no
    // DropArea of this view took it, offer the point to the other screens.
    // Either way the proxy springs back; the model moves the real tile.
    function finish(scenePos) {
        const action = proxy.Drag.drop();
        let landedHere = action === Qt.MoveAction;
        if (!landedHere) {
            const globalPos = proxy.root.targetScreen.mapToGlobal(scenePos);
            proxy.root.effect.checkItemDroppedOutOfScreen(globalPos, proxy);
        }
        const outsideThisScreen = scenePos.x < 0 || scenePos.y < 0 || scenePos.x >= proxy.root.width || scenePos.y >= proxy.root.height;
        // A window leaving a one-window cell empties it; hold this column's
        // reflow so the later destroy-renumber does not lurch. A drop landing
        // nowhere on this screen but outside it is the cross-screen case,
        // resolved by the other view.
        if (proxy.payload && proxy.payload.kind === "pz-window" && proxy.payload.windowCount === 1 && (landedHere || outsideThisScreen)) {
            proxy.root.workspaceColumn.holdReflow();
        }
        proxy.root.dragActive = false;
        returnX.to = proxy.startPos.x;
        returnY.to = proxy.startPos.y;
        returnAnimation.restart();
    }

    function reset() {
        proxy.visible = false;
        proxy.payload = null;
        proxy.origin = null;
        proxy.root.dragActive = false;
    }
}
