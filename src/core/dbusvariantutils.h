// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "plasmazones_export.h"
#include <QVariant>

namespace PlasmaZones {

/**
 * @brief D-Bus variant conversion utilities
 *
 * D-Bus wraps nested maps/lists in QDBusArgument which is read-only.
 * QML chokes on these, so we need to recursively unwrap everything
 * to plain QVariants. qdbus_cast won't help here - it only handles
 * top-level types.
 */
namespace DBusVariantUtils {

/**
 * @brief Recursively convert QDBusArgument values to plain QVariant types
 * @param value The QVariant that may contain QDBusArgument wrappers
 * @return A QVariant with all QDBusArgument wrappers converted to plain types
 *
 * Handles:
 * - QDBusArgument MapType → QVariantMap
 * - QDBusArgument ArrayType → QVariantList
 * - QDBusArgument StructureType → QVariantList
 * - QDBusArgument BasicType/VariantType → extracted value
 * - Nested QVariantList/QVariantMap → recursively converted
 * - Plain types → passed through unchanged
 */
PLASMAZONES_EXPORT QVariant convertDbusArgument(const QVariant& value);

} // namespace DBusVariantUtils

} // namespace PlasmaZones
