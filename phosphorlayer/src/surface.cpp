// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorLayer/ILayerShellTransport.h>
#include <PhosphorLayer/IQmlEngineProvider.h>
#include <PhosphorLayer/IScreenProvider.h>
#include <PhosphorLayer/Surface.h>

#include "internal.h"

#include <QLatin1String>
#include <QPointer>
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
        // Ordered teardown: transport → component → window → engine. The component
        // holds a pointer into the engine and touches it in its dtor, so it must
        // go before the engine. The window hosts QML objects whose dtors also
        // reach into the engine, so releaseEngine() (which a per-surface provider
        // is documented as free to implement with `delete engine`) must NOT run
        // while the window is still alive. Posting the provider release behind
        // the window's deleteLater via the engine's own event queue guarantees
        // window teardown completes first on the main event loop.
        m_handle.reset();

        // QQmlComponent dtor can touch the engine — drop it while the engine
        // is still valid. Unique_ptr reset is synchronous and safe here.
        m_component.reset();

        QQmlEngine* engine = m_engine.data();
        const bool ownEngine = m_engineOwned;
        IQmlEngineProvider* provider = m_deps.engineProvider;

        if (m_window) {
            // QPointer auto-nulls if the window was already destroyed externally
            // (e.g. consumer called ->deleteLater() before ~Surface).
            m_window->deleteLater();
        }

        if (ownEngine && engine) {
            // Same event queue as m_window — posted after, runs after. Window
            // teardown completes before engine destruction.
            engine->deleteLater();
        } else if (engine && provider) {
            // Same ordering guarantee: post a queued invocation on the engine's
            // thread so it runs after the window's DeferredDelete event.
            QMetaObject::invokeMethod(
                engine,
                [engine, provider]() {
                    provider->releaseEngine(engine);
                },
                Qt::QueuedConnection);
        }
        m_engine = nullptr;
    }

    Surface* const m_q;
    SurfaceConfig m_config; // non-const so we can release contentItem in ensureContent
    const SurfaceDeps m_deps;

    State m_state = State::Constructed;
    Intent m_intent = Intent::Idle;

    QPointer<QQmlEngine> m_engine;
    bool m_engineOwned = false;
    // QPointer: survives external destruction (e.g. a consumer that mistakenly
    // deleteLater()s the window directly instead of the Surface). Prevents UB
    // on ~Impl when the window is already gone.
    QPointer<QQuickWindow> m_window;
    QPointer<QQuickItem> m_rootItem; ///< Item-rooted content; null when QML root is a Window
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
        // Guard against re-entry after failWith() — onComponentStatus and
        // async transport callbacks funnel through drive() and must not
        // advance a Failed surface.
        if (m_state == State::Failed) {
            return;
        }
        switch (m_intent) {
        case Intent::Warm:
        case Intent::Show:
            driveWarmOrShow();
            break;
        case Intent::Hide:
            if (m_state == State::Shown && m_window) {
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
        // No parent: ~Impl calls deleteLater on the owned engine. Parenting
        // to m_q would create a second owner (the QObject parent-child tree
        // would also delete it on ~QObject), causing a race at teardown.
        m_engine = new QQmlEngine();
        m_engineOwned = true;
        return true;
    }

    /**
     * Lazily create a wrapper QQuickWindow for Item-rooted content.
     * Not called for Window-rooted QML — that case adopts the QML root.
     */
    void ensureWrapperWindow()
    {
        if (m_window) {
            return;
        }
        m_window = new QQuickWindow();
        // Layer-shell surfaces are frameless and transparent by default; the
        // consumer's QML can override via its own properties.
        m_window->setFlag(Qt::FramelessWindowHint);
        m_window->setColor(Qt::transparent);
        applyWindowProperties(m_window);
    }

    void applyWindowProperties(QQuickWindow* target)
    {
        for (auto it = m_config.windowProperties.begin(); it != m_config.windowProperties.end(); ++it) {
            target->setProperty(it.key().toUtf8().constData(), it.value());
        }
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
        if (m_config.contentItem) {
            ensureWrapperWindow();
            m_rootItem = m_config.contentItem.release();
            m_rootItem->setParentItem(m_window->contentItem());
            m_rootItem->setParent(m_window);
            bindSize();
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
        // Order: beginCreate → setInitialProperties → setGeometry →
        // finishAttach (transport) → completeCreate → setVisible(false).
        // The two invariants:
        //
        // 1) attach BEFORE completeCreate: Qt Wayland's createShellSurface
        //    runs synchronously inside componentComplete's auto-show, and
        //    reads _ps_layer_shell exactly once at that moment to choose
        //    between xdg_toplevel and zwlr_layer_surface_v1. If we attach
        //    after, the FIRST shell surface is xdg_toplevel and the wl_-
        //    surface is locked into that role until the platform window is
        //    fully destroyed. Doing it before makes the first attempt a
        //    layer_surface with no stale role to clean up.
        //
        // 2) Non-zero size BEFORE completeCreate: PhosphorShell's
        //    computeLayerSize sends zwlr_layer_surface_v1::set_size(w, h)
        //    derived from QWindow::size(). For partial anchors that are
        //    NOT doubly-anchored on either axis (e.g. zone selector at
        //    Top|Left), w and h MUST be non-zero — a 0×0 commit causes
        //    the compositor to send back a 0×0 configure and the surface
        //    stays stuck. Setting screen geometry guarantees a valid
        //    initial commit; consumers that want a different size call
        //    setGeometry/setWidth/setHeight before show().
        auto* ctx = m_engine->rootContext();
        QObject* root = m_component->beginCreate(ctx);
        if (!root) {
            failWith(m_component->errorString().isEmpty()
                         ? QStringLiteral("QQmlComponent::beginCreate() returned nullptr")
                         : m_component->errorString());
            return false;
        }

        if (!m_config.windowProperties.isEmpty()) {
            m_component->setInitialProperties(root, m_config.windowProperties);
        }

        if (auto* win = qobject_cast<QQuickWindow*>(root)) {
            m_window = win;
            QQmlEngine::setObjectOwnership(win, QQmlEngine::CppOwnership);

            // Sized first — see #2 above.
            if (m_config.screen) {
                const QRect geo = m_config.screen->geometry();
                if (!geo.isEmpty()) {
                    win->setGeometry(geo);
                }
            }

            // Attached second — see #1 above.
            if (!finishAttach()) {
                m_component->completeCreate();
                return false;
            }

            // completeCreate triggers componentComplete, which (for
            // AutomaticVisibility — the Qt Quick default) calls
            // setWindowVisibility(Windowed) → QPA createShellSurface with
            // _ps_layer_shell set and a non-zero size.
            m_component->completeCreate();

            // Leave the surface unmapped for the consumer's explicit
            // show(). On Qt Wayland this unmaps the layer_surface; the
            // role persists across hide/show cycles so the next
            // setVisible(true) just remaps the same layer_surface.
            win->setVisible(false);
            return true;
        }

        if (auto* item = qobject_cast<QQuickItem*>(root)) {
            // Item-rooted QML doesn't drive platform-window creation —
            // attach can happen after completeCreate without race risk.
            ensureWrapperWindow();
            m_rootItem = item;
            m_rootItem->setParentItem(m_window->contentItem());
            m_rootItem->setParent(m_window);
            bindSize();
            m_component->completeCreate();
            return finishAttach();
        }

        m_component->completeCreate();
        delete root;
        failWith(QStringLiteral("Root object is neither a QQuickWindow nor a QQuickItem"));
        return false;
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
    return m_impl->m_window.data();
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
