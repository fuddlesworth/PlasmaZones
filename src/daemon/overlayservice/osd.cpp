// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "internal.h"
#include "../overlayservice.h"
#include "../../core/logging.h"
#include <PhosphorSurfaces/SurfaceManager.h>
#include <PhosphorZones/Layout.h>
#include <PhosphorZones/LayoutUtils.h>
#include <PhosphorScreens/Manager.h>
#include "../../core/utils.h"
#include <QQuickWindow>
#include <QScreen>
#include <QSet>
#include <QQmlEngine>
#include <QGuiApplication>

#include <PhosphorLayer/ILayerShellTransport.h>
#include <PhosphorLayer/Surface.h>
#include "pz_roles.h"
#include <PhosphorScreens/ScreenIdentity.h>

namespace PlasmaZones {

namespace {

// Center an OSD/layer window within a screen geometry using layer surface margins.
// physScreenGeom is the full physical screen; targetGeom is the area to center within
// (same as physScreenGeom for physical screens, or a sub-region for virtual screens).
// Writes through the PhosphorLayer transport handle — keeps the daemon off the
// PhosphorShell API so OSDs can migrate to XdgToplevelTransport (or any future
// transport) without edits here.
void centerLayerWindowOnScreen(PhosphorLayer::ITransportHandle* handle, const QRect& physScreenGeom,
                               const QRect& targetGeom, int osdWidth, int osdHeight)
{
    if (!handle) {
        qCWarning(lcOverlay) << "centerLayerWindowOnScreen: no transport handle — surface was not warmed";
        return;
    }
    // Position OSD within the VS sub-region AND clamp it to never bleed past
    // the VS's edges. Naive centering computes `(vsOffset + (vsW - osd) / 2)`
    // but, for an OSD wider than the VS (e.g. a long navigation label on a
    // narrow half-screen VS), that value can push the right edge of the OSD
    // into the neighbouring VS. The qMin clamps cap the centre position at
    // "rightmost column where the OSD still fits entirely inside the VS";
    // the qMax floors cover the pathological case where the OSD is wider
    // than the VS itself (fall back to VS-aligned left edge).
    const int vsOffsetX = targetGeom.x() - physScreenGeom.x();
    const int vsOffsetY = targetGeom.y() - physScreenGeom.y();
    const int idealCenterX = vsOffsetX + qMax(0, (targetGeom.width() - osdWidth) / 2);
    const int idealCenterY = vsOffsetY + qMax(0, (targetGeom.height() - osdHeight) / 2);
    const int maxCenterX = qMax(vsOffsetX, vsOffsetX + targetGeom.width() - osdWidth);
    const int maxCenterY = qMax(vsOffsetY, vsOffsetY + targetGeom.height() - osdHeight);
    const int targetCenterX = qMin(idealCenterX, maxCenterX);
    const int targetCenterY = qMin(idealCenterY, maxCenterY);
    const int rightMargin = qMax(0, physScreenGeom.width() - targetCenterX - osdWidth);
    const int bottomMargin = qMax(0, physScreenGeom.height() - targetCenterY - osdHeight);

    handle->setAnchors(PhosphorLayer::AnchorAll);
    handle->setMargins(QMargins(targetCenterX, targetCenterY, rightMargin, bottomMargin));
}

// Read the QML-computed desired size from the host Window. Both OSD content
// types (LayoutOsdContent, NavigationOsdContent) expose contentDesiredWidth /
// contentDesiredHeight readonly properties measured from their own container
// items; NotificationOverlay.qml re-exports those via the loader item.
//
// Fallbacks are belt-and-suspenders for the unlikely case that the property
// can't be read (no loader.item, or QML metaobject lookup fails) — matching
// the warm-up defaults baked into NotificationOverlay.qml.
struct OsdContentSize
{
    int width;
    int height;
};

OsdContentSize readOsdContentSize(QQuickWindow* window)
{
    constexpr int kFallbackWidth = 240;
    constexpr int kFallbackHeight = 70;
    if (!window) {
        return {kFallbackWidth, kFallbackHeight};
    }
    const QVariant widthVar = window->property("contentDesiredWidth");
    const QVariant heightVar = window->property("contentDesiredHeight");
    const int width = (widthVar.isValid() && widthVar.toInt() > 0) ? widthVar.toInt() : kFallbackWidth;
    const int height = (heightVar.isValid() && heightVar.toInt() > 0) ? heightVar.toInt() : kFallbackHeight;
    return {width, height};
}

// Size the OSD window to its content-driven desired size and pin it via the
// layer-shell transport. `surface` resolves the transport handle; callers
// pass the state's notificationSurface (post-Phase-2 unification of the
// layout-OSD and navigation-OSD surfaces onto NotificationOverlay.qml).
//
// Caller contract: every property the QML content type uses to compute its
// own size MUST be written before this function runs. QQuickText measures
// synchronously when its text property changes, and Item width / height
// bindings re-evaluate eagerly on dependency change, so by the time we read
// contentDesiredWidth / Height the QML side has settled.
void sizeAndCenterOsd(QQuickWindow* window, PhosphorLayer::Surface* surface, QScreen* physScreen,
                      const QRect& targetGeom)
{
    if (!window) {
        return;
    }
    const auto [osdWidth, osdHeight] = readOsdContentSize(window);
    window->setWidth(osdWidth);
    window->setHeight(osdHeight);
    const QRect physGeom = physScreen ? physScreen->geometry() : targetGeom;
    centerLayerWindowOnScreen(surface ? surface->transport() : nullptr, physGeom, targetGeom, osdWidth, osdHeight);
}

} // namespace

bool OverlayService::prepareLayoutOsdWindow(QQuickWindow*& window, PhosphorLayer::Surface*& outSurface,
                                            QScreen*& outPhysScreen, QRect& screenGeom, qreal& aspectRatio,
                                            const QString& screenId)
{
    // Resolve target screen using shared helper (handles virtual IDs, fallback chain)
    QScreen* physScreen = resolveTargetScreen(m_screenManager, screenId);
    if (!physScreen) {
        qCWarning(lcOverlay) << "No screen available for layout OSD";
        return false;
    }

    outPhysScreen = physScreen;

    // Use virtual screen geometry if applicable, otherwise physical
    screenGeom = resolveScreenGeometry(m_screenManager, screenId);
    if (!screenGeom.isValid()) {
        screenGeom = physScreen->geometry();
    }

    QString effectiveId = screenId.isEmpty() ? Phosphor::Screens::ScreenIdentity::identifierFor(physScreen) : screenId;

    auto* state = ensureNotificationWindowFor(effectiveId, physScreen);
    if (!state || !state->notificationWindow) {
        qCWarning(lcOverlay) << "Failed to get notification window for layout OSD";
        return false;
    }
    window = state->notificationWindow;
    outSurface = state->notificationSurface;

    // Switch the unified host's Loader to LayoutOsdContent. Subsequent
    // writeQmlProperty calls flow root → Component-bound binding →
    // loader.item, so the layout-specific properties land on the freshly
    // loaded content. The mode write is idempotent — repeated calls with
    // the same mode are no-ops thanks to QML property equality guards.
    writeQmlProperty(window, QStringLiteral("mode"), QStringLiteral("layout-osd"));

    assertWindowOnScreen(window, physScreen, screenGeom);

    aspectRatio =
        (screenGeom.height() > 0) ? static_cast<qreal>(screenGeom.width()) / screenGeom.height() : (16.0 / 9.0);
    aspectRatio = qBound(0.5, aspectRatio, 4.0);

    return true;
}

void OverlayService::showLayoutOsd(PhosphorZones::Layout* layout, const QString& screenId)
{
    showLayoutOsdImpl(layout, screenId, false);
}

void OverlayService::showLockedLayoutOsd(PhosphorZones::Layout* layout, const QString& screenId)
{
    showLayoutOsdImpl(layout, screenId, true);
}

void OverlayService::showLayoutOsdImpl(PhosphorZones::Layout* layout, const QString& screenId, bool locked)
{
    if (!layout) {
        qCDebug(lcOverlay) << "No layout provided for OSD";
        return;
    }

    if (!locked && layout->zones().isEmpty()) {
        qCDebug(lcOverlay) << "Skipping OSD for empty layout=" << layout->name();
        return;
    }
    QQuickWindow* window = nullptr;
    PhosphorLayer::Surface* surface = nullptr;
    QScreen* physScreen = nullptr;
    QRect screenGeom;
    qreal aspectRatio = 0;
    if (!prepareLayoutOsdWindow(window, surface, physScreen, screenGeom, aspectRatio, screenId)) {
        return;
    }

    LayoutOsdContentParams p;
    p.id = layout->id().toString();
    p.name = layout->name();
    p.zones = layout->zones().isEmpty()
        ? QVariantList()
        : PhosphorZones::LayoutUtils::zonesToVariantList(layout, PhosphorZones::ZoneField::Full);
    p.category = static_cast<int>(PhosphorZones::LayoutCategory::Manual);
    p.autoAssign = layout->autoAssign();
    p.globalAutoAssign = m_settings && m_settings->autoAssignAllLayouts();
    p.locked = locked;
    p.screenAspectRatio = aspectRatio;
    p.aspectRatioClass = PhosphorLayout::ScreenClassification::toString(layout->aspectRatioClass());
    pushLayoutOsdContent(window, p);

    sizeAndCenterOsd(window, surface, physScreen, screenGeom);
    // Phase 5: Surface::show() drives the SurfaceAnimator (osd.show + osd.pop);
    // restartDismissTimer kicks the QML auto-dismiss Timer that emits
    // dismissRequested → surface->hide() (wired in createWarmedOsdSurface).
    surface->show();
    QMetaObject::invokeMethod(window, "restartDismissTimer");
    qCInfo(lcOverlay) << (locked ? "Locked" : "Layout") << "OSD: layout=" << layout->name() << "screen=" << screenId;
}

void OverlayService::showLayoutOsd(const QString& id, const QString& name, const QVariantList& zones, int category,
                                   bool autoAssign, const QString& screenId, bool showMasterDot,
                                   bool producesOverlappingZones, const QString& zoneNumberDisplay, int masterCount)
{
    if (zones.isEmpty()) {
        qCDebug(lcOverlay) << "Skipping OSD for empty layout=" << name;
        return;
    }

    QQuickWindow* window = nullptr;
    PhosphorLayer::Surface* surface = nullptr;
    QScreen* physScreen = nullptr;
    QRect screenGeom;
    qreal aspectRatio = 0;
    if (!prepareLayoutOsdWindow(window, surface, physScreen, screenGeom, aspectRatio, screenId)) {
        return;
    }

    // Resolve aspectRatioClass.
    //
    // Snap layouts (UUID id): use the layout's tagged aspect-ratio class so a
    // class="portrait" layout renders at the canonical 9:16 preview regardless
    // of the exact screen aspect — preserves the layout author's intent.
    //
    // Autotile algorithms (non-UUID id like "autotile:rows") have no intrinsic
    // class. Classify the screen's actual aspect ratio and use that class so
    // the preview snaps to the same canonical ratio the comparable snap-layout
    // OSD would render at on this screen. Without this, autotile previews
    // showed the raw screen aspect (e.g. 0.93 for a 1600×1716 VS, nearly
    // square) while snap layouts on the same screen rendered at 9:16 — a
    // visibly inconsistent feel between the two OSD paths.
    //
    // The class is the only AR information C++ needs to push — QML derives
    // the numeric preview ratio from it (LayoutOsdContent.previewAspectRatio
    // switch), and OSD outer size is content-driven, so no companion numeric
    // is required here.
    QString arClass = QStringLiteral("any");
    auto uuidOpt = Utils::parseUuid(id);
    if (uuidOpt && m_layoutManager) {
        PhosphorZones::Layout* layout = m_layoutManager->layoutById(*uuidOpt);
        if (layout) {
            arClass = PhosphorLayout::ScreenClassification::toString(layout->aspectRatioClass());
        }
    } else {
        const auto screenClass = PhosphorLayout::ScreenClassification::classify(aspectRatio);
        arClass = PhosphorLayout::ScreenClassification::toString(screenClass);
    }

    LayoutOsdContentParams p;
    p.id = id;
    p.name = name;
    p.zones = zones;
    p.category = category;
    p.autoAssign = autoAssign;
    // Forward the global master toggle (#370) only for manual layouts.
    // Autotile screens never reach calculateSnapToEmptyZone, so the global
    // flag has no effect on them and must not influence the badge.
    const bool isManual = category == static_cast<int>(PhosphorZones::LayoutCategory::Manual);
    p.globalAutoAssign = isManual && m_settings && m_settings->autoAssignAllLayouts();
    p.locked = false;
    p.screenAspectRatio = aspectRatio;
    p.aspectRatioClass = arClass;
    p.showMasterDot = showMasterDot;
    p.producesOverlappingZones = producesOverlappingZones;
    p.zoneNumberDisplay = zoneNumberDisplay;
    p.masterCount = masterCount;
    pushLayoutOsdContent(window, p);

    sizeAndCenterOsd(window, surface, physScreen, screenGeom);
    surface->show();
    QMetaObject::invokeMethod(window, "restartDismissTimer");
    qCInfo(lcOverlay) << "Layout OSD: name=" << name << "category=" << category << "screen=" << screenId;
}

void OverlayService::pushLayoutOsdContent(QQuickWindow* window, const LayoutOsdContentParams& p)
{
    if (!window) {
        return;
    }
    // Reset overlay-state flags first — the notification window is reused
    // across show calls, so a prior showLockedLayoutOsd / showDisabledOsd
    // would otherwise leave `locked` or `disabled` stuck on.
    resetOsdOverlayState(window);
    writeQmlProperty(window, QStringLiteral("locked"), p.locked);
    writeQmlProperty(window, QStringLiteral("layoutId"), p.id);
    writeQmlProperty(window, QStringLiteral("layoutName"), p.name);
    writeQmlProperty(window, QStringLiteral("screenAspectRatio"), p.screenAspectRatio);
    writeQmlProperty(window, QStringLiteral("aspectRatioClass"), p.aspectRatioClass);
    writeQmlProperty(window, QStringLiteral("category"), p.category);
    // Per-layout flag + global "Auto-assign for all layouts" master toggle
    // (#370). CategoryBadge folds them into the effective state. Same
    // convention as buildLayoutsList consumers (selector_update.cpp,
    // snapassist.cpp).
    writeQmlProperty(window, QStringLiteral("autoAssign"), p.autoAssign);
    writeQmlProperty(window, QStringLiteral("globalAutoAssign"), p.globalAutoAssign);
    writeAutotileMetadata(window, p.showMasterDot, p.producesOverlappingZones, p.zoneNumberDisplay, p.masterCount);
    writeQmlProperty(window, QStringLiteral("zones"), p.zones);
    writeFontProperties(window, m_settings);
}

void OverlayService::showDisabledOsd(const QString& reason, const QString& screenId)
{
    QQuickWindow* window = nullptr;
    PhosphorLayer::Surface* surface = nullptr;
    QScreen* physScreen = nullptr;
    QRect screenGeom;
    qreal aspectRatio = 0;
    if (!prepareLayoutOsdWindow(window, surface, physScreen, screenGeom, aspectRatio, screenId)) {
        return;
    }

    // Reset overlay state then set disabled. locked stays false (mutually
    // exclusive with disabled; also enforced in QML). The disabled state
    // and reason text live on `disabled` / `disabledReason`, not on the
    // shared layout-OSD properties — but we still push empty/zero values
    // into the layout-OSD slot via the shared content writer so stale
    // data from a prior showLayoutOsd doesn't leak through the
    // semi-transparent disabled overlay (CategoryBadge / Label both bind
    // to those properties even when disabled is true).
    //
    // Cross-mode property note: the host's `success` / `action` /
    // `reason` (NavigationOsd-only) are NOT reset here. NavigationOsdContent
    // is unloaded the moment we wrote `mode = "layout-osd"` in
    // prepareLayoutOsdWindow, so its bindings to those properties are
    // gone too. Keep this in mind if a future LayoutOsdContent ever
    // grows a binding that touches navigation-mode properties — see the
    // matching note in NotificationOverlay.qml's caveat block.
    LayoutOsdContentParams p;
    p.name = reason; // shown in nameLabel when disabled
    p.screenAspectRatio = aspectRatio;
    pushLayoutOsdContent(window, p);
    writeQmlProperty(window, QStringLiteral("disabled"), true);
    writeQmlProperty(window, QStringLiteral("disabledReason"), reason);

    sizeAndCenterOsd(window, surface, physScreen, screenGeom);
    surface->show();
    QMetaObject::invokeMethod(window, "restartDismissTimer");
    qCInfo(lcOverlay) << "Disabled OSD: reason=" << reason << "screen=" << screenId;
}

// hideLayoutOsd / hideNavigationOsd (formerly Q_SLOTS connected to a QML
// `dismissed()` signal) are intentionally gone. The Phase-5 dismiss path is:
//   QML dismissTimer → loaded content `dismissRequested()` signal
//     → host NotificationOverlay re-emits dismissRequested()
//     → wired by createWarmedOsdSurface to Surface::hide() (string-based)
//     → SurfaceAnimator::beginHide drives the visual fade
//     → Surface::Impl flips Qt::WindowTransparentForInput on the QWindow
// No C++ slot needs to run on dismiss; the QQuickWindow stays Qt-visible
// across the keepMappedOnHide=true lifecycle so the warmed Vulkan
// swapchain survives. Pre-warmed by warmUpNotifications and reused for
// the daemon's lifetime; destroyNotificationWindow only fires on
// screen-removal / shutdown.

// Hot-plug hook installed by warmUpNotifications. The single
// m_notificationsWarmed flag gates whether new screens get a per-screen
// NotificationOverlay window after the initial warm-up.
void OverlayService::ensureOsdScreenAddedConnected()
{
    if (m_screenAddedConnected) {
        return;
    }
    connect(qGuiApp, &QGuiApplication::screenAdded, this, [this](QScreen* screen) {
        if (!m_notificationsWarmed) {
            return;
        }
        auto* mgr2 = m_screenManager;
        const QString physId = Phosphor::Screens::ScreenIdentity::identifierFor(screen);
        const QStringList ids = mgr2 ? mgr2->virtualScreenIdsFor(physId) : QStringList{physId};
        for (const QString& sid : ids) {
            ensureNotificationWindowFor(sid, screen);
        }
    });
    m_screenAddedConnected = true;
}

OverlayService::PerScreenOverlayState* OverlayService::ensureNotificationWindowFor(const QString& effectiveId,
                                                                                   QScreen* physScreen)
{
    auto it = m_screenStates.find(effectiveId);
    if (it == m_screenStates.end() || !it->notificationWindow) {
        createNotificationWindow(effectiveId, physScreen);
        it = m_screenStates.find(effectiveId);
    }
    return (it == m_screenStates.end()) ? nullptr : &it.value();
}

// Pre-create the per-screen NotificationOverlay surface for every effective
// screen the manager knows about, then mark notifications warmed and ensure
// the screenAdded hot-plug hook is installed.
void OverlayService::warmUpNotifications()
{
    const QStringList effectiveIds = (m_screenManager ? m_screenManager->effectiveScreenIds() : QStringList());
    for (const QString& sid : effectiveIds) {
        QScreen* physScreen =
            m_screenManager ? m_screenManager->physicalQScreenFor(sid) : Utils::findScreenAtPosition(QPoint(0, 0));
        if (physScreen) {
            ensureNotificationWindowFor(sid, physScreen);
        }
    }
    m_notificationsWarmed = true;
    qCInfo(lcOverlay) << "Pre-warmed NotificationOverlay windows for" << effectiveIds.size() << "effective screens";
    ensureOsdScreenAddedConnected();
}

void OverlayService::createNotificationWindow(const QString& screenId, QScreen* physScreen)
{
    if (m_screenStates.contains(screenId) && m_screenStates[screenId].notificationSurface) {
        return;
    }
    // Spam-guard: if a previous attempt on this screen failed (compositor
    // refused the surface, layer-shell extension missing, etc.), don't
    // retry on every OSD show — one warning per screen is enough. The
    // sentinel is cleared in destroyAllWindowsForPhysicalScreen when the
    // monitor is hot-plug-cycled, so reconnecting the screen does retry.
    if (m_notificationCreationFailed.contains(screenId)) {
        return;
    }

    // Phase 5: keepMappedOnHide=true and dismissRequested → Surface::hide()
    // wiring are both done inside createWarmedOsdSurface. The per-instance
    // scope prefix is derived from PzRoles::Notification's base prefix via
    // `makePerInstanceRole` so the SurfaceAnimator's longest-prefix
    // role-config lookup always matches, even if the base literal in
    // pz_roles.h is renamed.
    const auto role =
        PzRoles::makePerInstanceRole(PzRoles::Notification, screenId, m_surfaceManager->nextScopeGeneration());
    auto* surface = createWarmedOsdSurface(role, QUrl(QStringLiteral("qrc:/ui/NotificationOverlay.qml")), physScreen,
                                           "notification");
    if (!surface) {
        qCWarning(lcOverlay) << "Failed to create notification window for screen=" << screenId
                             << "— suppressing further attempts until screen is replugged";
        m_notificationCreationFailed.insert(screenId);
        return;
    }

    // The host Window's Loader instantiates LayoutOsdContent /
    // NavigationOsdContent on demand when C++ writes the `mode` property
    // before each show; the surface itself stays warmed for the daemon's
    // lifetime so subsequent shows reuse the Vulkan swapchain.
    auto& state = m_screenStates[screenId];
    state.notificationSurface = surface;
    state.notificationWindow = surface->window();
    state.notificationPhysScreen = physScreen;
}

void OverlayService::destroyNotificationWindow(const QString& screenId)
{
    auto it = m_screenStates.find(screenId);
    if (it == m_screenStates.end()) {
        return;
    }
    if (it->notificationSurface) {
        it->notificationSurface->deleteLater();
        it->notificationSurface = nullptr;
        it->notificationWindow = nullptr;
    }
    it->notificationPhysScreen = nullptr;
}

void OverlayService::showNavigationOsd(bool success, const QString& action, const QString& reason,
                                       const QString& sourceZoneId, const QString& targetZoneId,
                                       const QString& screenId)
{
    qCDebug(lcOverlay) << "showNavigationOsd called: action=" << action << "reason=" << reason << "screen=" << screenId
                       << "success=" << success;

    // Only show OSD for successful actions - failures (no windows, no zones, etc.) don't need feedback
    if (!success) {
        qCDebug(lcOverlay) << "Skipping navigation OSD for failure:" << action << reason;
        return;
    }

    // Resolve target screen using shared helper (handles virtual IDs, fallback chain)
    QScreen* physScreen = resolveTargetScreen(m_screenManager, screenId);
    if (!physScreen) {
        qCWarning(lcOverlay) << "No screen available for navigation OSD";
        return;
    }

    // Use virtual screen geometry if applicable, otherwise physical
    QRect navScreenGeom = resolveScreenGeometry(m_screenManager, screenId);
    if (!navScreenGeom.isValid()) {
        navScreenGeom = physScreen->geometry();
    }

    QString effectiveId = screenId.isEmpty() ? Phosphor::Screens::ScreenIdentity::identifierFor(physScreen) : screenId;

    // Deduplicate: Skip if same action+reason+screen within 200ms (prevents duplicate from Qt signal + D-Bus signal).
    // Keyed on effectiveId (resolved from physScreen if the caller passed an
    // empty screenId) so two rapid calls with empty screenId on different
    // physical screens don't dedup against each other, and so the hot-plug
    // clear in destroyAllWindowsForPhysicalScreen — which prefix-matches on
    // the physical id — can clear stale dedup state on screen replug.
    //
    // The dedup state is updated only after we've decided to actually
    // show — see the matching m_lastNavigation* writes below near
    // navSurface->show(). A bail-out path further down (no layout,
    // no notification window, etc.) must NOT poison the dedup state,
    // otherwise a failed show silently swallows the next legitimate call
    // within 200 ms.
    const QString actionKey = action + QLatin1Char(':') + reason;
    if (actionKey == m_lastNavigationActionKey && effectiveId == m_lastNavigationScreenId
        && m_lastNavigationTime.isValid() && m_lastNavigationTime.elapsed() < 200) {
        qCDebug(lcOverlay) << "Skipping duplicate navigation OSD:" << action << reason;
        return;
    }

    // Resolve per-screen layout (not the global m_layout which may belong to another screen)
    // Float, algorithm, rotate, and autotile-only actions don't need layout/zones
    static const QSet<QString> noLayoutActions{QStringLiteral("float"),        QStringLiteral("algorithm"),
                                               QStringLiteral("rotate"),       QStringLiteral("focus_master"),
                                               QStringLiteral("swap_master"),  QStringLiteral("master_ratio"),
                                               QStringLiteral("master_count"), QStringLiteral("retile"),
                                               QStringLiteral("swap_vs"),      QStringLiteral("rotate_vs")};
    const bool needsLayout = !noLayoutActions.contains(action);
    PhosphorZones::Layout* screenLayout = resolveScreenLayout(effectiveId);
    if ((needsLayout && !screenLayout) || (screenLayout && screenLayout->zones().isEmpty() && needsLayout)) {
        qCDebug(lcOverlay) << "No layout or zones for navigation OSD: screen=" << effectiveId
                           << "layout=" << (screenLayout ? screenLayout->name() : QStringLiteral("null"))
                           << "zones=" << (screenLayout ? screenLayout->zones().size() : 0) << "action=" << action;
        return;
    }

    // Reuse existing per-screen NotificationOverlay window (create only if
    // not in map). The window stays alive (keepMappedOnHide=true) across
    // rapid navigation calls; Surface::show() re-dispatches beginShow on
    // every call so the visual fade replays, and restartDismissTimer
    // extends the auto-hide. Cleanup happens only on screen removal /
    // shutdown via destroyNotificationWindow; the dismiss path is QML
    // → Surface::hide(). The create-failure spam-guard lives in
    // createNotificationWindow itself.
    auto* navState = ensureNotificationWindowFor(effectiveId, physScreen);
    if (!navState || !navState->notificationWindow) {
        qCDebug(lcOverlay) << "No notification window for navigation OSD on screen=" << effectiveId;
        return;
    }
    auto* window = navState->notificationWindow;
    auto* navSurface = navState->notificationSurface;

    // Switch the unified host's Loader to NavigationOsdContent before the
    // per-show property writes — see the matching write in
    // prepareLayoutOsdWindow for the layout-OSD path.
    writeQmlProperty(window, QStringLiteral("mode"), QStringLiteral("navigation-osd"));

    // Process reason field - for rotation/resnap, extract window count
    // Format: "clockwise:N" or "counterclockwise:N" or "resnap:N" where N is window count
    int windowCount = 1;
    QString displayReason = reason;
    if (reason.contains(QLatin1Char(':'))) {
        QStringList parts = reason.split(QLatin1Char(':'));
        if (parts.size() >= 2) {
            bool ok = false;
            int count = parts.at(1).toInt(&ok);
            if (ok && count > 0) {
                windowCount = count;
            }
            if (action == QStringLiteral("rotate")) {
                displayReason = parts.at(0); // "clockwise" or "counterclockwise"
            }
            // resnap keeps full reason for displayReason (optional)
        }
    }

    // Set OSD data
    writeQmlProperty(window, QStringLiteral("success"), success);
    writeQmlProperty(window, QStringLiteral("action"), action);
    writeQmlProperty(window, QStringLiteral("reason"), displayReason);
    writeQmlProperty(window, QStringLiteral("windowCount"), windowCount);

    // Pass source zone ID for swap operations
    writeQmlProperty(window, QStringLiteral("sourceZoneId"), sourceZoneId);

    // Build highlighted zone IDs list (target zones)
    QStringList highlightedZoneIds;
    if (!targetZoneId.isEmpty()) {
        highlightedZoneIds.append(targetZoneId);
    }
    writeQmlProperty(window, QStringLiteral("highlightedZoneIds"), highlightedZoneIds);

    // Use shared PhosphorZones::LayoutUtils with minimal fields for zone number lookup
    // (only need zoneId and zoneNumber, not name/appearance)
    QVariantList zonesList =
        PhosphorZones::LayoutUtils::zonesToVariantList(screenLayout, PhosphorZones::ZoneField::Minimal);
    writeQmlProperty(window, QStringLiteral("zones"), zonesList);

    // Ensure the window is on the correct Wayland output (must come before sizing —
    // assertWindowOnScreen calls setGeometry(screen) which would override setWidth/setHeight).
    assertWindowOnScreen(window, physScreen, navScreenGeom);

    // Size + center via the shared helper, which reads the QML-computed
    // contentDesiredWidth / contentDesiredHeight from NotificationOverlay's
    // loaded NavigationOsdContent body. The binding has settled by now —
    // QQuickText measures synchronously when text changes, so the binding
    // chain message → messageLabel.implicitWidth → container.width →
    // contentDesiredWidth is up-to-date as soon as the writeQmlProperty
    // calls above return.
    sizeAndCenterOsd(window, navSurface, physScreen, navScreenGeom);

    // Phase 5: Surface::show() drives the SurfaceAnimator (osd.show + osd.pop);
    // restartDismissTimer kicks the QML auto-dismiss Timer that emits
    // dismissRequested → surface->hide() (wired in createWarmedOsdSurface).
    navSurface->show();
    QMetaObject::invokeMethod(window, "restartDismissTimer");

    // Update dedup state AFTER the Surface::show() + restartDismissTimer
    // dispatch. Every early-return above this point is a "no OSD shown"
    // outcome that must not poison the next call's dedup window — and
    // doing the writes here (instead of pre-show) means even an in-flight
    // animator failure that downgraded show() to a no-op wouldn't poison
    // the next legitimate call. Stored as effectiveId to match the dedup
    // check key at the top of this function and the prefix-matched clear
    // in destroyAllWindowsForPhysicalScreen.
    m_lastNavigationActionKey = actionKey;
    m_lastNavigationScreenId = effectiveId;
    m_lastNavigationTime.restart();

    qCInfo(lcOverlay) << "Showing navigation OSD: success=" << success << "action=" << action << "reason=" << reason
                      << "highlightedZones=" << highlightedZoneIds;
}

// hideNavigationOsd removed together with hideLayoutOsd — see the comment
// block above warmUpNotifications() for the rationale. The m_lastNavigation*
// dedup state is cleared implicitly by the 200 ms timeout check in
// showNavigationOsd() itself (the OSD's ~1000 ms dismiss timer is far
// longer than the dedup window, so any dismiss is always past the
// relevant timeout by the time it fires — no manual clear needed).
//
// The previous per-mode createNavigationOsdWindow / destroyNavigationOsdWindow
// pair is gone post-Phase-2: navigation OSDs share the per-screen
// NotificationOverlay surface created by createNotificationWindow above,
// so a single create/destroy pair serves both OSD modes.

} // namespace PlasmaZones
