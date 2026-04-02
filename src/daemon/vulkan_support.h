// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QtCore/qglobal.h> // Ensure QT_CONFIG is defined regardless of include order

#include <QVersionNumber>

#if QT_CONFIG(vulkan)
#include <QVulkanInstance>
Q_DECLARE_METATYPE(QVulkanInstance*)
#endif

#include <QGuiApplication>
#include <QLibrary>
#include <QQuickWindow>
#include <QSGRendererInterface>
#include <QString>

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
inline bool probeAndSetGraphicsApi(const QString& backend)
{
    if (backend == QLatin1String("vulkan")) {
#if QT_CONFIG(vulkan)
        QLibrary vulkanLib(QStringLiteral("vulkan"), 1);
        bool vulkanLibAvailable = vulkanLib.load();
        if (!vulkanLibAvailable) {
            vulkanLib.setFileName(QStringLiteral("vulkan"));
            vulkanLibAvailable = vulkanLib.load();
        }
        if (vulkanLibAvailable) {
            QQuickWindow::setGraphicsApi(QSGRendererInterface::Vulkan);
            return true;
        }
        QQuickWindow::setGraphicsApi(QSGRendererInterface::OpenGL);
        return false;
#else
        QQuickWindow::setGraphicsApi(QSGRendererInterface::OpenGL);
        return false;
#endif
    }
    if (backend == QLatin1String("opengl")) {
        QQuickWindow::setGraphicsApi(QSGRendererInterface::OpenGL);
    }
    // "auto" → let Qt choose (default behavior)
    return false;
}

/**
 * Create and register QVulkanInstance AFTER QGuiApplication construction.
 * Sets the instance as a dynamic property on the app for OverlayService retrieval.
 *
 * @param vulkanInstance  Pre-allocated QVulkanInstance (must outlive the app)
 * @param app             The running QGuiApplication
 * @return true if Vulkan instance was created successfully
 */
#if QT_CONFIG(vulkan)
inline bool createAndRegisterVulkanInstance(QVulkanInstance& vulkanInstance, QGuiApplication& app)
{
    vulkanInstance.setApiVersion(PzVulkanApiVersion);
    vulkanInstance.setExtensions(vulkanInstance.extensions() << QByteArrayLiteral("VK_EXT_swapchain_colorspace"));
    if (vulkanInstance.create()) {
        app.setProperty(PzVulkanInstanceProperty, QVariant::fromValue(&vulkanInstance));
        return true;
    }
    QQuickWindow::setGraphicsApi(QSGRendererInterface::OpenGL);
    return false;
}
#endif

} // namespace PlasmaZones
