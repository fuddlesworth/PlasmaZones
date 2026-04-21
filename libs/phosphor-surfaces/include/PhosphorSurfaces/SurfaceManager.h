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
class QScreen;
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

    PhosphorLayer::Surface* createSurface(PhosphorLayer::SurfaceConfig cfg);

    PhosphorLayer::Surface* warmUpSurface(PhosphorLayer::SurfaceConfig cfg);

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
