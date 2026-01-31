// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "plasmazones_export.h"

namespace PlasmaZones {

/**
 * @brief Utility functions for converting between DragModifier enum and Qt::KeyboardModifier bitmask
 *
 * These are used by the KCM to convert between the internal DragModifier enum
 * (stored in settings) and the Qt::KeyboardModifier bitmask (used by UI components).
 */
namespace ModifierUtils {

/**
 * @brief Convert DragModifier enum value to Qt::KeyboardModifier bitmask
 * @param enumValue DragModifier enum value (0=None, 1=Shift, 2=Ctrl, etc.)
 * @return Corresponding Qt::KeyboardModifier bitmask
 */
PLASMAZONES_EXPORT int dragModifierToBitmask(int enumValue);

/**
 * @brief Convert Qt::KeyboardModifier bitmask to DragModifier enum value
 * @param bitmask Qt::KeyboardModifier bitmask
 * @return Corresponding DragModifier enum value
 *
 * For bitmasks that don't exactly match a DragModifier enum value,
 * this returns the closest match (e.g., Ctrl+Alt+Shift+Meta -> Ctrl+Alt+Meta).
 */
PLASMAZONES_EXPORT int bitmaskToDragModifier(int bitmask);

} // namespace ModifierUtils

} // namespace PlasmaZones
