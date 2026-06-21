// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "internal.h"
#include "../overlayservice.h"
#include "../../core/logging.h"
#include <PhosphorZones/Layout.h>
#include <PhosphorZones/LayoutRegistry.h>
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

void updateZoneSelectorComputedProperties(PhosphorScreens::ScreenManager* mgr, QObject* window, QScreen* screen,
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

    // Compute scaled zone appearance values. Zone padding honors per-screen
    // overrides (resolution cascade: per-screen → global → default); border
    // width/radius are global-only settings (no per-screen key exists).
    if (settings) {
        const int zonePadding = GeometryUtils::getEffectiveZonePadding(nullptr, settings, virtualScreenId);
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

void applyZoneSelectorLayout(QObject* window, const ZoneSelectorLayout& layout)
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

// Size the zone selector slot to fill the entire (virtual) screen.
// Post-shell-migration the slot is a QQuickItem (anchors.fill on the shell
// QQuickWindow). The QML root uses internal anchors (selectorPosition
// state) to position the visible bar in the chosen corner of the
// transparent slot.
void applyZoneSelectorGeometry(QQuickItem* slot, const QRect& screenGeom, const ZoneSelectorLayout& /*layout*/,
                               ZoneSelectorPosition /*pos*/)
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

    auto* window = m_screenStates.value(screenId).zoneSelectorSlot();
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
        // PhosphorZones::Zone appearance for the scaled preview. Zone padding
        // honors per-screen overrides (per-screen → global → default); border
        // width/radius are global-only (no per-screen key exists).
        writeQmlProperty(window, QStringLiteral("zonePadding"),
                         GeometryUtils::getEffectiveZonePadding(nullptr, m_settings, screenId));
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

    // Global "Auto-assign for all layouts" master toggle (#370) - when on, every
    // layout effectively auto-assigns regardless of its per-layout flag. Pushed
    // here so the badge in each LayoutCard shows the effective state.
    writeQmlProperty(window, QStringLiteral("globalAutoAssign"), m_settings && m_settings->autoAssignAllLayouts());

    // Set active layout ID for this screen
    // Per-screen assignment takes priority so each monitor highlights its own layout
    QString activeLayoutId;
    PhosphorZones::Layout* screenLayout = resolveScreenLayout(screenId);
    if (screenLayout) {
        activeLayoutId = screenLayout->id().toString();
    }
    writeQmlProperty(window, QStringLiteral("activeLayoutId"), activeLayoutId);

    // Push lock state so QML disables non-active layout interaction.
    // isAnyModeLocked checks a LockContext rule first, then both manual modes -
    // the zone selector appears during drag for the current mode.
    bool locked = false;
    if (m_settings && m_layoutManager) {
        int curDesktop = currentVirtualDesktopForScreen(screenId);
        QString curActivity = m_layoutManager->currentActivity();
        locked = isAnyModeLocked(m_settings, m_layoutManager, screenId, curDesktop, curActivity);
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
    const QString curActivity = m_layoutManager->currentActivity();

    // Open zone selectors: one entry per screen with a live slot.
    for (auto it = m_screenStates.constBegin(); it != m_screenStates.constEnd(); ++it) {
        auto* window = it.value().zoneSelectorSlot();
        if (!window) {
            continue;
        }
        // Per-output virtual desktops (#648): each screen resolves its own desktop.
        const int curDesktop = currentVirtualDesktopForScreen(it.key());
        const bool locked = isAnyModeLocked(m_settings, m_layoutManager, it.key(), curDesktop, curActivity);
        writeQmlProperty(window, QStringLiteral("locked"), locked);
    }

    // Open layout picker (re-running showLayoutPicker would rebuild/re-animate
    // it, so push just the lock state to the live slot).
    if (m_layoutPickerVisible && !m_layoutPickerScreenId.isEmpty()) {
        if (auto* slot = m_screenStates.value(m_layoutPickerScreenId).layoutPickerSlot()) {
            // Per-output virtual desktops (#648): each screen resolves its own desktop.
            const int curDesktop = currentVirtualDesktopForScreen(m_layoutPickerScreenId);
            const bool locked =
                isAnyModeLocked(m_settings, m_layoutManager, m_layoutPickerScreenId, curDesktop, curActivity);
            writeQmlProperty(slot, QStringLiteral("locked"), locked);
        }
    }
}

} // namespace PlasmaZones
