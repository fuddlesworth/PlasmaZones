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
#include "phosphor_roles.h"
#include "phosphor_slot_keys.h"
#include "qml_property_names.h"
#include <PhosphorScreens/ScreenIdentity.h>

#include <PhosphorAnimation/SurfaceAnimator.h>

#include <PhosphorSurface/DecorationProfile.h>
#include <PhosphorSurface/DecorationProfileTree.h>
#include <PhosphorSurface/SurfaceShaderEffect.h>
#include <PhosphorSurface/SurfaceShaderRegistry.h>

#include "../../core/isettings.h"

#include <QUrl>

namespace PlasmaZones {

namespace {

// Size the OSD window to its target screen rect. The wl_surface is now
// screen-sized (mirrors zone-selector / snap-assist) - anchors and
// margins were set once at warm-up time by `createWarmedOsdSurface` from
// the same VS-aware placement vocabulary popups use, so this per-show
// path only ever has to update the window dimensions when the active
// screen rect changes (e.g. monitor hot-plug between shows). The QML
// inside positions the visible card centred via `anchors.centerIn:
// parent`, so the OSD looks identical on screen - the surface is just
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

    QString effectiveId = screenId.isEmpty() ? PhosphorScreens::ScreenIdentity::identifierFor(physScreen) : screenId;

    auto* state = ensurePassiveShellFor(effectiveId, physScreen);
    if (!state || !state->shell || !state->shell->shellWindow() || !state->shell->shellSurface() || !state->osdSlot()) {
        qCWarning(lcOverlay) << "Failed to get passive shell for layout OSD on screen=" << effectiveId;
        return false;
    }

    // Force-hide any zone selector on this screen so a fading-out
    // selector doesn't stack translucently behind the incoming OSD.
    // Slot-level animator hide; the shell surface stays Shown for the
    // OSD that follows.
    hideZoneSelectorSlotOnScreen(effectiveId);

    window = state->shell->shellWindow();
    outSurface = state->shell->shellSurface();
    outOsdSlot = state->osdSlot();

    // Mode is NOT written here - callers write data properties first, then
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

    // Pass the actual screen geometry so fixed-mode zones normalize against
    // the screen we're about to render on, not Layout::lastRecalcGeometry()
    // (which may belong to a different screen).
    LayoutOsdContentParams p;
    p.id = layout->id().toString();
    p.name = layout->name();
    p.zones = layout->zones().isEmpty()
        ? QVariantList()
        : PhosphorZones::LayoutUtils::zonesToVariantList(layout, PhosphorZones::ZoneField::Full, QRectF(screenGeom));
    p.category = static_cast<int>(PhosphorZones::LayoutCategory::Manual);
    p.autoAssign = layout->autoAssign();
    p.globalAutoAssign = m_settings && m_settings->autoAssignAllLayouts();
    p.locked = locked;
    p.screenAspectRatio = aspectRatio;
    p.aspectRatioClass = PhosphorLayout::ScreenClassification::toString(layout->aspectRatioClass());
    pushLayoutOsdContent(osdSlot, p);
    writeQmlProperty(osdSlot, QStringLiteral("mode"), QStringLiteral("layout-osd"));

    sizeOsdToScreen(window, screenGeom);
    // Disarm the render-pipeline prime first so its queued hide doesn't
    // race this real show - see primeSurfaceRenderPipeline.
    cancelSurfacePrime(surface);
    // Shell wl_surface is permanently mapped - only the slot's opacity
    // animates. Map the wl_surface on first show via Surface::show()
    // (idempotent on subsequent shows; the keepMappedOnHide=true config
    // means the wl_surface stays mapped between slot animations).
    if (!surface->isLogicallyShown()) {
        surface->show();
    }
    osdSlot->setVisible(true);
    m_surfaceAnimator->beginShow(surface, osdSlot, PhosphorRoles::Osd, []() { });
    // Surface::show() above unconditionally clears Qt::WindowTransparentForInput.
    // OSD slots don't grab input (they auto-dismiss; keeping the input
    // region active for the OSD's lifetime would block clicks on every
    // background window for several seconds). Re-evaluate the input
    // region now that the OSD slot is visible - `syncPassiveShellSurfaceState`
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
    // of the exact screen aspect - preserves the layout author's intent.
    //
    // Autotile algorithms (non-UUID id like "autotile:rows") have no intrinsic
    // class. Classify the screen's actual aspect ratio and use that class so
    // the preview snaps to the same canonical ratio the comparable snap-layout
    // OSD would render at on this screen. Without this, autotile previews
    // showed the raw screen aspect (e.g. 0.93 for a 1600×1716 VS, nearly
    // square) while snap layouts on the same screen rendered at 9:16 - a
    // visibly inconsistent feel between the two OSD paths.
    //
    // The class is the only AR information C++ needs to push - QML derives
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
    writeQmlProperty(osdSlot, QStringLiteral("mode"), QStringLiteral("layout-osd"));

    sizeOsdToScreen(window, screenGeom);
    cancelSurfacePrime(surface);
    if (!surface->isLogicallyShown()) {
        surface->show();
    }
    osdSlot->setVisible(true);
    m_surfaceAnimator->beginShow(surface, osdSlot, PhosphorRoles::Osd, []() { });
    // Surface::show() above unconditionally clears Qt::WindowTransparentForInput.
    // OSD slots don't grab input (they auto-dismiss; keeping the input
    // region active for the OSD's lifetime would block clicks on every
    // background window for several seconds). Re-evaluate the input
    // region now that the OSD slot is visible - `syncPassiveShellSurfaceState`
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
    // Reset overlay-state flags first - the OSD slot Item is reused
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
    // Stage d: resolve + push the OSD's surface-shader decoration (rounded
    // corners + border) onto the slot. Done here so every layout-OSD show path
    // (showLayoutOsdImpl / showLayoutOsd(string…) / showDisabledOsd) decorates
    // consistently; showNavigationOsd calls applyDecoration directly since it
    // does not route through pushLayoutOsdContent.
    applyDecoration(osdSlot, QStringLiteral("osd"));
}

void OverlayService::setSurfaceShaderRegistry(PhosphorSurfaceShaders::SurfaceShaderRegistry* registry)
{
    m_surfaceShaderRegistry = registry;
}

void OverlayService::applyDecoration(QObject* slot, const QString& surfacePath)
{
    if (!slot) {
        return;
    }

    // Helper to leave the slot undecorated: clear the chain so the QML
    // SurfaceDecoration stays inert and the card draws its native chrome.
    const auto clearDecoration = [this, slot]() {
        writeQmlProperty(slot, QStringLiteral("decorationChain"), QVariant::fromValue(QVariantList()));
        writeQmlProperty(slot, QStringLiteral("decorationOuterPadding"), 0.0);
        // No decoration -> no audio need on this slot; let CAVA wind down if it
        // was only kept alive for an audio decoration here.
        if (auto* item = qobject_cast<QQuickItem*>(slot)) {
            item->setProperty(OverlayQmlPropertyNames::WantsAudioDecoration.data(), false);
        }
        syncCavaState();
    };

    if (!m_settings || !m_surfaceShaderRegistry) {
        clearDecoration();
        return;
    }

    // Resolve @p surfacePath through the decoration tree. resolve() walks
    // baseline → category → leaf and returns a DecorationProfile carrying an
    // effective CHAIN (ordered pack ids) plus a per-pack parameters map.
    const PhosphorSurfaceShaders::DecorationProfileTree tree = m_settings->decorationProfileTree();
    const PhosphorSurfaceShaders::DecorationProfile profile = tree.resolve(surfacePath);
    // enabledChain(): a pack the user toggled off must not render here either.
    const QStringList chain = profile.enabledChain();
    if (chain.isEmpty()) {
        // No decoration packs configured for this surface — render it plainly.
        clearDecoration();
        return;
    }

    // The daemon composes the FULL chain: the QML SurfaceDecoration host runs
    // one SurfaceShaderItem per stage, each sampling the previous stage's
    // output through an interposed ShaderEffectSource — the QML analogue of
    // the compositor's composite ping-pong (renderSurfaceChainComposite), so
    // a border + glow chain renders both packs here too. Buffer passes
    // (multipass packs like the blur family) still degrade to single-pass on
    // this host; needsBackdrop packs have no scene to sample on the daemon
    // and take their documented uHasBackdrop = 0 fallback regardless.
    //
    // Per-pack parameter overrides come from the resolved profile (shape
    // { packId -> { paramId -> value } }). p_useSystemAccent is a
    // host-consumed flag; the overlay path passes the pack's declared colour
    // params through translateSurfaceParams unchanged (system-accent colour
    // resolution is performed by the daemon's colour pipeline, not
    // synthesised here). Each stage's vertexSource satisfies the warm-bake
    // HOST-WIRING PRECONDITION (daemon.cpp): a pack declaring its own vertex
    // stage keys the same vert here as the warm bake; the empty-URL case
    // (every current pack) falls through to the item's shared-surface.vert
    // resolution.
    const QVariantMap allPackParams = profile.effectiveParameters();
    QVariantList stages;
    double outerPadding = 0.0;
    bool chainWantsAudio = false;
    for (const QString& packId : chain) {
        if (!m_surfaceShaderRegistry->hasEffect(packId)) {
            qCWarning(lcOverlay) << "Surface decoration (" << surfacePath << "): resolved pack id" << packId
                                 << "is not present in the surface-shader registry — skipping this chain stage";
            continue;
        }
        const PhosphorSurfaceShaders::SurfaceShaderEffect effect = m_surfaceShaderRegistry->effect(packId);
        // isValid() already requires a non-empty fragmentShaderPath.
        if (!effect.isValid()) {
            qCWarning(lcOverlay) << "Surface decoration (" << surfacePath << "): pack" << packId
                                 << "has no valid fragment shader — skipping this chain stage";
            continue;
        }
        // Audio-reactive pack in the chain -> this decoration slot wants the
        // live CAVA spectrum (gated below so a plain border never starts audio).
        chainWantsAudio = chainWantsAudio || effect.audio;
        const QVariantMap friendlyParams = allPackParams.value(packId).toMap();

        // Outer-margin request (the pack's declared paddingParam, e.g. glow's
        // glowSize): the per-surface override wins, else the param's declared
        // default — the same resolution the compositor's updateWindowDecoration
        // applies, with the chain's LARGEST request padding the shared canvas.
        // The QML host inflates the capture + shader items by this logical-px
        // margin so an outer effect gets real transparent room; 0 (a
        // margin-less chain) keeps the classic 1:1 geometry.
        if (!effect.paddingParam.isEmpty()) {
            double request = 0.0;
            if (friendlyParams.contains(effect.paddingParam)) {
                request = friendlyParams.value(effect.paddingParam).toDouble();
            } else {
                for (const auto& param : effect.parameters) {
                    if (param.id == effect.paddingParam) {
                        request = param.defaultValue.toDouble();
                        break;
                    }
                }
            }
            outerPadding = qMax(outerPadding, request);
        }

        QVariantMap stageMap;
        stageMap.insert(QStringLiteral("source"), QUrl::fromLocalFile(effect.fragmentShaderPath));
        stageMap.insert(QStringLiteral("vertexSource"),
                        effect.vertexShaderPath.isEmpty() ? QUrl() : QUrl::fromLocalFile(effect.vertexShaderPath));
        stageMap.insert(QStringLiteral("preamble"),
                        PhosphorSurfaceShaders::SurfaceShaderRegistry::paramPreamble(effect));
        stageMap.insert(QStringLiteral("params"),
                        m_surfaceShaderRegistry->translateSurfaceParams(packId, friendlyParams));
        // Animated packs declare it in metadata; the QML host gates that
        // stage's per-frame iTime tick (playing) on this so static packs pay
        // nothing.
        stageMap.insert(QStringLiteral("animated"), effect.animated);
        stages.append(stageMap);
    }
    if (stages.isEmpty()) {
        clearDecoration();
        return;
    }
    // Same defensive cap as the compositor's wb.outerPadding.
    outerPadding = qBound(0.0, outerPadding, 128.0);

    // Padding BEFORE the chain: the chain write is the load trigger, and the
    // single list write hands every stage's source + params to QML atomically,
    // so no stage ever bakes against a half-written sibling (the old
    // per-property protocol needed a clear-first + source-last dance for the
    // same guarantee).
    writeQmlProperty(slot, QStringLiteral("decorationOuterPadding"), outerPadding);
    writeQmlProperty(slot, QStringLiteral("decorationChain"), QVariant::fromValue(stages));

    // Record whether this slot now carries an audio-reactive pack, then reconcile
    // CAVA: a newly-decorated audio surface may need audio capture started, or a
    // change from audio to non-audio may let it wind down.
    if (auto* item = qobject_cast<QQuickItem*>(slot)) {
        item->setProperty(OverlayQmlPropertyNames::WantsAudioDecoration.data(), chainWantsAudio);
        // Decoration is often applied while the slot is still hidden (popups
        // apply-then-show), so re-run syncCavaState whenever it shows/hides —
        // that starts CAVA once an audio surface becomes visible and stops it on
        // hide. UniqueConnection keeps re-decoration from stacking duplicates.
        connect(item, &QQuickItem::visibleChanged, this, &OverlayService::syncCavaState, Qt::UniqueConnection);
    }
    syncCavaState();
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
    // shared layout-OSD properties - but we still push empty/zero values
    // into the layout-OSD slot via the shared content writer so stale
    // data from a prior showLayoutOsd doesn't leak through the
    // semi-transparent disabled overlay (CategoryBadge / Label both bind
    // to those properties even when disabled is true).
    //
    // Geometry reuse: showDisabledOsd loads LayoutOsdContent, whose
    // container is sized from previewContainer.width (~14·gridUnit) even
    // though the visible content in disabled mode is just the reason text
    // + dialog-cancel icon over the opaque overlay. This is intentional -
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
    writeQmlProperty(osdSlot, QStringLiteral("mode"), QStringLiteral("layout-osd"));

    sizeOsdToScreen(window, screenGeom);
    cancelSurfacePrime(surface);
    if (!surface->isLogicallyShown()) {
        surface->show();
    }
    osdSlot->setVisible(true);
    m_surfaceAnimator->beginShow(surface, osdSlot, PhosphorRoles::Osd, []() { });
    // Surface::show() above unconditionally clears Qt::WindowTransparentForInput.
    // OSD slots don't grab input (they auto-dismiss; keeping the input
    // region active for the OSD's lifetime would block clicks on every
    // background window for several seconds). Re-evaluate the input
    // region now that the OSD slot is visible - `syncPassiveShellSurfaceState`
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
// ensureOsdScreenAddedConnected / ensurePassiveShellFor / wirePassiveShellSlots /
// warmUpNotifications / destroyPassiveShell / unwirePassiveShellSlots are
// extracted to overlayservice/shellhost_bridge.cpp - they're the daemon's
// bridge to PhosphorOverlay::ShellHost, not OSD-specific. Keeps this TU
// under the project's <800-line guideline.

void OverlayService::onOsdDismissRequested()
{
    // QML osdDismissRequested fired - find which screen's shell window
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
        if (it->shell && it->shell->shellWindow() == senderWindow) {
            matchedId = it.key();
            state = &it.value();
            break;
        }
    }
    if (!state || !state->shell || !state->shell->shellSurface() || !state->osdSlot()) {
        return;
    }
    m_shellHost->hideSlot(matchedId, PhosphorSlotKeys::Osd(), [this, effectiveId = matchedId]() {
        onOsdSlotHideCompleted(effectiveId);
    });
}

void OverlayService::onOsdSlotHideCompleted(const QString& effectiveId)
{
    // Animator hide leg settled - flip slot.visible=false so the next
    // show's beginShow re-asserts opacity 0 → 1 cleanly.
    auto it = m_screenStates.find(effectiveId);
    if (it == m_screenStates.end() || !it->osdSlot()) {
        return;
    }
    it->osdSlot()->setVisible(false);
    // Clear mode so the Loader unloads - keeps the QML scene tree
    // small between shows and forces a fresh per-show shaderAnchor on
    // the next mode write.
    writeQmlProperty(it->osdSlot(), QStringLiteral("mode"), QString());
    // Symmetric restore: layout/disabled/navigation OSD show paths
    // hid the zone-selector slot to keep it from peeking through the
    // OSD card. snap-assist's onSnapAssistSlotHideCompleted does the
    // analogous restore - keeping the symmetry in lock-step here
    // prevents a stuck-hidden selector after an OSD auto-dismiss
    // that fires mid-drag.
    restoreZoneSelectorAfterHide(effectiveId);
    syncPassiveShellSurfaceState(effectiveId);
}

// syncPassiveShellSurfaceState / syncPassiveShellSurfaceStateForSurface
// are extracted to overlayservice/shellhost_bridge.cpp - they translate
// PZ-content slot visibility into the booleans ShellHost::syncSurfaceState
// expects, conceptually part of the shell-host bridge rather than the
// OSD pipeline.

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

    QString effectiveId = screenId.isEmpty() ? PhosphorScreens::ScreenIdentity::identifierFor(physScreen) : screenId;

    // Deduplicate: Skip if same action+reason+screen within 200ms (prevents duplicate from Qt signal + D-Bus signal).
    // Keyed on effectiveId (resolved from physScreen if the caller passed an
    // empty screenId) so two rapid calls with empty screenId on different
    // physical screens don't dedup against each other, and so the hot-plug
    // clear in destroyAllWindowsForPhysicalScreen - which prefix-matches on
    // the physical id - can clear stale dedup state on screen replug.
    //
    // The dedup state is updated only after we've decided to actually
    // show - see the matching m_lastNavigation* writes below near
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
    // PhosphorRoles::Osd) replays the fade-in, and
    // restartDismissTimer extends the auto-hide. Cleanup happens only
    // on screen removal / shutdown via the shell's surface destroy
    // path; the dismiss path is QML → osdDismissRequested → animator
    // beginHide.
    auto* navState = ensurePassiveShellFor(effectiveId, physScreen);
    if (!navState || !navState->shell || !navState->shell->shellWindow() || !navState->shell->shellSurface()
        || !navState->osdSlot()) {
        qCDebug(lcOverlay) << "No passive shell for navigation OSD on screen=" << effectiveId;
        return;
    }

    hideZoneSelectorSlotOnScreen(effectiveId);

    auto* window = navState->shell->shellWindow();
    auto* navSurface = navState->shell->shellSurface();
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

    // Use shared PhosphorZones::LayoutUtils with minimal fields for zone number
    // lookup (only need zoneId and zoneNumber, not name/appearance). Pass
    // navScreenGeom so fixed-mode zones normalize against the navigated-to
    // screen rather than Layout::lastRecalcGeometry().
    QVariantList zonesList = PhosphorZones::LayoutUtils::zonesToVariantList(
        screenLayout, PhosphorZones::ZoneField::Minimal, QRectF(navScreenGeom));
    writeQmlProperty(osdSlot, QStringLiteral("zones"), zonesList);

    // Stage d: resolve + push the OSD surface decoration. Navigation OSDs do
    // not route through pushLayoutOsdContent, so apply it explicitly here (same
    // decoration the layout-OSD paths get via pushLayoutOsdContent).
    applyDecoration(osdSlot, QStringLiteral("osd"));

    // Write mode AFTER data properties so the Loader-instantiated
    // NavigationOsdContent picks up correct values on first binding pass.
    writeQmlProperty(osdSlot, QStringLiteral("mode"), QStringLiteral("navigation-osd"));

    // Ensure the window is on the correct Wayland output (must come before sizing -
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
    m_surfaceAnimator->beginShow(navSurface, osdSlot, PhosphorRoles::Osd, []() { });
    // OSDs don't grab input - see the matching syncPassiveShellSurfaceStateForSurface
    // call in showLayoutOsdImpl for the rationale.
    syncPassiveShellSurfaceStateForSurface(navSurface);
    QMetaObject::invokeMethod(osdSlot, "restartDismissTimer");

    // Update dedup state AFTER the Surface::show() + restartDismissTimer
    // dispatch. Every early-return above this point is a "no OSD shown"
    // outcome that must not poison the next call's dedup window - keeping
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

// hideNavigationOsd removed together with hideLayoutOsd - see the comment
// block above warmUpNotifications() for the rationale. The m_lastNavigation*
// dedup state is cleared implicitly by the 200 ms timeout check in
// showNavigationOsd() itself (the OSD's ~1000 ms dismiss timer is far
// longer than the dedup window, so any dismiss is always past the
// relevant timeout by the time it fires - no manual clear needed).
//
// The previous per-mode createNavigationOsdWindow / destroyNavigationOsdWindow
// pair is gone post-Phase-2: navigation OSDs share the per-screen passive
// overlay shell created by ensurePassiveShellFor above, so a single
// create/destroy pair serves both OSD modes.

} // namespace PlasmaZones
