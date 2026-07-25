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

    property string _appTitle: ""
    property string _appId: ""

    // Pick the activated toplevel from the current snapshot, or clear if
    // none is active. Driven by the per-toplevel delegates below.
    function _rescan() {
        const list = Toplevels.toplevels;
        for (let i = 0; i < list.length; i++) {
            const t = list[i];
            if (t && t.activated) {
                root._appTitle = t.title || "";
                root._appId = t.appId || "";
                return;
            }
        }
        root._appTitle = "";
        root._appId = "";
    }

    Instantiator {
        model: Toplevels.supported ? Toplevels.model : null

        delegate: QtObject {
            required property ForeignToplevel toplevel

            readonly property bool activeFlag: toplevel ? toplevel.activated : false
            readonly property string titleText: toplevel ? toplevel.title : ""

            onActiveFlagChanged: root._rescan()
            // Only the focused window's title can change what this shows; a
            // background window retitling (browser tab switch, terminal cwd)
            // would otherwise cost a full list walk that cannot change the
            // result.
            onTitleTextChanged: if (activeFlag)
                root._rescan()
            Component.onCompleted: root._rescan()
            Component.onDestruction: root._rescan()
        }
    }

    // Both bindings read this predicate, never `visible`. Item.visible READS
    // as EFFECTIVE visibility, and the slot gates its cell on this very
    // implicitWidth, so deriving the width from `visible` closes a loop that
    // latches at zero and never recovers.
    readonly property bool available: root._appTitle.length > 0

    visible: root.available
    implicitWidth: root.available ? row.implicitWidth : 0
    implicitHeight: row.implicitHeight

    Accessible.role: Accessible.StaticText
    Accessible.name: root._appTitle

    Row {
        id: row

        spacing: Tokens.spacing_xs

        Kirigami.Icon {
            // Best-effort app icon from the app id, kept full-colour (not a
            // mask) so real app icons render naturally. Hidden when there is
            // no app id; Kirigami draws a fallback for an unresolved name.
            width: 16
            height: 16
            source: root._appId
            visible: root._appId.length > 0
            anchors.verticalCenter: parent.verticalCenter
        }

        Text {
            // Folded into the root's Accessible.name already; QQuickText
            // exposes itself as its own StaticText node, so without this
            // assistive tech reads the composed name and then re-reads
            // this fragment.
            Accessible.ignored: true
            text: root._appTitle
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
