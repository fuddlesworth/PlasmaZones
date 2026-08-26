// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "internal.h"
#include "daemon/overlayservice.h"
#include "common/stripcardserialize.h"
#include "core/platform/logging.h"
#include <PhosphorZones/Layout.h>
#include <PhosphorZones/LayoutRegistry.h>
#include <PhosphorZones/Zone.h>
#include <PhosphorZones/LayoutUtils.h>
#include "core/utils/geometryutils.h"
#include <PhosphorScreens/Manager.h>
#include "core/utils/utils.h"
#include "core/types/zoneselectorlayout.h"
#include "config/configdefaults.h"
#include <QScreen>
#include <QQuickWindow>
#include <QQuickItem>
#include <QQmlEngine>

namespace PlasmaZones {

namespace {

void updateZoneSelectorComputedProperties(PhosphorScreens::ScreenManager* mgr, QObject* window, QScreen* screen,
                                          const QString& virtualScreenId, ISettings* settings,
                                          const ZoneSelectorLayout& layout,
                                          const PhosphorZones::ContextOverlayOverride& overlayOverride, int zonePadding,
                                          bool stripVerticalAxis)
{
    if (!window || !screen) {
        return;
    }

    // Use virtual screen geometry if available, falling back to physical
    const QRect screenGeom = resolveScreenGeometry(mgr, virtualScreenId);

    // previewScale is "how many miniature pixels one screen pixel becomes",
    // and it is read against the extent the card is sized along. A vertical
    // strip's cards take their preview extent from indicatorHeight (see the
    // vertical arm of computeZoneSelectorLayout and its QML twin), so scaling
    // against the screen WIDTH there would size the padding, border width and
    // corner radius against an extent the card never uses — badly wrong on a
    // portrait monitor, where the two differ by more than the aspect ratio.
    const int screenExtent = stripVerticalAxis ? screenGeom.height() : screenGeom.width();
    const int indicatorExtent = stripVerticalAxis ? layout.indicatorHeight : layout.indicatorWidth;
    const qreal previewScale = screenExtent > 0 ? static_cast<qreal>(indicatorExtent) / screenExtent : 0.09375;
    writeQmlProperty(window, QStringLiteral("previewScale"), previewScale);

    // No positionIsVertical write here: the caller sets it from the same
    // config.position BEFORE the layout properties, because the QML anchors
    // need it there. Repeating it after would be a second write of a value
    // that cannot have changed in between.

    // Compute scaled zone appearance values. Zone padding honors per-monitor gap
    // RULES (cascade: context-rule override → global → default) via the layout
    // registry's current context. This preview passes no layout, so the per-layout
    // tier does not apply here; border width/radius honor a SetOverlayBorderWidth /
    // SetOverlayBorderRadius context rule over the global setting, matching the
    // zoneBorderWidth/Radius written for the same window in updateZoneSelectorWindow.
    if (settings) {
        // overlayOverride and zonePadding arrive PRE-RESOLVED from the one
        // resolve updateZoneSelectorWindow performs for its raw-property
        // writes — the same inputs, resolved twice per popup update before
        // this was threaded through.
        const int zoneBorderWidth = overlayOverride.borderWidth.value_or(settings->borderWidth());
        const int zoneBorderRadius = overlayOverride.borderRadius.value_or(settings->borderRadius());

        const int scaledPadding = std::max(1, qRound(zonePadding * previewScale));
        const int scaledBorderWidth = std::max(1, qRound(zoneBorderWidth * previewScale * 2));
        const int scaledBorderRadius = std::max(2, qRound(zoneBorderRadius * previewScale * 2));

        writeQmlProperty(window, QStringLiteral("scaledPadding"), scaledPadding);
        writeQmlProperty(window, QStringLiteral("scaledBorderWidth"), scaledBorderWidth);
        writeQmlProperty(window, QStringLiteral("scaledBorderRadius"), scaledBorderRadius);
    }
}

void applyZoneSelectorLayout(QObject* window, const ZoneSelectorLayout& layout)
{
    if (!window) {
        return;
    }

    writeQmlProperty(window, QStringLiteral("indicatorWidth"), layout.indicatorWidth);
    writeQmlProperty(window, QStringLiteral("indicatorHeight"), layout.indicatorHeight);
    writeQmlProperty(window, QStringLiteral("indicatorSpacing"), layout.indicatorSpacing);
    // containerPadding / paddingSide / labelHeight are NOT pushed: they feed
    // the C++ bar math only, and their QML forward chain was dead end to end
    // (declared, forwarded, read by nothing).
    writeQmlProperty(window, QStringLiteral("containerTopMargin"), layout.containerTopMargin);
    writeQmlProperty(window, QStringLiteral("containerSideMargin"), layout.containerSideMargin);
    writeQmlProperty(window, QStringLiteral("labelTopMargin"), layout.labelTopMargin);
    writeQmlProperty(window, QStringLiteral("labelSpace"), layout.labelSpace);
    writeQmlProperty(window, QStringLiteral("cardPadding"), layout.cardPadding);
    writeQmlProperty(window, QStringLiteral("cardSidePadding"), layout.cardSidePadding);
    writeQmlProperty(window, QStringLiteral("layoutColumns"), layout.columns);
    writeQmlProperty(window, QStringLiteral("layoutRows"), layout.rows);
    writeQmlProperty(window, QStringLiteral("totalRows"), layout.totalRows);
    writeQmlProperty(window, QStringLiteral("contentWidth"), layout.contentWidth);
    writeQmlProperty(window, QStringLiteral("contentHeight"), layout.contentHeight);
    writeQmlProperty(window, QStringLiteral("scrollContentHeight"), layout.scrollContentHeight);
    writeQmlProperty(window, QStringLiteral("scrollContentWidth"), layout.scrollContentWidth);
    writeQmlProperty(window, QStringLiteral("needsScrolling"), layout.needsScrolling);
    writeQmlProperty(window, QStringLiteral("needsHorizontalScrolling"), layout.needsHorizontalScrolling);
    // Explicitly set containerWidth/Height after contentWidth/Height to ensure they update
    writeQmlProperty(window, QStringLiteral("containerWidth"), layout.containerWidth);
    writeQmlProperty(window, QStringLiteral("containerHeight"), layout.containerHeight);
    writeQmlProperty(window, QStringLiteral("barWidth"), layout.barWidth);
    writeQmlProperty(window, QStringLiteral("barHeight"), layout.barHeight);
}

// Size the zone selector slot to fill the entire (virtual) screen.
// Post-shell-migration the slot is a QQuickItem (anchors.fill on the shell
// QQuickWindow). The QML root uses internal anchors (selectorPosition
// state) to position the visible bar in the chosen corner of the
// transparent slot.
void applyZoneSelectorGeometry(QQuickItem* slot, const QRect& screenGeom)
{
    if (!slot || !screenGeom.isValid()) {
        return;
    }
    slot->setWidth(screenGeom.width());
    slot->setHeight(screenGeom.height());
}

} // namespace

void OverlayService::updateZoneSelectorWindow(const QString& screenId)
{
    if (screenId.isEmpty()) {
        return;
    }

    const auto it = m_screenStates.constFind(screenId);
    if (it == m_screenStates.constEnd()) {
        return;
    }
    auto* window = it->zoneSelectorSlot();
    if (!window) {
        return;
    }
    QScreen* screen = it->zoneSelectorPhysScreen;
    if (!screen) {
        return;
    }

    // Update screen properties (in case screen geometry changed)
    const QRect screenGeom = resolveScreenGeometry(m_screenManager, screenId);
    qreal aspectRatio =
        (screenGeom.height() > 0) ? static_cast<qreal>(screenGeom.width()) / screenGeom.height() : (16.0 / 9.0);
    // Clamped symmetrically about 1:1 — the lower bound is the inverse of the
    // upper, so a rotated 21:9 (about 0.43) keeps its shape. NOTE: for the
    // SELECTOR slot this write is currently declared-but-unforwarded
    // (ZoneSelectorContent derives its own aspect; see the contract note in
    // PassiveOverlayShell.qml), so the clamp's live consumers are the OSD
    // and picker contents, whose own safeAspectRatio floors match this one.
    aspectRatio = qBound(0.25, aspectRatio, 4.0);
    writeQmlProperty(window, QStringLiteral("screenAspectRatio"), aspectRatio);
    writeQmlProperty(window, QStringLiteral("screenWidth"), screenGeom.width());

    // Build resolved per-screen config. Strip-selector screens resolve the
    // scrolling variant, whose resolver stamps LayoutMode = Horizontal. That
    // stamp stands for "the user picks no form here": the popup mirrors the
    // strip, so its one row of cards runs whichever way the ENGINE resolved
    // the axis, and computeZoneSelectorLayout takes that axis as its own
    // argument rather than from the config.
    const bool stripMode = isStripSelectorScreen(screenId);
    // Resolved once for the whole update: the axis reaches QML, the layout
    // computation and the preview scale, and asking the provider three times
    // per rebuild would let a rotation landing mid-update answer differently
    // to each of them.
    const bool stripVerticalAxis = stripMode && stripIsVertical(screenId);
    const ZoneSelectorConfig config = m_settings
        ? (stripMode ? m_settings->resolvedScrollingZoneSelectorConfig(screenId)
                     : m_settings->resolvedZoneSelectorConfig(screenId))
        : defaultZoneSelectorConfig();

    // Resolved ONCE for the whole update: the raw-property writes below and
    // the scaled-preview writes in updateZoneSelectorComputedProperties read
    // the same per-screen context, and resolving it twice per popup rebuild
    // both duplicated the rule walk and let a context switch landing between
    // the two resolves hand each half a different answer.
    PhosphorZones::ContextOverlayOverride overlayOverride;
    int zonePadding = 0;
    if (m_settings) {
        overlayOverride = overlayOverrideForScreen(m_layoutManager, screenId);
        zonePadding = GeometryUtils::getEffectiveInnerGap(
            nullptr, m_settings, GeometryUtils::currentContextGapOverride(m_layoutManager, m_settings, screenId));
    }

    // Update settings-based properties
    if (m_settings) {
        // Context overlay-appearance overrides layer over config for this
        // screen's live context, matching the main zone overlay.
        writeColorSettings(window, m_settings, &overlayOverride);
        // PhosphorZones::Zone appearance for the scaled preview. Zone padding
        // honors per-monitor gap RULES (context-rule override → global → default)
        // via the layout registry's current context. This preview passes no
        // layout, so the per-layout tier does not apply here; border width/radius
        // layer the context overlay rule over the global config value.
        writeQmlProperty(window, QStringLiteral("zonePadding"), zonePadding);
        writeQmlProperty(window, QStringLiteral("zoneBorderWidth"),
                         overlayOverride.borderWidth.value_or(m_settings->borderWidth()));
        writeQmlProperty(window, QStringLiteral("zoneBorderRadius"),
                         overlayOverride.borderRadius.value_or(m_settings->borderRadius()));
        // Font settings for zone number labels
        writeFontProperties(window, m_settings, /*includeLabelFontColor=*/false);
    }
    // Pass resolved per-screen config values to QML
    writeQmlProperty(window, QStringLiteral("selectorPosition"), config.position);
    writeQmlProperty(window, QStringLiteral("selectorLayoutMode"), config.layoutMode);
    writeQmlProperty(window, QStringLiteral("selectorGridColumns"), config.gridColumns);
    writeQmlProperty(window, QStringLiteral("previewWidth"), config.previewWidth);
    writeQmlProperty(window, QStringLiteral("previewHeight"), config.previewHeight);
    writeQmlProperty(window, QStringLiteral("previewLockAspect"), config.previewLockAspect);

    // Build and pass the popup model: strip cards on strip-selector
    // screens, layouts everywhere else. Both lists are pushed every update
    // (the inactive one empty) so a screen crossing a mode boundary never
    // renders the previous mode's stale model.
    QVariantList layouts;
    QVariantList stripColumns;
    QList<qreal> stripFractions;
    if (stripMode) {
        stripColumns = buildStripList(screenId);
        stripFractions = stripFractionsFromColumns(stripColumns);
        // Write-through: this is a fresh build of the authoritative list, so
        // refresh the trigger-edge memo rather than letting the next probe
        // pay for a second identical build.
        m_stripCardFractionsCache.insert(screenId, stripFractions);
    } else {
        layouts = buildLayoutsList(screenId);
    }
    writeQmlProperty(window, QStringLiteral("stripMode"), stripMode);
    writeQmlProperty(window, QStringLiteral("stripColumns"), stripColumns);
    // Pushed even when this screen is not in strip mode: the property is a
    // plain bool and a stale true would transpose the next strip popup on a
    // screen that had since gone horizontal.
    writeQmlProperty(window, QStringLiteral("stripVerticalAxis"), stripVerticalAxis);
    writeQmlProperty(window, QStringLiteral("layouts"), layouts);

    // Global "Auto-assign for all layouts" master toggle (#370) - when on, every
    // layout effectively auto-assigns regardless of its per-layout flag. Pushed
    // here so the badge in each LayoutCard shows the effective state.
    writeQmlProperty(window, QStringLiteral("globalAutoAssign"), m_settings && m_settings->autoAssignAllLayouts());

    // Set active layout ID for this screen
    // Per-screen assignment takes priority so each monitor highlights its own
    // layout (or its autotile algorithm, via the "autotile:<algorithm>" id).
    writeQmlProperty(window, QStringLiteral("activeLayoutId"), activeLayoutIdForScreen(screenId));

    // Push lock state so QML disables non-active layout interaction.
    // isAnyModeLocked checks a LockContext rule first, then both manual modes -
    // the zone selector appears during drag for the current mode.
    bool locked = false;
    if (m_settings && m_layoutManager) {
        int curDesktop = currentVirtualDesktopForScreen(screenId);
        // m_currentActivity, not m_layoutManager->currentActivity(): the
        // hit-test that ENFORCES this badge (selector.cpp) reads the mirror,
        // and mixing sources lets the painted badge transiently disagree
        // with what a click does.
        locked = isAnyModeLocked(m_settings, m_layoutManager, screenId, curDesktop, m_currentActivity);
    }
    writeQmlProperty(window, QStringLiteral("locked"), locked);

    // Compute layout for geometry updates using per-screen config. Strip
    // cards are variable-width (each column's real work-area share), so the
    // fractions drive the bar width; an empty strip keeps one uniform cell
    // (empty fraction list) so the bar retains a hittable "open the first
    // column" body instead of collapsing.
    const int layoutCount = stripMode ? std::max(1, static_cast<int>(stripColumns.size())) : layouts.size();
    // The axis has to reach the layout too, not just QML: the cards stack
    // down the popup on a vertical strip, so sizing the container as one
    // horizontal card row would clip the tail cards away, and the hit-test
    // reads rendered rects back and would find them empty.
    const ZoneSelectorLayout layout =
        computeZoneSelectorLayout(config, screenGeom, layoutCount, stripFractions, stripVerticalAxis);

    // Set positionIsVertical before layout properties; QML anchors depend on it for
    // containerWidth/Height, so it has to be correct before we apply the layout.
    const auto pos = static_cast<ZoneSelectorPosition>(config.position);
    writeQmlProperty(window, QStringLiteral("positionIsVertical"),
                     (pos == ZoneSelectorPosition::Left || pos == ZoneSelectorPosition::Right));

    // Apply layout and geometry
    applyZoneSelectorLayout(window, layout);

    // Update computed properties that depend on layout and settings
    updateZoneSelectorComputedProperties(m_screenManager, window, screen, screenId, m_settings, layout, overlayOverride,
                                         zonePadding, stripVerticalAxis);

    // Positioning is entirely QML-internal: ZoneSelectorContent.qml's
    // selectorPosition state anchors the inner container to the requested
    // corner of the full-screen transparent surface. Anchors/margins are
    // baked at attach time (AnchorAll) and never mutated afterwards.
    applyZoneSelectorGeometry(window, screenGeom);

    // Keep stored geometry in sync so hit-testing uses the current value
    m_screenStates[screenId].zoneSelectorGeometry = screenGeom;

    // Slot is the QQuickItem hosting ZoneSelectorContent; root traversal
    // starts directly from it (no contentItem() - that's QQuickWindow-only).
    if (auto* contentRoot = window) {
        contentRoot->polish();
    }

    if (auto* contentRoot = window) {
        if (auto* gridItem = findQmlItemByName(contentRoot, QStringLiteral("zoneSelectorContentGrid"))) {
            gridItem->polish();
            gridItem->update();
        }
        if (auto* stripRow = findQmlItemByName(contentRoot, QStringLiteral("zoneSelectorStripRow"))) {
            stripRow->polish();
            stripRow->update();
        }
        if (auto* containerItem = findQmlItemByName(contentRoot, QStringLiteral("shaderAnchor"))) {
            containerItem->polish();
            containerItem->update();
        }
    }
}

void OverlayService::refreshContextLockState()
{
    // Targeted re-push of just the `locked` QML property (not a full
    // updateZoneSelectorWindow — only the lock state can change here). Both the
    // zone selector and the layout picker compute `locked` via isAnyModeLocked,
    // which folds the rule-driven LockContext lock over the manual lock store,
    // so re-resolving picks up a runtime rule edit. Without settings/registry we
    // cannot resolve a lock, and every overlay already defaults to unlocked.
    if (!m_settings || !m_layoutManager) {
        return;
    }
    // Mirror, not registry — same single-source rule as the `locked` write in
    // updateZoneSelectorWindow and the hit-test in selector.cpp.
    const QString curActivity = m_currentActivity;

    // Open zone selectors: one entry per screen with a live slot.
    for (auto it = m_screenStates.constBegin(); it != m_screenStates.constEnd(); ++it) {
        auto* window = it.value().zoneSelectorSlot();
        if (!window) {
            continue;
        }
        // Per-output virtual desktops (#648): each screen resolves its own desktop.
        const int curDesktop = currentVirtualDesktopForScreen(it.key());
        // Both-mode lens (-1) by design for the LAYOUT-MODE popup: it shows
        // the SNAP zone overlay, so a snapping lock is exactly the lock it
        // must reflect. Only the picker below takes the Templates lens. NOTE
        // for whoever adds a lock affordance to the STRIP popup: on a strip
        // screen the premise expires (the popup renders strip cards, not the
        // snap overlay), so that affordance should take the picker's
        // Templates lens — today no strip consumer reads `locked`, so the
        // both-mode value pushed there is inert.
        const bool locked = isAnyModeLocked(m_settings, m_layoutManager, it.key(), curDesktop, curActivity);
        writeQmlProperty(window, QStringLiteral("locked"), locked);
    }

    // Open layout picker (re-running showLayoutPicker would rebuild/re-animate
    // it, so push just the lock state to the live slot).
    if (m_layoutPickerVisible && !m_layoutPickerScreenId.isEmpty()) {
        if (auto* slot = m_screenStates.value(m_layoutPickerScreenId).layoutPickerSlot()) {
            // Per-output virtual desktops (#648): each screen resolves its own desktop.
            const int curDesktop = currentVirtualDesktopForScreen(m_layoutPickerScreenId);
            // Same Templates lens the picker's show path uses
            // (pickerLockModeFor). With the -1 default here, a Templates
            // screen's picker opened correctly against its scrolling lock
            // and then flipped to the snapping verdict on the first rule
            // edit that re-pushed this property.
            const bool locked = isAnyModeLocked(m_settings, m_layoutManager, m_layoutPickerScreenId, curDesktop,
                                                curActivity, pickerLockModeFor(m_layoutPickerScreenId));
            writeQmlProperty(slot, QStringLiteral("locked"), locked);
        }
    }
}

} // namespace PlasmaZones
