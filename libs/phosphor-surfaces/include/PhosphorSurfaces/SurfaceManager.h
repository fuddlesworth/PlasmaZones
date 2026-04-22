// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <PhosphorSurfaces/SurfaceManagerConfig.h>
#include <PhosphorSurfaces/phosphorsurfaces_export.h>

#include <PhosphorLayer/SurfaceConfig.h>

#include <QObject>

#include <memory>

QT_BEGIN_NAMESPACE
class QQmlEngine;
QT_END_NAMESPACE

namespace PhosphorLayer {
class Surface;
}

namespace PhosphorSurfaces {

class PHOSPHORSURFACES_EXPORT SurfaceManager : public QObject
{
    Q_OBJECT

public:
    explicit SurfaceManager(SurfaceManagerConfig config, QObject* parent = nullptr);
    ~SurfaceManager() override;

    QQmlEngine* engine() const;

    // The returned Surface* is parented to this SurfaceManager (QObject
    // ownership). The caller drives show/hide/destroy; if the caller
    // does not destroy the surface, ~SurfaceManager will — but only
    // after the engine is gone, so callers must destroy surfaces first.
    PhosphorLayer::Surface* createSurface(PhosphorLayer::SurfaceConfig cfg);

    quint64 nextScopeGeneration();

    bool keepAliveActive() const;

Q_SIGNALS:
    void keepAliveLost();

private:
    void createKeepAlive();

    class Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace PhosphorSurfaces
