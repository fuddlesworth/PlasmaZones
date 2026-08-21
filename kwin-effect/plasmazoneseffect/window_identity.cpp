// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "plasmazoneseffect.h"

#include "window_query.h"

#include <PhosphorAnimation/ProfilePaths.h>
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
        // The empty screenId makes ruleQueryFor derive a screen orientation
        // that this snapshot then never marshals — a knowingly discarded
        // by-product of reusing the shared builder, not a missing field: the
        // daemon has no metadata key for it and derives its own context
        // (stampScreenContext) from the live screen resolution instead.
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

PlasmaZonesEffect::ShellSurfaceKind PlasmaZonesEffect::shellSurfaceKindFor(KWin::EffectWindow* w)
{
    if (!w) {
        return ShellSurfaceKind::None;
    }
    // TYPE first, class second. isDock() is KWin's own answer (NET::Dock), and
    // it is the cheap read; the class check then confirms plasmashell OWNS the
    // dock, so a third-party panel (a wlr layer-shell bar, an Xembed tray) is
    // not silently swept into a path named for Plasma's. Verified live: both
    // Plasma panels report isDock() with resourceName/resourceClass
    // "plasmashell", while Kickoff and tray popups are NET::AppletPopup and
    // are therefore NOT Panel here — they are a separate family that will get
    // its own leaf, not a widening of this one.
    //
    // Deliberately reuses isPlasmaShellSurface ONLY as the ownership test,
    // never as the verdict: that predicate alone would also match the desktop,
    // notifications, the OSD and krunner, none of which are panels.
    if (w->isDock() && isPlasmaShellSurface(w->windowClass())) {
        return ShellSurfaceKind::Panel;
    }
    // NET::AppletPopup — the launcher, tray flyouts, any widget's expanded
    // view. KWin gives this its own type, so no class disambiguation is needed
    // or wanted: the type is Plasma-specific by construction and, unlike the
    // dock case, there is no third-party surface that could claim it. Measured
    // live: an applet popup sets NONE of KWin's generic predicates (not
    // isPopupWindow, isMenu, isDialog, isDock, isSpecialWindow aside), which
    // is why it needs an explicit arm here rather than falling out of one of
    // the type tests windowTypeFor runs.
    if (w->isAppletPopup()) {
        return ShellSurfaceKind::AppletPopup;
    }
    return ShellSurfaceKind::None;
}

QString PlasmaZonesEffect::animationEventPathFor(KWin::EffectWindow* w, const QString& requestedPath) const
{
    namespace PP = PhosphorAnimation::ProfilePaths;
    // The overwhelmingly common answer, and the cheapest: an application
    // window animates on the path its caller named. One window-type read
    // stands between every animated event and that answer, so the switch below
    // is written to fall through to it rather than to be consulted first.
    switch (shellSurfaceKindFor(w)) {
    case ShellSurfaceKind::AppletPopup:
        // The launcher, the tray flyouts, a widget's expanded view. These open
        // and close constantly, and they are the surfaces a decoration pack on
        // the Shell page is most visible on, so they get the two legs that
        // match what actually happens to them.
        if (requestedPath == PP::WindowOpen) {
            return PP::ShellAppletPopupShow;
        }
        if (requestedPath == PP::WindowClose) {
            return PP::ShellAppletPopupHide;
        }
        // EVERY other event declines. Not an oversight to be filled in later:
        // focus, minimize, maximize and the geometry legs describe things that
        // either never happen to an applet popup or are plasmashell's own
        // business, and a leg that fires on an event the surface does not
        // really have is an animation the user cannot explain or turn off.
        return QString();
    case ShellSurfaceKind::Panel:
        // A panel has no leg at all. It is mapped once and stays for the
        // session — its open and close are a plasmashell restart, which is
        // exactly when nobody wants a transition — and it hides by sliding
        // under the screen edge rather than by closing, which never reaches a
        // window-lifecycle hook. There is no event here worth naming, so the
        // taxonomy names none. A decoration pack still applies to it: that is
        // a per-frame paint, not a lifecycle event.
        return QString();
    case ShellSurfaceKind::None:
        break;
    }
    return requestedPath;
}

bool PlasmaZonesEffect::isRuleShieldedSurface(KWin::EffectWindow* w)
{
    if (!w) {
        return true;
    }
    // Structural / own-surface shield for the reconcilers that write PERSISTENT
    // window state from a rule match (stacking layer, title-bar hiding,
    // open-fullscreen). A broad match expression must never demote a dock, pin
    // a notification, hide the panel's (nonexistent) title bar, or strip the
    // daemon overlay's own keep-above.
    //
    // Transients / popups are deliberately NOT shielded: transient utility
    // surfaces are legitimate rule targets, and transient exclusion is
    // per-feature user opt-in in this project (the IsTransient match field),
    // never hardcoded policy.
    //
    // Distinct from shouldDecorateWindow's gate on purpose, and NOT to be
    // folded into it: this predicate answers "may a rule mutate this window's
    // persistent state", which stays NO for plasma-shell surfaces even when
    // the user has opted into DECORATING one. Decoration is our own paint pass
    // over a surface we do not own; rewriting that surface's window state is
    // not. Every caller wants a shielded window to resolve as rule-FREE rather
    // than to early-return, so an entry that was rule-held before its class
    // mutated (the Electron/CEF swap, an X11 type change) still drains its
    // snapshot through the caller's restore branch.
    const QString winClass = w->windowClass();
    return isOwnOverlayClass(winClass) || isPlasmaShellSurface(winClass) || isXdgDesktopPortalSurface(winClass)
        || w->isDesktop() || w->isDock() || w->isNotification() || w->isCriticalNotification()
        || w->isOnScreenDisplay();
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
