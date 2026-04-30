// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import org.kde.kirigami as Kirigami

// Informational banner shared by every Animations sub-page. Hidden by
// default — sub-pages opt in via `visible: true` if they have something
// worth saying. Lives as its own component so the copy can change in
// one place.
Kirigami.InlineMessage {
    type: Kirigami.MessageType.Information
    visible: false
    text: i18n("Animation changes apply immediately — no save required.")
}
