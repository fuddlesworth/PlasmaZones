// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorLayer/defaults/PhosphorShellTransport.h>

#include "../internal.h"

#include <PhosphorShell/LayerSurface.h>

#include <QMutex>
#include <QMutexLocker>
#include <QQuickWindow>

namespace PhosphorLayer {

namespace {

// Convert between PhosphorLayer's enums and PhosphorShell's. Both mirror
// the zwlr_layer_shell_v1 protocol values, so these are compile-time casts;
// keeping them in functions makes the intent explicit and gives us a place
// to hang static_assert guards if the mappings ever diverge.

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

private:
    QPointer<QQuickWindow> m_window;
    QPointer<PhosphorShell::LayerSurface> m_surface;
};

// ── Transport ──────────────────────────────────────────────────────────

class PhosphorShellTransport::Impl
{
public:
    QMutex m_cbMutex;
    QList<CompositorLostCallback> m_callbacks;
};

PhosphorShellTransport::PhosphorShellTransport()
    : m_impl(std::make_unique<Impl>())
{
}

PhosphorShellTransport::~PhosphorShellTransport() = default;

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
    if (!cb) {
        return;
    }
    QMutexLocker lock(&m_impl->m_cbMutex);
    m_impl->m_callbacks.append(std::move(cb));
    // PhosphorShell currently doesn't expose a generic "global removed"
    // hook on the transport level (it has global-removal callbacks on the
    // QPA integration, but those aren't part of the public LayerSurface
    // API). For now we store callbacks and document that they fire only
    // on explicit triggerCompositorLost() calls — a future PhosphorShell
    // revision will wire this up. Consumers relying on live compositor-
    // restart detection should subscribe via PhosphorShell::LayerShell-
    // Integration directly.
}

} // namespace PhosphorLayer
