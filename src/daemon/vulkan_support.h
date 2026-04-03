// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QtCore/qglobal.h> // Ensure QT_CONFIG is defined regardless of include order

#include <QVersionNumber>

#if QT_CONFIG(vulkan)
#include <QVulkanInstance>
Q_DECLARE_METATYPE(QVulkanInstance*)
#endif

#include <QString>

class QGuiApplication;

namespace PlasmaZones {

// Shared property name for passing the QVulkanInstance* between main.cpp and OverlayService.
// Using a constant avoids silent nullptr from typos on either side.
// inline constexpr ensures a single definition across all TUs (C++17).
inline constexpr const char* PzVulkanInstanceProperty = "_pz_vulkanInstance";

// Minimum Vulkan API version required by PlasmaZones.
// Vulkan 1.1 guarantees SPIR-V 1.3 support per the Vulkan spec appendix.
inline const QVersionNumber PzVulkanApiVersion = QVersionNumber(1, 1);

/**
 * Probe Vulkan availability before QGuiApplication. Sets the graphics API and
 * returns true if Vulkan should be used. Call BEFORE QGuiApplication construction.
 *
 * @param backend  Normalized rendering backend string ("vulkan", "opengl", "auto")
 * @return true if Vulkan was selected and library loaded; false otherwise
 */
bool probeAndSetGraphicsApi(const QString& backend);

/**
 * Create and register QVulkanInstance AFTER QGuiApplication construction.
 * Sets the instance as a dynamic property on the app for OverlayService retrieval.
 *
 * @param vulkanInstance  Pre-allocated QVulkanInstance (must outlive the app)
 * @param app             The running QGuiApplication
 * @return true if Vulkan instance was created successfully
 */
#if QT_CONFIG(vulkan)
bool createAndRegisterVulkanInstance(QVulkanInstance& vulkanInstance, QGuiApplication& app);
#endif

} // namespace PlasmaZones
