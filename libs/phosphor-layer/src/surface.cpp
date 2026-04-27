// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorLayer/ILayerShellTransport.h>
#include <PhosphorLayer/IQmlEngineProvider.h>
#include <PhosphorLayer/IScreenProvider.h>
#include <PhosphorLayer/ISurfaceAnimator.h>
#include <PhosphorLayer/Surface.h>
#include <PhosphorLayer/defaults/NoOpSurfaceAnimator.h>

#include "internal.h"

#include <QLatin1String>
#include <QMetaObject>
#include <QPointer>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QScreen>
#include <QWindow>

namespace PhosphorLayer {

// ── Out-of-line definitions that need complete types ──────────────────

SurfaceConfig::SurfaceConfig() = default;
SurfaceConfig::~SurfaceConfig() = default;
SurfaceConfig::SurfaceConfig(SurfaceConfig&&) noexcept = default;
SurfaceConfig& SurfaceConfig::operator=(SurfaceConfig&&) noexcept = default;

namespace {
/// Library-wide fallback animator used whenever the consumer leaves
/// `SurfaceFactory::Deps::animator` null. Shared across every Surface that
/// opts in to the no-op default; stateless and thread-safe (every method
/// is `const`-equivalent — calls onComplete synchronously, no per-instance
/// data). A function-static keeps initialisation out of the dynamic-init
/// phase so the dtor ordering at process shutdown can't accidentally call
/// into it after Qt has been torn down.
ISurfaceAnimator& noOpAnimator()
{
    static NoOpSurfaceAnimator s_instance;
    return s_instance;
}
} // namespace

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

    /// Tri-state engine ownership. Distinguishes the "shared engine (caller
    /// owns)" case from the "we created it" case from the "provider owns it"
    /// case so the dtor can call the right cleanup path. Using a 2-state
    /// `bool ownEngine` previously confused the shared + provider combination
    /// and could call `releaseEngine()` on an engine the provider never issued.
    enum class EngineOwnership : quint8 {
        None, ///< Caller retained ownership via SurfaceConfig::sharedEngine
        Self, ///< Constructed locally; dtor calls deleteLater()
        Provider, ///< Obtained from IQmlEngineProvider; dtor calls releaseEngine()
    };

    Impl(Surface* q, SurfaceConfig cfg, SurfaceDeps deps)
        : m_q(q)
        , m_config(std::move(cfg))
        , m_deps(std::move(deps))
    {
        // The "animator + keepMappedOnHide=false" combination is legitimate
        // for consumers that destroy on hide (snap-assist style) and use the
        // animator only for the show fade. A blanket constructor warning
        // would false-positive for that pattern; the actual misuse — calling
        // Surface::hide() expecting a visible fade-out under the unmap-on-
        // hide policy — is detected at the hide call site (gated on
        // m_hideFootgunWarned for once-per-surface diagnostics).
        // Subscribe to screen-removal so we never hand the transport a dangling
        // QScreen* after a hot-unplug. When the attached screen disappears we
        // null m_config.screen; the next finishAttach() falls back to the
        // provider's primary (validated on each call).
        if (auto* screens = m_deps.screenProvider) {
            if (auto* notifier = screens->notifier()) {
                m_screensChangedConnection =
                    QObject::connect(notifier, &ScreenProviderNotifier::screensChanged, m_q, [this] {
                        onScreensChanged();
                    });
            }
        }
    }

    ~Impl()
    {
        QObject::disconnect(m_screensChangedConnection);
        // Cancel any in-flight animation BEFORE we tear the QQuickItem tree
        // down. The animator may be holding a QPropertyAnimation or a Phase-3
        // AnimatedValue<qreal> targeting `m_rootItem` / `m_window->contentItem()`
        // — letting that survive into the destructor of the QQuickWindow it
        // animates causes a "modify after delete" on the property setter when
        // the next animation tick fires.
        animator().cancel(m_q);
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
        const EngineOwnership ownership = m_engineOwnership;
        IQmlEngineProvider* provider = m_deps.engineProvider;

        if (m_window) {
            // QPointer auto-nulls if the window was already destroyed externally
            // (e.g. consumer called ->deleteLater() before ~Surface).
            //
            // Reparent to null before deleteLater. Window-root QML adopted via
            // beginCreate is JS-parented to the engine's root context; deleteLater
            // on the engine (below, EngineOwnership::Self) would then delete the
            // window a second time when the engine tears down its object graph.
            // Explicit reparent-to-null breaks that implicit tree edge so our
            // deleteLater is the only delete path.
            m_window->setParent(nullptr);
            m_window->deleteLater();
        }

        switch (ownership) {
        case EngineOwnership::Self:
            if (engine) {
                // Same event queue as m_window — posted after, runs after. Window
                // teardown completes before engine destruction.
                engine->deleteLater();
            }
            break;
        case EngineOwnership::Provider:
            if (engine && provider) {
                // Same ordering guarantee: post a queued invocation on the engine's
                // thread so it runs after the window's DeferredDelete event.
                QMetaObject::invokeMethod(
                    engine,
                    [engine, provider]() {
                        provider->releaseEngine(engine);
                    },
                    Qt::QueuedConnection);
            }
            break;
        case EngineOwnership::None:
            // sharedEngine path: caller retains ownership; we touch nothing.
            break;
        }
        m_engine = nullptr;
    }

    Surface* const m_q;
    SurfaceConfig m_config; // non-const so we can release contentItem in ensureContent
    const SurfaceDeps m_deps;
    QMetaObject::Connection m_screensChangedConnection;

    State m_state = State::Constructed;
    Intent m_intent = Intent::Idle;

    /// Re-entry guard for drive(). QML binding evaluation triggered by
    /// setInitialProperties / applyWindowProperties can synchronously reach
    /// show()/hide()/warmUp(), which funnel back through drive(). If we
    /// recursed, an outer frame could observe a half-advanced state after
    /// its inner frame transitioned. The guard latches the new intent and
    /// lets the current drive() frame finish — the outer frame then re-
    /// evaluates with the latched intent when it loops back to the top.
    bool m_driving = false;
    bool m_intentPending = false; ///< A nested drive() latched a new intent
    /// Once-per-surface flag for the "animator + unmap-on-hide" diagnostic.
    /// Logged at the first Surface::hide() call where the combination would
    /// drop animation frames; subsequent hides on the same surface stay
    /// quiet so a hot dismiss path doesn't fill the journal.
    bool m_hideFootgunWarned = false;

    QPointer<QQmlEngine> m_engine;
    EngineOwnership m_engineOwnership = EngineOwnership::None;
    /// Child QQmlContext used ONLY for the shared-engine + contextProperties
    /// combination, to keep per-surface overrides from leaking onto the
    /// shared rootContext. Parented to m_q so teardown is automatic; lifetime
    /// matches the Surface. Null on the owned/provider-engine path.
    std::unique_ptr<QQmlContext> m_childContext;
    // QPointer: survives external destruction (e.g. a consumer that mistakenly
    // deleteLater()s the window directly instead of the Surface). Prevents UB
    // on ~Impl when the window is already gone.
    QPointer<QQuickWindow> m_window;
    QPointer<QQuickItem> m_rootItem; ///< Item-rooted content; null when QML root is a Window
    std::unique_ptr<QQmlComponent> m_component;
    std::unique_ptr<ITransportHandle> m_handle;
    QString m_failureReason;

    // ── Animator helpers ──────────────────────────────────────────────
    /// Resolve the injected animator or fall back to the library-wide no-op.
    /// Always returns a valid reference — every dispatch site can call
    /// `animator().beginShow(...)` without null-checking. The fallback is a
    /// shared no-op singleton (see noOpAnimator()).
    ISurfaceAnimator& animator() const
    {
        return m_deps.animator ? *m_deps.animator : noOpAnimator();
    }

    /// Resolve the QQuickItem the animator should drive: the explicit
    /// item-rooted content, or the Window's contentItem for Window-rooted
    /// QML. Both expose `opacity` / `scale` / transforms in the standard
    /// QQuickItem property surface, so the animator implementation needs no
    /// special-case for the two content shapes.
    QQuickItem* animatorTarget() const
    {
        if (m_rootItem) {
            return m_rootItem.data();
        }
        return m_window ? m_window->contentItem() : nullptr;
    }

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
        // Re-entry from a nested requestShow/Hide/Warm: the outer frame
        // will see m_intentPending on loop-exit and re-run with the latched
        // intent. Prevents two drive() frames from interleaving state
        // transitions mid-transition.
        if (m_driving) {
            m_intentPending = true;
            return;
        }
        m_driving = true;
        do {
            m_intentPending = false;
            switch (m_intent) {
            case Intent::Warm:
            case Intent::Show:
                driveWarmOrShow();
                break;
            case Intent::Hide:
                if (m_state == State::Shown && m_window) {
                    // Cancel any in-flight show animation before starting the
                    // hide path. The animator's per-surface state is keyed on
                    // m_q — letting an in-flight beginShow finish onto a
                    // window we've just unmapped writes a stale opacity into
                    // a torn-down QQuickItem.
                    animator().cancel(m_q);

                    if (m_config.keepMappedOnHide) {
                        // Phase-5 lifecycle: keep the wl_surface attached
                        // across hide/show cycles. The animator drives the
                        // QQuickItem's opacity to zero for the visual fade;
                        // the still-mapped layer surface stops intercepting
                        // clicks via WindowTransparentForInput. The QQuick-
                        // Window stays Qt-visible for the surface's entire
                        // lifetime (so the Vulkan swapchain survives). m_q
                        // captured by-value into the lambda — onComplete may
                        // fire after this drive() frame returns, and the
                        // raw `this` pointer is unsafe across that span.
                        m_window->setFlag(Qt::WindowTransparentForInput, true);
                        QPointer<Surface> self = m_q;
                        animator().beginHide(m_q, animatorTarget(), [self]() {
                            // No-op completion: we already transitioned
                            // synchronously below. The callback is a
                            // notification slot for consumers that want to
                            // observe animation end (none today; reserved
                            // for future ScreenSurfaceRegistry teardown
                            // signalling).
                            Q_UNUSED(self);
                        });
                    } else {
                        // Pre-Phase-5 lifecycle: animator runs the visual
                        // fade in parallel (synchronously for the no-op
                        // default), then we unmap the wl_surface. The
                        // ISurfaceAnimator contract is "beginHide fires
                        // on every Shown→Hidden transition" — preserved
                        // here so consumers can rely on the dispatch
                        // count for bookkeeping (e.g. settling counters,
                        // teardown gating). Most animators that *actually
                        // animate* will want keepMappedOnHide=true —
                        // synchronous unmap defeats the visual transition
                        // because the compositor tears the surface down
                        // before the animation is perceptible. Kept as
                        // the default for callers that never opt in to
                        // an animator. The per-surface footgun warning
                        // surfaces the mismatch in the journal.
                        if (m_deps.animator && !m_hideFootgunWarned) {
                            m_hideFootgunWarned = true;
                            qCWarning(lcPhosphorLayer)
                                << "Surface" << m_config.effectiveDebugName()
                                << "Surface::hide() dispatched beginHide on an animator but"
                                << "keepMappedOnHide=false — wl_surface unmaps synchronously and the"
                                << "fade-out frames will not paint. Set"
                                << "SurfaceConfig::keepMappedOnHide=true if a visible fade-out is"
                                << "desired, or destroy the surface instead of calling hide() if the"
                                << "animator was wired only for show.";
                        }
                        QPointer<Surface> self = m_q;
                        animator().beginHide(m_q, animatorTarget(), [self]() {
                            Q_UNUSED(self);
                        });
                        m_window->hide();
                    }
                    transitionTo(State::Hidden);
                }
                // Other states: already hidden-ish (Constructed/Warming/Hidden);
                // no-op, intent is latched for when Warming completes.
                break;
            case Intent::Idle:
                break;
            }
            // If a nested call latched a new intent, run the loop once more
            // under the same m_driving lock so the caller never observes a
            // stale state when control returns.
        } while (m_intentPending && m_state != State::Failed);
        m_driving = false;
    }

    void onScreensChanged()
    {
        // A screen the Surface was bound to has been removed. The QScreen*
        // is valid for the duration of the signal but dangling by the next
        // event-loop turn. Null the config eagerly; the next finishAttach()
        // falls back to the provider's primary.
        auto* screens = m_deps.screenProvider;
        if (!screens || !m_config.screen) {
            return;
        }
        const auto list = screens->screens();
        if (!list.contains(m_config.screen)) {
            qCWarning(lcPhosphorLayer) << "Surface" << m_config.effectiveDebugName()
                                       << "attached screen was removed — reassigning to primary on next attach";
            m_config.screen = nullptr;
            // Notify the consumer: if the Surface is Shown, its compositor-side
            // handle now points at a phantom output. Consumers have two valid
            // responses — destroy+rebuild, or wait for the screen to come back.
            // The library cannot pick either for them (destroy-during-signal is
            // unsafe without consumer cooperation), so surface the event.
            if (m_state == State::Shown || m_state == State::Hidden) {
                Q_EMIT m_q->screenLost();
            }
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
        if (m_intent == Intent::Show && (m_state == State::Hidden || m_state == State::Shown)) {
            if (!m_window) {
                if (m_state == State::Hidden) {
                    failWith(QStringLiteral("Internal error: Hidden state with no window"));
                }
                return;
            }
            // Cancel any in-flight animation before kicking the new beginShow.
            //   - Hidden → Shown: cancel a still-running beginHide so the
            //     supersession doesn't keep driving opacity to 0 after we
            //     set it back to 1.
            //   - Shown  → Shown: cancel a still-running beginShow so a
            //     rapid re-trigger (e.g. user switches layout while the
            //     OSD's fade-in is in progress) doesn't double-drive the
            //     same property.
            animator().cancel(m_q);

            // Dispatch beginShow BEFORE m_window->show(). runLeg sets the
            // target's opacity (and optionally scale) to the computed
            // fromOpacity synchronously on dispatch, so the QQuickItem's
            // opacity is at 0 (or wherever a supersession left it)
            // BEFORE the wl_surface maps. Without this ordering the cold-
            // start path mapped the window with opacity=1.0 (QQuickItem
            // default), giving the compositor a window to paint a single
            // frame at the terminal value before the animator's
            // setOpacity(0) lands — visible as a one-frame "flash" of the
            // fully-opaque OSD prior to the fade-in. Always dispatch
            // beginShow — the Shown→Shown path replays the visual fade so
            // OSD-style consumers that re-show on every user trigger see
            // the same animation each time.
            QPointer<Surface> self = m_q;
            animator().beginShow(m_q, animatorTarget(), [self]() {
                Q_UNUSED(self);
            });

            if (m_state == State::Hidden) {
                if (m_config.keepMappedOnHide) {
                    // Window is already Qt-visible across the keepMappedOnHide
                    // hide path; clearing TransparentForInput restores click
                    // routing. m_window->show() is still called for the
                    // first-show case where we go Constructed → Warming →
                    // Hidden → Shown (Qt window not yet mapped at that point).
                    m_window->setFlag(Qt::WindowTransparentForInput, false);
                }
                m_window->show();
            }
            if (m_state != State::Shown) {
                transitionTo(State::Shown);
            }
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
        // sharedEngine takes precedence over engineProvider when both are set.
        // SurfaceFactory::create rejects that combination, but keep the
        // precedence explicit here so the dtor path (driven by
        // m_engineOwnership) never calls releaseEngine() on an engine the
        // provider didn't issue.
        if (m_config.sharedEngine) {
            m_engine = m_config.sharedEngine;
            m_engineOwnership = EngineOwnership::None;
            return true;
        }
        if (m_deps.engineProvider) {
            m_engine = m_deps.engineProvider->engineForSurface(m_config);
            if (!m_engine) {
                failWith(QStringLiteral("IQmlEngineProvider::engineForSurface returned nullptr"));
                return false;
            }
            m_engineOwnership = EngineOwnership::Provider;
            return true;
        }
        // No parent: ~Impl calls deleteLater on the owned engine. Parenting
        // to m_q would create a second owner (the QObject parent-child tree
        // would also delete it on ~QObject), causing a race at teardown.
        m_engine = new QQmlEngine();
        m_engineOwnership = EngineOwnership::Self;
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

        // Pre-size from the target screen so the transport sees a non-zero
        // size at attach time. Same rationale as the QML-Window-root path:
        // for partial-anchor layer surfaces (e.g. Top|Left only), a 0×0
        // initial commit causes the compositor to echo back 0×0 and the
        // surface stays stuck. Callers that want a different size call
        // setWidth/setHeight between warmUp() and show().
        if (m_config.screen) {
            const QRect geo = m_config.screen->geometry();
            if (!geo.isEmpty()) {
                m_window->setGeometry(geo);
            }
        }
    }

    void applyWindowProperties(QQuickWindow* target)
    {
        for (auto it = m_config.windowProperties.begin(); it != m_config.windowProperties.end(); ++it) {
            target->setProperty(it.key().toUtf8().constData(), it.value());
        }
    }

    void applyContextProperties()
    {
        if (m_config.contextProperties.isEmpty()) {
            return;
        }
        // Per-surface properties must NOT leak onto a shared engine's root
        // context. Install them on a child context parented to m_q so
        // destruction is automatic and sibling surfaces see an unpolluted
        // root. The child context is used by instantiateFromComponent() /
        // ensureContent() — see childContextOrRoot().
        if (m_config.sharedEngine) {
            if (!m_childContext) {
                m_childContext = std::make_unique<QQmlContext>(m_engine->rootContext(), m_q);
            }
            for (auto it = m_config.contextProperties.begin(); it != m_config.contextProperties.end(); ++it) {
                m_childContext->setContextProperty(it.key(), it.value());
            }
            return;
        }
        // Owned or provider engine: root context is effectively per-surface
        // (or, for provider-shared, the consumer opted in at IQmlEngineProvider
        // level and is responsible for isolation). Writing to root is fine.
        auto* ctx = m_engine->rootContext();
        for (auto it = m_config.contextProperties.begin(); it != m_config.contextProperties.end(); ++it) {
            ctx->setContextProperty(it.key(), it.value());
        }
    }

    QQmlContext* childContextOrRoot()
    {
        return m_childContext ? m_childContext.get() : m_engine->rootContext();
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
                // QPointer capture: if the Surface is destroyed while the
                // component is still loading (rare but possible for qrc paths
                // under bundled resources), the lambda body must not
                // dereference a dead `this`. The receiver-based auto-
                // disconnect protects against the connection firing after
                // ~QObject, but the lambda can still race with ~Impl in the
                // narrow window where consumer code synchronously deletes
                // the Surface from inside a statusChanged slot elsewhere.
                QPointer<Surface> self = m_q;
                QObject::connect(m_component.get(), &QQmlComponent::statusChanged, m_q, [this, self] {
                    if (!self) {
                        return;
                    }
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
        // Latch self — failWith() defers its signals via QueuedConnection, but
        // instantiateFromComponent() emits stateChanged(Hidden) synchronously
        // via transitionTo(). A consumer slot on stateChanged that deletes
        // the Surface would leave us touching `this` on return. Bail if gone.
        QPointer<Surface> self = m_q;
        if (m_component->isError()) {
            failWith(m_component->errorString());
            return;
        }
        if (!instantiateFromComponent()) {
            return;
        }
        if (!self) {
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
        auto* ctx = childContextOrRoot();
        QObject* root = m_component->beginCreate(ctx);
        if (!root) {
            failWith(m_component->errorString().isEmpty()
                         ? QStringLiteral("QQmlComponent::beginCreate() returned nullptr")
                         : m_component->errorString());
            return false;
        }

        // Claim ownership BEFORE any binding runs. setInitialProperties
        // evaluates QML bindings that may trigger JS GC; without CppOwnership
        // in place the root is JS-owned and could be collected under us
        // between `beginCreate` and our later `m_window = win` / setParent.
        QQmlEngine::setObjectOwnership(root, QQmlEngine::CppOwnership);

        if (!m_config.windowProperties.isEmpty()) {
            m_component->setInitialProperties(root, m_config.windowProperties);
        }

        if (auto* win = qobject_cast<QQuickWindow*>(root)) {
            m_window = win;

            // Dynamic-property writes (setProperty) for anything
            // windowProperties contains that is NOT declared via QML
            // `property`. setInitialProperties above handles QML-declared
            // props; this covers arbitrary QObject dynamic properties so
            // both QML-rooted and Item-rooted paths honour the same
            // SurfaceConfig contract.
            applyWindowProperties(win);

            // Force the Window into Hidden visibility BEFORE completeCreate.
            // Default QQuickWindow visibility is AutomaticVisibility, and
            // componentComplete's AutomaticVisibility branch calls
            // setWindowVisibility(Windowed) → QPA createShellSurface with
            // whatever role Qt decides (xdg_toplevel for AutomaticVisibility
            // before our layer-shell attach lands). Setting Hidden up-front
            // suppresses that branch so the first shell surface created is
            // always the layer_surface we attach below. setVisible(false) is
            // insufficient: it flips m_visible but leaves m_visibility at
            // AutomaticVisibility, so componentComplete still auto-shows.
            win->setVisibility(QWindow::Hidden);

            // Sized first — see #2 above.
            if (m_config.screen) {
                const QRect geo = m_config.screen->geometry();
                if (!geo.isEmpty()) {
                    win->setGeometry(geo);
                }
            }

            // Attached second — see #1 above.
            if (!finishAttach()) {
                // completeCreate even on failure so QML teardown is clean.
                // Because visibility is already Hidden, componentComplete
                // does not auto-show and there is no xdg_toplevel flash
                // between failure and destruction.
                m_component->completeCreate();
                return false;
            }

            // completeCreate triggers componentComplete, which (for
            // AutomaticVisibility — NOT our case, we forced Hidden above)
            // would call setWindowVisibility(Windowed). Since we're Hidden
            // the auto-show branch is skipped and the window stays mapped-
            // but-hidden until show() flips it.
            m_component->completeCreate();
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
        // Revalidate the screen right before handing it to the transport.
        // onScreensChanged() nulls m_config.screen when the attached screen is
        // removed, but we also guard here for belt-and-braces: a caller that
        // short-circuits the notifier (e.g. a mock) could still leave us with
        // a screen that isn't in the provider's current list. Fall back to
        // primary; the transport accepts nullptr (compositor picks) but we
        // prefer a deterministic target so failures surface at the boundary.
        auto* screens = m_deps.screenProvider;
        if (screens && m_config.screen) {
            const auto list = screens->screens();
            if (!list.contains(m_config.screen)) {
                qCWarning(lcPhosphorLayer) << "Surface" << m_config.effectiveDebugName()
                                           << "screen is no longer in provider list — falling back to primary";
                m_config.screen = screens->primary();
            }
        }
        if (!m_config.screen && screens) {
            m_config.screen = screens->primary();
        }

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
        if (m_state == State::Failed) {
            return;
        }
        m_state = State::Failed;
        // Defer both failure signals so consumer slots that `delete surface`
        // as a teardown pattern never re-enter library code on a dead `this`.
        // drive() / instantiateFromComponent() / ensureContent() continue
        // touching m_component / m_window / m_handle after calling failWith;
        // synchronous emit + synchronous delete would make every such access
        // UAF. QueuedConnection posts a DeferredInvoke to m_q's thread, which
        // runs after the current call stack unwinds — by which point we've
        // either returned to the consumer's event loop or been explicitly
        // torn down via ~Surface.
        QMetaObject::invokeMethod(
            m_q,
            [q = m_q, reason]() {
                if (!q) {
                    return;
                }
                Q_EMIT q->stateChanged(State::Failed);
                Q_EMIT q->failed(reason);
            },
            Qt::QueuedConnection);
    }
};

// ══════════════════════════════════════════════════════════════════════
// Surface — thin façade over Impl
// ══════════════════════════════════════════════════════════════════════

Surface::Surface(CtorToken, SurfaceConfig cfg, SurfaceDeps deps, QObject* parent)
    : QObject(parent)
    , m_impl(std::make_unique<Impl>(this, std::move(cfg), std::move(deps)))
{
}

Surface::~Surface() = default;

Surface::State Surface::state() const noexcept
{
    return m_impl->m_state;
}

bool Surface::isLogicallyShown() const noexcept
{
    return m_impl->m_state == State::Shown;
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
