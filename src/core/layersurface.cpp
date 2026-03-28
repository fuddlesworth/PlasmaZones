// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "layersurface.h"
#include "qpa/layershellintegration.h"
#include "logging.h"

#include <QHash>
#include <QThread>
#include <QCoreApplication>

Q_LOGGING_CATEGORY(lcLayerSurface, "plasmazones.layersurface")

namespace PlasmaZones {

// Global registry: QWindow* → LayerSurface*
// LayerSurface is parented to the QWindow, so it is destroyed when the window is.
// Only accessed from the GUI thread (enforced by Q_ASSERT in get()).
static QHash<QWindow*, LayerSurface*> s_surfaces;

LayerSurface::LayerSurface(QWindow* window)
    : QObject(window)
    , m_window(window)
{
    // Mark window so the QPA shell integration creates a layer surface
    // instead of an xdg_toplevel when the platform window is created.
    window->setProperty(LayerSurfaceProps::IsLayerShell, true);
    window->setProperty(LayerSurfaceProps::Surface, QVariant::fromValue(this));
    // No QObject::destroyed connection needed — LayerSurface is parented to
    // the window, so ~LayerSurface runs (and removes from s_surfaces) before
    // the parent window finishes destruction.
}

LayerSurface::~LayerSurface()
{
    Q_ASSERT_X(!qApp || QThread::currentThread() == qApp->thread(), "LayerSurface::~LayerSurface",
               "must be destroyed from the GUI thread");
    s_surfaces.remove(m_window);
}

LayerSurface* LayerSurface::get(QWindow* window)
{
    Q_ASSERT_X(!qApp || QThread::currentThread() == qApp->thread(), "LayerSurface::get",
               "must be called from the GUI thread");

    if (!window) {
        return nullptr;
    }

    auto it = s_surfaces.find(window);
    if (it != s_surfaces.end()) {
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
    s_surfaces.insert(window, surface);
    return surface;
}

bool LayerSurface::isSupported()
{
    auto* integration = LayerShellIntegration::instance();
    return integration && integration->layerShell();
}

void LayerSurface::setLayer(Layer layer)
{
    if (m_layer == layer) {
        return;
    }
    m_layer = layer;
    m_window->setProperty(LayerSurfaceProps::Layer, static_cast<int>(layer));
    Q_EMIT layerChanged();
    Q_EMIT propertiesChanged();
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
    m_window->setProperty(LayerSurfaceProps::Anchors, static_cast<int>(anchors));
    Q_EMIT anchorsChanged();
    Q_EMIT propertiesChanged();
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
    m_window->setProperty(LayerSurfaceProps::ExclusiveZone, zone);
    Q_EMIT exclusiveZoneChanged();
    Q_EMIT propertiesChanged();
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
    m_window->setProperty(LayerSurfaceProps::Keyboard, static_cast<int>(interactivity));
    Q_EMIT keyboardInteractivityChanged();
    Q_EMIT propertiesChanged();
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
    m_scope = scope;
    m_window->setProperty(LayerSurfaceProps::Scope, scope);
    Q_EMIT scopeChanged();
    Q_EMIT propertiesChanged();
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
    if (m_window->isVisible()) {
        qCWarning(lcLayerSurface) << "setScreen() called after show(); output binding is immutable"
                                  << "— the layer surface remains on the original screen";
        return;
    }
    m_screen = screen;
    if (screen) {
        m_window->setScreen(screen);
    }
    Q_EMIT screenChanged();
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
    m_window->setProperty(LayerSurfaceProps::MarginsLeft, margins.left());
    m_window->setProperty(LayerSurfaceProps::MarginsTop, margins.top());
    m_window->setProperty(LayerSurfaceProps::MarginsRight, margins.right());
    m_window->setProperty(LayerSurfaceProps::MarginsBottom, margins.bottom());
    Q_EMIT marginsChanged();
    Q_EMIT propertiesChanged();
}

QMargins LayerSurface::margins() const
{
    return m_margins;
}

} // namespace PlasmaZones
