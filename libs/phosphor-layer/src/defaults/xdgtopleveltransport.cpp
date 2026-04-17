// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorLayer/defaults/XdgToplevelTransport.h>

#include "../internal.h"

#include <QGuiApplication>
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
        // commit; latch on first-visible so hide()/show() cycles don't
        // flip the handle back to "unconfigured" — once the compositor has
        // ack'd the role, it stays ack'd for the lifetime of the surface.
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
    void setKeyboardInteractivity(KeyboardInteractivity k) override
    {
        // xdg_toplevel is always OnDemand — every setter value is a no-op on
        // the protocol. Exclusive is security-relevant (lock screen, PIN
        // entry, modal that must capture every keystroke), so mirror the
        // attach-time warning on the handle path: a consumer that swapped
        // transports or upgraded to a wlr-layer-shell compositor at runtime
        // must not silently lose focus capture. Latched so a consumer that
        // re-asserts Exclusive on every mouse move (snap assist does this)
        // does not spam the log — one warning per handle lifetime is enough
        // to flag the downgrade.
        if (k == KeyboardInteractivity::Exclusive && !m_warnedKeyboardExclusive) {
            m_warnedKeyboardExclusive = true;
            qCWarning(lcPhosphorLayer)
                << "XdgToplevelTransport::setKeyboardInteractivity: exclusive keyboard REQUESTED but xdg_toplevel"
                << "is always OnDemand — focus may leak to other windows.";
        }
    }
    void setAnchors(Anchors) override
    {
        // xdg_toplevel has no concept of screen-edge anchoring — compositor
        // decides position. Silently accepted.
    }

private:
    QPointer<QQuickWindow> m_window;
    mutable bool m_everConfigured = false; ///< Latched true on first isVisible()
    bool m_warnedKeyboardExclusive = false; ///< Latched true after first Exclusive-keyboard warning
};

// ── Transport ──────────────────────────────────────────────────────────

class XdgToplevelTransport::Impl
{
public:
    CompositorLostBroadcaster m_broadcaster;
    QMetaObject::Connection m_aboutToQuitConnection;
};

XdgToplevelTransport::XdgToplevelTransport()
    : m_impl(std::make_unique<Impl>())
{
    // xdg_toplevel protocol lacks a dedicated global-removal signal, but the
    // Wayland connection is torn down on application shutdown the same way
    // wlr-layer-shell is. Firing on aboutToQuit covers the "clean exit" case
    // and matches PhosphorShellTransport's behaviour so consumers can treat
    // the two transports symmetrically.
    m_impl->m_aboutToQuitConnection = m_impl->m_broadcaster.hookAboutToQuit();
}

XdgToplevelTransport::~XdgToplevelTransport()
{
    QObject::disconnect(m_impl->m_aboutToQuitConnection);
}

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
    // If the platform window already exists (consumer prepared it before
    // attach) Qt has already committed the xdg_surface on a wl_output, and
    // calling setScreen() from user code triggers a surface-recreate on Qt
    // Wayland mid-attach — the new wl_output association arrives as a
    // separate configure and races against the caller's subsequent show().
    // Drop the hint rather than flail: the existing wl_output binding wins,
    // and the warning tells the consumer to reorder their setup so attach
    // runs before QWindow::handle() materialises.
    if (args.screen) {
        if (win->handle()) {
            qCWarning(lcPhosphorLayer)
                << "XdgToplevelTransport: attach() after QWindow::handle() is created — screen hint dropped"
                << "(Qt has already committed the xdg_surface; calling setScreen now would recreate it mid-attach)";
        } else {
            win->setScreen(args.screen);
        }
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
        // Exclusive keyboard is security-relevant (lock screen, PIN entry,
        // any modal that must capture every keystroke). Silent downgrade to
        // OnDemand would let focus leak to a background window; emit a
        // qCWarning so downgrades are visible in production logs.
        qCWarning(lcPhosphorLayer)
            << "XdgToplevelTransport: exclusive keyboard REQUESTED but xdg_toplevel is always OnDemand —"
            << "focus may leak to other windows. Use PhosphorShellTransport on compositors with wlr-layer-shell.";
    }
    if (!args.margins.isNull() || args.anchors != AnchorNone) {
        qCDebug(lcPhosphorLayer)
            << "XdgToplevelTransport: anchors/margins ignored (compositor controls xdg_toplevel placement)";
    }

    return std::make_unique<XdgToplevelTransportHandle>(win);
}

XdgToplevelTransport::CompositorLostCookie XdgToplevelTransport::addCompositorLostCallback(CompositorLostCallback cb)
{
    // xdg_toplevel doesn't expose a global-removal hook the way wlr-layer-
    // shell does. The callback fires on QGuiApplication::aboutToQuit (clean
    // exit); mid-session compositor crash is not detectable here.
    return m_impl->m_broadcaster.addCallback(std::move(cb));
}

void XdgToplevelTransport::removeCompositorLostCallback(CompositorLostCookie cookie)
{
    m_impl->m_broadcaster.removeCallback(cookie);
}

void XdgToplevelTransport::simulateCompositorLost()
{
    m_impl->m_broadcaster.fire();
}

} // namespace PhosphorLayer
