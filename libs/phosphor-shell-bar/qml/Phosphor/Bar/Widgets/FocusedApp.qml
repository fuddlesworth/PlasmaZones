// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
// Phosphor.Bar.FocusedApp, the active window's app + title.
//
// Binds to the Toplevels singleton's `activeToplevel`, which resolves the
// focused window process-wide. Collapses to zero width when nothing is
// focused (e.g. the bare desktop, or a compositor without the
// foreign-toplevel protocol, where the accessor stays null).

import QtQuick
import org.kde.kirigami as Kirigami
import Phosphor.Theme
import Phosphor.Shell

BarWidget {
    id: root

    readonly property int maxTitleWidth: 220

    // Plain bindings on the accessor. `activeToplevel` re-notifies when
    // focus moves, and the title/appId bindings re-evaluate on that
    // toplevel's own property signals, so a background window retitling
    // costs nothing here.
    readonly property string _appTitle: Toplevels.activeToplevel ? Toplevels.activeToplevel.title : ""
    readonly property string _appId: Toplevels.activeToplevel ? Toplevels.activeToplevel.appId : ""

    // Either half is enough. A Wayland toplevel can legitimately publish an
    // app id with an empty title (before the client sets one, splash and
    // loader surfaces), and gating on the title alone threw away a perfectly
    // good icon along with the missing label.
    available: root._appTitle.length > 0 || root._appId.length > 0
    contentWidth: row.implicitWidth
    contentHeight: row.implicitHeight

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
            id: title

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
            width: Math.min(title.implicitWidth, root.maxTitleWidth)
            anchors.verticalCenter: parent.verticalCenter
        }
    }
}
