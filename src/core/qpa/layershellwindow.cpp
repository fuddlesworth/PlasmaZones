// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "layershellwindow.h"
#include "layershellintegration.h"
#include "../layersurface.h"

#include <QLoggingCategory>
#include <QtWaylandClient/private/qwaylanddisplay_p.h>
#include <QtWaylandClient/private/qwaylandscreen_p.h>
#include <QtWaylandClient/private/qwaylandwindow_p.h>

Q_LOGGING_CATEGORY(lcLayerShellWindow, "plasmazones.qpa.layershellwindow")

namespace PlasmaZones {

static const struct zwlr_layer_surface_v1_listener s_layerSurfaceListener = {
    .configure = LayerShellWindow::handleConfigure,
    .closed = LayerShellWindow::handleClosed,
};

LayerShellWindow::LayerShellWindow(LayerShellIntegration* integration, QtWaylandClient::QWaylandWindow* window)
    : QWaylandShellSurface(window)
    , m_integration(integration)
{
    QWindow* qwindow = window->window();

    // Resolve wl_output from QScreen
    QScreen* targetScreen = qwindow->screen();
    if (targetScreen) {
        auto* waylandScreen = dynamic_cast<QtWaylandClient::QWaylandScreen*>(targetScreen->handle());
        if (waylandScreen) {
            m_output = waylandScreen->output();
        }
    }

    // Read initial properties from QWindow dynamic properties
    int layer = qwindow->property("_pz_layer").toInt();
    QString scope = qwindow->property("_pz_scope").toString();

    // Create the layer surface
    struct wl_surface* wlSurface = QWaylandShellIntegration::wlSurfaceForWindow(window);
    m_layerSurface = zwlr_layer_shell_v1_get_layer_surface(integration->layerShell(), wlSurface, m_output,
                                                           static_cast<uint32_t>(layer), scope.toUtf8().constData());

    zwlr_layer_surface_v1_add_listener(m_layerSurface, &s_layerSurfaceListener, this);

    // Apply all properties
    applyProperties();

    // Initial commit to get a configure event
    wl_surface_commit(wlSurface);

    qCDebug(lcLayerShellWindow) << "Created layer surface:" << scope << "layer=" << layer
                                << "screen=" << (targetScreen ? targetScreen->name() : QStringLiteral("null"));
}

LayerShellWindow::~LayerShellWindow()
{
    if (m_layerSurface) {
        zwlr_layer_surface_v1_destroy(m_layerSurface);
    }
}

bool LayerShellWindow::isExposed() const
{
    return m_configured;
}

void LayerShellWindow::applyConfigure()
{
    // Re-read properties in case they changed since creation
    applyProperties();
}

void LayerShellWindow::setWindowGeometry(const QRect& rect)
{
    if (m_layerSurface) {
        // For layer surfaces, size of 0 means "compositor decides" for that axis.
        // When anchored to opposing edges, we want 0 (let compositor size us).
        // Otherwise use the explicit size.
        QWindow* qwindow = waylandWindow()->window();
        int anchors = qwindow->property("_pz_anchors").toInt();
        bool anchoredH = (anchors & LayerSurface::AnchorLeft) && (anchors & LayerSurface::AnchorRight);
        bool anchoredV = (anchors & LayerSurface::AnchorTop) && (anchors & LayerSurface::AnchorBottom);

        uint32_t w = anchoredH ? 0 : static_cast<uint32_t>(rect.width());
        uint32_t h = anchoredV ? 0 : static_cast<uint32_t>(rect.height());
        zwlr_layer_surface_v1_set_size(m_layerSurface, w, h);
    }
}

void LayerShellWindow::applyProperties()
{
    if (!m_layerSurface) {
        return;
    }

    QWindow* qwindow = waylandWindow()->window();

    // Anchors
    int anchors = qwindow->property("_pz_anchors").toInt();
    zwlr_layer_surface_v1_set_anchor(m_layerSurface, static_cast<uint32_t>(anchors));

    // Exclusive zone
    int exclusiveZone = qwindow->property("_pz_exclusive_zone").toInt();
    zwlr_layer_surface_v1_set_exclusive_zone(m_layerSurface, exclusiveZone);

    // Keyboard interactivity
    int keyboard = qwindow->property("_pz_keyboard").toInt();
    zwlr_layer_surface_v1_set_keyboard_interactivity(m_layerSurface, static_cast<uint32_t>(keyboard));

    // Margins
    int marginLeft = qwindow->property("_pz_margins_left").toInt();
    int marginTop = qwindow->property("_pz_margins_top").toInt();
    int marginRight = qwindow->property("_pz_margins_right").toInt();
    int marginBottom = qwindow->property("_pz_margins_bottom").toInt();
    zwlr_layer_surface_v1_set_margin(m_layerSurface, marginTop, marginRight, marginBottom, marginLeft);

    // Size — use 0 for axes anchored to both edges
    bool anchoredH = (anchors & LayerSurface::AnchorLeft) && (anchors & LayerSurface::AnchorRight);
    bool anchoredV = (anchors & LayerSurface::AnchorTop) && (anchors & LayerSurface::AnchorBottom);
    QSize size = qwindow->size();
    uint32_t w = anchoredH ? 0 : static_cast<uint32_t>(size.width());
    uint32_t h = anchoredV ? 0 : static_cast<uint32_t>(size.height());
    zwlr_layer_surface_v1_set_size(m_layerSurface, w, h);
}

void LayerShellWindow::handleConfigure(void* data, struct zwlr_layer_surface_v1* surface, uint32_t serial,
                                       uint32_t width, uint32_t height)
{
    auto* self = static_cast<LayerShellWindow*>(data);
    self->m_configured = true;
    self->m_pendingWidth = width;
    self->m_pendingHeight = height;

    zwlr_layer_surface_v1_ack_configure(surface, serial);

    // Resize the Qt window to the compositor-assigned size
    if (width > 0 && height > 0) {
        QWindow* qwindow = self->waylandWindow()->window();
        qwindow->resize(static_cast<int>(width), static_cast<int>(height));
    }

    // Trigger a frame
    self->waylandWindow()->handleExpose(QRect(0, 0, static_cast<int>(width), static_cast<int>(height)));

    qCDebug(lcLayerShellWindow) << "Configured:" << width << "x" << height;
}

void LayerShellWindow::handleClosed(void* data, struct zwlr_layer_surface_v1* surface)
{
    Q_UNUSED(surface)
    auto* self = static_cast<LayerShellWindow*>(data);
    qCDebug(lcLayerShellWindow) << "Layer surface closed by compositor";

    QWindow* qwindow = self->waylandWindow()->window();
    if (qwindow) {
        qwindow->close();
    }
}

} // namespace PlasmaZones
