// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Layouts
import "WizardUtils.js" as WizardUtils
import org.kde.kirigami as Kirigami

/**
 * @brief Landscape step-2 preview frame for the wizard dialogs.
 *
 * A rounded, bordered box shaped to the monitor's aspect ratio. The dialog
 * supplies the preview content as children; they anchor to this Rectangle.
 */
Rectangle {
    id: frameRoot

    // The clamped monitor aspect ratio the frame keeps. See the wizard
    // dialogs' screenAspectRatio for how it is sampled and clamped.
    required property real aspectRatio
    readonly property var _colors: WizardUtils.wizardColors(Kirigami.Theme.textColor, Kirigami.Theme.highlightColor)

    // Let the column size this and cap it, rather than reading
    // parent.width upward: a layout child asking its parent how
    // wide it is asks the question the parent is still
    // answering. The height follows the width the layout
    // actually assigned.
    //
    // The 12-grid-unit height ceiling is folded into the width
    // cap rather than set as its own Layout.maximumHeight. A
    // standalone height cap overrides preferredHeight and
    // reshapes the box to whatever ratio the two caps form,
    // which discarded the monitor ratio on every display
    // narrower than 26:12. Capping width by the ratio holds the
    // same ceiling while keeping the box on-ratio.
    Layout.fillWidth: true
    Layout.maximumWidth: Math.min(Kirigami.Units.gridUnit * 26, Kirigami.Units.gridUnit * 12 * frameRoot.aspectRatio)
    Layout.preferredHeight: width / frameRoot.aspectRatio
    Layout.alignment: Qt.AlignHCenter
    radius: Kirigami.Units.smallSpacing * 2
    color: _colors.subtleBg
    // Repo-wide chrome-border convention: border.width is device-independent
    // and the renderer scales it, so a plain 1 stays a consistent hairline
    // (see the note in PositionPicker).
    border.width: 1
    border.color: _colors.accentBorder
}
