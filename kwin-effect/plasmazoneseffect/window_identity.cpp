// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "plasmazoneseffect.h"

#include "window_query.h"

#include <PhosphorIdentity/WindowId.h>
#include <PhosphorProtocol/ClientHelpers.h>
#include <PhosphorProtocol/ServiceConstants.h>

#include <virtualdesktops.h>
#include <wayland/surface.h>
#include <window.h>
#include <workspace.h>

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
    auto cacheIt = m_idCaches.windowIdCache.constFind(w);
    if (cacheIt != m_idCaches.windowIdCache.constEnd()) {
        return cacheIt.value();
    }

    KWin::Window* window = w->window();
    if (!window) {
        return QString();
    }
    const QString instanceId = window->internalId().toString(QUuid::WithoutBraces);
    const QString appId = getWindowAppId(w);
    const QString result = ::PhosphorIdentity::WindowId::buildCompositeId(appId, instanceId);
    m_idCaches.windowIdCache.insert(w, result);
    m_idCaches.windowIdReverse.insert(result, const_cast<KWin::EffectWindow*>(w));
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
    if (!m_daemonGate.serviceRegistered) {
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
    // shows instead of pinning to the first. Null desktop entries are skipped
    // on BOTH derivations, keeping the "virtualDesktop equals the span list's
    // first entry" invariant even if KWin hands back a null pointer.
    int virtualDesktop = 0;
    if (window) {
        const QList<KWin::VirtualDesktop*> desktops = window->desktops();
        for (const KWin::VirtualDesktop* vd : desktops) {
            if (vd) {
                virtualDesktop = static_cast<int>(vd->x11DesktopNumber());
                break;
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
    if (!includeExtended && window) {
        // captionNormal derives from the caption itself, so a caption tick
        // changes it too — without this the daemon's CaptionNormal predicate
        // matches a value frozen at the last full snapshot, permanently stale
        // on exactly the chatty-title path. A single cheap direct read, no
        // query walk; the daemon treats a CaptionNormal-only map as a caption
        // refresh (carry-forward plus this field), not a snapshot replace.
        // Known limitation: a captionNormal that transitions TO empty cannot
        // be cleared through this route — inserting an empty value would make
        // the map empty-or-caption-only ambiguous with "carry everything
        // forward" on the daemon side, so the stale value persists until the
        // next full snapshot. Full pushes re-derive it, so the staleness is
        // bounded by the next includeExtended=true push.
        const QString captionNormal = window->captionNormal();
        if (!captionNormal.isEmpty()) {
            extended.insert(PhosphorProtocol::Service::WindowMetadataKey::CaptionNormal, captionNormal);
        }
    }
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
        // Urgency is read straight off KWin::Window rather than through the
        // rule query: it is not a rule-matchable field, so adding it to
        // WindowQuery would grow the predicate vocabulary for a value only the
        // tab indicator consumes. A window with no underlying KWin::Window
        // leaves the key absent, which the daemon reads as "not urgent".
        if (window) {
            extended.insert(Key::IsDemandingAttention, window->isDemandingAttention());
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
        // Multi-desktop span list. Collected here (not with virtualDesktop
        // above) so a caption-only refresh skips the walk along with the rest
        // of the extended build. Built as an explicit QVariantList — a
        // QVariant wrapping QList<int> would not survive the daemon's
        // .toList() readback.
        if (window) {
            const QList<KWin::VirtualDesktop*> desktops = window->desktops();
            if (desktops.size() > 1) {
                QVariantList desktopsList;
                desktopsList.reserve(desktops.size());
                for (const KWin::VirtualDesktop* vd : desktops) {
                    if (vd) {
                        desktopsList.append(static_cast<int>(vd->x11DesktopNumber()));
                    }
                }
                if (!desktopsList.isEmpty()) {
                    extended.insert(Key::VirtualDesktops, desktopsList);
                }
            }
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
        // The shouldHandleWindow exclusion gate runs HERE, once per debounced
        // flush, rather than on every windowFrameGeometryChanged tick — it is
        // an uncached rule resolve over a freshly built ruleQuery, and
        // animated geometry fired it hundreds of times per second
        // (discussion #816). The decoration resync deliberately stayed per
        // tick in the stash lambda (window_lifecycle.cpp): it is cheap and
        // deferring it let a re-decorated title bar flash for the throttle
        // window. QPointer nulls if the window died since the stash; a dead
        // or excluded window contributes no daemon push.
        KWin::EffectWindow* w = it.value().window.data();
        if (!w || w->isDeleted()) {
            continue;
        }
        // Geometry-scoped rules (Width / Height / PositionX / PositionY in a
        // match) need their cached verdicts refreshed when the frame moves —
        // handled HERE, once per 50 ms flush and only when such a rule
        // exists (the set-level gate), never from the per-tick lambda
        // (discussion #816). Runs before the shouldHandleWindow gate below:
        // an EXCLUSION verdict can be geometry-scoped too, and an excluded
        // window's verdict must still refresh even though it contributes no
        // daemon push. Per-window eviction, NOT the coalesced state-change
        // helper: that helper's flush clears the GLOBAL animation match
        // cache, and this edge repeats for a drag's whole duration.
        if (m_hasGeometryScopedRules) {
            invalidateRuleCachesForWindowGeometry(it.key(), w);
        }
        if (!shouldHandleWindow(w)) {
            continue;
        }
        const QRect& geo = it.value().geometry;
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

bool PlasmaZonesEffect::isScrollTabIndicatorSurface(KWin::EffectWindow* w) const
{
    // The daemon draws the scrolling tab indicators into a layer-shell surface
    // of their own, one per screen, precisely so the paint path can slide it
    // with the strip. Nothing KWin exposes per window distinguishes it from the
    // daemon's other overlays — same window class, no caption or role, same
    // layer, same rect — and the layer-shell scope that names it is not
    // reachable from an exported API. So the daemon announces the surface's
    // protocol object id over D-Bus and this matches on that.
    //
    // The id ALONE is not a handle. A Wayland object id is unique only among
    // one client's live objects (PhosphorWayland/SurfaceIdentity.h states the
    // same contract on the announcing side), and ids start low and grow, so an
    // ordinary application's surface collides with a daemon id routinely rather
    // than exotically. A false positive here does not merely mispaint: the
    // window would take the strip's view offset every frame of every scroll,
    // forfeit occlusion culling via setTransformed/setTranslucent, and be
    // permanently lowered to the bottom of its layer by restackScrollTabSurfaces.
    // So the id match is qualified by the owning client, which for the daemon's
    // surfaces is exactly what isOwnPassthroughOverlayClass names.
    //
    // The empty-set short-circuit is the whole hot path: with no scrolling tab
    // indicators anywhere (the overwhelmingly common case) this costs one
    // container check per window per frame. The class comparison is ordered
    // after the id lookup so only a numeric match ever pays for it.
    if (m_scrollTabSurfaceIds.isEmpty() || !w) {
        return false;
    }
    KWin::Window* window = w->window();
    KWin::SurfaceInterface* surface = window ? window->surface() : nullptr;
    if (!surface || !m_scrollTabSurfaceIds.contains(surface->id())) {
        return false;
    }
    return isOwnPassthroughOverlayClass(w->windowClass());
}

void PlasmaZonesEffect::restackScrollTabSurfaces()
{
    // Put the tab-indicator surfaces at the bottom of their layer, under the
    // daemon's passive overlay shell.
    //
    // Both sit on the same wlr layer and wlr-layer-shell has no ordering
    // request within one, so the compositor stacks them by creation and the
    // indicator surface — created lazily, on the first tabbed column — lands on
    // top. That inverts the tiers the two used to have as slots on ONE surface,
    // where the indicators deliberately sat under every card: without this the
    // layout picker, cheatsheet and snap-assist would wear a set of tab bars
    // across them.
    //
    // A client cannot express this, but we are also the compositor, so we
    // simply say it. Lowering is one-shot per window: nothing activates a layer
    // surface, so nothing raises it again, and a surface that unmaps and comes
    // back arrives through windowAdded, which calls this.
    //
    // Bottom of the LAYER, not of the screen — the layer bucket is above every
    // ordinary window either way. The one visible consequence is that the zone
    // overlay now paints over the indicators during a drag rather than under
    // them, which is the right way round for a drag preview.
    if (m_scrollTabSurfaceIds.isEmpty() || !KWin::effects) {
        return;
    }
    auto* ws = KWin::Workspace::self();
    if (!ws) {
        return;
    }
    for (KWin::EffectWindow* w : KWin::effects->stackingOrder()) {
        if (!w || w->isDeleted() || !isScrollTabIndicatorSurface(w)) {
            continue;
        }
        if (KWin::Window* kw = w->window()) {
            // Multiple screens each lower to the very bottom in turn, so they
            // end up reversed among themselves. That orders surfaces that never
            // overlap: one per output, each covering only its own.
            ws->lowerWindow(kw);
        }
    }
}

void PlasmaZonesEffect::onScrollTabSurfaceChanged(const QString& screenId, uint surfaceId)
{
    // The set is what the paint path tests, since it already resolves the
    // output from the window itself and has no screen to look up. The
    // per-screen map beside it exists solely so an announcement can retract the
    // id it replaces: the announcement names a screen, not the outgoing id.
    const quint32 previous = m_scrollTabSurfaceIdsByScreen.value(screenId, 0);
    if (previous != 0) {
        m_scrollTabSurfaceIds.remove(previous);
    }
    if (surfaceId == 0) {
        m_scrollTabSurfaceIdsByScreen.remove(screenId);
        return;
    }
    m_scrollTabSurfaceIdsByScreen.insert(screenId, surfaceId);
    m_scrollTabSurfaceIds.insert(surfaceId);
    // The usual order: KWin sees the layer surface before the daemon announces
    // it (the announcement follows the surface being shown), so the window is
    // already in the stacking order and this is where it gets lowered. The
    // windowAdded hook covers the other order and any later re-map.
    restackScrollTabSurfaces();
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
