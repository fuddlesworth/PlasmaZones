// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorLayer/ILayerShellTransport.h>
#include <PhosphorLayer/IQmlEngineProvider.h>
#include <PhosphorLayer/IScreenProvider.h>
#include <PhosphorLayer/Surface.h>

#include "internal.h"

#include <QLatin1String>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWindow>

namespace PhosphorLayer {

// ── Out-of-line definitions that need complete types ──────────────────

SurfaceConfig::SurfaceConfig() = default;
SurfaceConfig::~SurfaceConfig() = default;
SurfaceConfig::SurfaceConfig(SurfaceConfig&&) noexcept = default;
SurfaceConfig& SurfaceConfig::operator=(SurfaceConfig&&) noexcept = default;

ScreenProviderNotifier::ScreenProviderNotifier(QObject* parent)
    : QObject(parent)
{
}
ScreenProviderNotifier::~ScreenProviderNotifier() = default;

// ══════════════════════════════════════════════════════════════════════
// Surface::Impl — state machine
// ══════════════════════════════════════════════════════════════════════

class Surface::Impl
{
public:
    enum class Intent {
        Idle, ///< No pending user action
        Warm, ///< Ensure content is loaded, stay hidden
        Show, ///< Ensure content is loaded, make window visible
        Hide, ///< Unmap window (attached state retained)
    };

    Impl(Surface* q, SurfaceConfig cfg, SurfaceDeps deps)
        : m_q(q)
        , m_config(std::move(cfg))
        , m_deps(std::move(deps))
    {
    }

    ~Impl()
    {
        // Ordered teardown: transport → window → engine. Doing this in Impl's
        // dtor (rather than Surface's) keeps the lifetime dance local.
        m_handle.reset();
        delete m_window;
        m_window = nullptr;
        if (m_engineOwned && m_engine) {
            m_engine->deleteLater();
        } else if (m_engine && m_deps.engineProvider) {
            m_deps.engineProvider->releaseEngine(m_engine);
        }
        m_engine = nullptr;
    }

    Surface* const m_q;
    const SurfaceConfig m_config;
    const SurfaceDeps m_deps;

    State m_state = State::Constructed;
    Intent m_intent = Intent::Idle;

    QQmlEngine* m_engine = nullptr;
    bool m_engineOwned = false;
    QQuickWindow* m_window = nullptr;
    QQuickItem* m_rootItem = nullptr; ///< Either released from cfg.contentItem or created from cfg.contentUrl
    std::unique_ptr<QQmlComponent> m_component;
    std::unique_ptr<ITransportHandle> m_handle;
    QString m_failureReason;

    // ── Public-API drivers ────────────────────────────────────────────

    void requestWarm()
    {
        if (!guardAgainstFailed(QLatin1String("warmUp"))) {
            return;
        }
        m_intent = Intent::Warm;
        drive();
    }
    void requestShow()
    {
        if (!guardAgainstFailed(QLatin1String("show"))) {
            return;
        }
        m_intent = Intent::Show;
        drive();
    }
    void requestHide()
    {
        if (!guardAgainstFailed(QLatin1String("hide"))) {
            return;
        }
        m_intent = Intent::Hide;
        drive();
    }

private:
    bool guardAgainstFailed(QLatin1String action)
    {
        if (m_state == State::Failed) {
            qCWarning(lcPhosphorLayer) << "Surface" << m_config.effectiveDebugName() << action
                                       << "ignored: state is Failed (" << m_failureReason << ")";
            return false;
        }
        return true;
    }

    void drive()
    {
        switch (m_intent) {
        case Intent::Warm:
        case Intent::Show:
            driveWarmOrShow();
            break;
        case Intent::Hide:
            if (m_state == State::Shown) {
                m_window->hide();
                transitionTo(State::Hidden);
            }
            // Other states: already hidden-ish (Constructed/Warming/Hidden);
            // no-op, intent is latched for when Warming completes.
            break;
        case Intent::Idle:
            break;
        }
    }

    void driveWarmOrShow()
    {
        if (m_state == State::Constructed) {
            transitionTo(State::Warming);
            if (!ensureWarmed()) {
                // ensureWarmed has transitioned us to Failed already.
                return;
            }
            // ensureWarmed may have reached Hidden synchronously (inline-
            // ready content) OR stayed in Warming (async component load).
        }
        if (m_state == State::Warming) {
            // Still loading — onComponentStatus() will re-enter drive() when ready.
            return;
        }
        if (m_state == State::Hidden && m_intent == Intent::Show) {
            if (!m_window) {
                failWith(QStringLiteral("Internal error: Hidden state with no window"));
                return;
            }
            m_window->show();
            transitionTo(State::Shown);
        }
        // Intent::Warm + state Hidden → nothing more to do; we're warmed.
    }

    bool ensureWarmed()
    {
        if (!ensureEngine()) {
            return false;
        }
        if (!ensureWindow()) {
            return false;
        }
        applyContextProperties();
        return ensureContent();
    }

    bool ensureEngine()
    {
        if (m_engine) {
            return true;
        }
        if (m_config.sharedEngine) {
            m_engine = m_config.sharedEngine;
            m_engineOwned = false;
            return true;
        }
        if (m_deps.engineProvider) {
            m_engine = m_deps.engineProvider->engineForSurface(m_config);
            if (!m_engine) {
                failWith(QStringLiteral("IQmlEngineProvider::engineForSurface returned nullptr"));
                return false;
            }
            m_engineOwned = false;
            return true;
        }
        m_engine = new QQmlEngine(m_q);
        m_engineOwned = true;
        return true;
    }

    bool ensureWindow()
    {
        if (m_window) {
            return true;
        }
        m_window = new QQuickWindow();
        // Layer-shell surfaces are frameless and transparent by default. The
        // consumer's QML can override via its own properties if it needs an
        // opaque background.
        m_window->setFlag(Qt::FramelessWindowHint);
        m_window->setColor(Qt::transparent);
        return true;
    }

    void applyContextProperties()
    {
        auto* ctx = m_engine->rootContext();
        for (auto it = m_config.contextProperties.begin(); it != m_config.contextProperties.end(); ++it) {
            ctx->setContextProperty(it.key(), it.value());
        }
    }

    bool ensureContent()
    {
        // contentItem is unique_ptr const-membered on SurfaceConfig; since
        // the config is const we can't release() it. Work around by const_cast
        // on Impl's copy (Impl owns the moved-in config).
        auto& cfgMut = const_cast<SurfaceConfig&>(m_config);
        if (cfgMut.contentItem) {
            m_rootItem = cfgMut.contentItem.release();
            attachRootItem();
            return finishAttach();
        }
        if (!m_config.contentUrl.isEmpty()) {
            m_component = std::make_unique<QQmlComponent>(m_engine, m_config.contentUrl);
            if (m_component->isError()) {
                failWith(m_component->errorString());
                return false;
            }
            if (m_component->isLoading()) {
                QObject::connect(m_component.get(), &QQmlComponent::statusChanged, m_q, [this] {
                    onComponentStatus();
                });
                return true; // stays in Warming; drive() resumes on ready
            }
            return instantiateFromComponent();
        }
        failWith(QStringLiteral("SurfaceConfig has neither contentUrl nor contentItem"));
        return false;
    }

    void onComponentStatus()
    {
        if (!m_component || m_component->isLoading()) {
            return;
        }
        if (m_component->isError()) {
            failWith(m_component->errorString());
            return;
        }
        if (!instantiateFromComponent()) {
            return;
        }
        // Content ready — resume whatever intent was latched.
        drive();
    }

    bool instantiateFromComponent()
    {
        QObject* root = m_component->create(m_engine->rootContext());
        if (!root) {
            failWith(m_component->errorString().isEmpty() ? QStringLiteral("QQmlComponent::create() returned nullptr")
                                                          : m_component->errorString());
            return false;
        }
        auto* rootItem = qobject_cast<QQuickItem*>(root);
        if (!rootItem) {
            delete root;
            failWith(QStringLiteral("Root object is not a QQuickItem"));
            return false;
        }
        m_rootItem = rootItem;
        attachRootItem();
        return finishAttach();
    }

    void attachRootItem()
    {
        m_rootItem->setParentItem(m_window->contentItem());
        m_rootItem->setParent(m_window);
        bindSize();
    }

    void bindSize()
    {
        const auto sync = [this] {
            if (!m_rootItem || !m_window) {
                return;
            }
            m_rootItem->setWidth(m_window->width());
            m_rootItem->setHeight(m_window->height());
        };
        QObject::connect(m_window, &QQuickWindow::widthChanged, m_q, sync);
        QObject::connect(m_window, &QQuickWindow::heightChanged, m_q, sync);
        sync();
    }

    bool finishAttach()
    {
        TransportAttachArgs args;
        args.screen = m_config.screen;
        args.layer = m_config.effectiveLayer();
        args.anchors = m_config.effectiveAnchors();
        args.exclusiveZone = m_config.effectiveExclusiveZone();
        args.keyboard = m_config.effectiveKeyboard();
        args.margins = m_config.effectiveMargins();
        args.scope = m_config.role.scopePrefix;

        m_handle = m_deps.transport->attach(m_window, args);
        if (!m_handle) {
            failWith(QStringLiteral("ILayerShellTransport::attach returned nullptr"));
            return false;
        }
        transitionTo(State::Hidden);
        return true;
    }

    void transitionTo(State next)
    {
        if (m_state == next) {
            return;
        }
        m_state = next;
        Q_EMIT m_q->stateChanged(next);
    }

    void failWith(const QString& reason)
    {
        m_failureReason = reason;
        qCWarning(lcPhosphorLayer) << "Surface" << m_config.effectiveDebugName() << "failed:" << reason;
        if (m_state != State::Failed) {
            m_state = State::Failed;
            Q_EMIT m_q->stateChanged(State::Failed);
            Q_EMIT m_q->failed(reason);
        }
    }
};

// ══════════════════════════════════════════════════════════════════════
// Surface — thin façade over Impl
// ══════════════════════════════════════════════════════════════════════

Surface::Surface(SurfaceConfig cfg, SurfaceDeps deps, QObject* parent)
    : QObject(parent)
    , m_impl(std::make_unique<Impl>(this, std::move(cfg), std::move(deps)))
{
}

Surface::~Surface() = default;

Surface::State Surface::state() const noexcept
{
    return m_impl->m_state;
}

const SurfaceConfig& Surface::config() const noexcept
{
    return m_impl->m_config;
}

QQuickWindow* Surface::window() const noexcept
{
    return m_impl->m_window;
}

ITransportHandle* Surface::transport() const noexcept
{
    return m_impl->m_handle.get();
}

void Surface::show()
{
    m_impl->requestShow();
}

void Surface::hide()
{
    m_impl->requestHide();
}

void Surface::warmUp()
{
    m_impl->requestWarm();
}

} // namespace PhosphorLayer
