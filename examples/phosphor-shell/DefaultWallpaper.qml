// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import Phosphor.Theme

Image {
    source: "wallpapers/" + Appearance.settings.palette + ".png"
    fillMode: Image.PreserveAspectCrop
    asynchronous: true
    smooth: true
}
