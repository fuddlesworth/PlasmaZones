// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorSurfaces/SurfaceManager.h>

#include <PhosphorLayer/Role.h>
#include <PhosphorLayer/Surface.h>
#include <PhosphorLayer/SurfaceConfig.h>
#include <PhosphorLayer/SurfaceFactory.h>

#include <QCoreApplication>
#include <QEventLoop>
#include <QGuiApplication>
#include <QLoggingCategory>
#include <QPointer>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QScreen>
#include <QTimer>

namespace PhosphorSurfaces {

Q_LOGGING_CATEGORY(lcSurfaces, "phosphor.surfaces")

class SurfaceManager::Impl
{
public:
    SurfaceManagerConfig config;
    std::unique_ptr<QQmlEngine> engine;
    PhosphorLayer::Surface* keepAliveSurface = nullptr;
    QPointer<QQuickWindow> keepAliveWindow;
    quint64 scopeGeneration = 0;

    void configureEngine()
    {
        engine = std::make_unique<QQmlEngine>();
        if (config.engineConfigurator) {
            config.engineConfigurator(*engine);
        }
    }
};

SurfaceManager::SurfaceManager(SurfaceManagerConfig config, QObject* parent)
    : QObject(parent)
    , m_impl(std::make_unique<Impl>())
{
    m_impl->config = std::move(config);
    m_impl->configureEngine();
    QTimer::singleShot(0, this, &SurfaceManager::createKeepAlive);
}

SurfaceManager::~SurfaceManager()
{
    if (m_impl->keepAliveSurface) {
        m_impl->keepAliveSurface->deleteLater();
        m_impl->keepAliveSurface = nullptr;
    }
    m_impl->keepAliveWindow = nullptr;

    constexpr int kDrainCap = 32;
    QEventLoop drainLoop;
    int passes = 0;
    for (; passes < kDrainCap; ++passes) {
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        if (!drainLoop.processEvents(QEventLoop::ExcludeUserInputEvents)) {
            break;
        }
    }
    if (passes == kDrainCap) {
        qCWarning(lcSurfaces) << "deferred-delete drain hit safety cap" << kDrainCap;
    }

    m_impl->engine.reset();
}

QQmlEngine* SurfaceManager::engine() const
{
    return m_impl->engine.get();
}

PhosphorLayer::Surface* SurfaceManager::createSurface(PhosphorLayer::SurfaceConfig cfg)
{
    if (!m_impl->config.surfaceFactory) {
        qCWarning(lcSurfaces) << "No SurfaceFactory configured";
        return nullptr;
    }

    cfg.sharedEngine = m_impl->engine.get();

    auto* surface = m_impl->config.surfaceFactory->create(std::move(cfg), this);
    if (!surface) {
        qCWarning(lcSurfaces) << "SurfaceFactory::create() returned nullptr";
        return nullptr;
    }

    surface->warmUp();

    if (surface->state() == PhosphorLayer::Surface::State::Failed) {
        qCWarning(lcSurfaces) << "Surface warm-up failed:" << surface->config().effectiveDebugName();
        delete surface;
        return nullptr;
    }

    if (!m_impl->config.pipelineCachePath.isEmpty() && surface->window()) {
        surface->window()->setProperty("_pipelineCachePath", m_impl->config.pipelineCachePath);
    }

    if (m_impl->config.windowConfigurator && surface->window()) {
        m_impl->config.windowConfigurator(*surface->window());
    }

    return surface;
}

quint64 SurfaceManager::nextScopeGeneration()
{
    return ++m_impl->scopeGeneration;
}

bool SurfaceManager::keepAliveActive() const
{
    return m_impl->keepAliveWindow && m_impl->keepAliveSurface;
}

void SurfaceManager::createKeepAlive()
{
    if (!m_impl->config.surfaceFactory) {
        return;
    }

    QScreen* screen = QGuiApplication::primaryScreen();
    if (!screen) {
        qCWarning(lcSurfaces) << "Keep-alive: no screen available at creation time";
        return;
    }

    PhosphorLayer::SurfaceConfig cfg;
    cfg.role = PhosphorLayer::Roles::Background.withScopePrefix(QStringLiteral("phosphor-surfaces-keepalive"));
    cfg.contentItem = std::make_unique<QQuickItem>();
    cfg.sharedEngine = m_impl->engine.get();
    cfg.debugName = QStringLiteral("keepalive");
    cfg.screen = screen;
    cfg.anchorsOverride = PhosphorLayer::AnchorNone;
    cfg.exclusiveZoneOverride = 0;

    auto* surface = m_impl->config.surfaceFactory->create(std::move(cfg), this);
    if (!surface) {
        qCWarning(lcSurfaces) << "Failed to create keep-alive surface";
        return;
    }

    surface->warmUp();

    if (surface->state() == PhosphorLayer::Surface::State::Failed) {
        qCWarning(lcSurfaces) << "Keep-alive surface warm-up failed";
        delete surface;
        return;
    }

    if (surface->window()) {
        surface->window()->setWidth(1);
        surface->window()->setHeight(1);
        if (m_impl->config.windowConfigurator) {
            m_impl->config.windowConfigurator(*surface->window());
        }
    }

    surface->show();
    m_impl->keepAliveSurface = surface;
    m_impl->keepAliveWindow = surface->window();

    connect(surface, &PhosphorLayer::Surface::failed, this, [this, surface](const QString& reason) {
        qCWarning(lcSurfaces) << "Keep-alive surface failed:" << reason;
        m_impl->keepAliveSurface = nullptr;
        m_impl->keepAliveWindow = nullptr;
        surface->deleteLater();
        Q_EMIT keepAliveLost();
    });

    qCDebug(lcSurfaces) << "Keep-alive surface created";
}

} // namespace PhosphorSurfaces
