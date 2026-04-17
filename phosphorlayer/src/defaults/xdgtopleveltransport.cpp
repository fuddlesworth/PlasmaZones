// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorLayer/defaults/XdgToplevelTransport.h>

#include "../internal.h"

#include <QGuiApplication>
#include <QMutex>
#include <QMutexLocker>
#include <QPointer>
#include <QQuickWindow>
#include <QScreen>

namespace PhosphorLayer {

/**
 * Handle for a plain xdg_toplevel-backed surface. Every wlr-layer-shell
 * knob (layer, anchor, exclusive zone, margins, keyboard-interactivity)
 * is silently ignored here — xdg_toplevel does not model any of them and
 * Wayland compositors do not honour client-requested window positions.
 * The ignores are documented per call-site and emitted once at attach()
 * so consumers can audit what was dropped.
 */
class XdgToplevelTransportHandle : public ITransportHandle
{
public:
    explicit XdgToplevelTransportHandle(QQuickWindow* win)
        : m_window(win)
    {
    }

    QQuickWindow* window() const override
    {
        return m_window;
    }

    bool isConfigured() const override
    {
        // xdg_toplevel receives its first configure immediately after
        // commit; treat any visible window as configured.
        return m_window && m_window->isVisible();
    }

    QSize configuredSize() const override
    {
        return m_window ? m_window->size() : QSize();
    }

    void setMargins(QMargins) override
    {
        // xdg_toplevel has no client-positioning hooks on Wayland. Not
        // forwarding to QWindow::setPosition — that is also a no-op on
        // every major Wayland compositor (Sway, Hyprland, Mutter, KWin)
        // and would give callers a false sense of success. Consumers that
        // genuinely need placement must use the layer-shell transport.
    }
    void setLayer(Layer) override
    {
        // No xdg_toplevel equivalent — silently accepted.
    }
    void setExclusiveZone(int) override
    {
        // No xdg_toplevel equivalent — silently accepted.
    }
    void setKeyboardInteractivity(KeyboardInteractivity) override
    {
        // xdg_toplevel is always OnDemand — mutator is a no-op.
    }
    void setAnchors(Anchors) override
    {
        // xdg_toplevel has no concept of screen-edge anchoring — compositor
        // decides position. Silently accepted.
    }

private:
    QPointer<QQuickWindow> m_window;
};

// ── Transport ──────────────────────────────────────────────────────────

class XdgToplevelTransport::Impl
{
public:
    QMutex m_cbMutex;
    QList<CompositorLostCallback> m_callbacks;
};

XdgToplevelTransport::XdgToplevelTransport()
    : m_impl(std::make_unique<Impl>())
{
}

XdgToplevelTransport::~XdgToplevelTransport() = default;

bool XdgToplevelTransport::isSupported() const
{
    // xdg_toplevel is the baseline shell on every Wayland compositor and
    // is trivially available via Xwayland. Requiring a GUI application is
    // the only honest precondition — platform-specific attachment happens
    // inside attach(), which gracefully warns and returns nullptr if the
    // QPA plugin lacks a shell integration (e.g. offscreen/minimal). That
    // keeps the "supported" check cheap and lets consumers fall through
    // to attach() for the real diagnostic.
    return qGuiApp != nullptr;
}

std::unique_ptr<ITransportHandle> XdgToplevelTransport::attach(QQuickWindow* win, const TransportAttachArgs& args)
{
    if (!win) {
        qCWarning(lcPhosphorLayer) << "XdgToplevelTransport::attach: window is nullptr";
        return nullptr;
    }

    // Hook up the target screen so Qt picks the right wl_output at show time.
    if (args.screen) {
        win->setScreen(args.screen);
    }
    // The scope is a machine identifier, not a user-facing label — don't
    // forward it as QWindow::setTitle() (which surfaces in Alt-Tab and
    // task bars). If a consumer wants a title they can set it on the
    // window directly before show().

    // Log every parameter we dropped so consumers can audit the fallback.
    if (args.layer != Layer::Top && args.layer != Layer::Overlay) {
        qCDebug(lcPhosphorLayer) << "XdgToplevelTransport: layer" << static_cast<int>(args.layer)
                                 << "ignored (no xdg_toplevel equivalent)";
    }
    if (args.exclusiveZone > 0) {
        qCDebug(lcPhosphorLayer) << "XdgToplevelTransport: exclusive zone" << args.exclusiveZone
                                 << "ignored (no xdg_toplevel equivalent)";
    }
    if (args.keyboard == KeyboardInteractivity::Exclusive) {
        qCDebug(lcPhosphorLayer)
            << "XdgToplevelTransport: exclusive keyboard ignored (xdg_toplevel is always on-demand)";
    }
    if (!args.margins.isNull() || args.anchors != AnchorNone) {
        qCDebug(lcPhosphorLayer)
            << "XdgToplevelTransport: anchors/margins ignored (compositor controls xdg_toplevel placement)";
    }

    return std::make_unique<XdgToplevelTransportHandle>(win);
}

void XdgToplevelTransport::addCompositorLostCallback(CompositorLostCallback cb)
{
    // xdg_toplevel doesn't expose a global-removal hook the way wlr-layer-
    // shell does. The callback will fire only on explicit shutdown — we
    // store it for symmetry with PhosphorShellTransport but cannot
    // proactively invoke it on compositor crash.
    if (!cb) {
        return;
    }
    QMutexLocker lock(&m_impl->m_cbMutex);
    m_impl->m_callbacks.append(std::move(cb));
}

} // namespace PhosphorLayer
