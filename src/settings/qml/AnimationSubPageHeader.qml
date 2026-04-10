// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import org.kde.kirigami as Kirigami

Kirigami.InlineMessage {
    type: Kirigami.MessageType.Information
    visible: true
    text: i18n("Per-event overrides are saved to your config and will take effect once the animation engine integrates the profile tree.")
}
