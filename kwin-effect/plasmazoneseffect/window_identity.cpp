// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "../plasmazoneseffect.h"

#include <PhosphorIdentity/WindowId.h>
#include <PhosphorProtocol/ClientHelpers.h>
#include <PhosphorProtocol/ServiceConstants.h>

#include <window.h>

#include <utility>

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
    // Canonical appId derivation lives in PhosphorIdentity so the daemon and
    // effect spell it identically. A blank / whitespace-only window class
    // yields an empty appId (never " ") — see normalizeAppId.
    return ::PhosphorIdentity::WindowId::normalizeAppId(window->desktopFileName(), w->windowClass());
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

    // Fire-and-forget — the daemon side is idempotent.
    PhosphorProtocol::ClientHelpers::fireAndForget(
        this, PhosphorProtocol::Service::Interface::WindowTracking, QStringLiteral("setWindowMetadata"),
        {instanceId, appId, desktopFile, title}, QStringLiteral("setWindowMetadata"));
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

bool PlasmaZonesEffect::isOwnOverlayClass(const QString& windowClass)
{
    // Match the same substrings the shouldHandleWindow filter uses for its
    // "own overlay/editor window class" rejection. The settings app is
    // deliberately NOT here — it is a real user window the snap/tile pipeline
    // should treat normally.
    return windowClass.contains(QLatin1String("plasmazonesd"), Qt::CaseInsensitive)
        || windowClass.contains(QLatin1String("plasmazones-editor"), Qt::CaseInsensitive);
}

PhosphorEngine::WindowKind PlasmaZonesEffect::classifyWindowKind(KWin::EffectWindow* w) const
{
    if (!w) {
        return PhosphorEngine::WindowKind::Unknown;
    }
    // Structurally unmanageable types — popups, dialogs, menus, tooltips,
    // splash screens, transient children — are all Transient. The predicate
    // is the shared single source of truth used by shouldHandleWindow and
    // notifyWindowActivated, so the classifier picks up future additions for
    // free.
    if (isStructurallyUnmanageableWindowType(w)) {
        return PhosphorEngine::WindowKind::Transient;
    }
    // KWin's `isNormalWindow` is false for several types not caught above
    // (toolbars, docks, etc.). Treat anything that is not a normal top-level
    // as Transient on the gate — restoring a saved zone to a toolbar is
    // never what the user wanted.
    if (!w->isNormalWindow()) {
        return PhosphorEngine::WindowKind::Transient;
    }
    return PhosphorEngine::WindowKind::Normal;
}

} // namespace PlasmaZones
