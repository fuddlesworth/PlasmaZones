// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "internal.h"
#include "../overlayservice.h"
#include "../../core/logging.h"
#include <PhosphorSurfaces/SurfaceManager.h>
#include <PhosphorZones/Layout.h>
#include <PhosphorZones/LayoutRegistry.h>
#include <PhosphorZones/Zone.h>
#include <PhosphorZones/LayoutUtils.h>
#include "../../core/geometryutils.h"
#include <PhosphorScreens/Manager.h>
#include "../../core/utils.h"
#include <PhosphorScreens/VirtualScreen.h>
#include "../../core/zoneselectorlayout.h"
#include "../config/configdefaults.h"
#include <QCursor>
#include <QScreen>
#include <QQuickWindow>
#include <QQuickItem>
#include <QQmlEngine>
#include <QVector>

#include <PhosphorLayer/Surface.h>
#include "pz_roles.h"
#include <PhosphorScreens/ScreenIdentity.h>

namespace PlasmaZones {

void OverlayService::showZoneSelector(const QString& targetScreenId)
{
    if (m_zoneSelectorVisible) {
        return;
    }

    // Check if zone selector is enabled in settings
    if (m_settings && !m_settings->zoneSelectorEnabled()) {
        return;
    }

    // A deferred recreation is pending (virtualScreensChanged handler).
    // Skip this call to avoid racing with the deferred timer, which will
    // call showZoneSelector() once layout migrations have settled.
    if (m_zoneSelectorRecreationPending) {
        return;
    }

    m_zoneSelectorVisible = true;

    // Resolve target screen from screenId (supports virtual screen IDs)
    auto* mgr = m_screenManager;
    QScreen* targetScreen = nullptr;
    if (!targetScreenId.isEmpty()) {
        targetScreen = mgr ? mgr->physicalQScreenFor(targetScreenId)
                           : Phosphor::Screens::ScreenIdentity::findByIdOrName(targetScreenId);
    }

    const QStringList effectiveIds = mgr ? mgr->effectiveScreenIds() : QStringList();

    if (mgr && !effectiveIds.isEmpty()) {
        for (const QString& screenId : effectiveIds) {
            QScreen* physScreen = mgr->physicalQScreenFor(screenId);
            if (!physScreen) {
                continue;
            }
            // Only show on the target screen (nullptr/empty = all screens)
            if (!targetScreenId.isEmpty() && screenId != targetScreenId) {
                continue;
            }
            // Skip monitors/desktops/activities where PlasmaZones is disabled
            if (isContextDisabled(m_settings, PhosphorZones::AssignmentEntry::Snapping, screenId,
                                  m_currentVirtualDesktop, m_currentActivity)) {
                continue;
            }
            // Skip autotile-managed screens (zone selector is for manual zone selection)
            if (m_excludedScreens.contains(screenId)) {
                continue;
            }
            const QRect geom = mgr->screenGeometry(screenId);
            const QRect targetGeom = geom.isValid() ? geom : physScreen->geometry();
            // Reuse the warmed surface when one already exists for this
            // screen — wlr-layer-shell v3 anchors are immutable post-attach,
            // so the only thing that forces a rebuild is the screen entry
            // disappearing entirely (handled by destroyZoneSelectorWindow on
            // screen removal). Rebuilding on every drag-near-edge would pay
            // ~50-100 ms for surface attach + Vulkan swapchain init before
            // the show animation starts.
            const auto& state = m_screenStates.value(screenId);
            if (!state.zoneSelectorWindow) {
                createZoneSelectorWindow(screenId, physScreen, targetGeom);
            }
            const auto& s = m_screenStates.value(screenId);
            auto* window = s.zoneSelectorWindow;
            auto* surface = s.zoneSelectorSurface;
            if (!window || !surface) {
                continue;
            }
            assertWindowOnScreen(window, physScreen, targetGeom);
            updateZoneSelectorWindow(screenId);
            window->setWidth(targetGeom.width());
            window->setHeight(targetGeom.height());
            qCDebug(lcOverlay) << "showZoneSelector: screen=" << screenId << "targetGeom=" << targetGeom
                               << "physScreenGeom=" << physScreen->geometry()
                               << "windowSizeAfterSet=" << window->width() << "x" << window->height();
            // Phase 5: Surface::show() drives SurfaceAnimator (panel.popup)
            // and clears Qt.WindowTransparentForInput so input routes again.
            surface->show();
        }
    } else {
        // Fallback: no Phosphor::Screens::ScreenManager
        for (auto* screen : Utils::allScreens()) {
            if (targetScreen && screen != targetScreen) {
                continue;
            }
            QString screenId = Phosphor::Screens::ScreenIdentity::identifierFor(screen);
            if (isContextDisabled(m_settings, PhosphorZones::AssignmentEntry::Snapping, screenId,
                                  m_currentVirtualDesktop, m_currentActivity)) {
                continue;
            }
            if (m_excludedScreens.contains(screenId)) {
                continue;
            }
            auto* smgr = m_screenManager;
            QRect geom = (smgr && smgr->screenGeometry(screenId).isValid()) ? smgr->screenGeometry(screenId)
                                                                            : screen->geometry();
            const auto& state = m_screenStates.value(screenId);
            if (!state.zoneSelectorWindow) {
                createZoneSelectorWindow(screenId, screen, geom);
            }
            const auto& s = m_screenStates.value(screenId);
            auto* window = s.zoneSelectorWindow;
            auto* surface = s.zoneSelectorSurface;
            if (!window || !surface) {
                continue;
            }
            assertWindowOnScreen(window, screen);
            updateZoneSelectorWindow(screenId);
            window->setWidth(geom.width());
            window->setHeight(geom.height());
            // Phase 5: Surface::show() drives SurfaceAnimator (panel.popup).
            surface->show();
        }
    }

    Q_EMIT zoneSelectorVisibilityChanged(true);
}

void OverlayService::hideZoneSelector()
{
    if (!m_zoneSelectorVisible) {
        return;
    }

    m_zoneSelectorVisible = false;

    // Note: Don't clear selected zone here — we need it for snapping when
    // drag ends. The selected zone is cleared after the snap is processed.

    // Phase 5: Surface::hide() routes through SurfaceAnimator's beginHide
    // (widget.fadeOut profile, opacity 1→0) and flips
    // Qt.WindowTransparentForInput on the QWindow. Surfaces stay alive so
    // the next drag-near-edge reuses the warmed Vulkan swapchain instead
    // of paying ~50-100 ms for a fresh layer-shell attach. Reset the QML-
    // side hover state explicitly — autoScrollTimer would otherwise tick
    // on stale cursor coords between hide and the next show.
    //
    // Restrict the reset + hide to surfaces actually in Shown state. With
    // many virtual screens, m_screenStates accumulates one entry per VS
    // even if only one selector was ever shown for the active drag; the
    // unfiltered loop wrote a small storm of redundant QML invocations on
    // every hide. Surface::hide() on a non-Shown surface is already a
    // no-op + warning, so filtering also keeps the journal quiet.
    const QStringList screenIds = m_screenStates.keys();
    for (const QString& screenId : screenIds) {
        const auto& s = m_screenStates.value(screenId);
        if (!s.zoneSelectorSurface || !s.zoneSelectorSurface->isLogicallyShown()) {
            continue;
        }
        if (s.zoneSelectorWindow) {
            QMetaObject::invokeMethod(s.zoneSelectorWindow, "resetCursorState");
        }
        s.zoneSelectorSurface->hide();
    }

    Q_EMIT zoneSelectorVisibilityChanged(false);
}

void OverlayService::updateSelectorPosition(int cursorX, int cursorY)
{
    if (!m_zoneSelectorVisible) {
        return;
    }

    // Find which screen the cursor is on
    QScreen* screen = Utils::findScreenAtPosition(cursorX, cursorY);

    if (!screen) {
        return;
    }

    // Update the zone selector window with cursor position for hover effects
    // Resolve to effective (virtual) screen ID if applicable
    QString cursorScreenId = Utils::effectiveScreenIdAt(m_screenManager, QPoint(cursorX, cursorY), screen);

    // Skip excluded screens (autotile-managed) — matches showZoneSelector exclusion
    if (m_excludedScreens.contains(cursorScreenId)) {
        return;
    }

    auto* mgr = m_screenManager;
    // Clear selection highlight on all OTHER zone selector windows when cursor moves
    // to a different virtual screen, preventing stale highlights from previous screen.
    for (auto it = m_screenStates.begin(); it != m_screenStates.end(); ++it) {
        if (it.key() != cursorScreenId && it.value().zoneSelectorWindow) {
            writeQmlProperty(it.value().zoneSelectorWindow, QStringLiteral("selectedLayoutId"), QString());
            writeQmlProperty(it.value().zoneSelectorWindow, QStringLiteral("selectedZoneIndex"), -1);
        }
    }

    if (auto* window = m_screenStates.value(cursorScreenId).zoneSelectorWindow) {
        // Convert global cursor position to window-local coordinates.
        // On Wayland with LayerShell, mapFromGlobal may return wrong coordinates
        // before the first frame (geometry not yet applied). Fall back to
        // manual translation using the window's reported geometry.
        int localX, localY;
        // On Wayland LayerShell, QWindow::geometry() is unreliable until the compositor
        // acknowledges the surface position. Prefer the stored zoneSelectorGeometry which
        // reflects the geometry we requested at creation time.
        const QRect& storedGeom = m_screenStates.value(cursorScreenId).zoneSelectorGeometry;
        const QRect winGeom = storedGeom.isValid() ? storedGeom : window->geometry();
        if (winGeom.isValid() && winGeom.width() > 0) {
            localX = cursorX - winGeom.x();
            localY = cursorY - winGeom.y();
        } else {
            const QPoint localPos = window->mapFromGlobal(QPoint(cursorX, cursorY));
            localX = localPos.x();
            localY = localPos.y();
        }

        // Selector hit-detection debug. Enable with:
        //   QT_LOGGING_RULES="org.plasmazones.overlay.debug=true"
        // Throttled to once per ~64 px cursor movement to keep the log readable
        // while still showing geometry on every meaningful position change.
        static QPoint sLastLoggedCursor(-1000000, -1000000);
        const QPoint thisCursor(cursorX, cursorY);
        const bool farMoved = std::abs(thisCursor.x() - sLastLoggedCursor.x()) > 64
            || std::abs(thisCursor.y() - sLastLoggedCursor.y()) > 64;
        if (farMoved && lcOverlay().isDebugEnabled()) {
            sLastLoggedCursor = thisCursor;
            qCDebug(lcOverlay) << "selector hit-test:"
                               << "screen=" << cursorScreenId << "cursor(global)=" << thisCursor
                               << "winGeom=" << winGeom << "windowSize=" << window->width() << "x" << window->height()
                               << "local=(" << localX << "," << localY << ")";
        }

        writeQmlProperty(window, QStringLiteral("cursorX"), localX);
        writeQmlProperty(window, QStringLiteral("cursorY"), localY);

        // Get layouts from QML window
        QVariantList layouts = window->property("layouts").toList();
        if (layouts.isEmpty()) {
            return;
        }

        const int layoutCount = layouts.size();
        const ZoneSelectorConfig selectorConfig =
            m_settings ? m_settings->resolvedZoneSelectorConfig(cursorScreenId) : defaultZoneSelectorConfig();
        // Use virtual screen geometry for layout computation when available
        const QRect vsGeom = mgr ? mgr->screenGeometry(cursorScreenId) : QRect();
        const QRect effectiveGeom = vsGeom.isValid() ? vsGeom : screen->geometry();
        const ZoneSelectorLayout layout = computeZoneSelectorLayout(selectorConfig, effectiveGeom, layoutCount);

        // Get grid position from QML - it knows exactly where the content is rendered
        int contentGridX = 0;
        int contentGridY = 0;
        QSizeF gridActualSize;

        // Per-cell actual positions, populated from the GridLayout's child
        // items in declaration order. Computing positions from
        // (cellWidth + indicatorSpacing) is wrong: when the GridLayout's
        // explicit width (= scrollContentWidth, sized for `gridColumns`
        // columns) exceeds the natural content width (when fewer cards than
        // gridColumns are populated), Qt distributes the slack across the
        // populated columns. Cards 1..N-1 then drift further right than the
        // C++ math predicts, with cumulative error per card. Reading
        // mapRectToItem on each cell is the only way to track this exactly.
        QVector<QRectF> cellRectsInWindow;
        cellRectsInWindow.reserve(layouts.size());

        if (auto* contentRoot = window->contentItem()) {
            if (auto* gridItem = findQmlItemByName(contentRoot, QStringLiteral("zoneSelectorContentGrid"))) {
                QRectF gridRect =
                    gridItem->mapRectToItem(contentRoot, QRectF(0, 0, gridItem->width(), gridItem->height()));
                contentGridX = qRound(gridRect.x());
                contentGridY = qRound(gridRect.y());
                gridActualSize = QSizeF(gridItem->width(), gridItem->height());

                // Walk grid children, skipping the Repeater (zero-size
                // sentinel) and any non-cell auxiliary items. The remaining
                // entries are the layout-card delegates in model order, so
                // cellRectsInWindow[i] corresponds to layouts[i].
                const auto kids = gridItem->childItems();
                for (QQuickItem* cell : kids) {
                    if (!cell) {
                        continue;
                    }
                    if (cell->width() <= 0 || cell->height() <= 0) {
                        continue;
                    }
                    cellRectsInWindow.append(
                        cell->mapRectToItem(contentRoot, QRectF(0, 0, cell->width(), cell->height())));
                    if (cellRectsInWindow.size() >= layouts.size()) {
                        break;
                    }
                }
            }
        }

        if (farMoved && lcOverlay().isDebugEnabled()) {
            qCDebug(lcOverlay) << "selector layout:"
                               << "columns=" << layout.columns << "rows=" << layout.rows
                               << "cellWxH=" << layout.cellWidth << "x" << layout.cellHeight
                               << "indicatorWxH=" << layout.indicatorWidth << "x" << layout.indicatorHeight
                               << "spacing=" << layout.indicatorSpacing << "cardSidePad=" << layout.cardSidePadding
                               << "cardTopMargin=" << layout.cardTopMargin << "gridOriginInWindow=(" << contentGridX
                               << "," << contentGridY << ")"
                               << "gridActualSize=" << gridActualSize << "predictedGridSize=("
                               << layout.columns * layout.cellWidth + (layout.columns - 1) * layout.indicatorSpacing
                               << "x"
                               << layout.totalRows * layout.cellHeight
                    + (layout.totalRows - 1) * layout.indicatorSpacing
                               << ")";

            // Walk the GridLayout's child cells and report where they ACTUALLY
            // render relative to contentRoot. Compare against the per-card
            // prediction emitted in the loop below — any divergence is the
            // bug. Log only the first 8 cells to keep the journal sane.
            if (auto* contentRoot = window->contentItem()) {
                if (auto* gridItem = findQmlItemByName(contentRoot, QStringLiteral("zoneSelectorContentGrid"))) {
                    const auto kids = gridItem->childItems();
                    const int dumped = std::min<int>(kids.size(), 8);
                    for (int k = 0; k < dumped; ++k) {
                        QQuickItem* cell = kids.at(k);
                        if (!cell) {
                            continue;
                        }
                        const QRectF cellRect =
                            cell->mapRectToItem(contentRoot, QRectF(0, 0, cell->width(), cell->height()));
                        qCDebug(lcOverlay) << "  actual cell[" << k << "]"
                                           << "topLeftInWindow=(" << cellRect.x() << "," << cellRect.y() << ")"
                                           << "size=" << cellRect.width() << "x" << cellRect.height();
                    }
                }
            }
        }

        // Check each layout indicator
        for (int i = 0; i < layouts.size(); ++i) {
            int indicatorX;
            int indicatorY;
            if (i < cellRectsInWindow.size()) {
                // Authoritative: read where QML actually rendered this card.
                // The previewArea inside the LayoutCard is offset by
                // cardSidePadding horizontally (anchors.horizontalCenter
                // resolves to that within an Item of width
                // indicatorWidth + 2*cardSidePadding) and cardTopMargin
                // vertically (anchors.top + topMargin = Kirigami.Units.gridUnit
                // in card mode).
                const QRectF& cellRect = cellRectsInWindow.at(i);
                indicatorX = qRound(cellRect.x()) + layout.cardSidePadding;
                indicatorY = qRound(cellRect.y()) + layout.cardTopMargin;
            } else {
                // Fallback when QML traversal missed: derive from layout math.
                // Hits the same drift bug as before, but only fires when the
                // child enumeration produced fewer cells than expected (e.g.
                // before the first frame is laid out).
                int row = (layout.columns > 0) ? (i / layout.columns) : 0;
                int col = (layout.columns > 0) ? (i % layout.columns) : 0;
                indicatorX = contentGridX + col * (layout.cellWidth + layout.indicatorSpacing) + layout.cardSidePadding;
                indicatorY = contentGridY + row * (layout.cellHeight + layout.indicatorSpacing) + layout.cardTopMargin;
            }

            if (farMoved && lcOverlay().isDebugEnabled()) {
                qCDebug(lcOverlay) << "  card[" << i << "]"
                                   << "indicator=(" << indicatorX << "," << indicatorY << ")"
                                   << "size=" << layout.indicatorWidth << "x" << layout.indicatorHeight
                                   << "containsCursor="
                                   << (localX >= indicatorX && localX < indicatorX + layout.indicatorWidth
                                       && localY >= indicatorY && localY < indicatorY + layout.indicatorHeight);
            }

            // Check if cursor is over this indicator
            if (localX >= indicatorX && localX < indicatorX + layout.indicatorWidth && localY >= indicatorY
                && localY < indicatorY + layout.indicatorHeight) {
                QVariantMap layoutMap = layouts[i].toMap();
                QString layoutId = layoutMap[QStringLiteral("id")].toString();

                // Skip non-active layouts when screen is locked (either mode)
                if (m_settings && m_layoutManager) {
                    int curDesktop = m_layoutManager->currentVirtualDesktop();
                    QString curActivity = m_layoutManager->currentActivity();
                    bool locked = isAnyModeLocked(m_settings, cursorScreenId, curDesktop, curActivity);
                    if (locked) {
                        // Only allow zone selection from the active layout
                        PhosphorZones::Layout* activeLayout = m_layoutManager->resolveLayoutForScreen(cursorScreenId);
                        if (activeLayout && layoutId != activeLayout->id().toString()) {
                            continue; // Skip this non-active layout entirely
                        }
                    }
                }

                // Per-zone hit testing
                QVariantList zones = layoutMap[QStringLiteral("zones")].toList();
                int scaledPadding = window->property("scaledPadding").toInt();
                if (scaledPadding <= 0)
                    scaledPadding = 1;
                // Read from the QML root property so the hit-test clamp
                // matches the visual clamp used by LayoutCard / ZonePreview.
                // Both sides reading the same property is the single source
                // of truth; fall back to the historical 8px default if the
                // property isn't set (older QML revisions).
                int minZoneSize = window->property("minZoneSize").toInt();
                if (minZoneSize <= 0)
                    minZoneSize = 8;

                for (int z = 0; z < zones.size(); ++z) {
                    QVariantMap zoneMap = zones[z].toMap();
                    // LayoutPreview serializes zones with flat x/y/width/height
                    // (layoutpreviewserialize.cpp::zoneMap). The legacy
                    // zonesToVariantList path produced a nested relativeGeometry
                    // sub-map. Match ZonePreview.qml's resolution order: prefer
                    // flat keys, fall back to nested. Reading nested-only made
                    // every rx/ry/rw/rh come out as 0 once LayoutPreview became
                    // the canonical wire format, collapsing every zone hit-rect
                    // to a tiny box at the indicator's top-left corner.
                    const QVariantMap relGeo = zoneMap.value(QStringLiteral("relativeGeometry")).toMap();
                    auto coord = [&](QLatin1String flatKey, QLatin1String nestedKey) {
                        const QVariant flat = zoneMap.value(flatKey);
                        if (flat.isValid() && !flat.isNull()) {
                            return flat.toReal();
                        }
                        return relGeo.value(nestedKey).toReal();
                    };
                    qreal rx = coord(QLatin1String("x"), QLatin1String("x"));
                    qreal ry = coord(QLatin1String("y"), QLatin1String("y"));
                    qreal rw = coord(QLatin1String("width"), QLatin1String("width"));
                    qreal rh = coord(QLatin1String("height"), QLatin1String("height"));

                    // Calculate zone rectangle exactly as QML does
                    int zoneX = indicatorX + static_cast<int>(rx * layout.indicatorWidth) + scaledPadding;
                    int zoneY = indicatorY + static_cast<int>(ry * layout.indicatorHeight) + scaledPadding;
                    int zoneW = std::max(minZoneSize, static_cast<int>(rw * layout.indicatorWidth) - scaledPadding * 2);
                    int zoneH =
                        std::max(minZoneSize, static_cast<int>(rh * layout.indicatorHeight) - scaledPadding * 2);

                    if (localX >= zoneX && localX < zoneX + zoneW && localY >= zoneY && localY < zoneY + zoneH) {
                        // Found the zone - update selection
                        if (m_selectedLayoutId != layoutId || m_selectedZoneIndex != z) {
                            m_selectedLayoutId = layoutId;
                            m_selectedZoneIndex = z;
                            m_selectedZoneRelGeo = QRectF(rx, ry, rw, rh);
                            writeQmlProperty(window, QStringLiteral("selectedLayoutId"), layoutId);
                            writeQmlProperty(window, QStringLiteral("selectedZoneIndex"), z);
                        }
                        return;
                    }
                }
                // Cursor is over layout indicator but not on a specific zone
                // Always clear zone selection — the cursor may have moved off a zone
                // within the same layout, so checking layout ID is not sufficient
                if (!m_selectedLayoutId.isEmpty() || m_selectedZoneIndex >= 0) {
                    m_selectedLayoutId.clear();
                    m_selectedZoneIndex = -1;
                    m_selectedZoneRelGeo = QRectF();
                    writeQmlProperty(window, QStringLiteral("selectedLayoutId"), QString());
                    writeQmlProperty(window, QStringLiteral("selectedZoneIndex"), -1);
                }
                return;
            }
        }

        // Cursor is not over any layout indicator - clear selection
        if (!m_selectedLayoutId.isEmpty()) {
            m_selectedLayoutId.clear();
            m_selectedZoneIndex = -1;
            m_selectedZoneRelGeo = QRectF();
            writeQmlProperty(window, QStringLiteral("selectedLayoutId"), QString());
            writeQmlProperty(window, QStringLiteral("selectedZoneIndex"), -1);
        }
    }
}

void OverlayService::createZoneSelectorWindow(const QString& screenId, QScreen* physScreen, const QRect& geom)
{
    // Callers reach here via showZoneSelector's per-screen loop where physScreen
    // is validated, but handleScreenAdded / setupForScreen paths can pass null
    // under racy screen-disconnect orderings. Guard explicitly — every `->`
    // below would otherwise deref null. Mirrors createOverlayWindow's pattern.
    if (!physScreen) {
        qCWarning(lcOverlay) << "createZoneSelectorWindow: null physScreen for screen=" << screenId;
        return;
    }
    if (m_screenStates.contains(screenId) && m_screenStates[screenId].zoneSelectorSurface) {
        return;
    }

    const QRect screenGeom = geom.isValid() ? geom : physScreen->geometry();

    // Build resolved per-screen config
    const ZoneSelectorConfig config =
        m_settings ? m_settings->resolvedZoneSelectorConfig(screenId) : defaultZoneSelectorConfig();

    // Confine the layer-shell surface to the (virtual) screen region. Mirrors
    // overlay.cpp's pattern via the shared layerPlacementForVs helper:
    // physical screen → AnchorAll; virtual screen → Top|Left + margins
    // offsetting into the physical screen, so the compositor positions +
    // sizes the surface to the VS sub-region. Without this, a VS selector
    // covers the whole physical screen and the QML-internal corner anchor
    // lands on the wrong VS.
    const bool isVS = PhosphorIdentity::VirtualScreenId::isVirtual(screenId);
    const auto placement = layerPlacementForVs(isVS ? screenGeom : QRect(), physScreen->geometry());
    std::optional<PhosphorLayer::Anchors> anchorsOverride(placement.anchors);
    std::optional<QMargins> marginsOverride;
    if (!placement.margins.isNull()) {
        marginsOverride = placement.margins;
    }

    // makePerInstanceRole derives the per-instance scope by appending
    // `-{screenId}-{gen}` to PzRoles::ZoneSelector's base prefix, so the
    // longest-prefix match in SurfaceAnimator's configFor lookup is
    // guaranteed by construction. Pre-fix the daemon hand-rolled the
    // literal as "plasmazones-selector-..." which did NOT start with the
    // base scope, and configFor silently fell back to the empty default
    // config — every show/hide ran on the library's 150 ms OutCubic
    // instead of panel.popup / widget.fadeOut.
    const auto role =
        PzRoles::makePerInstanceRole(PzRoles::ZoneSelector, screenId, m_surfaceManager->nextScopeGeneration());

    // keepMappedOnHide=true: Phase 5 lifecycle. Surface stays Qt-visible
    // across hide/show cycles; SurfaceAnimator drives the visual fade and
    // the library flips Qt::WindowTransparentForInput during the hide so
    // the still-mapped layer surface stops eating clicks.
    auto* surface = createLayerSurface({.qmlUrl = QUrl(QStringLiteral("qrc:/ui/ZoneSelectorWindow.qml")),
                                        .screen = physScreen,
                                        .role = role,
                                        .windowType = "zone selector",
                                        .anchorsOverride = anchorsOverride,
                                        .marginsOverride = marginsOverride,
                                        .keepMappedOnHide = true});
    if (!surface) {
        return;
    }
    auto* window = surface->window();

    // Store the intended geometry so hit-testing in updateSelectorPosition() can use it
    // before the compositor acknowledges the LayerShell surface position.
    // The margins for virtual screen confinement are applied by updateZoneSelectorWindow()
    // which is called immediately after createZoneSelectorWindow() in showZoneSelector().
    m_screenStates[screenId].zoneSelectorGeometry = screenGeom;

    // Set screen properties for layout preview scaling
    qreal aspectRatio =
        (screenGeom.height() > 0) ? static_cast<qreal>(screenGeom.width()) / screenGeom.height() : (16.0 / 9.0);
    aspectRatio = qBound(0.5, aspectRatio, 4.0);
    writeQmlProperty(window, QStringLiteral("screenAspectRatio"), aspectRatio);
    writeQmlProperty(window, QStringLiteral("screenWidth"), screenGeom.width());

    // Pass zone appearance settings for scaled preview (global settings)
    if (m_settings) {
        writeQmlProperty(window, QStringLiteral("zonePadding"), m_settings->zonePadding());
        writeQmlProperty(window, QStringLiteral("zoneBorderWidth"), m_settings->borderWidth());
        writeQmlProperty(window, QStringLiteral("zoneBorderRadius"), m_settings->borderRadius());
    }
    // Pass resolved per-screen config values to QML
    writeQmlProperty(window, QStringLiteral("selectorPosition"), config.position);
    writeQmlProperty(window, QStringLiteral("selectorLayoutMode"), config.layoutMode);
    writeQmlProperty(window, QStringLiteral("selectorGridColumns"), config.gridColumns);
    writeQmlProperty(window, QStringLiteral("previewWidth"), config.previewWidth);
    writeQmlProperty(window, QStringLiteral("previewHeight"), config.previewHeight);
    writeQmlProperty(window, QStringLiteral("previewLockAspect"), config.previewLockAspect);

    // Initial layout is applied by updateZoneSelectorWindow() which is always
    // called immediately after createZoneSelectorWindow() in showZoneSelector().

    auto conn = connect(window, SIGNAL(zoneSelected(QString, int, QVariant)), this,
                        SLOT(onZoneSelected(QString, int, QVariant)));
    if (!conn) {
        qCWarning(lcOverlay) << "Failed to connect zoneSelected signal for screen" << screenId
                             << "- zone selector layout switching will not work";
    }
    auto& state = m_screenStates[screenId];
    state.zoneSelectorSurface = surface;
    state.zoneSelectorWindow = window;
    state.zoneSelectorPhysScreen = physScreen;
}

void OverlayService::destroyZoneSelectorWindow(const QString& screenId)
{
    auto it = m_screenStates.find(screenId);
    if (it != m_screenStates.end()) {
        if (it->zoneSelectorSurface) {
            it->zoneSelectorSurface->deleteLater();
            it->zoneSelectorSurface = nullptr;
            it->zoneSelectorWindow = nullptr;
        }
        it->zoneSelectorPhysScreen = nullptr;
        // Hit-testing on updateSelectorPosition() gates reads on
        // zoneSelectorWindow != nullptr, but clearing the cached geometry
        // alongside the window keeps the state entry consistent if a
        // future reader ever drops that guard.
        it->zoneSelectorGeometry = QRect();
    }
}

bool OverlayService::hasSelectedZone() const
{
    return !m_selectedLayoutId.isEmpty() && m_selectedZoneIndex >= 0;
}

void OverlayService::clearSelectedZone()
{
    m_selectedLayoutId.clear();
    m_selectedZoneIndex = -1;
    m_selectedZoneRelGeo = QRectF();
}

QRect OverlayService::getSelectedZoneGeometry(QScreen* screen) const
{
    if (!hasSelectedZone() || !screen) {
        return QRect();
    }
    // Delegate to screenId overload for virtual-screen-aware geometry.
    // WARNING: QCursor::pos() may be stale on Wayland. Callers should prefer
    // the getSelectedZoneGeometry(const QString& screenId) overload when possible.
    QString screenId = Utils::effectiveScreenIdAt(m_screenManager, QCursor::pos(), screen);
    return getSelectedZoneGeometry(screenId);
}

QRect OverlayService::getSelectedZoneGeometry(const QString& screenId) const
{
    if (!hasSelectedZone()) {
        return QRect();
    }

    auto* mgr = m_screenManager;
    QScreen* physScreen =
        mgr ? mgr->physicalQScreenFor(screenId) : Phosphor::Screens::ScreenIdentity::findByIdOrName(screenId);

    // Primary path: use layout/zone geometry pipeline with virtual screen bounds
    if (m_layoutManager && !m_selectedLayoutId.isEmpty()) {
        PhosphorZones::Layout* selectedLayout = m_layoutManager->layoutById(QUuid::fromString(m_selectedLayoutId));
        if (selectedLayout && m_selectedZoneIndex >= 0
            && m_selectedZoneIndex < static_cast<int>(selectedLayout->zones().size())) {
            PhosphorZones::Zone* zone = selectedLayout->zones().at(m_selectedZoneIndex);
            if (zone) {
                QRect result = GeometryUtils::getZoneGeometryForScreen(m_screenManager, zone, physScreen, screenId,
                                                                       selectedLayout, m_settings);
                if (result.isValid()) {
                    return result;
                }
            }
        }
    }

    // Fallback: manual calculation using relative geometry
    QRect areaGeom;
    if (mgr) {
        QRect vsAvailGeom = mgr->screenAvailableGeometry(screenId);
        if (vsAvailGeom.isValid()) {
            areaGeom = vsAvailGeom;
        }
    }
    if (!areaGeom.isValid() && physScreen) {
        areaGeom = (m_screenManager ? m_screenManager->actualAvailableGeometry(physScreen)
                                    : ((physScreen) ? (physScreen)->availableGeometry() : QRect()));
    }
    if (!areaGeom.isValid()) {
        return QRect();
    }

    QRectF geom(areaGeom.x() + m_selectedZoneRelGeo.x() * areaGeom.width(),
                areaGeom.y() + m_selectedZoneRelGeo.y() * areaGeom.height(),
                m_selectedZoneRelGeo.width() * areaGeom.width(), m_selectedZoneRelGeo.height() * areaGeom.height());
    return GeometryUtils::snapToRect(geom);
}

void OverlayService::onZoneSelected(const QString& layoutId, int zoneIndex, const QVariant& relativeGeometry)
{
    m_selectedLayoutId = layoutId;
    m_selectedZoneIndex = zoneIndex;

    // Convert QVariant to QVariantMap and extract relative geometry
    QVariantMap relGeoMap = relativeGeometry.toMap();
    qreal x = relGeoMap.value(QStringLiteral("x"), 0.0).toReal();
    qreal y = relGeoMap.value(QStringLiteral("y"), 0.0).toReal();
    qreal width = relGeoMap.value(QStringLiteral("width"), 0.0).toReal();
    qreal height = relGeoMap.value(QStringLiteral("height"), 0.0).toReal();
    m_selectedZoneRelGeo = QRectF(x, y, width, height);

    // Determine which screen the zone selector is on from the sender window
    // Primary: look up in our window-to-screen map (authoritative assignment)
    // Fallback: use Qt's screen assignment on the window itself
    QString screenId;
    auto* senderWindow = qobject_cast<QQuickWindow*>(sender());
    if (senderWindow) {
        for (auto it = m_screenStates.constBegin(); it != m_screenStates.constEnd(); ++it) {
            if (it.value().zoneSelectorWindow == senderWindow) {
                screenId = it.key();
                break;
            }
        }
        if (screenId.isEmpty() && senderWindow->screen()) {
            screenId = Phosphor::Screens::ScreenIdentity::identifierFor(senderWindow->screen());
        }
    }

    // Route to the correct signal based on whether this is an autotile algorithm or manual layout
    if (PhosphorLayout::LayoutId::isAutotile(layoutId)) {
        const QString algoId = PhosphorLayout::LayoutId::extractAlgorithmId(layoutId);
        qCInfo(lcOverlay) << "Zone selector: autotile algorithm selected, algoId=" << algoId << "screen=" << screenId;
        Q_EMIT autotileLayoutSelected(algoId, screenId);
    } else {
        qCInfo(lcOverlay) << "Zone selector: layout selected, layoutId=" << layoutId << "screen=" << screenId;
        Q_EMIT manualLayoutSelected(layoutId, screenId);
    }
}

void OverlayService::scrollZoneSelector(int angleDeltaY)
{
    if (!m_zoneSelectorVisible) {
        return;
    }
    for (auto it_ = m_screenStates.constBegin(); it_ != m_screenStates.constEnd(); ++it_) {
        auto* window = it_.value().zoneSelectorWindow;
        if (window) {
            QMetaObject::invokeMethod(window, "applyScrollDelta", Q_ARG(QVariant, angleDeltaY));
        }
    }
}

} // namespace PlasmaZones
