// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <PhosphorScreens/VirtualScreen.h>

#include <QString>
#include <QVariantMap>

namespace PlasmaZones::VirtualScreenUtils {

/// Convert a QVariantMap (from QML virtual screen editor) to a VirtualScreenDef.
/// Shared by SettingsController's immediate-apply path and StagingService's
/// persistence + D-Bus flush paths so both emit the same shape regardless of
/// which entry point the user hit.
Phosphor::Screens::VirtualScreenDef variantMapToVirtualScreenDef(const QVariantMap& map,
                                                                 const QString& physicalScreenId, int index);

} // namespace PlasmaZones::VirtualScreenUtils
