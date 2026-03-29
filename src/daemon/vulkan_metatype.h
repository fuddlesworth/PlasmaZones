// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

namespace PlasmaZones {

// Shared property name for passing the QVulkanInstance* between main.cpp and OverlayService.
// Using a constant avoids silent nullptr from typos on either side.
// inline constexpr ensures a single definition across all TUs (C++17).
inline constexpr const char* PzVulkanInstanceProperty = "_pz_vulkanInstance";

} // namespace PlasmaZones

#if QT_CONFIG(vulkan)
#include <QVulkanInstance>
Q_DECLARE_METATYPE(QVulkanInstance*)
#endif
