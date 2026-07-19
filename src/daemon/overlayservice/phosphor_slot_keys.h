// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

// PZ-specific slot keys used as lookup strings into
// PhosphorOverlay::ShellState::slots. The library does not know about
// these names (it carries a generic key → QQuickItem* map); the daemon
// owns the slot-vocabulary the PassiveOverlayShell.qml resource exposes
// via its QML aliases (`osdSlotItem`, `snapAssistSlotItem`, …).
//
// Wrapped in function-local-static accessors to dodge static-init-order
// concerns when consumers read keys from another translation unit's
// static initializer. Per-call cost is one branch; the static QString
// is constructed once on first call.

#include <QString>

namespace PlasmaZones::PhosphorSlotKeys {

inline const QString& Osd()
{
    static const auto s = QStringLiteral("osd");
    return s;
}

inline const QString& SnapAssist()
{
    static const auto s = QStringLiteral("snapAssist");
    return s;
}

inline const QString& LayoutPicker()
{
    static const auto s = QStringLiteral("layoutPicker");
    return s;
}

inline const QString& ZoneSelector()
{
    static const auto s = QStringLiteral("zoneSelector");
    return s;
}

inline const QString& MainOverlay()
{
    static const auto s = QStringLiteral("mainOverlay");
    return s;
}

inline const QString& Cheatsheet()
{
    static const auto s = QStringLiteral("cheatsheet");
    return s;
}

} // namespace PlasmaZones::PhosphorSlotKeys
