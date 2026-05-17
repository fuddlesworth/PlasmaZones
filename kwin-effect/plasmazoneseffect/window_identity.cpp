// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "../plasmazoneseffect.h"

#include <PhosphorIdentity/WindowId.h>
#include <PhosphorProtocol/ClientHelpers.h>
#include <PhosphorProtocol/ServiceConstants.h>
#include <PhosphorProtocol/WindowTypeEnum.h>

#include <virtualdesktops.h>
#include <window.h>

#include <utility>

namespace {

/// Map KWin's overlapping window-type predicates onto exactly one
/// PhosphorProtocol::WindowType. Ordered most-specific-first because a window
/// can satisfy several predicates at once (a modal dialog is both a dialog and
/// modal — modality is orthogonal state, deliberately not a WindowType).
/// Returns the enum's underlying int so the value can cross D-Bus without the
/// effect exposing the enum type on a public interface.
int windowTypeFor(KWin::EffectWindow* w)
{
    using PhosphorProtocol::WindowType;
    if (!w) {
        return static_cast<int>(WindowType::Unknown);
    }
    if (w->isDesktop()) {
        return static_cast<int>(WindowType::Desktop);
    }
    if (w->isDock()) {
        return static_cast<int>(WindowType::Dock);
    }
    if (w->isOnScreenDisplay()) {
        return static_cast<int>(WindowType::OnScreenDisplay);
    }
    if (w->isNotification()) {
        return static_cast<int>(WindowType::Notification);
    }
    if (w->isSplash()) {
        return static_cast<int>(WindowType::Splash);
    }
    if (w->isTooltip()) {
        return static_cast<int>(WindowType::Tooltip);
    }
    if (w->isDropdownMenu() || w->isPopupMenu() || w->isMenu()) {
        return static_cast<int>(WindowType::Menu);
    }
    if (w->isUtility()) {
        return static_cast<int>(WindowType::Utility);
    }
    if (w->isDialog()) {
        return static_cast<int>(WindowType::Dialog);
    }
    if (w->isPopupWindow()) {
        return static_cast<int>(WindowType::Popup);
    }
    if (w->isNormalWindow()) {
        return static_cast<int>(WindowType::Normal);
    }
    return static_cast<int>(WindowType::Unknown);
}

} // namespace

namespace PlasmaZones {

QString PlasmaZonesEffect::getWindowId(KWin::EffectWindow* w) const
{
    // windowId IS the instance id. The daemon's runtime primary key is this
    // opaque, compositor-supplied string. It's stable for the window's
    // lifetime regardless of class mutations, so every map/set keyed by
    // windowId inside the daemon is immune to Electron/CEF apps swapping
    // their WM_CLASS after the surface is mapped.
    //
    // App class is looked up separately — via getWindowAppId() here in the
    // effect, and via WindowRegistry in the daemon after pushWindowMetadata
    // updates the registry on KWin's class-change signals. Both read the live
    // value rather than trusting a frozen first-seen string.
    if (!w) {
        return QString();
    }

    // Cache hit: the composite is frozen at first observation for the
    // window's lifetime so daemon maps keyed by windowId stay stable even
    // when an Electron/CEF app mutates its class mid-session.
    auto cacheIt = m_windowIdCache.constFind(w);
    if (cacheIt != m_windowIdCache.constEnd()) {
        return cacheIt.value();
    }

    KWin::Window* window = w->window();
    if (!window) {
        return QString();
    }
    const QString instanceId = window->internalId().toString(QUuid::WithoutBraces);
    const QString appId = getWindowAppId(w);
    const QString result = ::PhosphorIdentity::WindowId::buildCompositeId(appId, instanceId);
    m_windowIdCache.insert(w, result);
    m_windowIdReverse.insert(result, const_cast<KWin::EffectWindow*>(w));
    return result;
}

QString PlasmaZonesEffect::getWindowInstanceId(KWin::EffectWindow* w) const
{
    if (!w) {
        return QString();
    }
    KWin::Window* window = w->window();
    if (!window) {
        return QString();
    }
    return window->internalId().toString(QUuid::WithoutBraces);
}

QString PlasmaZonesEffect::getWindowAppId(KWin::EffectWindow* w) const
{
    if (!w) {
        return QString();
    }
    KWin::Window* window = w->window();
    if (!window) {
        return QString();
    }
    // Prefer desktopFileName (stable cross-session identifier when available).
    QString appId = window->desktopFileName();
    if (appId.isEmpty()) {
        // Fallback: normalize windowClass
        //   X11: "resourceName resourceClass" → extract resourceClass
        //   Wayland: app_id as-is
        QString wc = w->windowClass();
        int spaceIdx = wc.indexOf(QLatin1Char(' '));
        appId = (spaceIdx > 0) ? wc.mid(spaceIdx + 1) : wc;
    }
    return appId.toLower();
}

void PlasmaZonesEffect::pushWindowMetadata(KWin::EffectWindow* w)
{
    if (!w) {
        return;
    }
    const QString instanceId = getWindowInstanceId(w);
    if (instanceId.isEmpty()) {
        return;
    }

    const QString appId = getWindowAppId(w);
    KWin::Window* window = w->window();
    const QString desktopFile = window ? window->desktopFileName() : QString();
    const QString title = w->caption();

    // windowRole is the X11 WM_WINDOW_ROLE — empty for Wayland-native windows.
    const QString windowRole = window ? window->windowRole() : QString();
    const int pid = static_cast<int>(w->pid());

    // virtualDesktop: 0 = on all desktops / unknown; otherwise the 1-based x11
    // desktop number of the window's first desktop. A window spanning several
    // (but not all) desktops reports its first — the registry stores one int.
    int virtualDesktop = 0;
    if (window) {
        const QList<KWin::VirtualDesktop*> desktops = window->desktops();
        if (!desktops.isEmpty() && desktops.first()) {
            virtualDesktop = static_cast<int>(desktops.first()->x11DesktopNumber());
        }
    }

    // activity: empty = on all activities / unknown; otherwise the first UUID.
    const QStringList activities = w->activities();
    const QString activity = activities.isEmpty() ? QString() : activities.first();

    const int windowType = windowTypeFor(w);

    // Fire-and-forget — the daemon side is idempotent.
    PhosphorProtocol::ClientHelpers::fireAndForget(
        this, PhosphorProtocol::Service::Interface::WindowTracking, QStringLiteral("setWindowMetadata"),
        {instanceId, appId, desktopFile, title, windowRole, pid, virtualDesktop, activity, windowType},
        QStringLiteral("setWindowMetadata"));
}

void PlasmaZonesEffect::flushPendingFrameGeometry()
{
    if (m_pendingFrameGeometry.isEmpty()) {
        return;
    }
    // Move into a local so reentrancy from D-Bus (or later pushes) can't
    // disturb the iteration.
    const auto batch = std::exchange(m_pendingFrameGeometry, {});
    for (auto it = batch.constBegin(); it != batch.constEnd(); ++it) {
        const QRect& geo = it.value();
        PhosphorProtocol::ClientHelpers::fireAndForget(
            this, PhosphorProtocol::Service::Interface::WindowTracking, QStringLiteral("setFrameGeometry"),
            {it.key(), geo.x(), geo.y(), geo.width(), geo.height()}, QStringLiteral("setFrameGeometry"));
    }
}

bool PlasmaZonesEffect::isPlasmaShellSurface(const QString& windowClass)
{
    // Substring match on "plasmashell" already subsumes "org.kde.plasmashell".
    // Listed classes are the layer-shell surfaces that leak into autotile
    // tracking on Wayland: notification containers, system tray popups, the
    // OSD, the emoji picker, and krunner. Case-insensitive because Wayland
    // appIds and X11 class names differ in casing conventions.
    return windowClass.contains(QLatin1String("plasmashell"), Qt::CaseInsensitive)
        || windowClass.contains(QLatin1String("org.kde.plasma.emojier"), Qt::CaseInsensitive)
        || windowClass.contains(QLatin1String("org.kde.plasma.notifications"), Qt::CaseInsensitive)
        || windowClass.contains(QLatin1String("org.kde.krunner"), Qt::CaseInsensitive);
}

} // namespace PlasmaZones
