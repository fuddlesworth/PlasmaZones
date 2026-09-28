// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
import QtQuick
import Phosphor.Theme

FocusScope {
    id: root
    readonly property bool fullScreen: true
    property var surfaceEffects: null
    readonly property rect materialRect: Qt.rect(0, 0, width, height)
    readonly property bool blurred: Appearance.settings.material !== "solid"
    function applyMaterial(): void {
        if (surfaceEffects)
            surfaceEffects.setBlurBehind(root, blurred ? materialRect : Qt.rect(0, 0, 0, 0), Qt.rect(0, 0, 0, 0), 0);
    }
    onMaterialRectChanged: Qt.callLater(applyMaterial)
    onBlurredChanged: Qt.callLater(applyMaterial)
    onSurfaceEffectsChanged: Qt.callLater(applyMaterial)
    Window.onWindowChanged: Qt.callLater(applyMaterial)
    property alias agent: prompt.agent
    property alias request: prompt.request
    property alias requester: prompt.requester
    property alias requesterProgram: prompt.requesterProgram
    property alias resourceText: prompt.resourceText
    property alias errorText: prompt.errorText
    property alias keyboard: prompt.keyboard
    property alias decoration: prompt.decoration
    property alias prompt: prompt
    implicitWidth: Screen.width
    implicitHeight: Screen.height
    PolkitDim {
        anchors.fill: parent
    }
    MouseArea {
        anchors.fill: parent
        acceptedButtons: Qt.AllButtons
        onWheel: wheel => wheel.accepted = true
    }
    PolkitPrompt {
        id: prompt
        width: Math.max(0, Math.min(460, root.width - 48))
        height: Math.max(0, Math.min(implicitHeight, root.height - 48))
        x: (root.width - width) / 2
        y: Math.max(24, Math.min(root.height - height - 24, root.height * (root.height <= 650 ? .5 : .45) - height / 2))
        focus: true
    }
    function focusInput(): void {
        prompt.focusInput();
    }
    Component.onCompleted: Qt.callLater(focusInput)
}
