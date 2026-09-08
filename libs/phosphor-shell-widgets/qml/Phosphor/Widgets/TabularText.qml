// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
// Phosphor.Widgets.TabularText, the shell's number.
//
// Every value in the shell is set in the mono face with tabular figures
// (05 R7), so a clock or a percentage never shifts its neighbours when a
// digit changes. With `tickOnChange` a 2 px spectrum underline flashes
// under the text each time `text` changes and releases again: the
// signature under a changed digit. `t` picks the underline's hue on
// whichever axis the host is on.
//
//   TabularText { text: clock.time; tickOnChange: true; t: rail.t }

import QtQuick
import Phosphor.Theme

Text {
    id: root

    property bool tickOnChange: false
    property real t: 0

    font.family: Tokens.font_family_mono
    font.features: ({
            "tnum": 1
        })
    color: Theme.on_surface

    onTextChanged: {
        if (tickOnChange)
            underline.tick();
    }

    SpectrumUnderline {
        id: underline

        anchors.left: parent.left
        anchors.top: parent.bottom
        anchors.topMargin: Tokens.spacing_xxs
        length: root.contentWidth
        t: root.t
        restOpacity: 0
    }
}
