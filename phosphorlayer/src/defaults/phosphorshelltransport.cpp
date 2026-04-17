// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorLayer/defaults/PhosphorShellTransport.h>

#include "../internal.h"

#include <PhosphorShell/LayerSurface.h>

#include <QQuickWindow>

namespace PhosphorLayer {

namespace {

// Convert between PhosphorLayer's enums and PhosphorShell's. Both mirror
// the zwlr_layer_shell_v1 protocol values, so these are compile-time casts;
// keeping them in functions makes the intent explicit and gives us a place
// to hang static_assert guards if the mappings ever diverge.

// Backstop against either side renumbering the shared wlr-layer-shell enum.
// Both enums mirror the zwlr_layer_shell_v1 protocol values (0-based layer
// sequence, Wayland keyboard-interactivity values); if the mapping ever
// diverges these asserts break the build instead of silently translating
// wrong values at runtime.
static_assert(static_cast<int>(Layer::Background) == PhosphorShell::LayerSurface::LayerBackground);
static_assert(static_cast<int>(Layer::Bottom) == PhosphorShell::LayerSurface::LayerBottom);
static_assert(static_cast<int>(Layer::Top) == PhosphorShell::LayerSurface::LayerTop);
static_assert(static_cast<int>(Layer::Overlay) == PhosphorShell::LayerSurface::LayerOverlay);
static_assert(static_cast<int>(KeyboardInteractivity::None) == PhosphorShell::LayerSurface::KeyboardInteractivityNone);
static_assert(static_cast<int>(KeyboardInteractivity::Exclusive)
              == PhosphorShell::LayerSurface::KeyboardInteractivityExclusive);
static_assert(static_cast<int>(KeyboardInteractivity::OnDemand)
              == PhosphorShell::LayerSurface::KeyboardInteractivityOnDemand);

constexpr PhosphorShell::LayerSurface::Layer toShellLayer(Layer l)
{
    return static_cast<PhosphorShell::LayerSurface::Layer>(static_cast<int>(l));
}
constexpr PhosphorShell::LayerSurface::KeyboardInteractivity toShellKbd(KeyboardInteractivity k)
{
    return static_cast<PhosphorShell::LayerSurface::KeyboardInteractivity>(static_cast<int>(k));
}
PhosphorShell::LayerSurface::Anchors toShellAnchors(Anchors a)
{
    PhosphorShell::LayerSurface::Anchors out;
    if (a.testFlag(Anchor::Top)) {
        out |= PhosphorShell::LayerSurface::AnchorTop;
    }
    if (a.testFlag(Anchor::Bottom)) {
        out |= PhosphorShell::LayerSurface::AnchorBottom;
    }
    if (a.testFlag(Anchor::Left)) {
        out |= PhosphorShell::LayerSurface::AnchorLeft;
    }
    if (a.testFlag(Anchor::Right)) {
        out |= PhosphorShell::LayerSurface::AnchorRight;
    }
    return out;
}

} // namespace

// ── Handle ─────────────────────────────────────────────────────────────

class PhosphorShellTransportHandle : public ITransportHandle
{
public:
    PhosphorShellTransportHandle(QQuickWindow* win, PhosphorShell::LayerSurface* surface)
        : m_window(win)
        , m_surface(surface)
    {
    }
    ~PhosphorShellTransportHandle() override
    {
        // PhosphorShell::LayerSurface is parented to the QWindow it was
        // created on; Qt will clean it up when the window dies. Don't
        // delete it manually here or we race against Qt's destruction.
    }

    QQuickWindow* window() const override
    {
        return m_window;
    }

    bool isConfigured() const override
    {
        // PhosphorShell doesn't expose an explicit "configured" flag, but
        // window exposure is a strong proxy — exposure happens only after
        // the compositor's initial configure event.
        return m_window && m_window->isVisible();
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
    QPointer<PhosphorShell::LayerSurface> m_surface;
};

// ── Transport ──────────────────────────────────────────────────────────

class PhosphorShellTransport::Impl
{
public:
    CompositorLostBroadcaster m_broadcaster;
    QMetaObject::Connection m_aboutToQuitConnection;
};

PhosphorShellTransport::PhosphorShellTransport()
    : m_impl(std::make_unique<Impl>())
{
    // PhosphorShell's QPA plugin owns the wlr-layer-shell global-removal
    // signal but does not expose it through a public API. The best we can
    // do from user-space is listen for application shutdown — when Qt
    // enters aboutToQuit the wl_display is about to disconnect, so every
    // active layer surface is effectively lost. Consumers that need
    // earlier detection (mid-session compositor crash) should subscribe
    // via PhosphorShell::LayerShellIntegration once it gains a public
    // accessor; this default covers the "clean compositor exit" case.
    m_impl->m_aboutToQuitConnection = m_impl->m_broadcaster.hookAboutToQuit();
}

PhosphorShellTransport::~PhosphorShellTransport()
{
    QObject::disconnect(m_impl->m_aboutToQuitConnection);
}

bool PhosphorShellTransport::isSupported() const
{
    return PhosphorShell::LayerSurface::isSupported();
}

std::unique_ptr<ITransportHandle> PhosphorShellTransport::attach(QQuickWindow* win, const TransportAttachArgs& args)
{
    if (!win) {
        qCWarning(lcPhosphorLayer) << "PhosphorShellTransport::attach: window is nullptr";
        return nullptr;
    }
    if (!isSupported()) {
        qCWarning(lcPhosphorLayer) << "PhosphorShellTransport::attach: compositor lacks wlr-layer-shell";
        return nullptr;
    }
    auto* surface = PhosphorShell::LayerSurface::get(win);
    if (!surface) {
        qCWarning(lcPhosphorLayer) << "PhosphorShellTransport::attach: LayerSurface::get returned nullptr";
        return nullptr;
    }

    // Pre-show-immutable properties MUST be set before the caller calls
    // window->show(). We batch so PhosphorShell fires one propertiesChanged
    // signal at the end rather than six.
    PhosphorShell::LayerSurface::BatchGuard batch(surface);
    surface->setScope(args.scope);
    if (args.screen) {
        surface->setScreen(args.screen);
    }
    surface->setLayer(toShellLayer(args.layer));
    surface->setAnchors(toShellAnchors(args.anchors));
    surface->setKeyboardInteractivity(toShellKbd(args.keyboard));
    surface->setExclusiveZone(args.exclusiveZone);
    surface->setMargins(args.margins);

    return std::make_unique<PhosphorShellTransportHandle>(win, surface);
}

void PhosphorShellTransport::addCompositorLostCallback(CompositorLostCallback cb)
{
    m_impl->m_broadcaster.addCallback(std::move(cb));
}

} // namespace PhosphorLayer
