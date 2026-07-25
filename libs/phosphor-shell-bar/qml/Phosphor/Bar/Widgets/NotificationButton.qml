// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
// Phosphor.Bar.NotificationButton, opens the notification center.
//
// An icon button whose `activated` signal is the seam the notification
// center popout binds to (Phase 4.3). The unread `badgeCount` is left at
// 0 here on purpose: the count must come from the shell's single shared
// NotificationServer (the daemon that owns org.freedesktop.Notifications,
// also feeding the toast host), not a second server instantiated
// per-widget, which would fail to acquire the bus name. The badge binds
// to that shared service when the notification center lands.

import QtQuick

BarIconButton {
    iconName: "notifications"
    label: "Notifications"
}
