// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <PhosphorSurfaces/phosphorsurfaces_export.h>

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
};

} // namespace PhosphorSurfaces
