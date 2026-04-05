// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "internal.h"
#include "../overlayservice.h"
#include "../../core/logging.h"
#include "../../core/layout.h"
#include "../../core/layoutmanager.h"
#include "../../core/zone.h"
#include "../../core/layoututils.h"
#include "../../core/geometryutils.h"
#include "../../core/screenmanager.h"
#include "../../core/utils.h"
#include "../../core/zoneselectorlayout.h"
#include "../config/configdefaults.h"
#include <QScreen>
#include <QQuickWindow>
#include <QQuickItem>
#include <QQmlEngine>
#include "../../core/layersurface.h"

namespace PlasmaZones {

namespace {

void updateZoneSelectorComputedProperties(QQuickWindow* window, QScreen* screen, const QString& virtualScreenId,
                                          const ZoneSelectorConfig& config, ISettings* settings,
                                          const ZoneSelectorLayout& layout)
{
    if (!window || !screen) {
        return;
    }

    // Use virtual screen geometry if available, falling back to physical
    const QRect screenGeom = resolveScreenGeometry(virtualScreenId);
    const int screenWidth = screenGeom.width();
    const int indicatorWidth = layout.indicatorWidth;

    // Compute previewScale
    const qreal previewScale = screenWidth > 0 ? static_cast<qreal>(indicatorWidth) / screenWidth : 0.09375;
    writeQmlProperty(window, QStringLiteral("previewScale"), previewScale);

    // Compute positionIsVertical from per-screen config
    const auto pos = static_cast<ZoneSelectorPosition>(config.position);
    writeQmlProperty(window, QStringLiteral("positionIsVertical"),
                     (pos == ZoneSelectorPosition::Left || pos == ZoneSelectorPosition::Right));

    // Compute scaled zone appearance values (from global settings - not per-screen)
    if (settings) {
        const int zonePadding = settings->zonePadding();
        const int zoneBorderWidth = settings->borderWidth();
        const int zoneBorderRadius = settings->borderRadius();

        const int scaledPadding = std::max(1, qRound(zonePadding * previewScale));
        const int scaledBorderWidth = std::max(1, qRound(zoneBorderWidth * previewScale * 2));
        const int scaledBorderRadius = std::max(2, qRound(zoneBorderRadius * previewScale * 2));

        writeQmlProperty(window, QStringLiteral("scaledPadding"), scaledPadding);
        writeQmlProperty(window, QStringLiteral("scaledBorderWidth"), scaledBorderWidth);
        writeQmlProperty(window, QStringLiteral("scaledBorderRadius"), scaledBorderRadius);
    }
}

void applyZoneSelectorLayout(QQuickWindow* window, const ZoneSelectorLayout& layout)
{
    if (!window) {
        return;
    }

    writeQmlProperty(window, QStringLiteral("indicatorWidth"), layout.indicatorWidth);
    writeQmlProperty(window, QStringLiteral("indicatorHeight"), layout.indicatorHeight);
    writeQmlProperty(window, QStringLiteral("indicatorSpacing"), layout.indicatorSpacing);
    writeQmlProperty(window, QStringLiteral("containerPadding"), layout.containerPadding);
    writeQmlProperty(window, QStringLiteral("containerPaddingSide"), layout.paddingSide);
    writeQmlProperty(window, QStringLiteral("containerTopMargin"), layout.containerTopMargin);
    writeQmlProperty(window, QStringLiteral("containerSideMargin"), layout.containerSideMargin);
    writeQmlProperty(window, QStringLiteral("labelTopMargin"), layout.labelTopMargin);
    writeQmlProperty(window, QStringLiteral("labelHeight"), layout.labelHeight);
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

// Set the zone selector window size for the computed layout.
// Positioning is handled by layer surface anchors + margins (set in updateZoneSelectorWindow),
// NOT by setX()/setY() — those are no-ops on layer surfaces since the compositor controls placement.
// We only set width/height here, which the QPA plugin forwards via zwlr_layer_surface_v1_set_size.
void applyZoneSelectorGeometry(QQuickWindow* window, const QRect& screenGeom, const ZoneSelectorLayout& layout,
                               ZoneSelectorPosition pos)
{
    if (!window || !screenGeom.isValid()) {
        return;
    }

    if (pos == ZoneSelectorPosition::Center) {
        // Fill the entire screen; QML "center" state positions the container in the middle
        window->setWidth(screenGeom.width());
        window->setHeight(screenGeom.height());
    } else {
        window->setWidth(layout.barWidth);
        window->setHeight(layout.barHeight);
    }
}

} // namespace

void OverlayService::updateZoneSelectorWindow(const QString& screenId)
{
    if (screenId.isEmpty()) {
        return;
    }

    auto* window = m_screenStates.value(screenId).zoneSelectorWindow;
    if (!window) {
        return;
    }

    QScreen* screen = m_screenStates.value(screenId).zoneSelectorPhysScreen;
    if (!screen) {
        return;
    }

    // Update screen properties (in case screen geometry changed)
    const QRect screenGeom = resolveScreenGeometry(screenId);
    qreal aspectRatio =
        (screenGeom.height() > 0) ? static_cast<qreal>(screenGeom.width()) / screenGeom.height() : (16.0 / 9.0);
    writeQmlProperty(window, QStringLiteral("screenAspectRatio"), aspectRatio);
    writeQmlProperty(window, QStringLiteral("screenWidth"), screenGeom.width());

    // Build resolved per-screen config
    const ZoneSelectorConfig config =
        m_settings ? m_settings->resolvedZoneSelectorConfig(screenId) : defaultZoneSelectorConfig();

    // Update settings-based properties
    if (m_settings) {
        writeQmlProperty(window, QStringLiteral("highlightColor"), m_settings->highlightColor());
        writeQmlProperty(window, QStringLiteral("inactiveColor"), m_settings->inactiveColor());
        writeQmlProperty(window, QStringLiteral("borderColor"), m_settings->borderColor());
        writeQmlProperty(window, QStringLiteral("activeOpacity"), m_settings->activeOpacity());
        writeQmlProperty(window, QStringLiteral("inactiveOpacity"), m_settings->inactiveOpacity());
        // Zone appearance settings for scaled preview (global)
        writeQmlProperty(window, QStringLiteral("zonePadding"), m_settings->zonePadding());
        writeQmlProperty(window, QStringLiteral("zoneBorderWidth"), m_settings->borderWidth());
        writeQmlProperty(window, QStringLiteral("zoneBorderRadius"), m_settings->borderRadius());
        // Font settings for zone number labels
        writeFontProperties(window, m_settings);
    }
    // Pass resolved per-screen config values to QML
    writeQmlProperty(window, QStringLiteral("selectorPosition"), config.position);
    writeQmlProperty(window, QStringLiteral("selectorLayoutMode"), config.layoutMode);
    writeQmlProperty(window, QStringLiteral("selectorGridColumns"), config.gridColumns);
    writeQmlProperty(window, QStringLiteral("previewWidth"), config.previewWidth);
    writeQmlProperty(window, QStringLiteral("previewHeight"), config.previewHeight);
    writeQmlProperty(window, QStringLiteral("previewLockAspect"), config.previewLockAspect);

    // Build and pass layout data (filtered per-screen mode)
    QVariantList layouts = buildLayoutsList(screenId);
    writeQmlProperty(window, QStringLiteral("layouts"), layouts);

    // Set active layout ID for this screen
    // Per-screen assignment takes priority so each monitor highlights its own layout
    QString activeLayoutId;
    Layout* screenLayout = resolveScreenLayout(screenId);
    if (screenLayout) {
        activeLayoutId = screenLayout->id().toString();
    }
    writeQmlProperty(window, QStringLiteral("activeLayoutId"), activeLayoutId);

    // Push lock state so QML disables non-active layout interaction
    // Check both modes — zone selector appears during drag for the current mode
    bool locked = false;
    if (m_settings && m_layoutManager) {
        int curDesktop = m_layoutManager->currentVirtualDesktop();
        QString curActivity = m_layoutManager->currentActivity();
        locked = isAnyModeLocked(m_settings, screenId, curDesktop, curActivity);
    }
    writeQmlProperty(window, QStringLiteral("locked"), locked);

    // Compute layout for geometry updates using per-screen config
    const int layoutCount = layouts.size();
    const ZoneSelectorLayout layout = computeZoneSelectorLayout(config, screenGeom, layoutCount);

    // Set positionIsVertical before layout properties; QML anchors depend on it for
    // containerWidth/Height, so it has to be correct before we apply the layout.
    const auto pos = static_cast<ZoneSelectorPosition>(config.position);
    writeQmlProperty(window, QStringLiteral("positionIsVertical"),
                     (pos == ZoneSelectorPosition::Left || pos == ZoneSelectorPosition::Right));

    // Apply layout and geometry
    applyZoneSelectorLayout(window, layout);

    // Update computed properties that depend on layout and settings
    updateZoneSelectorComputedProperties(window, screen, screenId, config, m_settings, layout);

    if (auto* layerSurface = LayerSurface::find(window)) {
        const int screenW = screenGeom.width();
        const int screenH = screenGeom.height();
        const int hMargin = std::max(0, (screenW - layout.barWidth) / 2);
        const int vMargin = std::max(0, (screenH - layout.barHeight) / 2);

        // Virtual screen offsets: margins are relative to PHYSICAL screen edges.
        // For virtual screens, add the offset from each physical edge.
        // setX/setY in applyZoneSelectorGeometry provides position hints for mapFromGlobal;
        // margins are what the compositor actually uses for rendering on Wayland.
        const QRect physGeom = screen->geometry();
        const int vsLeftOff = screenGeom.x() - physGeom.x();
        const int vsTopOff = screenGeom.y() - physGeom.y();
        const int vsRightOff = physGeom.right() - screenGeom.right();
        const int vsBottomOff = physGeom.bottom() - screenGeom.bottom();

        // exclusiveZone(-1) ignores panel geometry; the popup renders at absolute screen
        // coordinates over any panels, so hover coordinates match (no offset mismatch).

        // Anchors use the shared helper (single source of truth for position→anchors mapping)
        LayerSurface::Anchors anchors = getAnchorsForPosition(pos);

        // Margins are position-specific: center the bar within the anchored region.
        // Virtual screen offsets are added to confine positioning within the virtual
        // screen region of the physical monitor.
        QMargins margins;
        switch (pos) {
        case ZoneSelectorPosition::TopLeft:
            margins = QMargins(vsLeftOff + 0, vsTopOff + 0, vsRightOff + screenW - layout.barWidth,
                               vsBottomOff + screenH - layout.barHeight);
            break;
        case ZoneSelectorPosition::Top:
            margins = QMargins(vsLeftOff + hMargin, vsTopOff + 0, vsRightOff + hMargin,
                               vsBottomOff + std::max(0, screenH - layout.barHeight));
            break;
        case ZoneSelectorPosition::TopRight:
            margins = QMargins(vsLeftOff + screenW - layout.barWidth, vsTopOff + 0, vsRightOff + 0,
                               vsBottomOff + screenH - layout.barHeight);
            break;
        case ZoneSelectorPosition::Left:
            margins = QMargins(vsLeftOff + 0, vsTopOff + vMargin, vsRightOff + 0, vsBottomOff + vMargin);
            break;
        case ZoneSelectorPosition::Right:
            margins = QMargins(vsLeftOff + 0, vsTopOff + vMargin, vsRightOff + 0, vsBottomOff + vMargin);
            break;
        case ZoneSelectorPosition::BottomLeft:
            margins = QMargins(vsLeftOff + 0, vsTopOff + screenH - layout.barHeight,
                               vsRightOff + screenW - layout.barWidth, vsBottomOff + 0);
            break;
        case ZoneSelectorPosition::Bottom:
            margins = QMargins(vsLeftOff + hMargin, vsTopOff + std::max(0, screenH - layout.barHeight),
                               vsRightOff + hMargin, vsBottomOff + 0);
            break;
        case ZoneSelectorPosition::BottomRight:
            margins = QMargins(vsLeftOff + screenW - layout.barWidth, vsTopOff + screenH - layout.barHeight,
                               vsRightOff + 0, vsBottomOff + 0);
            break;
        case ZoneSelectorPosition::Center:
            margins = QMargins(vsLeftOff, vsTopOff, vsRightOff, vsBottomOff);
            break;
        default:
            // Default to Top margins (matches getAnchorsForPosition default)
            margins = QMargins(vsLeftOff + hMargin, vsTopOff + 0, vsRightOff + hMargin,
                               vsBottomOff + std::max(0, screenH - layout.barHeight));
            break;
        }
        // Clamp individual margin components to prevent underflow on narrow virtual screens
        margins = QMargins(qMax(0, margins.left()), qMax(0, margins.top()), qMax(0, margins.right()),
                           qMax(0, margins.bottom()));
        // Clamp margins to prevent negative window dimensions
        int totalHMargin = margins.left() + margins.right();
        if (totalHMargin >= physGeom.width()) {
            // Fall back to centering within the virtual screen
            margins.setLeft(vsLeftOff);
            margins.setRight(vsRightOff);
        }
        int totalVMargin = margins.top() + margins.bottom();
        if (totalVMargin >= physGeom.height()) {
            margins.setTop(vsTopOff);
            margins.setBottom(vsBottomOff);
        }

        // Guard against unusably narrow/short popup on very small virtual screens.
        // If the available dimension after margins is below MinUsablePopupSize,
        // fall back to filling the virtual screen area for that axis.
        constexpr int MinUsablePopupSize = 200;
        const int availableWidth = physGeom.width() - margins.left() - margins.right();
        if (availableWidth < MinUsablePopupSize) {
            margins.setLeft(vsLeftOff);
            margins.setRight(vsRightOff);
        }
        const int availableHeight = physGeom.height() - margins.top() - margins.bottom();
        if (availableHeight < MinUsablePopupSize) {
            margins.setTop(vsTopOff);
            margins.setBottom(vsBottomOff);
        }

        // Batch anchors + margins into a single propertiesChanged() emission
        // to avoid a one-frame glitch with new anchors but old margins.
        LayerSurface::BatchGuard batch(layerSurface);
        layerSurface->setAnchors(anchors);
        layerSurface->setMargins(margins);
    }
    applyZoneSelectorGeometry(window, screenGeom, layout, pos);

    if (auto* contentRoot = window->contentItem()) {
        // Ensure the root item matches the window size after geometry changes.
        // This avoids anchors evaluating against a 0x0 root during rapid updates.
        contentRoot->setWidth(window->width());
        contentRoot->setHeight(window->height());

        // Schedule polish for next render frame (NO processEvents — see #152)
        contentRoot->polish();
    }

    // Schedule QML items for layout recalculation on the next frame
    if (auto* contentRoot = window->contentItem()) {
        if (auto* gridItem = findQmlItemByName(contentRoot, QStringLiteral("zoneSelectorContentGrid"))) {
            gridItem->polish();
            gridItem->update();
        }
        if (auto* containerItem = findQmlItemByName(contentRoot, QStringLiteral("zoneSelectorContainer"))) {
            containerItem->polish();
            containerItem->update();
        }
    }
}

} // namespace PlasmaZones
