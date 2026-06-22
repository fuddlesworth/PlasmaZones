// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

/**
 * @brief Inset separator for use between SettingsRow items inside a card.
 */
Kirigami.Separator {
    // Hide when disabled, mirroring SettingsRow's `visible: enabled`. In a
    // master-toggle card that is switched off, the rows collapse out; without
    // this the interleaved separators would remain as stray lines floating over
    // the dimmed body. Consumers that set their own `visible` override this.
    visible: enabled

    Layout.fillWidth: true
    Layout.leftMargin: Kirigami.Units.largeSpacing
    Layout.rightMargin: Kirigami.Units.largeSpacing
}
