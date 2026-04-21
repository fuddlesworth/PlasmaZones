// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <PhosphorSurfaces/phosphorsurfaces_export.h>

#include <QString>

#include <functional>

QT_BEGIN_NAMESPACE
class QQmlEngine;
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
};

} // namespace PhosphorSurfaces
