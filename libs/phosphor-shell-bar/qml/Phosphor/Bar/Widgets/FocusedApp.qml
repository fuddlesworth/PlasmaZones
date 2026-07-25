// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
// Phosphor.Bar.FocusedApp, the active window's app + title.
//
// Binds to the Toplevels singleton (foreign-toplevel list). There is no
// "active toplevel" accessor, so a non-visual delegate per toplevel
// reports activation and title changes, and a rescan picks the activated
// window (or clears when none is). Collapses to zero width when nothing
// is focused (e.g. the bare desktop).

import QtQuick
import QtQml
import org.kde.kirigami as Kirigami
import Phosphor.Theme
import Phosphor.Shell

Item {
    id: root

    readonly property int maxTitleWidth: 220

    property string appTitle: ""
    property string appId: ""

    // Pick the activated toplevel from the current snapshot, or clear if
    // none is active. Driven by the per-toplevel delegates below.
    function _rescan() {
        const list = Toplevels.toplevels;
        for (let i = 0; i < list.length; i++) {
            const t = list[i];
            if (t && t.activated) {
                root.appTitle = t.title || "";
                root.appId = t.appId || "";
                return;
            }
        }
        root.appTitle = "";
        root.appId = "";
    }

    Instantiator {
        model: Toplevels.supported ? Toplevels.model : null

        delegate: QtObject {
            required property var toplevel

            readonly property bool activeFlag: toplevel ? toplevel.activated : false
            readonly property string titleText: toplevel ? toplevel.title : ""

            onActiveFlagChanged: root._rescan()
            onTitleTextChanged: root._rescan()
            Component.onCompleted: root._rescan()
            Component.onDestruction: root._rescan()
        }
    }

    visible: root.appTitle.length > 0
    implicitWidth: visible ? row.implicitWidth : 0
    implicitHeight: row.implicitHeight

    Accessible.role: Accessible.StaticText
    Accessible.name: root.appTitle

    Row {
        id: row

        spacing: Tokens.spacing_xs

        Kirigami.Icon {
            // Best-effort app icon from the app id, kept full-colour (not a
            // mask) so real app icons render naturally. Hidden when there is
            // no app id; Kirigami draws a fallback for an unresolved name.
            width: 16
            height: 16
            source: root.appId
            visible: root.appId.length > 0
            anchors.verticalCenter: parent.verticalCenter
        }

        Text {
            text: root.appTitle
            color: Theme.on_surface
            font.pixelSize: Tokens.font_size_label_l
            font.weight: Tokens.font_weight_medium
            font.family: Tokens.font_family
            elide: Text.ElideRight
            width: Math.min(implicitWidth, root.maxTitleWidth)
            anchors.verticalCenter: parent.verticalCenter
        }
    }
}
