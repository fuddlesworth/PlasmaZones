// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "vulkan_support.h"

#include <QLibrary>
#include <QQuickWindow>
#include <QSGRendererInterface>
#if QT_CONFIG(vulkan)
#include <QVulkanFunctions>
#endif

namespace PlasmaZones {

bool probeAndSetGraphicsApi(const QString& backend)
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

#if QT_CONFIG(vulkan)
bool createAndRegisterVulkanInstance(QVulkanInstance& vulkanInstance, QGuiApplication& app)
{
    vulkanInstance.setApiVersion(PVulkanApiVersion);
    vulkanInstance.setExtensions(vulkanInstance.extensions() << QByteArrayLiteral("VK_EXT_swapchain_colorspace"));
    if (!vulkanInstance.create()) {
        QQuickWindow::setGraphicsApi(QSGRendererInterface::OpenGL);
        return false;
    }

    // A successful create() is NOT sufficient: vkCreateInstance only loads the
    // Vulkan loader + ICD, it does NOT require an enumerable GPU. When the
    // userspace driver and the loaded kernel module are version-skewed (e.g. an
    // nvidia-utils upgrade without a reboot), the loader is present and the
    // instance is created, but vkEnumeratePhysicalDevices returns
    // VK_ERROR_INITIALIZATION_FAILED / zero devices. If we proceed, QRhi
    // discovers this only at scenegraph init on the render thread, where
    // QQuickWindow treats it as a qFatal — aborting the whole process and
    // crash-looping the daemon under systemd. Probe for a usable physical
    // device here, while we can still cleanly fall back to OpenGL.
    QVulkanFunctions* functions = vulkanInstance.functions();
    uint32_t physicalDeviceCount = 0;
    const VkResult enumResult = functions
        ? functions->vkEnumeratePhysicalDevices(vulkanInstance.vkInstance(), &physicalDeviceCount, nullptr)
        : VK_ERROR_INITIALIZATION_FAILED;
    if (enumResult != VK_SUCCESS || physicalDeviceCount == 0) {
        vulkanInstance.destroy();
        QQuickWindow::setGraphicsApi(QSGRendererInterface::OpenGL);
        return false;
    }

    app.setProperty(PVulkanInstanceProperty, QVariant::fromValue(&vulkanInstance));
    return true;
}
#endif

} // namespace PlasmaZones
