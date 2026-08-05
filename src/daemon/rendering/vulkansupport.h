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
#include <QStringList>
#include <QVariantMap>

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
 * Sets DRI_PRIME to the configured "vendor:device" hex pair, which recent
 * Mesa (AMD / Intel / nouveau+NVK) resolves to the matching render node,
 * after verifying the pair against the machine's DRM render nodes (a stale
 * pin warns and leaves the driver default instead of exporting a dead tag).
 * When the selected vendor is NVIDIA, also exports the proprietary stack's
 * equivalents (__NV_PRIME_RENDER_OFFLOAD and the GLX vendor pick), since
 * that loader ignores DRI_PRIME; when it is NOT NVIDIA, an inherited
 * session-wide __NV_PRIME_RENDER_OFFLOAD is cleared so it cannot override
 * the pin. No-op when the preference is "auto" or DRI_PRIME is already set
 * in the environment (a pre-set value outranks the config).
 *
 * Applied on the Vulkan path too, deliberately: DRI_PRIME primarily steers
 * the GL loader, and covers the case where Vulkan falls back to OpenGL.
 * Called by the daemon and editor mains only — the standalone settings app
 * deliberately applies neither the backend nor the GPU pin (it never has;
 * its in-app shader previews are advisory rather than daemon-identical).
 * (Whether Mesa's Vulkan WSI also consults DRI_PRIME for presentation on
 * hybrid setups is driver-version-dependent; the exported pair names the
 * same device the Vulkan pin selects, so the two cannot disagree.)
 *
 * @param gpuDevice  Normalized preference ("auto" or "vendor:device" hex)
 */
void applyOpenGlGpuPreference(const QString& gpuDevice);

/**
 * Environment variable NAMES this process exported for the GPU preference
 * (DRI_PRIME, QT_VK_PHYSICAL_DEVICE_INDEX, NVIDIA offload vars). Spawn sites
 * MUST remove these from a child PlasmaZones process's environment: an
 * inherited export trips the child's pre-set-value guards (which cannot
 * distinguish a user's session export from a parent daemon's), freezing the
 * child on this process's stale pin — and QT_VK_PHYSICAL_DEVICE_INDEX is an
 * enumeration index only meaningful to the process that computed it. Only
 * variables this process actually set are listed, so scrubbing with this
 * list preserves genuine user-session overrides. The daemon and editor mains
 * publish the list app-wide as the PGpuExportedVarsProperty dynamic property
 * (core/types/constants.h) for spawn sites outside this link unit.
 */
QStringList exportedGpuPreferenceVariables();

/**
 * Environment variables this process CLEARED for the GPU preference, mapped
 * to their original values (currently only a session-wide
 * __NV_PRIME_RENDER_OFFLOAD when the pinned GPU is not NVIDIA). Spawn sites
 * restore these into a child's environment — the clear is scoped to this
 * process's own rendering and children must see the user's session value.
 * Published as PGpuClearedVarsProperty alongside the exported list.
 */
QVariantMap clearedGpuPreferenceVariables();

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
 * Only reached when probeAndSetGraphicsApi selected Vulkan, i.e. when the
 * configured backend is explicitly "vulkan". With Backend at "auto" the
 * Vulkan half of the GPU pin does not apply (Qt may still resolve the RHI to
 * Vulkan on its own, unpinned); only the DRI_PRIME half is in effect then.
 *
 * @param vulkanInstance  Pre-allocated QVulkanInstance (must outlive the app)
 * @param app             The running QGuiApplication
 * @param gpuDevice       Normalized Rendering.Gpu preference ("auto" or "vendor:device" hex)
 * @return true if Vulkan instance was created successfully
 */
#if QT_CONFIG(vulkan)
bool createAndRegisterVulkanInstance(QVulkanInstance& vulkanInstance, QGuiApplication& app, const QString& gpuDevice);
#endif

} // namespace PlasmaZones
