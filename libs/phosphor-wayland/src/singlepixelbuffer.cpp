// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorWayland/SinglePixelBuffer.h>
#include "qpa/layershellintegration.h"
#include "qpa/single_pixel_buffer_protocol.h"

#include <QLoggingCategory>
#include <QWindow>
#include <QtWaylandClient/private/qwaylandwindow_p.h>

#include <limits>

Q_LOGGING_CATEGORY(lcSinglePixelBuffer, "phosphorwayland.singlepixelbuffer")

namespace PhosphorWayland {

static uint32_t colorComponentToU32(qreal v)
{
    v = qBound(0.0, v, 1.0);
    return static_cast<uint32_t>(v * std::numeric_limits<uint32_t>::max());
}

class SinglePixelBuffer::Private
{
public:
    QColor color;
    struct wl_buffer* buffer = nullptr;

    void recreateBuffer()
    {
        destroyBuffer();
        auto* integration = LayerShellIntegration::instance();
        if (!integration)
            return;
        auto* manager = integration->singlePixelBufferManager();
        if (!manager)
            return;
        buffer = wp_single_pixel_buffer_manager_v1_create_u32_rgba_buffer(
            manager, colorComponentToU32(color.redF()), colorComponentToU32(color.greenF()),
            colorComponentToU32(color.blueF()), colorComponentToU32(color.alphaF()));
    }

    void destroyBuffer()
    {
        if (buffer) {
            wl_buffer_destroy(buffer);
            buffer = nullptr;
        }
    }
};

SinglePixelBuffer::SinglePixelBuffer(const QColor& color, QObject* parent)
    : QObject(parent)
    , d(std::make_unique<Private>())
{
    d->color = color;
    d->recreateBuffer();
}

SinglePixelBuffer::~SinglePixelBuffer()
{
    d->destroyBuffer();
}

QColor SinglePixelBuffer::color() const
{
    return d->color;
}

void SinglePixelBuffer::setColor(const QColor& color)
{
    if (d->color == color)
        return;
    d->color = color;
    d->recreateBuffer();
    Q_EMIT colorChanged();
}

bool SinglePixelBuffer::attachTo(QWindow* window)
{
    if (!window || !d->buffer) {
        qCWarning(lcSinglePixelBuffer) << "Cannot attach: window or buffer is null";
        return false;
    }
    auto* waylandWindow = dynamic_cast<QtWaylandClient::QWaylandWindow*>(window->handle());
    if (!waylandWindow) {
        qCWarning(lcSinglePixelBuffer) << "Cannot attach: window has no Wayland platform handle";
        return false;
    }
    struct wl_surface* surface = QtWaylandClient::QWaylandShellIntegration::wlSurfaceForWindow(waylandWindow);
    if (!surface) {
        qCWarning(lcSinglePixelBuffer) << "Cannot attach: no wl_surface for window";
        return false;
    }
    wl_surface_attach(surface, d->buffer, 0, 0);
    return true;
}

bool SinglePixelBuffer::isSupported()
{
    auto* integration = LayerShellIntegration::instance();
    return integration && integration->singlePixelBufferManager();
}

} // namespace PhosphorWayland
