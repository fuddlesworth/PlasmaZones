// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "internal.h"
#include "../overlayservice.h"
#include "../../core/logging.h"
#include <PhosphorOverlay/ShellHost.h>
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
#include "pz_slot_keys.h"
#include <PhosphorScreens/ScreenIdentity.h>

#include <PhosphorAnimation/AnimationShaderEffect.h>
#include <PhosphorAnimation/AnimationShaderRegistry.h>
#include <PhosphorAnimation/ProfilePaths.h>
#include <PhosphorAnimation/ShaderProfileTree.h>
#include <PhosphorAnimation/SurfaceAnimator.h>

#include "../../core/isettings.h"

namespace PlasmaZones {

namespace {

// Resolve the per-side padding (fraction of container size) the active
// SurfaceAnimator shader leg needs reserved around the OSD's inner card.
// The wl_surface itself is now screen-sized (see `createWarmedOsdSurface`)
// and no longer needs to grow per-effect, but the QML side still consumes
// this fraction to set `shaderBoundsPadding` so the inner card's
// shader-anchor metadata stays consistent across legs.
//
// Returns 0.0 when no shader is selected for either leg, when the shader
// id doesn't resolve in the registry, or when settings/registry are not
// yet wired (pre-construction races). The QML side defaults shaderBounds-
// Padding to 0.0 too — written to confirm intent each show, not to rely on
// stale prior writes.
qreal resolveOsdShaderPadding(ISettings* settings, PhosphorAnimationShaders::AnimationShaderRegistry* registry,
                              const QString& showPath, const QString& hidePath)
{
    if (!settings || !registry) {
        return 0.0;
    }
    const auto tree = settings->shaderProfileTree();
    qreal pad = 0.0;
    const QString showId = tree.resolve(showPath).effectiveEffectId();
    if (!showId.isEmpty()) {
        pad = qMax(pad, registry->effect(showId).fboExtentRing);
    }
    const QString hideId = tree.resolve(hidePath).effectiveEffectId();
    if (!hideId.isEmpty()) {
        pad = qMax(pad, registry->effect(hideId).fboExtentRing);
    }
    return pad;
}

// Size the OSD window to its target screen rect. The wl_surface is now
// screen-sized (mirrors zone-selector / snap-assist) — anchors and
// margins were set once at warm-up time by `createWarmedOsdSurface` from
// the same VS-aware placement vocabulary popups use, so this per-show
// path only ever has to update the window dimensions when the active
// screen rect changes (e.g. monitor hot-plug between shows). The QML
// inside positions the visible card centred via `anchors.centerIn:
// parent`, so the OSD looks identical on screen — the surface is just
// bigger underneath, giving vertex-shader transitions the geometry
// runway they need to translate past the card's bounds.
void sizeOsdToScreen(QQuickWindow* window, const QRect& targetGeom)
{
    if (!window || !targetGeom.isValid()) {
        return;
    }
    window->setWidth(targetGeom.width());
    window->setHeight(targetGeom.height());
}

} // namespace

bool OverlayService::prepareLayoutOsdWindow(QQuickWindow*& window, PhosphorLayer::Surface*& outSurface,
                                            QQuickItem*& outOsdSlot, QScreen*& outPhysScreen, QRect& screenGeom,
                                            qreal& aspectRatio, const QString& screenId)
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

    auto* state = ensurePassiveShellFor(effectiveId, physScreen);
    if (!state || !state->shell->shellWindow || !state->osdSlot()) {
        qCWarning(lcOverlay) << "Failed to get passive shell for layout OSD on screen=" << effectiveId;
        return false;
    }

    // Force-hide any zone selector on this screen so a fading-out
    // selector doesn't stack translucently behind the incoming OSD.
    // Slot-level animator hide; the shell surface stays Shown for the
    // OSD that follows.
    hideZoneSelectorSlotOnScreen(effectiveId);

    window = state->shell->shellWindow;
    outSurface = state->shell->shellSurface;
    outOsdSlot = state->osdSlot();

    // Mode is NOT written here — callers write data properties first, then
    // set mode. This ensures the Loader's freshly instantiated content
    // component picks up the correct root property values via its bindings
    // on the very first frame, instead of briefly seeing defaults/stale
    // values from the previous show (or from QML property initialisers on
    // the first-ever show).

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
    QQuickItem* osdSlot = nullptr;
    QScreen* physScreen = nullptr;
    QRect screenGeom;
    qreal aspectRatio = 0;
    if (!prepareLayoutOsdWindow(window, surface, osdSlot, physScreen, screenGeom, aspectRatio, screenId)) {
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
    pushLayoutOsdContent(osdSlot, p);
    // shaderBoundsPadding written BEFORE the mode flip so the Loader's
    // freshly-instantiated content observes the correct padding on first
    // binding evaluation. The wl_surface itself is screen-sized
    // regardless (see `createWarmedOsdSurface`); this fraction propagates
    // into the loaded content for shader-anchor bookkeeping.
    {
        namespace PP = PhosphorAnimation::ProfilePaths;
        writeQmlProperty(osdSlot, QStringLiteral("shaderBoundsPadding"),
                         resolveOsdShaderPadding(m_settings, m_animShaderRegistry, PP::OsdShow, PP::OsdHide));
    }
    writeQmlProperty(osdSlot, QStringLiteral("mode"), QStringLiteral("layout-osd"));

    sizeOsdToScreen(window, screenGeom);
    // Disarm the render-pipeline prime first so its queued hide doesn't
    // race this real show — see primeSurfaceRenderPipeline.
    cancelSurfacePrime(surface);
    // Shell wl_surface is permanently mapped — only the slot's opacity
    // animates. Map the wl_surface on first show via Surface::show()
    // (idempotent on subsequent shows; the keepMappedOnHide=true config
    // means the wl_surface stays mapped between slot animations).
    if (!surface->isLogicallyShown()) {
        surface->show();
    }
    osdSlot->setVisible(true);
    m_surfaceAnimator->beginShow(surface, osdSlot, PzRoles::Osd, []() { });
    // Surface::show() above unconditionally clears Qt::WindowTransparentForInput.
    // OSD slots don't grab input (they auto-dismiss; keeping the input
    // region active for the OSD's lifetime would block clicks on every
    // background window for several seconds). Re-evaluate the input
    // region now that the OSD slot is visible — `syncPassiveShellSurfaceState`
    // counts only modal slots toward `anyInputGrabbing`.
    syncPassiveShellSurfaceStateForSurface(surface);
    QMetaObject::invokeMethod(osdSlot, "restartDismissTimer");
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
    QQuickItem* osdSlot = nullptr;
    QScreen* physScreen = nullptr;
    QRect screenGeom;
    qreal aspectRatio = 0;
    if (!prepareLayoutOsdWindow(window, surface, osdSlot, physScreen, screenGeom, aspectRatio, screenId)) {
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
    pushLayoutOsdContent(osdSlot, p);
    // shaderBoundsPadding before mode — see the matching note in
    // showLayoutOsdImpl above.
    {
        namespace PP = PhosphorAnimation::ProfilePaths;
        writeQmlProperty(osdSlot, QStringLiteral("shaderBoundsPadding"),
                         resolveOsdShaderPadding(m_settings, m_animShaderRegistry, PP::OsdShow, PP::OsdHide));
    }
    writeQmlProperty(osdSlot, QStringLiteral("mode"), QStringLiteral("layout-osd"));

    sizeOsdToScreen(window, screenGeom);
    cancelSurfacePrime(surface);
    if (!surface->isLogicallyShown()) {
        surface->show();
    }
    osdSlot->setVisible(true);
    m_surfaceAnimator->beginShow(surface, osdSlot, PzRoles::Osd, []() { });
    // Surface::show() above unconditionally clears Qt::WindowTransparentForInput.
    // OSD slots don't grab input (they auto-dismiss; keeping the input
    // region active for the OSD's lifetime would block clicks on every
    // background window for several seconds). Re-evaluate the input
    // region now that the OSD slot is visible — `syncPassiveShellSurfaceState`
    // counts only modal slots toward `anyInputGrabbing`.
    syncPassiveShellSurfaceStateForSurface(surface);
    QMetaObject::invokeMethod(osdSlot, "restartDismissTimer");
    qCInfo(lcOverlay) << "Layout OSD: name=" << name << "category=" << category << "screen=" << screenId;
}

void OverlayService::pushLayoutOsdContent(QObject* osdSlot, const LayoutOsdContentParams& p)
{
    if (!osdSlot) {
        return;
    }
    // Reset overlay-state flags first — the OSD slot Item is reused
    // across show calls, so a prior showLockedLayoutOsd / showDisabledOsd
    // would otherwise leave `locked` or `disabled` stuck on.
    resetOsdOverlayState(osdSlot);
    writeQmlProperty(osdSlot, QStringLiteral("locked"), p.locked);
    writeQmlProperty(osdSlot, QStringLiteral("layoutId"), p.id);
    writeQmlProperty(osdSlot, QStringLiteral("layoutName"), p.name);
    writeQmlProperty(osdSlot, QStringLiteral("screenAspectRatio"), p.screenAspectRatio);
    writeQmlProperty(osdSlot, QStringLiteral("aspectRatioClass"), p.aspectRatioClass);
    writeQmlProperty(osdSlot, QStringLiteral("category"), p.category);
    // Per-layout flag + global "Auto-assign for all layouts" master toggle
    // (#370). CategoryBadge folds them into the effective state. Same
    // convention as buildLayoutsList consumers (selector_update.cpp,
    // snapassist.cpp).
    writeQmlProperty(osdSlot, QStringLiteral("autoAssign"), p.autoAssign);
    writeQmlProperty(osdSlot, QStringLiteral("globalAutoAssign"), p.globalAutoAssign);
    writeAutotileMetadata(osdSlot, p.showMasterDot, p.producesOverlappingZones, p.zoneNumberDisplay, p.masterCount);
    writeQmlProperty(osdSlot, QStringLiteral("zones"), p.zones);
    writeFontProperties(osdSlot, m_settings);
}

void OverlayService::showDisabledOsd(const QString& reason, const QString& screenId)
{
    QQuickWindow* window = nullptr;
    PhosphorLayer::Surface* surface = nullptr;
    QQuickItem* osdSlot = nullptr;
    QScreen* physScreen = nullptr;
    QRect screenGeom;
    qreal aspectRatio = 0;
    if (!prepareLayoutOsdWindow(window, surface, osdSlot, physScreen, screenGeom, aspectRatio, screenId)) {
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
    // Geometry reuse: showDisabledOsd loads LayoutOsdContent, whose
    // container is sized from previewContainer.width (~14·gridUnit) even
    // though the visible content in disabled mode is just the reason text
    // + dialog-cancel icon over the opaque overlay. This is intentional —
    // the disabled OSD shares the same outer dimensions as the layout-OSD
    // it replaces so the user perceives the "switch was blocked" state as
    // a sibling of the regular layout-switch confirmation rather than a
    // distinct, smaller toast. If a future design wants a tighter disabled
    // card, route showDisabledOsd through a dedicated content type rather
    // than a Loader-mode flag on LayoutOsdContent.
    //
    // Cross-mode property note: the host's `success` / `action` /
    // `reason` (NavigationOsd-only) are NOT reset here. NavigationOsdContent
    // is unloaded the moment the mode write below switches to "layout-osd",
    // so its bindings to those properties are gone too. Keep this in mind
    // if a future LayoutOsdContent ever grows a binding that touches
    // navigation-mode properties. See the matching note in
    // PassiveOverlayShell.qml's caveat block.
    LayoutOsdContentParams p;
    p.name = reason; // shown in nameLabel when disabled
    p.screenAspectRatio = aspectRatio;
    pushLayoutOsdContent(osdSlot, p);
    writeQmlProperty(osdSlot, QStringLiteral("disabled"), true);
    writeQmlProperty(osdSlot, QStringLiteral("disabledReason"), reason);
    // shaderBoundsPadding before mode — see showLayoutOsdImpl note.
    {
        namespace PP = PhosphorAnimation::ProfilePaths;
        writeQmlProperty(osdSlot, QStringLiteral("shaderBoundsPadding"),
                         resolveOsdShaderPadding(m_settings, m_animShaderRegistry, PP::OsdShow, PP::OsdHide));
    }
    writeQmlProperty(osdSlot, QStringLiteral("mode"), QStringLiteral("layout-osd"));

    sizeOsdToScreen(window, screenGeom);
    cancelSurfacePrime(surface);
    if (!surface->isLogicallyShown()) {
        surface->show();
    }
    osdSlot->setVisible(true);
    m_surfaceAnimator->beginShow(surface, osdSlot, PzRoles::Osd, []() { });
    // Surface::show() above unconditionally clears Qt::WindowTransparentForInput.
    // OSD slots don't grab input (they auto-dismiss; keeping the input
    // region active for the OSD's lifetime would block clicks on every
    // background window for several seconds). Re-evaluate the input
    // region now that the OSD slot is visible — `syncPassiveShellSurfaceState`
    // counts only modal slots toward `anyInputGrabbing`.
    syncPassiveShellSurfaceStateForSurface(surface);
    QMetaObject::invokeMethod(osdSlot, "restartDismissTimer");
    qCInfo(lcOverlay) << "Disabled OSD: reason=" << reason << "screen=" << screenId;
}

// hideLayoutOsd / hideNavigationOsd (formerly Q_SLOTS connected to a QML
// `dismissed()` signal) are intentionally gone. The Phase-5 dismiss path is:
//   QML dismissTimer → loaded content `dismissRequested()` signal
//     → host PassiveOverlayShell re-emits dismissRequested()
//     → wired by createWarmedOsdSurface to Surface::hide() (string-based)
//     → SurfaceAnimator::beginHide drives the visual fade
//     → Surface::Impl flips Qt::WindowTransparentForInput on the QWindow
// No C++ slot needs to run on dismiss; the QQuickWindow stays Qt-visible
// across the keepMappedOnHide=true lifecycle so the warmed Vulkan
// swapchain survives. Pre-warmed by warmUpNotifications and reused for
// the daemon's lifetime; destroyPassiveShell only fires on
// screen-removal / shutdown.

// Hot-plug hook installed by warmUpNotifications. The single
// m_notificationsWarmed flag gates whether new screens get a per-screen
// passive overlay shell after the initial warm-up.
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
            ensurePassiveShellFor(sid, screen);
        }
    });
    m_screenAddedConnected = true;
}

OverlayService::PerScreenOverlayState* OverlayService::ensurePassiveShellFor(const QString& effectiveId,
                                                                             QScreen* physScreen)
{
    // Library-side lifecycle delegated to ShellHost. The SurfaceFactory /
    // PostCreate callbacks registered in the OverlayService ctor handle
    // the PZ-specific surface creation (role + qmlSource + warmed-surface
    // pipeline) and slot wiring; this shim caches the lib's stable
    // ShellState pointer on the daemon's PerScreenOverlayState so every
    // downstream consumer of `state->shell` reaches the lib's single
    // source of truth.
    auto* shellState = m_shellHost->ensureShell(effectiveId, physScreen);
    if (!shellState) {
        auto it = m_screenStates.find(effectiveId);
        return (it == m_screenStates.end()) ? nullptr : &it.value();
    }
    auto& pzState = m_screenStates[effectiveId];
    pzState.shell = shellState;
    return &pzState;
}

void OverlayService::wirePassiveShellSlots(const QString& screenId, PhosphorOverlay::ShellState& shellState)
{
    // Look up the per-content slot Items by their QML object names —
    // exposed as `osdSlotItem` / `snapAssistSlotItem` / … on the shell
    // window root via QML aliases — and cache them under the daemon's
    // slot-key vocabulary in the lib's generic slot map. Per-show
    // writeQmlProperty / animator beginShow target these Items via
    // the PerScreenOverlayState::xxxSlot() accessors.
    auto* window = shellState.shellWindow;
    if (!window) {
        return;
    }

    auto wireSlot = [&](const QString& slotKey, const char* qmlObjectName, const char* descriptionForLog) {
        auto* item = qvariant_cast<QQuickItem*>(window->property(qmlObjectName));
        if (!item) {
            qCWarning(lcOverlay) << "PassiveOverlayShell on screen=" << screenId << "did not expose `" << qmlObjectName
                                 << "`:" << descriptionForLog << "will fail. Check QML resource.";
        }
        shellState.slots.insert(slotKey, item);
    };

    wireSlot(PzSlotKeys::Osd(), "osdSlotItem", "OSD content writes");
    wireSlot(PzSlotKeys::SnapAssist(), "snapAssistSlotItem", "snap-assist on this screen");
    wireSlot(PzSlotKeys::LayoutPicker(), "layoutPickerSlotItem", "picker on this screen");
    wireSlot(PzSlotKeys::ZoneSelector(), "zoneSelectorSlotItem", "selector on this screen");
    wireSlot(PzSlotKeys::MainOverlay(), "mainOverlaySlotItem", "main overlay on this screen");

    // Wire QML signals → animator-driven slot hide / forward.
    QObject::connect(window, SIGNAL(osdDismissRequested()), this, SLOT(onOsdDismissRequested()));
    QObject::connect(window, SIGNAL(snapAssistDismissRequested()), this, SLOT(onSnapAssistDismissRequested()));
    QObject::connect(window, SIGNAL(snapAssistWindowSelected(QString, QString, QString)), this,
                     SLOT(onSnapAssistWindowSelected(QString, QString, QString)));
    QObject::connect(window, SIGNAL(layoutPickerSelected(QString)), this, SLOT(onLayoutPickerSelected(QString)));
    QObject::connect(window, SIGNAL(layoutPickerDismissRequested()), this, SLOT(onLayoutPickerDismissRequested()));
    QObject::connect(window, SIGNAL(zoneSelectorZoneSelected(QString, int, QVariant)), this,
                     SLOT(onZoneSelected(QString, int, QVariant)));

    // Prime the wl_surface map + Vulkan swapchain init + first-frame
    // render so the very first user-triggered slot show doesn't race
    // the FBO capture used by shader-exclusive transitions.
    primeSurfaceRenderPipeline(shellState.shellSurface);
}

// Pre-create the per-screen passive overlay shell for every effective
// screen the manager knows about, then mark notifications warmed and ensure
// the screenAdded hot-plug hook is installed.
void OverlayService::warmUpNotifications()
{
    const QStringList effectiveIds = (m_screenManager ? m_screenManager->effectiveScreenIds() : QStringList());
    int createdCount = 0;
    for (const QString& sid : effectiveIds) {
        QScreen* physScreen =
            m_screenManager ? m_screenManager->physicalQScreenFor(sid) : Utils::findScreenAtPosition(QPoint(0, 0));
        if (physScreen) {
            auto* state = ensurePassiveShellFor(sid, physScreen);
            if (state && state->shell->shellSurface) {
                ++createdCount;
            }
        }
    }
    m_notificationsWarmed = true;
    qCInfo(lcOverlay) << "Pre-warmed passive overlay shell windows for" << createdCount << "of" << effectiveIds.size()
                      << "effective screens";
    ensureOsdScreenAddedConnected();
}

void OverlayService::destroyPassiveShell(const QString& screenId)
{
    // Library-side teardown delegated to ShellHost::destroyShell. The
    // PreDestroy callback (wirePassiveShellSlots' inverse) clears every
    // PZ-content sentinel on the daemon's PerScreenOverlayState before
    // the lib schedules the shell surface for deletion; the lib then
    // nulls its own ShellState mechanism fields. The shell QQuickWindow
    // owns every slot QQuickItem as a scene-graph descendant so the
    // single deleteLater on the surface cascades through every slot.
    m_shellHost->destroyShell(screenId);
}

void OverlayService::unwirePassiveShellSlots(const QString& screenId)
{
    auto it = m_screenStates.find(screenId);
    if (it == m_screenStates.end()) {
        return;
    }
    // The lib's destroyShell clears ShellState::slots after this hook
    // runs, so no slot-pointer nulling is needed here. We only have to
    // clear the daemon's PZ-content sentinels and disconnect the geom
    // watcher — those are the parallel-state bookkeeping the lib does
    // not know about.
    QObject::disconnect(it->overlayGeomConnection);
    it->overlayGeomConnection = {};
    it->overlayPhysScreen = nullptr;
    it->overlayGeometry = QRect();
    it->labelsTextureHash = 0;
    it->zoneSelectorPhysScreen = nullptr;
    it->zoneSelectorGeometry = QRect();
}

void OverlayService::onOsdDismissRequested()
{
    // QML osdDismissRequested fired — find which screen's shell window
    // emitted, then run an animator-driven slot-hide. Sender-based
    // resolution rather than carrying the screen id through the signal
    // because layer-shell QML signals are parameter-less per the
    // existing project convention (dismissRequested → Surface::hide()
    // wiring lives in createWarmedOsdSurface).
    QObject* senderObj = sender();
    auto* senderWindow = qobject_cast<QQuickWindow*>(senderObj);
    if (!senderWindow) {
        return;
    }
    QString matchedId;
    PerScreenOverlayState* state = nullptr;
    for (auto it = m_screenStates.begin(); it != m_screenStates.end(); ++it) {
        if (it->shell->shellWindow == senderWindow) {
            matchedId = it.key();
            state = &it.value();
            break;
        }
    }
    if (!state || !state->shell->shellSurface || !state->osdSlot()) {
        return;
    }
    auto* slot = state->osdSlot();
    m_surfaceAnimator->beginHide(state->shell->shellSurface, slot, PzRoles::Osd, [this, effectiveId = matchedId]() {
        onOsdSlotHideCompleted(effectiveId);
    });
}

void OverlayService::onOsdSlotHideCompleted(const QString& effectiveId)
{
    // Animator hide leg settled — flip slot.visible=false so the next
    // show's beginShow re-asserts opacity 0 → 1 cleanly.
    auto it = m_screenStates.find(effectiveId);
    if (it == m_screenStates.end() || !it->osdSlot()) {
        return;
    }
    it->osdSlot()->setVisible(false);
    // Clear mode so the Loader unloads — keeps the QML scene tree
    // small between shows and forces a fresh per-show shaderAnchor on
    // the next mode write.
    writeQmlProperty(it->osdSlot(), QStringLiteral("mode"), QString());
    // Symmetric restore: layout/disabled/navigation OSD show paths
    // hid the zone-selector slot to keep it from peeking through the
    // OSD card. The drag may still be active, so re-show the selector
    // for the captured (physScreen, geometry) if those are still
    // valid. snap-assist's onSnapAssistSlotHideCompleted does the
    // analogous restore — keeping the symmetry in lock-step here
    // prevents a stuck-hidden selector after an OSD auto-dismiss
    // that fires mid-drag.
    if (m_zoneSelectorVisible && it->zoneSelectorPhysScreen && it->zoneSelectorGeometry.isValid()) {
        showZoneSelectorSlotOnScreen(effectiveId, it->zoneSelectorPhysScreen, it->zoneSelectorGeometry);
    }
    syncPassiveShellSurfaceState(effectiveId);
}

void OverlayService::syncPassiveShellSurfaceState(const QString& effectiveId)
{
    // Compute PZ-specific visibility predicates from the parallel slot
    // pointers on PerScreenOverlayState, then hand the resulting booleans
    // to the library. The lib decides the mapping + input-region toggle;
    // PZ decides what counts as "live" and "input-grabbing".
    //
    // Input-region rationale (Qt::WindowTransparentForInput): pre-shell-
    // migration each overlay had its own wl_surface sized to its visible
    // content, so clicks outside the toast / card naturally fell through
    // to underlying windows. Post-shell every kbd-None overlay shares the
    // screen-sized shell surface — there's no per-slot input region the
    // daemon can hand to the compositor. The pragmatic split: only MODAL
    // slots (snap-assist, layout picker) grab input. OSD / main overlay /
    // zone-selector are purely visual:
    //   - OSDs auto-dismiss on a timer; a click-to-dismiss MouseArea
    //     inside the OSD content is the accepted casualty — the
    //     alternative is the daemon eating every click on the screen
    //     for the OSD's full lifetime, which the user reported as worse
    //     than losing click-dismiss.
    //   - Main overlay during drag is driven by KWin's drag stream
    //     (cursor pushes via OverlayService::updateMousePosition);
    //     it never needs Qt-level input on its own.
    //   - Zone selector during drag is the same — D-Bus
    //     updateSelectorPosition pushes cursor coords; the zone is
    //     committed via drag-end-on-hovered-zone, not a Qt click.
    auto it = m_screenStates.constFind(effectiveId);
    if (it == m_screenStates.constEnd()) {
        return;
    }
    const auto& s = *it;
    auto isVisible = [](QQuickItem* item) {
        return item != nullptr && item->isVisible();
    };
    // Main overlay slot stays setVisible(true) across drag-pause idles
    // (setIdleForDragPause / applyIdleStateForCursor) so the warm RHI
    // pipeline survives mid-drag trigger thrashing. During those idles
    // the slot's `_idled` property is true and the inner content's
    // `visible: !root._idled` binding makes the rendered subtree invisible
    // — but the slot Item itself stays Qt-visible. Treat `_idled` slots
    // as not visible for the surface-show predicate.
    auto isMainOverlayLive = [](QQuickItem* slot) {
        if (!slot || !slot->isVisible()) {
            return false;
        }
        return !slot->property("_idled").toBool();
    };
    const bool anyVisible = isVisible(s.osdSlot()) || isVisible(s.snapAssistSlot()) || isVisible(s.layoutPickerSlot())
        || isVisible(s.zoneSelectorSlot()) || isMainOverlayLive(s.mainOverlaySlot());
    const bool anyInputGrabbing = isVisible(s.snapAssistSlot()) || isVisible(s.layoutPickerSlot());

    m_shellHost->syncSurfaceState(effectiveId, anyVisible, anyInputGrabbing);
}

void OverlayService::syncPassiveShellSurfaceStateForSurface(PhosphorLayer::Surface* surface)
{
    if (!surface) {
        return;
    }
    for (auto it = m_screenStates.constBegin(); it != m_screenStates.constEnd(); ++it) {
        if (it.value().shell->shellSurface == surface) {
            syncPassiveShellSurfaceState(it.key());
            return;
        }
    }
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

    // Reuse the per-screen passive shell (create only if not in map).
    // The shell stays mapped for the daemon's lifetime; per-show the
    // SurfaceAnimator's beginShow on (shellSurface, osdSlot,
    // PzRoles::Osd) replays the fade-in, and
    // restartDismissTimer extends the auto-hide. Cleanup happens only
    // on screen removal / shutdown via the shell's surface destroy
    // path; the dismiss path is QML → osdDismissRequested → animator
    // beginHide.
    auto* navState = ensurePassiveShellFor(effectiveId, physScreen);
    if (!navState || !navState->shell->shellWindow || !navState->osdSlot()) {
        qCDebug(lcOverlay) << "No passive shell for navigation OSD on screen=" << effectiveId;
        return;
    }

    hideZoneSelectorSlotOnScreen(effectiveId);

    auto* window = navState->shell->shellWindow;
    auto* navSurface = navState->shell->shellSurface;
    auto* osdSlot = navState->osdSlot();

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
    writeQmlProperty(osdSlot, QStringLiteral("success"), success);
    writeQmlProperty(osdSlot, QStringLiteral("action"), action);
    writeQmlProperty(osdSlot, QStringLiteral("reason"), displayReason);
    writeQmlProperty(osdSlot, QStringLiteral("windowCount"), windowCount);

    // Pass source zone ID for swap operations
    writeQmlProperty(osdSlot, QStringLiteral("sourceZoneId"), sourceZoneId);

    // Build highlighted zone IDs list (target zones)
    QStringList highlightedZoneIds;
    if (!targetZoneId.isEmpty()) {
        highlightedZoneIds.append(targetZoneId);
    }
    writeQmlProperty(osdSlot, QStringLiteral("highlightedZoneIds"), highlightedZoneIds);

    // Use shared PhosphorZones::LayoutUtils with minimal fields for zone number lookup
    // (only need zoneId and zoneNumber, not name/appearance)
    QVariantList zonesList =
        PhosphorZones::LayoutUtils::zonesToVariantList(screenLayout, PhosphorZones::ZoneField::Minimal);
    writeQmlProperty(osdSlot, QStringLiteral("zones"), zonesList);

    // shaderBoundsPadding before mode — the Loader instantiates
    // NavigationOsdContent on the mode flip and the inner card's
    // shader-anchor bookkeeping reads this fraction at instantiation.
    // Surface dimensions are screen-sized regardless (see
    // `createWarmedOsdSurface`).
    {
        namespace PP = PhosphorAnimation::ProfilePaths;
        writeQmlProperty(osdSlot, QStringLiteral("shaderBoundsPadding"),
                         resolveOsdShaderPadding(m_settings, m_animShaderRegistry, PP::OsdShow, PP::OsdHide));
    }
    // Write mode AFTER data properties so the Loader-instantiated
    // NavigationOsdContent picks up correct values on first binding pass.
    writeQmlProperty(osdSlot, QStringLiteral("mode"), QStringLiteral("navigation-osd"));

    // Ensure the window is on the correct Wayland output (must come before sizing —
    // assertWindowOnScreen calls setGeometry(screen) which would override setWidth/setHeight).
    assertWindowOnScreen(window, physScreen, navScreenGeom);

    // Window dimensions match the active screen rect; layer-shell anchors +
    // margins were set once at warm-up time (createWarmedOsdSurface).
    sizeOsdToScreen(window, navScreenGeom);

    // Slot-level show: Surface::show() (idempotent) maps the shell
    // wl_surface on first call; thereafter the SurfaceAnimator's
    // beginShow on (shellSurface, osdSlot) drives the visual fade-in
    // via the per-(Surface, target) keying. restartDismissTimer kicks
    // the QML auto-dismiss Timer that emits dismissRequested →
    // animator beginHide on the slot (see osdDismissRequested wiring
    // in ensurePassiveShellFor).
    cancelSurfacePrime(navSurface);
    if (!navSurface->isLogicallyShown()) {
        navSurface->show();
    }
    osdSlot->setVisible(true);
    m_surfaceAnimator->beginShow(navSurface, osdSlot, PzRoles::Osd, []() { });
    // OSDs don't grab input — see the matching syncPassiveShellSurfaceStateForSurface
    // call in showLayoutOsdImpl for the rationale.
    syncPassiveShellSurfaceStateForSurface(navSurface);
    QMetaObject::invokeMethod(osdSlot, "restartDismissTimer");

    // Update dedup state AFTER the Surface::show() + restartDismissTimer
    // dispatch. Every early-return above this point is a "no OSD shown"
    // outcome that must not poison the next call's dedup window — keeping
    // the writes here means a bail-out (no physScreen, no notification
    // window, no layout/zones, etc.) leaves dedup state untouched and the
    // next legitimate call goes through. Surface::show() itself is `void`,
    // so a silent animator no-op after this point would still poison
    // dedup; that's an animator-layer concern, not something the ordering
    // here can guard against. Stored as effectiveId to match the dedup
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
// pair is gone post-Phase-2: navigation OSDs share the per-screen passive
// overlay shell created by ensurePassiveShellFor above, so a single
// create/destroy pair serves both OSD modes.

} // namespace PlasmaZones
