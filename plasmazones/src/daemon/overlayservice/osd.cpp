// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "internal.h"
#include "daemon/overlayservice.h"
#include "core/interfaces/isettings.h"
#include "core/platform/logging.h"
#include "core/utils/utils.h"
#include "phosphor_roles.h"
#include "phosphor_slot_keys.h"
#include "qml_property_names.h"

#include <PhosphorAnimation/SurfaceAnimator.h>
#include <PhosphorLayer/ILayerShellTransport.h>
#include <PhosphorLayer/Surface.h>
#include <PhosphorOverlay/ShellHost.h>
#include <PhosphorScreens/Manager.h>
#include <PhosphorScreens/ScreenIdentity.h>
#include <PhosphorSurface/DecorationSupportedPaths.h>
#include <PhosphorSurfaces/SurfaceManager.h>
#include <PhosphorZones/Layout.h>
#include <PhosphorZones/LayoutUtils.h>

#include <QQuickWindow>
#include <QScreen>
#include <QSet>

#include <optional>

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

std::optional<PreparedLayoutOsdWindow> OverlayService::prepareLayoutOsdWindow(const QString& screenId)
{
    // Resolve target screen using shared helper (handles virtual IDs, fallback chain)
    QScreen* physScreen = resolveTargetScreen(m_screenManager, screenId);
    if (!physScreen) {
        qCWarning(lcOverlay) << "No screen available for layout OSD";
        return std::nullopt;
    }

    PreparedLayoutOsdWindow prep;

    // Use virtual screen geometry if applicable, otherwise physical
    prep.screenGeom = resolveScreenGeometry(m_screenManager, screenId);
    if (!prep.screenGeom.isValid()) {
        prep.screenGeom = physScreen->geometry();
    }

    prep.effectiveScreenId = screenId.isEmpty() ? PhosphorScreens::ScreenIdentity::identifierFor(physScreen) : screenId;

    auto* state = ensurePassiveShellFor(prep.effectiveScreenId, physScreen);
    if (!state || !state->shell || !state->shell->shellWindow() || !state->shell->shellSurface() || !state->osdSlot()) {
        qCWarning(lcOverlay) << "Failed to get passive shell for layout OSD on screen=" << prep.effectiveScreenId;
        return std::nullopt;
    }

    prep.window = state->shell->shellWindow();
    prep.surface = state->shell->shellSurface();
    prep.osdSlot = state->osdSlot();

    // Mode is NOT written here - callers write data properties first, then
    // set mode. This ensures the Loader's freshly instantiated content
    // component picks up the correct root property values via its bindings
    // on the very first frame, instead of briefly seeing defaults/stale
    // values from the previous show (or from QML property initialisers on
    // the first-ever show).

    assertWindowOnScreen(prep.window, physScreen, prep.screenGeom);

    prep.aspectRatio = (prep.screenGeom.height() > 0)
        ? static_cast<qreal>(prep.screenGeom.width()) / prep.screenGeom.height()
        : (16.0 / 9.0);
    prep.aspectRatio = qBound(0.5, prep.aspectRatio, 4.0);

    return prep;
}

void OverlayService::finishOsdShow(QQuickWindow* window, PhosphorLayer::Surface* surface, QQuickItem* osdSlot,
                                   const QRect& screenGeom, const QString& effectiveScreenId)
{
    // Force-hide any zone selector on this screen so a fading-out selector doesn't
    // stack translucently behind the incoming OSD. Slot-level animator hide; the
    // shell surface stays Shown for the OSD that follows.
    //
    // Deliberately here rather than in prepareLayoutOsdWindow. Most callers refuse
    // before they prepare, but showNavigationOsd refuses AFTER (its dedup and
    // needsLayout checks both follow the prepare), so that one path could prepare
    // and then bail. A path that did hid
    // the selector with nothing to bring it back: the restore runs off a sibling
    // slot's hide completion, and no OSD had been shown to complete one, while
    // showZoneSelector early-returns on the still-set visible flag. The slot then
    // stayed hidden for the rest of the drag.
    hideZoneSelectorSlotOnScreen(effectiveScreenId);
    sizeOsdToScreen(window, screenGeom);
    // Disarm the render-pipeline prime first so its queued hide doesn't
    // race this real show - see primeSurfaceRenderPipeline.
    cancelSurfacePrime(surface);
    // Only the slot's opacity animates. Map the wl_surface via
    // Surface::show() (idempotent on subsequent shows; keepMappedOnHide
    // is effects-gated - see createWarmedOsdSurface - so the wl_surface
    // stays mapped between slot animations while shaders or animations
    // are enabled, and unmaps between shows otherwise).
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
    const auto prep = prepareLayoutOsdWindow(screenId);
    if (!prep) {
        return;
    }
    QQuickWindow* const window = prep->window;
    PhosphorLayer::Surface* const surface = prep->surface;
    QQuickItem* const osdSlot = prep->osdSlot;
    const QRect screenGeom = prep->screenGeom;
    const qreal aspectRatio = prep->aspectRatio;
    const QString effectiveScreenId = prep->effectiveScreenId;

    // Pass the actual screen geometry so fixed-mode zones normalize against
    // the screen we're about to render on, not Layout::lastRecalcGeometry()
    // (which may belong to a different screen).
    LayoutOsdContentParams p;
    p.screenId = effectiveScreenId;
    p.id = layout->id().toString();
    p.name = layout->name();
    p.zones = layout->zones().isEmpty()
        ? QVariantList()
        : PhosphorZones::LayoutUtils::zonesToVariantList(layout, PhosphorZones::ZoneField::Full, QRectF(screenGeom));
    // Since the native-template pivot this path serves NON-template screens
    // only: a template apply carries no Layout* and routes to
    // showScrollingTemplateOsd instead, and every producer of this overload
    // gates on the context's mode not being Scrolling (applyEntry refuses a
    // manual layout on a Templates screen, the lock/unlock and KCM-apply
    // sites take their template arm first). Category is therefore Manual in
    // practice.
    //
    // The live-capability read below is defence in depth for that invariant.
    // The Auto badge advertises snap-to-empty-zone, whose ONLY behavioural
    // consumer is the snap engine, so if a Templates screen ever does reach
    // here the badge, the category and the caption are corrected rather than
    // left claiming a behaviour the screen cannot perform. Category and
    // isTemplate move together — a "Manual" badge beside a "Column template"
    // caption is a contradiction on one card.
    // Polarity is deliberate and DIFFERS from the `!resolver ||` sibling
    // sites in overlayservice.cpp: those sit inside an isScrolling(assignment)
    // arm where the raw read already says scrolling, so missing-resolver
    // defaults to trusting it. Here there is NO assignment gate — the
    // resolver alone decides — and `!resolver ||` would classify EVERY
    // screen, snapping included, as Templates.
    const bool templatesScreen =
        m_layoutSupportResolver && m_layoutSupportResolver(effectiveScreenId) == LayoutSupportTemplates;
    p.category = static_cast<int>(templatesScreen ? PhosphorZones::LayoutCategory::ScrollingTemplate
                                                  : PhosphorZones::LayoutCategory::Manual);
    p.autoAssign = !templatesScreen && layout->autoAssign();
    p.globalAutoAssign = !templatesScreen && m_settings && m_settings->autoAssignAllLayouts();
    p.isTemplate = templatesScreen;
    p.locked = locked;
    p.screenAspectRatio = aspectRatio;
    p.aspectRatioClass = PhosphorLayout::ScreenClassification::toString(layout->aspectRatioClass());
    pushLayoutOsdContent(osdSlot, p);
    writeQmlProperty(osdSlot, QStringLiteral("mode"), QStringLiteral("layout-osd"));

    finishOsdShow(window, surface, osdSlot, screenGeom, effectiveScreenId);
    qCInfo(lcOverlay) << (locked ? "Locked" : "Layout") << "OSD: layout=" << layout->name() << "screen=" << screenId;
}

void OverlayService::showScrollingTemplateOsd(const QString& id, const QString& name, const QVariantList& zones,
                                              bool verticalAxis, const QString& screenId, bool locked)
{
    // The native-template twin of showLayoutOsdImpl: no Layout* backs a
    // ScrollingTemplate, so the caller supplies the id, name and the
    // blueprint-derived preview zones. Always captioned as a template; the
    // Auto badge never applies (snap-to-empty-zone cannot happen on a
    // Templates screen).
    //
    // Empty-preview bail matching showLayoutOsdImpl: a card with no columns
    // to draw is a blank rectangle. The locked variant still shows, because
    // there the lock badge is the message and the preview is decoration.
    if (!locked && zones.isEmpty()) {
        qCDebug(lcOverlay) << "Skipping OSD for empty template=" << name;
        return;
    }
    const auto prep = prepareLayoutOsdWindow(screenId);
    if (!prep) {
        return;
    }
    QQuickWindow* const window = prep->window;
    PhosphorLayer::Surface* const surface = prep->surface;
    QQuickItem* const osdSlot = prep->osdSlot;
    const QRect screenGeom = prep->screenGeom;
    const qreal aspectRatio = prep->aspectRatio;
    const QString effectiveScreenId = prep->effectiveScreenId;

    LayoutOsdContentParams p;
    p.screenId = effectiveScreenId;
    p.id = id;
    p.name = name;
    p.zones = zones;
    // Category 2, matching what the wire serializer stamps for a template
    // card: the QML badge arm keys on this, and Manual(0) made the OSD
    // announce a native template as a zone layout.
    p.category = static_cast<int>(PhosphorZones::LayoutCategory::ScrollingTemplate);
    p.autoAssign = false;
    p.globalAutoAssign = false;
    p.isTemplate = true;
    p.locked = locked;
    p.screenAspectRatio = aspectRatio;
    // A template has no authored aspect class; classify the live screen so
    // the preview renders at the same canonical ratio the sibling OSD paths
    // use (see the autotile rationale in the string overload below).
    p.aspectRatioClass =
        PhosphorLayout::ScreenClassification::toString(PhosphorLayout::ScreenClassification::classify(aspectRatio));
    // Same axis the caller laid the bands along, so this card carries the edge
    // ticks the picker's template cards do. Without it the same template is
    // chevroned in the picker and bare here.
    p.stripAxisHint = verticalAxis ? QStringLiteral("vertical") : QStringLiteral("horizontal");
    pushLayoutOsdContent(osdSlot, p);
    writeQmlProperty(osdSlot, QStringLiteral("mode"), QStringLiteral("layout-osd"));

    finishOsdShow(window, surface, osdSlot, screenGeom, effectiveScreenId);
    qCInfo(lcOverlay) << (locked ? "Locked template" : "Template") << "OSD: template=" << name << "screen=" << screenId
                      << "vertical=" << verticalAxis;
}

void OverlayService::showScrollingStripOsd(const QString& name, const QVariantList& zones, bool verticalAxis,
                                           const QString& emptyCaption, const QString& screenId)
{
    // NO empty-zones bail, unlike every sibling above. An empty strip is a
    // state worth showing, and the card shows it as an axis arrow plus the
    // caller's caption. What would be wrong is showing it as an invented
    // three-column sketch, which is what this path used to do.
    //
    // What IS refused is empty zones with no caption: the QML gates the empty
    // state on the caption, so that pair renders bare edge ticks over a blank
    // well — the "failed to load" picture the empty state exists to replace.
    // Enforcing it here rather than trusting the doc comment, because the
    // renderer has no way to invent the reason the strip is blank.
    if (zones.isEmpty() && emptyCaption.isEmpty()) {
        qCWarning(lcOverlay) << "Refusing strip OSD with no zones and no caption: screen=" << screenId;
        return;
    }
    const auto prep = prepareLayoutOsdWindow(screenId);
    if (!prep) {
        return;
    }
    QQuickWindow* const window = prep->window;
    PhosphorLayer::Surface* const surface = prep->surface;
    QQuickItem* const osdSlot = prep->osdSlot;
    const QRect screenGeom = prep->screenGeom;
    const qreal aspectRatio = prep->aspectRatio;

    LayoutOsdContentParams p;
    p.screenId = prep->effectiveScreenId;
    // No id, deliberately, matching the settings thumbnail's throwaway card
    // literal: a live strip snapshot has no persisted identity, and a stable
    // id only invites code elsewhere to treat it as a real layout.
    p.name = name;
    p.zones = zones;
    // Autotile: the renderer reads it as "generated, not editable", which is
    // exactly what a live strip snapshot is.
    p.category = static_cast<int>(PhosphorZones::LayoutCategory::Autotile);
    p.autoAssign = false;
    p.globalAutoAssign = false;
    p.locked = false;
    p.screenAspectRatio = aspectRatio;
    p.aspectRatioClass =
        PhosphorLayout::ScreenClassification::toString(PhosphorLayout::ScreenClassification::classify(aspectRatio));
    p.stripAxisHint = verticalAxis ? QStringLiteral("vertical") : QStringLiteral("horizontal");
    // Only meaningful with no zones; pushed as empty otherwise so the QML
    // gate is the caption itself rather than a second flag that could
    // disagree with it.
    p.stripEmptyCaption = zones.isEmpty() ? emptyCaption : QString();
    pushLayoutOsdContent(osdSlot, p);
    writeQmlProperty(osdSlot, QStringLiteral("mode"), QStringLiteral("layout-osd"));

    finishOsdShow(window, surface, osdSlot, screenGeom, prep->effectiveScreenId);
    qCInfo(lcOverlay) << "Scrolling strip OSD: screen=" << screenId << "zones=" << zones.size()
                      << "vertical=" << verticalAxis;
}

void OverlayService::showLayoutOsd(const QString& id, const QString& name, const QVariantList& zones, int category,
                                   bool autoAssign, const QString& screenId, bool showMasterDot,
                                   bool producesOverlappingZones, const QString& zoneNumberDisplay, int masterCount)
{
    if (zones.isEmpty()) {
        qCDebug(lcOverlay) << "Skipping OSD for empty layout=" << name;
        return;
    }

    const auto prep = prepareLayoutOsdWindow(screenId);
    if (!prep) {
        return;
    }
    QQuickWindow* const window = prep->window;
    PhosphorLayer::Surface* const surface = prep->surface;
    QQuickItem* const osdSlot = prep->osdSlot;
    const QRect screenGeom = prep->screenGeom;
    const qreal aspectRatio = prep->aspectRatio;
    const QString effectiveScreenId = prep->effectiveScreenId;

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
    // A UUID whose layout the manager cannot find falls through to the CLASSIFY
    // branch rather than keeping "any". Leaving it at "any" resolved to the raw
    // screen aspect in QML, which is exactly the inconsistency the classify arm
    // exists to prevent, and the sibling case (a UUID with no layout manager at
    // all) already classified. Latent today, since the one caller of this overload
    // always passes a non-UUID "autotile:<algorithmId>".
    PhosphorZones::Layout* layout = nullptr;
    const auto uuidOpt = Utils::parseUuid(id);
    if (uuidOpt && m_layoutManager) {
        layout = m_layoutManager->layoutById(*uuidOpt);
    }
    const QString arClass = layout
        ? PhosphorLayout::ScreenClassification::toString(layout->aspectRatioClass())
        : PhosphorLayout::ScreenClassification::toString(PhosphorLayout::ScreenClassification::classify(aspectRatio));

    LayoutOsdContentParams p;
    p.screenId = effectiveScreenId;
    p.id = id;
    p.name = name;
    p.zones = zones;
    p.category = category;
    // Category and isTemplate move together (showLayoutOsdImpl and
    // showScrollingTemplateOsd both enforce it): derive rather than default,
    // so a caller passing the ScrollingTemplate category cannot produce the
    // "Manual badge beside a Column template caption" contradiction. The one
    // current caller passes Autotile, so this is latent-proofing.
    p.isTemplate = (category == static_cast<int>(PhosphorZones::LayoutCategory::ScrollingTemplate));
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

    finishOsdShow(window, surface, osdSlot, screenGeom, effectiveScreenId);
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
    writeQmlProperty(osdSlot, QStringLiteral("isTemplate"), p.isTemplate);
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
    // Both written unconditionally: the OSD slot Item is reused across show
    // calls, so a prior strip card would otherwise leave its ticks or its
    // empty-state caption on the next layout card.
    writeQmlProperty(osdSlot, QStringLiteral("stripAxisHint"), p.stripAxisHint);
    writeQmlProperty(osdSlot, QStringLiteral("stripEmptyCaption"), p.stripEmptyCaption);
    writeQmlProperty(osdSlot, QStringLiteral("zones"), p.zones);
    writeFontProperties(osdSlot, m_settings, /*includeLabelFontColor=*/false);
    // Zone preview colors and opacities follow the same settings pipeline as
    // the live overlays and the picker/selector/snap-assist slots (per-zone
    // custom colors ride inside p.zones; these are the layout-wide effective
    // defaults). The context overlay-appearance override is layered on top,
    // exactly like the siblings' writeColorSettings(..., &overlayOverride)
    // path — all five properties, so a context rule that recolors or
    // re-fades the popups applies to this OSD too. Without this push the
    // OSD fell back to QML-side theme roles and rendered differently from
    // the picker/selector for the same layout.
    if (m_settings) {
        const PhosphorZones::ContextOverlayOverride overlayOverride =
            overlayOverrideForScreen(m_layoutManager, p.screenId);
        writeQmlProperty(osdSlot, QStringLiteral("highlightColor"),
                         overlayOverride.highlightColor.value_or(m_settings->highlightColor()));
        writeQmlProperty(osdSlot, QStringLiteral("inactiveColor"),
                         overlayOverride.inactiveColor.value_or(m_settings->inactiveColor()));
        writeQmlProperty(osdSlot, QStringLiteral("borderColor"),
                         overlayOverride.borderColor.value_or(m_settings->borderColor()));
        writeQmlProperty(osdSlot, QStringLiteral("activeOpacity"),
                         overlayOverride.activeOpacity.value_or(m_settings->activeOpacity()));
        writeQmlProperty(osdSlot, QStringLiteral("inactiveOpacity"),
                         overlayOverride.inactiveOpacity.value_or(m_settings->inactiveOpacity()));
    }
    // Stage d: resolve + push the OSD's surface-shader decoration (rounded
    // corners + border) onto the slot. Done here so every layout-OSD show path
    // (showLayoutOsdImpl / showLayoutOsd(string…) / showDisabledOsd) decorates
    // consistently; showNavigationOsd calls applyDecoration directly since it
    // does not route through pushLayoutOsdContent.
    applyDecoration(osdSlot, PhosphorSurfaceShaders::decorationOsdPath());
}

void OverlayService::showDisabledOsd(const QString& reason, const QString& screenId)
{
    const auto prep = prepareLayoutOsdWindow(screenId);
    if (!prep) {
        return;
    }
    QQuickWindow* const window = prep->window;
    PhosphorLayer::Surface* const surface = prep->surface;
    QQuickItem* const osdSlot = prep->osdSlot;
    const QRect screenGeom = prep->screenGeom;
    const qreal aspectRatio = prep->aspectRatio;
    const QString effectiveScreenId = prep->effectiveScreenId;

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
    p.screenId = effectiveScreenId;
    p.name = reason; // shown in nameLabel when disabled
    p.screenAspectRatio = aspectRatio;
    pushLayoutOsdContent(osdSlot, p);
    writeQmlProperty(osdSlot, QStringLiteral("disabled"), true);
    writeQmlProperty(osdSlot, QStringLiteral("disabledReason"), reason);
    // Written explicitly rather than left to the QML default: the slot is
    // REUSED across shows, so the glyph must be re-stated on each one.
    writeQmlProperty(osdSlot, QStringLiteral("disabledIcon"), QStringLiteral("dialog-cancel"));
    writeQmlProperty(osdSlot, QStringLiteral("mode"), QStringLiteral("layout-osd"));

    finishOsdShow(window, surface, osdSlot, screenGeom, effectiveScreenId);
    qCInfo(lcOverlay) << "Disabled OSD: reason=" << reason << "screen=" << screenId;
}

// hideLayoutOsd / hideNavigationOsd (formerly Q_SLOTS connected to a QML
// `dismissed()` signal) are intentionally gone. The dismiss path is:
//   QML dismissTimer → loaded content `dismissRequested()` signal
//     → shell window re-emits it as `osdDismissRequested`
//     → wirePassiveShellSlots (shellhost_bridge.cpp) string-connects that
//       to OverlayService::onOsdDismissRequested (below)
//     → ShellHost::hideSlot runs an animator-driven slot-hide
//     → onOsdSlotHideCompleted flips slot.visible=false and re-syncs
//       surface state.
// keepMappedOnHide is conditional (createWarmedOsdSurface): the shell
// window stays mapped across hides only while shaders or animations are
// enabled, keeping the warmed Vulkan swapchain alive; with effects off
// the next syncSurfaceState unmaps the wl_surface. Pre-warmed by
// warmUpNotifications and reused for the daemon's lifetime;
// destroyPassiveShell only fires on screen-removal / shutdown.

// Hot-plug hook installed by warmUpNotifications. The single
// ensureOsdScreenAddedConnected / ensurePassiveShellFor / wirePassiveShellSlots /
// warmUpNotifications / destroyPassiveShell / unwirePassiveShellSlots live in
// overlayservice/shellhost_bridge.cpp - they're the daemon's bridge to
// PhosphorOverlay::ShellHost, not OSD-specific.

void OverlayService::onOsdDismissRequested()
{
    // QML osdDismissRequested fired - find which screen's shell window
    // emitted, then run an animator-driven slot-hide. Sender-based
    // resolution rather than carrying the screen id through the signal
    // because the shell's QML dismiss signals are declared parameter-less
    // by project convention (see PassiveOverlayShell.qml's
    // osdDismissRequested and its wiring in wirePassiveShellSlots).
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
    // Release the backdrop stand-in for the same reason clearDecoration does:
    // a hidden slot draws none of it, and the image is wallpaper-sized. Every
    // show runs applyDecoration again, which rewrites this, so nothing is lost
    // by dropping it for the idle interval between shows. The chain itself is
    // left alone deliberately: it is rewritten per show and costs a list.
    writeQmlProperty(it->osdSlot(), QString(OverlayQmlPropertyNames::BackdropTexture), QVariant());
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
// PZ-content state into the predicates ShellHost::syncSurfaceState expects,
// conceptually part of the shell-host bridge rather than the OSD pipeline.

void OverlayService::showNavigationOsd(bool success, const QString& action, const QString& reason,
                                       const QString& sourceZoneId, const QString& targetZoneId,
                                       const QString& screenId)
{
    qCDebug(lcOverlay) << "showNavigationOsd called: action=" << action << "reason=" << reason << "screen=" << screenId
                       << "success=" << success;

    // Shared window preparation (screen resolve, passive shell, geometry).
    // Runs BEFORE the dedup check because it is the source of effectiveId;
    // that is safe — on the duplicate path the first show already created
    // the shell, so the helper's side effects are no-ops there, and the
    // zone-selector hide now lives in finishOsdShow past every bail. The
    // bundle's aspect ratio is unused: the nav card is text-sized, not
    // preview-sized.
    const auto prep = prepareLayoutOsdWindow(screenId);
    if (!prep) {
        return;
    }
    QQuickWindow* const window = prep->window;
    PhosphorLayer::Surface* const navSurface = prep->surface;
    QQuickItem* const osdSlot = prep->osdSlot;
    const QRect navScreenGeom = prep->screenGeom;
    const QString effectiveId = prep->effectiveScreenId;

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
    //
    // Successful span steps are exempt: the reason is direction-stable
    // ("grow:right" on every step), so consecutive genuine steps produce an
    // identical key, and the shortcut's own 100 ms debounce is shorter than
    // this window — a second real step would otherwise commit geometry with
    // no feedback. The duplicate this window catches (one action arriving on
    // both the Qt and D-Bus paths) does not apply to span, which has a single
    // relay. Span FAILURES stay eligible: their reasons are direction-stable
    // too, so leaving them exempt would re-show the same message at the
    // 100 ms shortcut debounce rate. (The window halves that to ~200 ms
    // rather than suppressing the repeat outright — the dedup clock is only
    // stamped on a shown OSD, so a suppressed one does not extend it.)
    // The fullscreen action's reason is a resulting-state token ("on"/"off")
    // and its window rides sourceZoneId, so two DIFFERENT windows toggled to
    // the same state within the window are distinct events — key them apart.
    // Other actions keep the plain key: their reasons discriminate the
    // event. (The float-family actions carry WINDOW ids in the zone fields,
    // but they stay on the plain key deliberately. On snap and autotile a
    // key-auto-repeat switch re-resolves the same leg and target before the
    // focus report lands, so suppressing the identical repeat OSD is the
    // desired outcome. The scroll engine's eager floatingHasFocus clear
    // flips the derivation on the float-to-tiled press only, and the return
    // leg is armed by the genuine focus report — so scroll alternates legs
    // when the report keeps up with the repeat rate, with distinct reasons
    // shown, and otherwise dedups like its siblings.)
    QString actionKey = action + QLatin1Char(':') + reason;
    if (action == QLatin1String("fullscreen")) {
        actionKey += QLatin1Char(':') + sourceZoneId;
    }
    const bool dedupEligible = !(success && action == QLatin1String("span"));
    if (dedupEligible && actionKey == m_lastNavigationActionKey && effectiveId == m_lastNavigationScreenId
        && m_lastNavigationTime.isValid() && m_lastNavigationTime.elapsed() < 200) {
        qCDebug(lcOverlay) << "Skipping duplicate navigation OSD:" << action << reason;
        return;
    }

    // Resolve per-screen layout (not the global m_layout which may belong to another screen)
    // Float, algorithm, rotate, and autotile-only actions don't need layout/zones
    // "fullscreen", "tabbed", "resize", "center", "scroll", "consume" and
    // "expel" belong here too: their success arms render plain text and never
    // consult zone data, so the missing-layout bail below could only ever
    // swallow their feedback (for the scrolling-only ones that takes a
    // screen whose strip-zone provider answers empty, but the hardening
    // costs nothing).
    static const QSet<QString> noLayoutActions{
        QStringLiteral("float"),       QStringLiteral("rotate"),       QStringLiteral("focus_master"),
        QStringLiteral("swap_master"), QStringLiteral("master_ratio"), QStringLiteral("master_count"),
        QStringLiteral("retile"),      QStringLiteral("swap_vs"),      QStringLiteral("rotate_vs"),
        QStringLiteral("fullscreen"),  QStringLiteral("tabbed"),       QStringLiteral("resize"),
        QStringLiteral("center"),      QStringLiteral("scroll"),       QStringLiteral("consume"),
        QStringLiteral("expel")};
    // Failure OSDs never need layout/zone data: every failure branch in
    // NavigationOsdContent.qml renders plain text (and reasons like
    // "no_zones" / "no_active_layout" fire precisely when no layout is
    // resolvable), so gating them on a layout would drop the feedback the
    // engine emitted them for.
    const bool needsLayout = success && !noLayoutActions.contains(action);
    // Scrolling screens have no zone layout of their own; the daemon-injected
    // provider supplies the strip's visible-tile-number model instead, so
    // "Zone %1" copy resolves and the missing-layout bail below must not
    // swallow the feedback.
    //
    // Resolved lazily and memoised. What the laziness actually saves is ONE
    // path: the ensurePassiveShellFor bail below, which returns before the
    // zones are written. Every OSD that renders — failures and no-layout
    // actions included — still reaches the zones write and pays the walk
    // once, because skipping it there would change the zones QML receives.
    std::optional<QVariantList> scrollZonesCache;
    const auto scrollZonesFor = [this, &scrollZonesCache, &effectiveId]() -> const QVariantList& {
        if (!scrollZonesCache.has_value()) {
            scrollZonesCache = m_scrollZonesProvider ? m_scrollZonesProvider(effectiveId) : QVariantList();
        }
        return *scrollZonesCache;
    };
    PhosphorZones::Layout* screenLayout = resolveScreenLayout(effectiveId);
    if (needsLayout && scrollZonesFor().isEmpty() && (!screenLayout || screenLayout->zones().isEmpty())) {
        qCDebug(lcOverlay) << "No layout or zones for navigation OSD: screen=" << effectiveId
                           << "layout=" << (screenLayout ? screenLayout->name() : QStringLiteral("null"))
                           << "zones=" << (screenLayout ? screenLayout->zones().size() : 0) << "action=" << action;
        return;
    }

    // The passive shell, window, surface and slot all came from
    // prepareLayoutOsdWindow above; finishOsdShow below is what hides any
    // fading zone selector on this screen. The shell is kept mapped across hides
    // while shaders or animations are enabled (effects-gated
    // keepMappedOnHide); per-show the SurfaceAnimator's beginShow replays
    // the fade-in and restartDismissTimer extends the auto-hide.

    // Process reason field - for rotation, extract the window count.
    // Format: "clockwise:N" or "counterclockwise:N" where N is window count.
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
            if (action == QLatin1String("rotate")) {
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
    QVariantList zonesList = scrollZonesFor();
    if (zonesList.isEmpty()) {
        zonesList = PhosphorZones::LayoutUtils::zonesToVariantList(screenLayout, PhosphorZones::ZoneField::Minimal,
                                                                   QRectF(navScreenGeom));
    }
    writeQmlProperty(osdSlot, QStringLiteral("zones"), zonesList);

    // User overlay font settings: navigation OSDs do not route through
    // pushLayoutOsdContent, so without this explicit write the slot keeps
    // whatever the last layout-OSD show left there and the nav card ignores
    // the user's family/scale entirely.
    writeFontProperties(osdSlot, m_settings, /*includeLabelFontColor=*/false);

    // Stage d: resolve + push the OSD surface decoration. Navigation OSDs do
    // not route through pushLayoutOsdContent, so apply it explicitly here (same
    // decoration the layout-OSD paths get via pushLayoutOsdContent).
    applyDecoration(osdSlot, PhosphorSurfaceShaders::decorationOsdPath());

    // Write mode AFTER data properties so the Loader-instantiated
    // NavigationOsdContent picks up correct values on first binding pass.
    // (assertWindowOnScreen already ran inside prepareLayoutOsdWindow.)
    writeQmlProperty(osdSlot, QStringLiteral("mode"), QStringLiteral("navigation-osd"));

    finishOsdShow(window, navSurface, osdSlot, navScreenGeom, effectiveId);

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
