// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorShell/LayerSurface.h>
#include "qpa/layershellintegration.h"

#include <QGuiApplication>
#include <QLoggingCategory>
#include <QHash>
#include <QThread>

Q_LOGGING_CATEGORY(lcLayerSurface, "phosphorshell.layersurface")

namespace PhosphorShell {

using SurfaceRegistry = QHash<QWindow*, LayerSurface*>;
Q_GLOBAL_STATIC(SurfaceRegistry, s_surfaces)

LayerSurface::LayerSurface(QWindow* window)
    : QObject(window)
    , m_window(window)
    , m_scope(QStringLiteral("phosphorshell"))
{
    window->setProperty(LayerSurfaceProps::IsLayerShell, true);
    window->setProperty(LayerSurfaceProps::Surface, QVariant::fromValue(this));
    window->setProperty(LayerSurfaceProps::Layer, static_cast<int>(m_layer));
    window->setProperty(LayerSurfaceProps::Anchors, static_cast<int>(m_anchors));
    window->setProperty(LayerSurfaceProps::ExclusiveZone, m_exclusiveZone);
    window->setProperty(LayerSurfaceProps::Keyboard, static_cast<int>(m_keyboard));
    window->setProperty(LayerSurfaceProps::Scope, m_scope);
    window->setProperty(LayerSurfaceProps::MarginsLeft, 0);
    window->setProperty(LayerSurfaceProps::MarginsTop, 0);
    window->setProperty(LayerSurfaceProps::MarginsRight, 0);
    window->setProperty(LayerSurfaceProps::MarginsBottom, 0);

    QWindow* rawWindow = window;
    m_destroyedConnection = connect(window, &QObject::destroyed, this, [rawWindow]() {
        if (!s_surfaces.isDestroyed()) {
            s_surfaces->remove(rawWindow);
        }
    });
}

LayerSurface::~LayerSurface()
{
    Q_ASSERT_X(!qApp || QThread::currentThread() == qApp->thread(), "LayerSurface::~LayerSurface",
               "must be destroyed from the GUI thread");
    disconnect(m_destroyedConnection);
    if (!s_surfaces.isDestroyed() && m_window) {
        s_surfaces->remove(m_window.data());
    }
}

LayerSurface* LayerSurface::get(QWindow* window)
{
    Q_ASSERT_X(!qApp || QThread::currentThread() == qApp->thread(), "LayerSurface::get",
               "must be called from the GUI thread");
    if (!window)
        return nullptr;
    auto it = s_surfaces->find(window);
    if (it != s_surfaces->end())
        return *it;
    if (window->property(LayerSurfaceProps::IsLayerShell).toBool()) {
        qCWarning(lcLayerSurface) << "LayerSurface::get() called on a window that already had a"
                                  << "LayerSurface which was explicitly deleted — refusing to"
                                  << "create a replacement (platform window state is inconsistent)";
        return nullptr;
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

LayerSurface* LayerSurface::find(QWindow* window)
{
    Q_ASSERT_X(!qApp || QThread::currentThread() == qApp->thread(), "LayerSurface::find",
               "must be called from the GUI thread");
    if (!window)
        return nullptr;
    auto it = s_surfaces->find(window);
    return (it != s_surfaces->end()) ? *it : nullptr;
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
    if (static_cast<int>(layer) < static_cast<int>(LayerBackground)
        || static_cast<int>(layer) > static_cast<int>(LayerOverlay)) {
        qCWarning(lcLayerSurface) << "setLayer() called with out-of-range value" << static_cast<int>(layer)
                                  << "— ignoring";
        return;
    }
    if (m_layer == layer)
        return;
    m_layer = layer;
    if (m_window)
        m_window->setProperty(LayerSurfaceProps::Layer, static_cast<int>(layer));
    Q_EMIT layerChanged();
    emitPropertiesChanged();
}

LayerSurface::Layer LayerSurface::layer() const
{
    return m_layer;
}

void LayerSurface::setAnchors(Anchors anchors)
{
    if (static_cast<int>(anchors) & ~static_cast<int>(AnchorAll)) {
        qCWarning(lcLayerSurface) << "setAnchors() called with undefined bits set:" << Qt::hex
                                  << static_cast<int>(anchors) << "— masking to valid range";
        anchors &= AnchorAll;
    }
    if (m_anchors == anchors)
        return;
    m_anchors = anchors;
    if (m_window)
        m_window->setProperty(LayerSurfaceProps::Anchors, static_cast<int>(anchors));
    Q_EMIT anchorsChanged();
    emitPropertiesChanged();
}

LayerSurface::Anchors LayerSurface::anchors() const
{
    return m_anchors;
}

void LayerSurface::setExclusiveZone(int32_t zone)
{
    if (m_exclusiveZone == zone)
        return;
    m_exclusiveZone = zone;
    if (m_window)
        m_window->setProperty(LayerSurfaceProps::ExclusiveZone, zone);
    Q_EMIT exclusiveZoneChanged();
    emitPropertiesChanged();
}

int32_t LayerSurface::exclusiveZone() const
{
    return m_exclusiveZone;
}

void LayerSurface::setKeyboardInteractivity(KeyboardInteractivity interactivity)
{
    if (static_cast<int>(interactivity) < static_cast<int>(KeyboardInteractivityNone)
        || static_cast<int>(interactivity) > static_cast<int>(KeyboardInteractivityOnDemand)) {
        qCWarning(lcLayerSurface) << "setKeyboardInteractivity() called with out-of-range value"
                                  << static_cast<int>(interactivity) << "— ignoring";
        return;
    }
    if (m_keyboard == interactivity)
        return;
    m_keyboard = interactivity;
    if (m_window)
        m_window->setProperty(LayerSurfaceProps::Keyboard, static_cast<int>(interactivity));
    Q_EMIT keyboardInteractivityChanged();
    emitPropertiesChanged();
}

LayerSurface::KeyboardInteractivity LayerSurface::keyboardInteractivity() const
{
    return m_keyboard;
}

void LayerSurface::setScope(const QString& scope)
{
    if (m_scope == scope)
        return;
    if (m_window && m_window->isVisible()) {
        qCWarning(lcLayerSurface) << "setScope() called after show(); scope is immutable at the protocol level"
                                  << "— the layer surface retains the original scope";
        return;
    }
    m_scope = scope;
    if (m_window)
        m_window->setProperty(LayerSurfaceProps::Scope, scope);
    Q_EMIT scopeChanged();
}

QString LayerSurface::scope() const
{
    return m_scope;
}

void LayerSurface::setScreen(QScreen* screen)
{
    if (m_window && m_window->isVisible()) {
        qCWarning(lcLayerSurface) << "setScreen() called after show(); output binding is immutable";
        return;
    }
    QScreen* resolved = screen ? screen : QGuiApplication::primaryScreen();
    if (m_screen == resolved)
        return;
    m_screen = resolved;
    if (m_window) {
        if (resolved)
            m_window->setScreen(resolved);
    }
    Q_EMIT screenChanged();
}

QScreen* LayerSurface::screen() const
{
    return m_screen;
}

void LayerSurface::setMargins(const QMargins& margins)
{
    if (m_margins == margins)
        return;
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

std::pair<uint32_t, uint32_t> LayerSurface::computeLayerSize(Anchors anchors, const QSize& windowSize)
{
    bool anchoredH = (anchors & AnchorLeft) && (anchors & AnchorRight);
    bool anchoredV = (anchors & AnchorTop) && (anchors & AnchorBottom);
    uint32_t w = anchoredH ? 0 : static_cast<uint32_t>(qMax(0, windowSize.width()));
    uint32_t h = anchoredV ? 0 : static_cast<uint32_t>(qMax(0, windowSize.height()));
    return {w, h};
}

} // namespace PhosphorShell
