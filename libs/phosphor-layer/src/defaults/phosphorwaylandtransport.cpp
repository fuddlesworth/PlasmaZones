// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorLayer/defaults/PhosphorWaylandTransport.h>

#include "../internal.h"

#include <PhosphorWayland/LayerSurface.h>

#include <QQuickWindow>

namespace PhosphorLayer {

namespace {

// Convert between PhosphorLayer's enums and PhosphorWayland's. Both mirror
// the zwlr_layer_shell_v1 protocol values, so these are compile-time casts;
// keeping them in functions makes the intent explicit and gives us a place
// to hang static_assert guards if the mappings ever diverge.

// Backstop against either side renumbering the shared wlr-layer-shell enum.
// Both enums mirror the zwlr_layer_shell_v1 protocol values (0-based layer
// sequence, Wayland keyboard-interactivity values); if the mapping ever
// diverges these asserts break the build instead of silently translating
// wrong values at runtime.
static_assert(static_cast<int>(Layer::Background) == PhosphorWayland::LayerSurface::LayerBackground);
static_assert(static_cast<int>(Layer::Bottom) == PhosphorWayland::LayerSurface::LayerBottom);
static_assert(static_cast<int>(Layer::Top) == PhosphorWayland::LayerSurface::LayerTop);
static_assert(static_cast<int>(Layer::Overlay) == PhosphorWayland::LayerSurface::LayerOverlay);
static_assert(static_cast<int>(KeyboardInteractivity::None)
              == PhosphorWayland::LayerSurface::KeyboardInteractivityNone);
static_assert(static_cast<int>(KeyboardInteractivity::Exclusive)
              == PhosphorWayland::LayerSurface::KeyboardInteractivityExclusive);
static_assert(static_cast<int>(KeyboardInteractivity::OnDemand)
              == PhosphorWayland::LayerSurface::KeyboardInteractivityOnDemand);

constexpr PhosphorWayland::LayerSurface::Layer toShellLayer(Layer l)
{
    return static_cast<PhosphorWayland::LayerSurface::Layer>(static_cast<int>(l));
}
constexpr PhosphorWayland::LayerSurface::KeyboardInteractivity toShellKbd(KeyboardInteractivity k)
{
    return static_cast<PhosphorWayland::LayerSurface::KeyboardInteractivity>(static_cast<int>(k));
}
PhosphorWayland::LayerSurface::Anchors toShellAnchors(Anchors a)
{
    PhosphorWayland::LayerSurface::Anchors out;
    if (a.testFlag(Anchor::Top)) {
        out |= PhosphorWayland::LayerSurface::AnchorTop;
    }
    if (a.testFlag(Anchor::Bottom)) {
        out |= PhosphorWayland::LayerSurface::AnchorBottom;
    }
    if (a.testFlag(Anchor::Left)) {
        out |= PhosphorWayland::LayerSurface::AnchorLeft;
    }
    if (a.testFlag(Anchor::Right)) {
        out |= PhosphorWayland::LayerSurface::AnchorRight;
    }
    return out;
}

} // namespace

// ── Handle ─────────────────────────────────────────────────────────────

class PhosphorWaylandTransportHandle : public ITransportHandle
{
public:
    PhosphorWaylandTransportHandle(QQuickWindow* win, PhosphorWayland::LayerSurface* surface)
        : m_window(win)
        , m_surface(surface)
    {
    }
    ~PhosphorWaylandTransportHandle() override
    {
        // PhosphorWayland::LayerSurface is parented to the QWindow it was
        // created on; Qt will clean it up when the window dies. Don't
        // delete it manually here or we race against Qt's destruction.
        // Clear the QPointers defensively so any stray setter call during
        // unwind hits a null check instead of a dangling pointer if a
        // future PhosphorWayland refactor breaks the parenting invariant.
        m_surface.clear();
        m_window.clear();
    }

    QQuickWindow* window() const override
    {
        return m_window;
    }

    bool isConfigured() const override
    {
        // PhosphorWayland doesn't expose an explicit "configured" flag. We
        // use window-exposure as a proxy for "compositor has sent at least
        // one configure event", then latch — once configured, always
        // configured across hide()/show() cycles. Without the latch, a
        // consumer that gates setAnchors() on isConfigured() would get a
        // false negative after the first hide and incorrectly think the
        // layer_surface was never ack'd.
        if (!m_window) {
            return false;
        }
        if (m_window->isVisible()) {
            m_everConfigured = true;
        }
        return m_everConfigured;
    }

    QSize configuredSize() const override
    {
        return m_window ? m_window->size() : QSize();
    }

    void setMargins(QMargins m) override
    {
        if (m_surface) {
            m_surface->setMargins(m);
        }
    }
    void setLayer(Layer l) override
    {
        if (m_surface) {
            m_surface->setLayer(toShellLayer(l));
        }
    }
    void setExclusiveZone(int z) override
    {
        if (m_surface) {
            m_surface->setExclusiveZone(z);
        }
    }
    void setKeyboardInteractivity(KeyboardInteractivity k) override
    {
        if (m_surface) {
            m_surface->setKeyboardInteractivity(toShellKbd(k));
        }
    }
    void setAnchors(Anchors a) override
    {
        if (m_surface) {
            m_surface->setAnchors(toShellAnchors(a));
        }
    }

private:
    QPointer<QQuickWindow> m_window;
    QPointer<PhosphorWayland::LayerSurface> m_surface;
    mutable bool m_everConfigured = false; ///< Latched true on first isVisible()
};

// ── Transport ──────────────────────────────────────────────────────────

class PhosphorWaylandTransport::Impl
{
public:
    CompositorLostBroadcaster m_broadcaster;
    QMetaObject::Connection m_aboutToQuitConnection;
};

PhosphorWaylandTransport::PhosphorWaylandTransport()
    : m_impl(std::make_unique<Impl>())
{
    // PhosphorWayland's QPA plugin owns the wlr-layer-shell global-removal
    // signal but does not expose it through a public API. The best we can
    // do from user-space is listen for application shutdown — when Qt
    // enters aboutToQuit the wl_display is about to disconnect, so every
    // active layer surface is effectively lost. Consumers that need
    // earlier detection (mid-session compositor crash) should subscribe
    // via PhosphorWayland::LayerShellIntegration once it gains a public
    // accessor; this default covers the "clean compositor exit" case.
    m_impl->m_aboutToQuitConnection = m_impl->m_broadcaster.hookAboutToQuit();
}

PhosphorWaylandTransport::~PhosphorWaylandTransport()
{
    QObject::disconnect(m_impl->m_aboutToQuitConnection);
}

bool PhosphorWaylandTransport::isSupported() const
{
    return PhosphorWayland::LayerSurface::isSupported();
}

std::unique_ptr<ITransportHandle> PhosphorWaylandTransport::attach(QQuickWindow* win, const TransportAttachArgs& args)
{
    if (!win) {
        qCWarning(lcPhosphorLayer) << "PhosphorWaylandTransport::attach: window is nullptr";
        return nullptr;
    }
    if (!isSupported()) {
        qCWarning(lcPhosphorLayer) << "PhosphorWaylandTransport::attach: compositor lacks wlr-layer-shell";
        return nullptr;
    }
    // Pre-show-immutable properties (scope, layer, anchors, kbd interactivity)
    // can only be set before the wl_surface commits as a layer_surface. If
    // the consumer already show()'d, PhosphorWayland::LayerSurface::get logs
    // a qCCritical but returns a surface whose setters silently no-op —
    // that yields a handle that reports "configured" but never honoured
    // its initial config. Refuse loudly so the caller fixes their ordering.
    if (win->isVisible()) {
        qCWarning(lcPhosphorLayer)
            << "PhosphorWaylandTransport::attach: window is already visible — attach must run before show()"
            << "(pre-show-immutable properties would be silently discarded)";
        return nullptr;
    }
    auto* surface = PhosphorWayland::LayerSurface::get(win);
    if (!surface) {
        qCWarning(lcPhosphorLayer) << "PhosphorWaylandTransport::attach: LayerSurface::get returned nullptr";
        return nullptr;
    }

    // Pre-show-immutable properties MUST be set before the caller calls
    // window->show(). We batch so PhosphorWayland fires one propertiesChanged
    // signal at the end rather than six.
    PhosphorWayland::LayerSurface::BatchGuard batch(surface);
    surface->setScope(args.scope);
    if (args.screen) {
        surface->setScreen(args.screen);
    }
    surface->setLayer(toShellLayer(args.layer));
    surface->setAnchors(toShellAnchors(args.anchors));
    surface->setKeyboardInteractivity(toShellKbd(args.keyboard));
    surface->setExclusiveZone(args.exclusiveZone);
    surface->setMargins(args.margins);

    return std::make_unique<PhosphorWaylandTransportHandle>(win, surface);
}

PhosphorWaylandTransport::CompositorLostCookie
PhosphorWaylandTransport::addCompositorLostCallback(CompositorLostCallback cb)
{
    return m_impl->m_broadcaster.addCallback(std::move(cb));
}

void PhosphorWaylandTransport::removeCompositorLostCallback(CompositorLostCookie cookie)
{
    m_impl->m_broadcaster.removeCallback(cookie);
}

} // namespace PhosphorLayer
