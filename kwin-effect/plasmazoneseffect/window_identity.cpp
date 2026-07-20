// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "../plasmazoneseffect.h"

#include "window_query.h"

#include <PhosphorIdentity/WindowId.h>
#include <PhosphorProtocol/ClientHelpers.h>
#include <PhosphorProtocol/ServiceConstants.h>

#include <virtualdesktops.h>
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

void PlasmaZonesEffect::pushWindowMetadata(KWin::EffectWindow* w, bool includeExtended)
{
    if (!w) {
        return;
    }
    // Gate on daemon readiness. KWin's class/desktop/caption/activity change
    // signals fire during session restore well before the daemon attaches its
    // bus name, and `fireAndForget` would WARN once per signal × N windows
    // when the WindowTracking service is missing. The bringup path re-pushes
    // metadata for every live window in continueDaemonReadySetup() once the
    // bridge is registered, so deferring here loses nothing.
    if (!m_daemonServiceRegistered) {
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
    // KWin's EffectWindow::pid() returns -1 for windows whose PID is unknown
    // (notably during session restore before the client reattaches). Clamp
    // to 0 at the source so the metadata struct's `int` field is always a
    // valid PID or the well-known "unknown" sentinel — the daemon's
    // adaptor doesn't have to second-guess negative values either.
    const int rawPid = static_cast<int>(w->pid());
    const int pid = rawPid > 0 ? rawPid : 0;

    // virtualDesktop: 0 = on all desktops / unknown; otherwise the 1-based x11
    // desktop number of the window's first desktop. A window spanning several
    // (but not all) desktops reports its first here and the FULL list via the
    // VirtualDesktops extended key below, so the daemon's per-window mode
    // resolution can prefer whichever spanned desktop the screen currently
    // shows instead of pinning to the first.
    int virtualDesktop = 0;
    QList<int> spannedDesktops;
    if (window) {
        const QList<KWin::VirtualDesktop*> desktops = window->desktops();
        if (!desktops.isEmpty() && desktops.first()) {
            virtualDesktop = static_cast<int>(desktops.first()->x11DesktopNumber());
        }
        if (desktops.size() > 1) {
            spannedDesktops.reserve(desktops.size());
            for (const KWin::VirtualDesktop* vd : desktops) {
                if (vd) {
                    spannedDesktops.append(static_cast<int>(vd->x11DesktopNumber()));
                }
            }
        }
    }

    // activity: empty = on all activities / unknown; otherwise the first UUID.
    const QStringList activities = w->activities();
    const QString activity = activities.isEmpty() ? QString() : activities.first();

    const int windowType = static_cast<int>(windowTypeFor(w));

    // Extended window-property snapshot. Built via the SAME ruleQueryFor the
    // effect's live rule path uses, so the daemon resolves byte-identical values —
    // no second, drift-prone accessor copy here. Placement state (isFloating /
    // isSnapped / zone) is intentionally NOT carried: those are resolved at
    // window-open before any placement exists, so the daemon's open-path resolvers
    // must see them absent (a predicate over an unplaced window stays inert). Only
    // the present (engaged) optionals are inserted, so an unknown field (e.g. no
    // underlying KWin::Window) leaves the daemon-side WindowQuery field disengaged,
    // mirroring the engage-only-when-known contract on both ends.
    //
    // Skipped entirely on a caption-only refresh (@p includeExtended false): the
    // map stays empty and the daemon preserves the existing extended snapshot,
    // avoiding a per-frame query build + a{sv} marshal for chatty-title windows.
    QVariantMap extended;
    if (includeExtended) {
        PhosphorRules::WindowQuery props = ruleQueryFor(w, QString(), false, false, false, QString());
        // Report the window's OWN (pre-rule) keepAbove/keepBelow — the daemon
        // matches its KeepAbove/KeepBelow predicates against this metadata,
        // and rule output must not feed rule input on that side of the
        // boundary either. Shared invariant; see applyOwnLayerFlags.
        applyOwnLayerFlags(props, getWindowId(w));
        namespace Key = PhosphorProtocol::Service::WindowMetadataKey;
        if (props.isMinimized) {
            extended.insert(Key::IsMinimized, *props.isMinimized);
        }
        if (props.isFullscreen) {
            extended.insert(Key::IsFullscreen, *props.isFullscreen);
        }
        if (props.isSticky) {
            extended.insert(Key::IsSticky, *props.isSticky);
        }
        if (props.isMaximized) {
            extended.insert(Key::IsMaximized, *props.isMaximized);
        }
        if (props.isFocused) {
            extended.insert(Key::IsFocused, *props.isFocused);
        }
        if (props.isTransient) {
            extended.insert(Key::IsTransient, *props.isTransient);
        }
        if (props.isNotification) {
            extended.insert(Key::IsNotification, *props.isNotification);
        }
        if (props.keepAbove) {
            extended.insert(Key::KeepAbove, *props.keepAbove);
        }
        if (props.keepBelow) {
            extended.insert(Key::KeepBelow, *props.keepBelow);
        }
        if (props.skipTaskbar) {
            extended.insert(Key::SkipTaskbar, *props.skipTaskbar);
        }
        if (props.skipPager) {
            extended.insert(Key::SkipPager, *props.skipPager);
        }
        if (props.skipSwitcher) {
            extended.insert(Key::SkipSwitcher, *props.skipSwitcher);
        }
        if (props.isModal) {
            extended.insert(Key::IsModal, *props.isModal);
        }
        if (props.hasDecoration) {
            extended.insert(Key::HasDecoration, *props.hasDecoration);
        }
        if (props.isResizable) {
            extended.insert(Key::IsResizable, *props.isResizable);
        }
        if (props.isMovable) {
            extended.insert(Key::IsMovable, *props.isMovable);
        }
        if (props.isMaximizable) {
            extended.insert(Key::IsMaximizable, *props.isMaximizable);
        }
        if (props.width) {
            extended.insert(Key::Width, *props.width);
        }
        if (props.height) {
            extended.insert(Key::Height, *props.height);
        }
        if (props.positionX) {
            extended.insert(Key::PositionX, *props.positionX);
        }
        if (props.positionY) {
            extended.insert(Key::PositionY, *props.positionY);
        }
        if (props.captionNormal) {
            extended.insert(Key::CaptionNormal, *props.captionNormal);
        }
        if (!spannedDesktops.isEmpty()) {
            QVariantList desktopsList;
            desktopsList.reserve(spannedDesktops.size());
            for (int d : std::as_const(spannedDesktops)) {
                desktopsList.append(d);
            }
            extended.insert(Key::VirtualDesktops, desktopsList);
        }
    }

    // Fire-and-forget — the daemon side is idempotent.
    PhosphorProtocol::ClientHelpers::fireAndForget(
        this, PhosphorProtocol::Service::Interface::WindowTracking, QStringLiteral("setWindowMetadata"),
        {instanceId, appId, desktopFile, title, windowRole, pid, virtualDesktop, activity, windowType, extended},
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

bool PlasmaZonesEffect::isOwnOverlayClass(const QString& windowClass)
{
    // Match the same substrings the shouldHandleWindow filter uses for its
    // "own overlay/editor window class" rejection. The settings app is
    // deliberately NOT here — it is a real user window the snap/tile pipeline
    // should treat normally.
    return windowClass.contains(QLatin1String("plasmazonesd"), Qt::CaseInsensitive)
        || windowClass.contains(QLatin1String("plasmazones-editor"), Qt::CaseInsensitive);
}

bool PlasmaZonesEffect::isOwnPassthroughOverlayClass(const QString& windowClass)
{
    // The daemon's overlay layer-shell surface (windowClass "plasmazonesd")
    // covers the whole autotile monitor and is permanently topmost, but it is a
    // non-interactive passthrough surface that never holds keyboard focus. The
    // focus-follows-mouse stacking walk must look THROUGH it to the real window
    // beneath, or FFM bails on every cursor move once any OSD/snap-preview/
    // layout-picker has been shown (discussion #461 #3 / PR #517).
    //
    // The editor (windowClass "plasmazones-editor") is deliberately NOT here:
    // it is an interactive fullscreen xdg-shell toplevel the user works in, not
    // a passthrough overlay. Looking through it stole the editor's focus to the
    // tiled window beneath on every cursor move — that is what this predicate
    // exists to prevent. It is still rejected from tiling by isOwnOverlayClass()
    // (shouldHandleWindow), so FFM treats it as a genuine occluder and pauses,
    // leaving focus on the editor.
    return windowClass.contains(QLatin1String("plasmazonesd"), Qt::CaseInsensitive);
}

bool PlasmaZonesEffect::isXdgDesktopPortalSurface(const QString& windowClass)
{
    // Substring match on "xdg-desktop-portal" covers every brokered portal
    // variant (kde / gtk / lxqt). Case-insensitive because the same class
    // appears differently between Wayland appId and X11 resource name.
    return windowClass.contains(QLatin1String("xdg-desktop-portal"), Qt::CaseInsensitive);
}

PhosphorEngine::WindowKind PlasmaZonesEffect::classifyWindowKind(KWin::EffectWindow* w) const
{
    if (!w) {
        return PhosphorEngine::WindowKind::Unknown;
    }
    if (isStructurallyUnmanageableWindowType(w) || !w->isNormalWindow()) {
        return PhosphorEngine::WindowKind::Transient;
    }
    return PhosphorEngine::WindowKind::Normal;
}

} // namespace PlasmaZones
