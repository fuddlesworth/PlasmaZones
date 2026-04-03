// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "vulkan_support.h"

#include <QLibrary>
#include <QQuickWindow>
#include <QSGRendererInterface>

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
