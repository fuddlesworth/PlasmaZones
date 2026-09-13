// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
// Phosphor.Bar.NotificationButton, opens the notification center.
//
// An icon button whose `activated` signal opens NotificationPanel, and
// whose badge counts what has arrived since that panel was last opened.
//
// The count comes from the shell's SINGLE shared notification service,
// reached as the NotificationRegistry context property, never from a
// server instantiated per widget: org.freedesktop.Notifications admits one
// owner per session, so a second one would fail to acquire the name and
// count nothing forever.
//
// The badge is `unreadCount`, not the list length. It answers "is there
// anything new", which is what a bar glyph is for; the panel is where the
// full list lives.

BarIconButton {
    iconName: "notifications"
    label: qsTr("Notifications")
    badgeCount: NotificationRegistry.unreadCount
}
