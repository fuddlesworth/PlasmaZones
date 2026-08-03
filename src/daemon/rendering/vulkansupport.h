// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QtGui/qtguiglobal.h> // Ensure QT_CONFIG(vulkan)/QT_FEATURE_vulkan is defined regardless of include order

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
inline constexpr const char* PVulkanInstanceProperty = "_p_vulkanInstance";

// Minimum Vulkan API version required by PlasmaZones.
// Vulkan 1.1 guarantees SPIR-V 1.3 support per the Vulkan spec appendix.
inline const QVersionNumber PVulkanApiVersion = QVersionNumber(1, 1);

/**
 * Probe Vulkan availability before QGuiApplication. Sets the graphics API and
 * returns true if Vulkan should be used. Call BEFORE QGuiApplication construction.
 *
 * @param backend  Normalized rendering backend string ("vulkan", "opengl", "auto")
 * @return true if Vulkan was selected and library loaded; false otherwise
 */
bool probeAndSetGraphicsApi(const QString& backend);

/**
 * Apply the Rendering.Gpu preference for the OpenGL path. Call BEFORE
 * QGuiApplication construction — Mesa reads DRI_PRIME when the EGL/DRI
 * screen is first opened, which happens during platform init.
 *
 * Sets DRI_PRIME to the configured "vendor:device" hex pair, which Mesa
 * (>= 22.0, i.e. AMD / Intel / nouveau+NVK) resolves to the matching render
 * node. When the selected vendor is NVIDIA, also exports the proprietary
 * stack's equivalents (__NV_PRIME_RENDER_OFFLOAD and the GLX vendor pick),
 * since that loader ignores DRI_PRIME. No-op when the preference is "auto"
 * or DRI_PRIME is already set in the environment (an explicit user override
 * outranks the config).
 *
 * Harmless on the Vulkan path (DRI_PRIME only steers the GL loader), and
 * deliberately applied there too: if Vulkan later falls back to OpenGL, the
 * GL context still lands on the configured GPU.
 *
 * @param gpuDevice  Normalized preference ("auto" or "vendor:device" hex)
 */
void applyOpenGlGpuPreference(const QString& gpuDevice);

/**
 * Create and register QVulkanInstance AFTER QGuiApplication construction.
 * Sets the instance as a dynamic property on the app for OverlayService retrieval.
 *
 * When @p gpuDevice is not "auto", the physical devices are enumerated and
 * the one matching the "vendor:device" hex pair is pinned by exporting
 * QT_VK_PHYSICAL_DEVICE_INDEX before any QQuickWindow initializes its
 * scenegraph (Qt's Vulkan RHI honors that variable at device selection). A
 * preference with no matching device logs a warning and leaves Qt's default
 * choice; an index already present in the environment outranks the config.
 *
 * @param vulkanInstance  Pre-allocated QVulkanInstance (must outlive the app)
 * @param app             The running QGuiApplication
 * @param gpuDevice       Normalized Rendering.Gpu preference ("auto" or "vendor:device" hex)
 * @return true if Vulkan instance was created successfully
 */
#if QT_CONFIG(vulkan)
bool createAndRegisterVulkanInstance(QVulkanInstance& vulkanInstance, QGuiApplication& app,
                                     const QString& gpuDevice = QString());
#endif

} // namespace PlasmaZones
