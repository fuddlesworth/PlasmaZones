// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

// Singleton registration is driven by the `QT_QML_SINGLETON_TYPE TRUE`
// source-file property in src/settings/CMakeLists.txt — see the comment
// on CurvePresets.qml for why the CMake property, not this pragma, is the
// load-bearing piece under our generator setup.
pragma Singleton

import QtQuick

/**
 * @brief The size every shader pack preview composes at, whatever size it is
 * shown at.
 *
 * A singleton rather than a property on the shared wrapper, because the panes
 * need this number to lay themselves out before any wrapper exists: the
 * pointer stage and the animation field are sized by it directly, and both are
 * also hosted bare by their browser detail dialogs, with no wrapper above them
 * at all. A value every host and every pane can read without being handed it
 * is the only shape that covers both cases.
 *
 * Why a fixed canvas at all is written up in full on
 * DecorationChainPreview._canvasSize, which composes at this same size: two
 * previews of different sizes cannot show the same pack the same way, because
 * a multipass pack's blur buffer, the capture margin and the wallpaper
 * minification all follow the item's size rather than any parameter. Composing
 * at one size and displaying the result scaled makes a thumbnail and a detail
 * pane the same render at two magnifications.
 *
 * Sized to the detail pane, the largest place a preview is shown, so that pane
 * composes at roughly 1:1 and only the smaller slots are minified. The scale
 * itself belongs to the host, never to the pane.
 */
QtObject {
    /// Canvas dimensions in item pixels. Both axes are pinned, not just the
    /// width: a shared aspect is what keeps a backdrop slice and a ground crop
    /// identical between two hosts.
    readonly property size size: Qt.size(420, 236)
}
