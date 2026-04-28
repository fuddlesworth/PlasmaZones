// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorSurfaces/SurfaceManager.h>

#include <PhosphorLayer/Role.h>
#include <PhosphorLayer/Surface.h>
#include <PhosphorLayer/SurfaceConfig.h>
#include <PhosphorLayer/SurfaceFactory.h>

#include <QCoreApplication>
#include <QDir>
#include <QEventLoop>
#include <QGuiApplication>
#include <QLoggingCategory>
#include <QPointer>
#include <QQmlEngine>
#include <QQuickGraphicsConfiguration>
#include <QQuickItem>
#include <QQuickWindow>
#include <QScreen>
#include <QSGRendererInterface>
#include <QStandardPaths>
#include <QTimer>

#if QT_CONFIG(vulkan)
#include <QVulkanInstance>
#endif

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
    QMetaObject::Connection screenAddedConnection;

#if QT_CONFIG(vulkan)
    std::unique_ptr<QVulkanInstance> ownedVulkanInstance;
#endif

    bool pipelineCacheDirCreated = false;
    bool creatingKeepAlive = false;

    void configureEngine()
    {
        engine = std::make_unique<QQmlEngine>();
        if (config.engineConfigurator) {
            config.engineConfigurator(*engine);
        }
    }

    QVulkanInstance* resolveVulkanInstance()
    {
#if QT_CONFIG(vulkan)
        if (config.vulkanInstance) {
            return config.vulkanInstance;
        }
        if (QQuickWindow::graphicsApi() != QSGRendererInterface::Vulkan) {
            return nullptr;
        }
        if (ownedVulkanInstance) {
            return ownedVulkanInstance.get();
        }
        ownedVulkanInstance = std::make_unique<QVulkanInstance>();
        ownedVulkanInstance->setApiVersion(config.vulkanApiVersion);
        if (ownedVulkanInstance->create()) {
            qCDebug(lcSurfaces) << "Created fallback QVulkanInstance";
            return ownedVulkanInstance.get();
        }
        qCWarning(lcSurfaces) << "Failed to create fallback QVulkanInstance";
        ownedVulkanInstance.reset();
#endif
        return nullptr;
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
    if (m_impl->screenAddedConnection) {
        QObject::disconnect(m_impl->screenAddedConnection);
    }

    if (m_impl->keepAliveSurface) {
        m_impl->keepAliveSurface->deleteLater();
        m_impl->keepAliveSurface = nullptr;
    }
    m_impl->keepAliveWindow = nullptr;

    drainDeferredDeletes();
    m_impl->engine.reset();
}

void SurfaceManager::drainDeferredDeletes()
{
    constexpr int kDrainCap = 64;
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
}

QQmlEngine* SurfaceManager::engine() const
{
    return m_impl->engine.get();
}

PhosphorLayer::Surface* SurfaceManager::createSurface(PhosphorLayer::SurfaceConfig cfg, QObject* surfaceParent)
{
    if (!m_impl->config.surfaceFactory) {
        qCWarning(lcSurfaces) << "No SurfaceFactory configured";
        return nullptr;
    }

    cfg.sharedEngine = m_impl->engine.get();

    auto* surface = m_impl->config.surfaceFactory->create(std::move(cfg), surfaceParent ? surfaceParent : this);
    if (!surface) {
        qCWarning(lcSurfaces) << "SurfaceFactory::create() returned nullptr";
        return nullptr;
    }

    surface->warmUp();

    const auto state = surface->state();
    if (state == PhosphorLayer::Surface::State::Failed) {
        qCWarning(lcSurfaces) << "Surface warm-up failed:" << surface->config().effectiveDebugName();
        surface->deleteLater();
        return nullptr;
    }
    if (state == PhosphorLayer::Surface::State::Warming) {
        qCWarning(lcSurfaces) << "Surface still warming after warmUp() —"
                              << "async QML load paths are not supported by callers that"
                              << "dereference window() synchronously:" << surface->config().effectiveDebugName();
        surface->deleteLater();
        return nullptr;
    }

    configureWindow(surface);
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

void SurfaceManager::configureWindow(PhosphorLayer::Surface* surface)
{
    auto* w = surface->window();
    if (!w) {
        return;
    }

    if (!m_impl->config.pipelineCachePath.isEmpty()) {
        if (!m_impl->pipelineCacheDirCreated) {
            const QFileInfo fi(m_impl->config.pipelineCachePath);
            QDir().mkpath(fi.path());
            m_impl->pipelineCacheDirCreated = true;
        }
        QQuickGraphicsConfiguration gfxCfg = w->graphicsConfiguration();
        gfxCfg.setPipelineCacheSaveFile(m_impl->config.pipelineCachePath);
        w->setGraphicsConfiguration(gfxCfg);
    }

#if QT_CONFIG(vulkan)
    auto* vi = m_impl->resolveVulkanInstance();
    if (vi) {
        w->setVulkanInstance(vi);
    }
#endif
}

void SurfaceManager::createKeepAlive()
{
    if (m_impl->keepAliveSurface || m_impl->creatingKeepAlive) {
        return;
    }
    if (!m_impl->config.surfaceFactory) {
        return;
    }
    m_impl->creatingKeepAlive = true;

    QScreen* screen = QGuiApplication::primaryScreen();
    if (!screen) {
        qCWarning(lcSurfaces) << "Keep-alive: no screen available — will retry on screenAdded";
        m_impl->creatingKeepAlive = false;
        if (qGuiApp && !m_impl->screenAddedConnection) {
            m_impl->screenAddedConnection = connect(qGuiApp, &QGuiApplication::screenAdded, this, [this]() {
                if (!m_impl->keepAliveSurface) {
                    createKeepAlive();
                }
            });
        }
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
    // Warm-up at the eventual 1x1 visible size so the Vulkan swapchain isn't
    // first allocated at full-screen geometry and then resized — that costs
    // a destroy+recreate cycle on NVIDIA's proprietary stack and pre-allocates
    // ~25 MB at 4K for a surface whose only job is to keep the QSG/QRhi
    // device + QML engine warm for subsequent overlays.
    cfg.initialSize = QSize(1, 1);

    auto* surface = m_impl->config.surfaceFactory->create(std::move(cfg), this);
    if (!surface) {
        qCWarning(lcSurfaces) << "Failed to create keep-alive surface";
        m_impl->creatingKeepAlive = false;
        return;
    }

    surface->warmUp();

    const auto keepAliveState = surface->state();
    if (keepAliveState == PhosphorLayer::Surface::State::Failed
        || keepAliveState == PhosphorLayer::Surface::State::Warming) {
        qCWarning(lcSurfaces) << "Keep-alive surface warm-up failed (state:" << static_cast<int>(keepAliveState) << ")";
        surface->deleteLater();
        m_impl->creatingKeepAlive = false;
        return;
    }

    if (surface->window()) {
        surface->window()->setWidth(1);
        surface->window()->setHeight(1);
    }
    configureWindow(surface);

    surface->show();
    m_impl->keepAliveSurface = surface;
    m_impl->keepAliveWindow = surface->window();
    m_impl->creatingKeepAlive = false;

    connect(surface, &PhosphorLayer::Surface::failed, this, [this, surface](const QString& reason) {
        qCWarning(lcSurfaces) << "Keep-alive surface failed:" << reason;
        m_impl->keepAliveSurface = nullptr;
        m_impl->keepAliveWindow = nullptr;
        surface->deleteLater();
        Q_EMIT keepAliveLost();
        QTimer::singleShot(0, this, &SurfaceManager::createKeepAlive);
    });

    qCDebug(lcSurfaces) << "Keep-alive surface created";
}

} // namespace PhosphorSurfaces
