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

    // Create a layer-shell surface from the given config. The surface is
    // warmed up (QML loaded, window created) before return. Returns nullptr
    // on failure (logged internally). Rejects async QML load paths — callers
    // must use qrc:/ or file:/ URLs that resolve synchronously.
    //
    // Ownership: the returned Surface* is parented to `surfaceParent` if
    // non-null, otherwise to this SurfaceManager. The caller drives
    // show/hide/destroy. If the caller does not destroy the surface
    // explicitly, the QObject parent will — but engine teardown runs in
    // ~SurfaceManager, so surfaces parented elsewhere must be destroyed
    // before this SurfaceManager is destroyed.
    PhosphorLayer::Surface* createSurface(PhosphorLayer::SurfaceConfig cfg, QObject* surfaceParent = nullptr);

    quint64 nextScopeGeneration();

    bool keepAliveActive() const;

Q_SIGNALS:
    void keepAliveLost();

private:
    void createKeepAlive();
    void configureWindow(PhosphorLayer::Surface* surface);

    class Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace PhosphorSurfaces
