// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "layersurface.h"
#include "qpa/layershellintegration.h"
#include "logging.h"

#include <QGuiApplication>
#include <QHash>
#include <QThread>

Q_LOGGING_CATEGORY(lcLayerSurface, "plasmazones.layersurface")

namespace PlasmaZones {

// Global registry: QWindow* → LayerSurface*
// LayerSurface is parented to the QWindow, so it is destroyed when the window is.
// Only accessed from the GUI thread (enforced by Q_ASSERT in get()).
using SurfaceRegistry = QHash<QWindow*, LayerSurface*>;
Q_GLOBAL_STATIC(SurfaceRegistry, s_surfaces)

LayerSurface::LayerSurface(QWindow* window)
    : QObject(window)
    , m_window(window)
    , m_scope(QStringLiteral("plasmazones"))
{
    // Mark window so the QPA shell integration creates a layer surface
    // instead of an xdg_toplevel when the platform window is created.
    window->setProperty(LayerSurfaceProps::IsLayerShell, true);
    window->setProperty(LayerSurfaceProps::Surface, QVariant::fromValue(this));

    // Propagate ALL default values to QWindow dynamic properties so the QPA plugin
    // reads correct initial state when the platform window is created (during show()).
    // Without this, setters that are called with the default value (e.g. setLayer(LayerTop)
    // when m_layer is already LayerTop) hit the change-guard early return and never set
    // the QWindow property, causing the QPA plugin to read 0 (LayerBackground) instead.
    window->setProperty(LayerSurfaceProps::Layer, static_cast<int>(m_layer));
    window->setProperty(LayerSurfaceProps::Anchors, static_cast<int>(m_anchors));
    window->setProperty(LayerSurfaceProps::ExclusiveZone, m_exclusiveZone);
    window->setProperty(LayerSurfaceProps::Keyboard, static_cast<int>(m_keyboard));
    window->setProperty(LayerSurfaceProps::Scope, m_scope);
    window->setProperty(LayerSurfaceProps::MarginsLeft, 0);
    window->setProperty(LayerSurfaceProps::MarginsTop, 0);
    window->setProperty(LayerSurfaceProps::MarginsRight, 0);
    window->setProperty(LayerSurfaceProps::MarginsBottom, 0);
}

LayerSurface::~LayerSurface()
{
    Q_ASSERT_X(!qApp || QThread::currentThread() == qApp->thread(), "LayerSurface::~LayerSurface",
               "must be destroyed from the GUI thread");
    s_surfaces->remove(m_window);
}

LayerSurface* LayerSurface::get(QWindow* window)
{
    Q_ASSERT_X(!qApp || QThread::currentThread() == qApp->thread(), "LayerSurface::get",
               "must be called from the GUI thread");

    if (!window) {
        return nullptr;
    }

    auto it = s_surfaces->find(window);
    if (it != s_surfaces->end()) {
        return *it;
    }

    if (window->isVisible()) {
        qCCritical(lcLayerSurface) << "LayerSurface::get() called after window is already visible."
                                   << "The platform window was created as xdg_toplevel, not a layer surface."
                                   << "Layer, scope, screen, and anchors will have NO effect."
                                   << "Caller must call LayerSurface::get() BEFORE QWindow::show().";
        return nullptr;
    }

    auto* surface = new LayerSurface(window);
    s_surfaces->insert(window, surface);
    return surface;
}

bool LayerSurface::isSupported()
{
    auto* integration = LayerShellIntegration::instance();
    return integration && integration->layerShell();
}

void LayerSurface::emitPropertiesChanged()
{
    if (m_batchDepth > 0) {
        m_batchDirty = true;
    } else {
        Q_EMIT propertiesChanged();
    }
}

void LayerSurface::setLayer(Layer layer)
{
    if (m_layer == layer) {
        return;
    }
    m_layer = layer;
    if (m_window) {
        m_window->setProperty(LayerSurfaceProps::Layer, static_cast<int>(layer));
    }
    Q_EMIT layerChanged();
    emitPropertiesChanged();
}

LayerSurface::Layer LayerSurface::layer() const
{
    return m_layer;
}

void LayerSurface::setAnchors(Anchors anchors)
{
    if (m_anchors == anchors) {
        return;
    }
    m_anchors = anchors;
    if (m_window) {
        m_window->setProperty(LayerSurfaceProps::Anchors, static_cast<int>(anchors));
    }
    Q_EMIT anchorsChanged();
    emitPropertiesChanged();
}

LayerSurface::Anchors LayerSurface::anchors() const
{
    return m_anchors;
}

void LayerSurface::setExclusiveZone(int32_t zone)
{
    if (m_exclusiveZone == zone) {
        return;
    }
    m_exclusiveZone = zone;
    if (m_window) {
        m_window->setProperty(LayerSurfaceProps::ExclusiveZone, zone);
    }
    Q_EMIT exclusiveZoneChanged();
    emitPropertiesChanged();
}

int32_t LayerSurface::exclusiveZone() const
{
    return m_exclusiveZone;
}

void LayerSurface::setKeyboardInteractivity(KeyboardInteractivity interactivity)
{
    if (m_keyboard == interactivity) {
        return;
    }
    m_keyboard = interactivity;
    if (m_window) {
        m_window->setProperty(LayerSurfaceProps::Keyboard, static_cast<int>(interactivity));
    }
    Q_EMIT keyboardInteractivityChanged();
    emitPropertiesChanged();
}

LayerSurface::KeyboardInteractivity LayerSurface::keyboardInteractivity() const
{
    return m_keyboard;
}

void LayerSurface::setScope(const QString& scope)
{
    if (m_scope == scope) {
        return;
    }
    if (m_window && m_window->isVisible()) {
        qCWarning(lcLayerSurface) << "setScope() called after show(); scope is immutable at the protocol level"
                                  << "— the layer surface retains the original scope"
                                  << "(scope is baked into zwlr_layer_shell_v1_get_layer_surface)";
        return;
    }
    m_scope = scope;
    if (m_window) {
        m_window->setProperty(LayerSurfaceProps::Scope, scope);
    }
    Q_EMIT scopeChanged();
    // No emitPropertiesChanged() — scope is only read at surface creation time by
    // the QPA plugin (baked into zwlr_layer_shell_v1_get_layer_surface), so pushing
    // a protocol update would be meaningless. Same rationale applies to setScreen().
}

QString LayerSurface::scope() const
{
    return m_scope;
}

void LayerSurface::setScreen(QScreen* screen)
{
    if (m_screen == screen) {
        return;
    }
    if (m_window && m_window->isVisible()) {
        qCWarning(lcLayerSurface) << "setScreen() called after show(); output binding is immutable"
                                  << "— the layer surface remains on the original screen";
        return;
    }
    m_screen = screen;
    if (m_window) {
        if (screen) {
            m_window->setScreen(screen);
        } else {
            QScreen* primary = QGuiApplication::primaryScreen();
            if (primary) {
                m_window->setScreen(primary);
            } else {
                qCWarning(lcLayerSurface) << "setScreen(nullptr): no primary screen available —"
                                          << "window retains its current screen binding";
            }
        }
    }
    Q_EMIT screenChanged();
    // No emitPropertiesChanged() — screen/output binding is immutable at the protocol
    // level (baked into zwlr_layer_shell_v1_get_layer_surface at surface creation time).
    // The QPA plugin reads the screen once in LayerShellWindow's constructor and does
    // not listen for screenChanged, so a protocol push would be meaningless.
}

QScreen* LayerSurface::screen() const
{
    return m_screen;
}

void LayerSurface::setMargins(const QMargins& margins)
{
    if (m_margins == margins) {
        return;
    }
    m_margins = margins;
    if (m_window) {
        m_window->setProperty(LayerSurfaceProps::MarginsLeft, margins.left());
        m_window->setProperty(LayerSurfaceProps::MarginsTop, margins.top());
        m_window->setProperty(LayerSurfaceProps::MarginsRight, margins.right());
        m_window->setProperty(LayerSurfaceProps::MarginsBottom, margins.bottom());
    }
    Q_EMIT marginsChanged();
    emitPropertiesChanged();
}

QMargins LayerSurface::margins() const
{
    return m_margins;
}

std::pair<uint32_t, uint32_t> LayerSurface::computeLayerSize(int anchors, const QSize& windowSize)
{
    // windowSize is in logical (device-independent) pixels from QWindow::size().
    // zwlr_layer_surface_v1_set_size expects logical pixels — Qt's Wayland backend
    // handles the logical→buffer scaling internally (wl_surface.set_buffer_scale).
    bool anchoredH = (anchors & AnchorLeft) && (anchors & AnchorRight);
    bool anchoredV = (anchors & AnchorTop) && (anchors & AnchorBottom);
    uint32_t w = anchoredH ? 0 : static_cast<uint32_t>(qMax(0, windowSize.width()));
    uint32_t h = anchoredV ? 0 : static_cast<uint32_t>(qMax(0, windowSize.height()));
    return {w, h};
}

} // namespace PlasmaZones
