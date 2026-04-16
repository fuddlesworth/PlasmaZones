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
 * Handle for a plain xdg_toplevel-backed surface. All protocol-level
 * mutators are best-effort — xdg_toplevel has no concept of layer,
 * exclusive zone, or anchor policy, so we store the intent (for
 * isConfigured-like queries) and apply geometry hints when we can.
 */
class XdgToplevelTransportHandle : public ITransportHandle
{
public:
    explicit XdgToplevelTransportHandle(QQuickWindow* win, QMargins initialMargins)
        : m_window(win)
        , m_margins(initialMargins)
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

    void setMargins(QMargins m) override
    {
        m_margins = m;
        applyGeometry();
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

    void applyGeometry()
    {
        // Best-effort positioning. A compositor that tiles aggressively
        // (Sway, Hyprland) will ignore client position hints anyway.
        if (!m_window || !m_window->screen()) {
            return;
        }
        const QRect screenRect = m_window->screen()->availableGeometry();
        const QMargins& m = m_margins;
        const int x = screenRect.x() + m.left();
        const int y = screenRect.y() + m.top();
        m_window->setPosition(x, y);
    }

private:
    QPointer<QQuickWindow> m_window;
    QMargins m_margins;
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
    // xdg_toplevel is mandatory on every Wayland compositor, and trivially
    // available on X11 via the Xwayland path. Always supported when we
    // have a GUI application.
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
    // xdg_toplevel windows have a title that the compositor may display in
    // Alt-Tab / taskbars. Repurpose the scope string so the user sees
    // something meaningful instead of "Unnamed window".
    if (!args.scope.isEmpty()) {
        win->setTitle(args.scope);
    }
    // Nothing else to commit before show() — the compositor handles the
    // rest. Log the ignored parameters so consumers can audit.
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

    return std::make_unique<XdgToplevelTransportHandle>(win, args.margins);
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
