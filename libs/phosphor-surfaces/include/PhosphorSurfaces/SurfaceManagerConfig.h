// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <PhosphorSurfaces/phosphorsurfaces_export.h>

#include <QByteArrayList>
#include <QString>
#include <QVersionNumber>

#include <functional>

QT_BEGIN_NAMESPACE
class QQmlEngine;
class QVulkanInstance;
QT_END_NAMESPACE

namespace PhosphorLayer {
class SurfaceFactory;
}

namespace PhosphorSurfaces {

struct PHOSPHORSURFACES_EXPORT SurfaceManagerConfig
{
    PhosphorLayer::SurfaceFactory* surfaceFactory = nullptr;

    std::function<void(QQmlEngine&)> engineConfigurator;

    QString pipelineCachePath;

    // Caller-owned Vulkan instance. When non-null, every window created by
    // SurfaceManager will have setVulkanInstance() called with this pointer.
    // When null and the active Qt graphics API is Vulkan, SurfaceManager
    // creates and owns a fallback instance internally.
    QVulkanInstance* vulkanInstance = nullptr;

    // Vulkan API version for the fallback instance (only used when
    // vulkanInstance is null and the graphics API is Vulkan).
    QVersionNumber vulkanApiVersion = QVersionNumber(1, 1);

    // Extra Vulkan device extensions to request on every window's QRhi device
    // (via QQuickGraphicsConfiguration::setDeviceExtensions). Qt enables only
    // those the physical device actually supports, so listing an unsupported
    // one is harmless. Empty by default; callers that import external buffers
    // (e.g. dma-buf window thumbnails) populate it with the import extensions.
    QByteArrayList vulkanDeviceExtensions;
};

} // namespace PhosphorSurfaces
