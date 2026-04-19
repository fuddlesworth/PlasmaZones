// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "internal.h"
#include "../overlayservice.h"
#include "../../core/logging.h"
#include <PhosphorZones/Layout.h>
#include <PhosphorZones/LayoutManager.h>
#include <PhosphorZones/Zone.h>
#include <PhosphorZones/LayoutUtils.h>
#include "../../core/geometryutils.h"
#include <PhosphorScreens/Manager.h>
#include "../../core/utils.h"
#include "../../core/zoneselectorlayout.h"
#include "../config/configdefaults.h"
#include <QScreen>
#include <QQuickWindow>
#include <QQuickItem>
#include <QQmlEngine>

namespace PlasmaZones {

namespace {

void updateZoneSelectorComputedProperties(Phosphor::Screens::ScreenManager* mgr, QQuickWindow* window, QScreen* screen,
                                          const QString& virtualScreenId, const ZoneSelectorConfig& config,
                                          ISettings* settings, const ZoneSelectorLayout& layout)
{
    if (!window || !screen) {
        return;
    }

    // Use virtual screen geometry if available, falling back to physical
    const QRect screenGeom = resolveScreenGeometry(mgr, virtualScreenId);
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

// Size the zone selector window to fill the entire (virtual) screen.
// With AnchorAll the compositor sizes the surface to the full output;
// the QML root uses internal anchors (selectorPosition state) to position
// the visible bar in the chosen corner of the transparent window.
void applyZoneSelectorGeometry(QQuickWindow* window, const QRect& screenGeom, const ZoneSelectorLayout& /*layout*/,
                               ZoneSelectorPosition /*pos*/)
{
    if (!window || !screenGeom.isValid()) {
        return;
    }
    window->setWidth(screenGeom.width());
    window->setHeight(screenGeom.height());
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
    const QRect screenGeom = resolveScreenGeometry(m_screenManager, screenId);
    qreal aspectRatio =
        (screenGeom.height() > 0) ? static_cast<qreal>(screenGeom.width()) / screenGeom.height() : (16.0 / 9.0);
    aspectRatio = qBound(0.5, aspectRatio, 4.0);
    writeQmlProperty(window, QStringLiteral("screenAspectRatio"), aspectRatio);
    writeQmlProperty(window, QStringLiteral("screenWidth"), screenGeom.width());

    // Build resolved per-screen config
    const ZoneSelectorConfig config =
        m_settings ? m_settings->resolvedZoneSelectorConfig(screenId) : defaultZoneSelectorConfig();

    // Update settings-based properties
    if (m_settings) {
        writeColorSettings(window, m_settings);
        // PhosphorZones::Zone appearance settings for scaled preview (global)
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
    PhosphorZones::Layout* screenLayout = resolveScreenLayout(screenId);
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
    updateZoneSelectorComputedProperties(m_screenManager, window, screen, screenId, config, m_settings, layout);

    // Positioning is entirely QML-internal: ZoneSelectorWindow.qml's
    // selectorPosition state anchors the inner container to the requested
    // corner of the full-screen transparent surface. Anchors/margins are
    // baked at attach time (AnchorAll) and never mutated afterwards.
    applyZoneSelectorGeometry(window, screenGeom, layout, pos);

    // Keep stored geometry in sync so hit-testing uses the current value
    m_screenStates[screenId].zoneSelectorGeometry = screenGeom;

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
