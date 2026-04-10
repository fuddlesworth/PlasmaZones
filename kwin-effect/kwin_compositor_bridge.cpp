// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "kwin_compositor_bridge.h"
#include "plasmazoneseffect.h"

#include "geometry_helpers.h"
#include "window_id.h"

#include <effect/effecthandler.h>
#include <effect/effectwindow.h>
#include <window.h>
#include <workspace.h>

#include <QtGlobal>

namespace PlasmaZones {

KWinCompositorBridge::KWinCompositorBridge(PlasmaZonesEffect* effect)
    : m_effect(effect)
{
    Q_ASSERT(m_effect);
}

// ═══════════════════════════════════════════════════════════════════════════════
// Window Lookup
// ═══════════════════════════════════════════════════════════════════════════════

WindowHandle KWinCompositorBridge::findWindowById(const QString& windowId) const
{
    return fromEffectWindow(m_effect->findWindowById(windowId));
}

QVector<WindowHandle> KWinCompositorBridge::findAllWindowsById(const QString& windowId) const
{
    const auto windows = m_effect->findAllWindowsById(windowId);
    QVector<WindowHandle> result;
    result.reserve(windows.size());
    for (auto* w : windows) {
        result.append(fromEffectWindow(w));
    }
    return result;
}

QVector<WindowHandle> KWinCompositorBridge::stackingOrder() const
{
    const auto windows = KWin::effects->stackingOrder();
    QVector<WindowHandle> result;
    result.reserve(windows.size());
    for (auto* w : windows) {
        result.append(fromEffectWindow(w));
    }
    return result;
}

// ═══════════════════════════════════════════════════════════════════════════════
// Window Identity
// ═══════════════════════════════════════════════════════════════════════════════

QString KWinCompositorBridge::windowId(WindowHandle w) const
{
    auto* ew = toEffectWindow(w);
    if (!ew)
        return QString();
    return m_effect->getWindowId(ew);
}

QString KWinCompositorBridge::windowScreenId(WindowHandle w) const
{
    auto* ew = toEffectWindow(w);
    if (!ew)
        return QString();
    return m_effect->getWindowScreenId(ew);
}

// ═══════════════════════════════════════════════════════════════════════════════
// Window Properties
// ═══════════════════════════════════════════════════════════════════════════════

QRectF KWinCompositorBridge::frameGeometry(WindowHandle w) const
{
    auto* ew = toEffectWindow(w);
    return ew ? ew->frameGeometry() : QRectF();
}

QSizeF KWinCompositorBridge::minSize(WindowHandle w) const
{
    auto* ew = toEffectWindow(w);
    if (!ew)
        return QSizeF();
    auto* kw = ew->window();
    return kw ? kw->minSize() : QSizeF();
}

bool KWinCompositorBridge::isMinimized(WindowHandle w) const
{
    auto* ew = toEffectWindow(w);
    return ew && ew->isMinimized();
}

bool KWinCompositorBridge::isOnCurrentDesktop(WindowHandle w) const
{
    auto* ew = toEffectWindow(w);
    return ew && ew->isOnCurrentDesktop();
}

bool KWinCompositorBridge::isOnCurrentActivity(WindowHandle w) const
{
    auto* ew = toEffectWindow(w);
    return ew && ew->isOnCurrentActivity();
}

bool KWinCompositorBridge::hasDecoration(WindowHandle w) const
{
    auto* ew = toEffectWindow(w);
    return ew && ew->hasDecoration();
}

WindowInfo KWinCompositorBridge::windowInfo(WindowHandle w) const
{
    auto* ew = toEffectWindow(w);
    WindowInfo info;
    if (!ew)
        return info;

    info.handle = w;
    info.windowId = m_effect->getWindowId(ew);
    info.appId = WindowIdUtils::extractAppId(info.windowId);
    info.windowClass = ew->windowClass();
    info.screenId = m_effect->getWindowScreenId(ew);
    info.caption = ew->caption();
    info.icon = ew->icon();
    info.frameGeometry = ew->frameGeometry();
    info.isMinimized = ew->isMinimized();
    info.isFullScreen = ew->isFullScreen();
    info.isOnCurrentDesktop = ew->isOnCurrentDesktop();
    info.isOnCurrentActivity = ew->isOnCurrentActivity();
    info.isNormalWindow = ew->isNormalWindow();
    info.keepAbove = ew->keepAbove();
    info.pid = ew->pid();

    info.hasDecoration = ew->hasDecoration();

    auto* kw = ew->window();
    if (kw) {
        info.minSize = kw->minSize();
    }

    return info;
}

// ═══════════════════════════════════════════════════════════════════════════════
// Window Filtering
// ═══════════════════════════════════════════════════════════════════════════════

bool KWinCompositorBridge::shouldHandleWindow(WindowHandle w) const
{
    auto* ew = toEffectWindow(w);
    if (!ew)
        return false;
    return m_effect->shouldHandleWindow(ew);
}

bool KWinCompositorBridge::isTileableWindow(WindowHandle w) const
{
    auto* ew = toEffectWindow(w);
    if (!ew)
        return false;
    return m_effect->isTileableWindow(ew);
}

// ═══════════════════════════════════════════════════════════════════════════════
// Window Actions
// ═══════════════════════════════════════════════════════════════════════════════

void KWinCompositorBridge::moveResize(WindowHandle w, const QRectF& geometry)
{
    auto* ew = toEffectWindow(w);
    if (!ew)
        return;
    auto* kw = ew->window();
    if (kw) {
        kw->moveResize(geometry);
    }
}

void KWinCompositorBridge::setNoBorder(WindowHandle w, bool noBorder)
{
    auto* ew = toEffectWindow(w);
    if (!ew)
        return;
    auto* kw = ew->window();
    if (kw) {
        kw->setNoBorder(noBorder);
    }
}

void KWinCompositorBridge::setMaximized(WindowHandle w, bool maximized)
{
    auto* ew = toEffectWindow(w);
    if (!ew)
        return;
    auto* kw = ew->window();
    if (kw) {
        kw->maximize(maximized ? KWin::MaximizeFull : KWin::MaximizeRestore);
    }
}

void KWinCompositorBridge::activateWindow(WindowHandle w)
{
    auto* ew = toEffectWindow(w);
    if (!ew)
        return;
    KWin::effects->activateWindow(ew);
}

void KWinCompositorBridge::raiseWindow(WindowHandle w)
{
    auto* ew = toEffectWindow(w);
    if (!ew)
        return;
    auto* kw = ew->window();
    if (kw) {
        KWin::Workspace::self()->raiseWindow(kw);
    }
}

void KWinCompositorBridge::applySnapGeometry(WindowHandle w, const QRectF& geometry, bool skipAnimation)
{
    auto* ew = toEffectWindow(w);
    if (!ew)
        return;
    m_effect->applySnapGeometry(ew, GeometryHelpers::snapToRect(geometry), false, skipAnimation);
}

// ═══════════════════════════════════════════════════════════════════════════════
// D-Bus / Utility
// ═══════════════════════════════════════════════════════════════════════════════

QObject* KWinCompositorBridge::asQObject()
{
    return m_effect;
}

bool KWinCompositorBridge::isDaemonReady() const
{
    return m_effect->isDaemonReady("compositor bridge");
}

void KWinCompositorBridge::invalidateScreenIdCache()
{
    m_effect->m_screenIdCache.clear();
}

} // namespace PlasmaZones
