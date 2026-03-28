// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "layersurface.h"
#include "logging.h"

#include <QHash>

Q_LOGGING_CATEGORY(lcLayerSurface, "plasmazones.layersurface")

namespace PlasmaZones {

// Global registry: QWindow* → LayerSurface*
// LayerSurface is parented to the QWindow, so it is destroyed when the window is.
static QHash<QWindow*, LayerSurface*> s_surfaces;

LayerSurface::LayerSurface(QWindow* window)
    : QObject(window)
    , m_window(window)
{
    // Mark window so the QPA shell integration creates a layer surface
    // instead of an xdg_toplevel when the platform window is created.
    window->setProperty("_pz_layer_shell", true);
    window->setProperty("_pz_layer_shell_surface", QVariant::fromValue(this));

    connect(window, &QObject::destroyed, this, [window]() {
        s_surfaces.remove(window);
    });
}

LayerSurface::~LayerSurface()
{
    s_surfaces.remove(m_window);
}

LayerSurface* LayerSurface::get(QWindow* window)
{
    if (!window) {
        return nullptr;
    }

    auto it = s_surfaces.find(window);
    if (it != s_surfaces.end()) {
        return *it;
    }

    auto* surface = new LayerSurface(window);
    s_surfaces.insert(window, surface);
    return surface;
}

void LayerSurface::setLayer(Layer layer)
{
    if (m_layer == layer) {
        return;
    }
    m_layer = layer;
    m_window->setProperty("_pz_layer", static_cast<int>(layer));
    Q_EMIT layerChanged();
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
    m_window->setProperty("_pz_anchors", static_cast<int>(anchors));
    Q_EMIT anchorsChanged();
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
    m_window->setProperty("_pz_exclusive_zone", zone);
    Q_EMIT exclusionZoneChanged();
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
    m_window->setProperty("_pz_keyboard", static_cast<int>(interactivity));
    Q_EMIT keyboardInteractivityChanged();
}

LayerSurface::KeyboardInteractivity LayerSurface::keyboardInteractivity() const
{
    return m_keyboard;
}

void LayerSurface::setScope(const QString& scope)
{
    m_scope = scope;
    m_window->setProperty("_pz_scope", scope);
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
    m_window->setProperty("_pz_margins_left", margins.left());
    m_window->setProperty("_pz_margins_top", margins.top());
    m_window->setProperty("_pz_margins_right", margins.right());
    m_window->setProperty("_pz_margins_bottom", margins.bottom());
    Q_EMIT marginsChanged();
}

QMargins LayerSurface::margins() const
{
    return m_margins;
}

} // namespace PlasmaZones
