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

    if (m_settings && !m_settings->zoneSelectorEnabled()) {
        return;
    }

    if (m_zoneSelectorRecreationPending) {
        return;
    }

    m_zoneSelectorVisible = true;

    auto* mgr = m_screenManager;
    QScreen* targetScreen = nullptr;
    if (!targetScreenId.isEmpty()) {
        targetScreen = mgr ? mgr->physicalQScreenFor(targetScreenId)
                           : Phosphor::Screens::ScreenIdentity::findByIdOrName(targetScreenId);
    }

    const QStringList effectiveIds = mgr ? mgr->effectiveScreenIds() : QStringList();

    auto showOnScreen = [this](const QString& screenId, QScreen* physScreen, const QRect& targetGeom) {
        auto* state = ensurePassiveShellFor(screenId, physScreen);
        if (!state || !state->passiveShellSurface || !state->passiveShellZoneSelectorSlot) {
            return;
        }
        // Mirror legacy zoneSelector* fields onto the shell so existing
        // hit-test / config code that reads these continues to work.
        state->zoneSelectorSurface = state->passiveShellSurface;
        state->zoneSelectorWindow = state->passiveShellWindow;
        state->zoneSelectorPhysScreen = physScreen;
        state->zoneSelectorGeometry = targetGeom;
        if (state->passiveShellWindow) {
            assertWindowOnScreen(state->passiveShellWindow, physScreen, targetGeom);
            state->passiveShellWindow->setWidth(targetGeom.width());
            state->passiveShellWindow->setHeight(targetGeom.height());
        }
        updateZoneSelectorWindow(screenId);
        auto* slot = state->passiveShellZoneSelectorSlot;
        // OSD-style content lifecycle: toggle `loaded` false→true so the
        // Loader re-instantiates ZoneSelectorContent fresh per show.
        writeQmlProperty(slot, QStringLiteral("loaded"), false);
        writeQmlProperty(slot, QStringLiteral("loaded"), true);
        cancelSurfacePrime(state->passiveShellSurface);
        if (!state->passiveShellSurface->isLogicallyShown()) {
            state->passiveShellSurface->show();
        }
        slot->setVisible(true);
        m_surfaceAnimator->beginShow(state->passiveShellSurface, slot, PzRoles::ZoneSelector, []() { });
    };

    if (mgr && !effectiveIds.isEmpty()) {
        for (const QString& screenId : effectiveIds) {
            QScreen* physScreen = mgr->physicalQScreenFor(screenId);
            if (!physScreen) {
                continue;
            }
            if (!targetScreenId.isEmpty() && screenId != targetScreenId) {
                continue;
            }
            if (isContextDisabled(m_settings, PhosphorZones::AssignmentEntry::Snapping, screenId,
                                  m_currentVirtualDesktop, m_currentActivity)) {
                continue;
            }
            if (m_excludedScreens.contains(screenId)) {
                continue;
            }
            const QRect geom = mgr->screenGeometry(screenId);
            const QRect targetGeom = geom.isValid() ? geom : physScreen->geometry();
            showOnScreen(screenId, physScreen, targetGeom);
        }
    } else {
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
            showOnScreen(screenId, screen, geom);
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

    // Selected zone NOT cleared here — drag-end snap path needs it.
    //
    // Iterate every screen state and animate a slot-hide on each one
    // whose zone selector slot is currently visible. The shell surface
    // itself stays mapped — only the slot Item's opacity fades out.
    const QStringList screenIds = m_screenStates.keys();
    for (const QString& screenId : screenIds) {
        auto& s = m_screenStates[screenId];
        auto* slot = s.passiveShellZoneSelectorSlot;
        if (!slot || !slot->isVisible()) {
            continue;
        }
        QMetaObject::invokeMethod(slot, "resetCursorState");
        m_surfaceAnimator->beginHide(s.passiveShellSurface, slot, PzRoles::ZoneSelector,
                                     [this, effectiveId = screenId]() {
                                         onZoneSelectorSlotHideCompleted(effectiveId);
                                     });
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
    // Clear selection highlight on all OTHER zone selector slots when cursor
    // moves to a different VS, preventing stale highlights from previous VS.
    for (auto it = m_screenStates.begin(); it != m_screenStates.end(); ++it) {
        if (it.key() != cursorScreenId && it.value().passiveShellZoneSelectorSlot) {
            writeQmlProperty(it.value().passiveShellZoneSelectorSlot, QStringLiteral("selectedLayoutId"), QString());
            writeQmlProperty(it.value().passiveShellZoneSelectorSlot, QStringLiteral("selectedZoneIndex"), -1);
        }
    }

    if (auto* slot = m_screenStates.value(cursorScreenId).passiveShellZoneSelectorSlot) {
        auto* window = m_screenStates.value(cursorScreenId).passiveShellWindow;
        // Convert global cursor position to window-local coordinates.
        int localX, localY;
        const QRect& storedGeom = m_screenStates.value(cursorScreenId).zoneSelectorGeometry;
        const QRect winGeom = storedGeom.isValid() ? storedGeom : (window ? window->geometry() : QRect());
        if (winGeom.isValid() && winGeom.width() > 0) {
            localX = cursorX - winGeom.x();
            localY = cursorY - winGeom.y();
        } else if (window) {
            const QPoint localPos = window->mapFromGlobal(QPoint(cursorX, cursorY));
            localX = localPos.x();
            localY = localPos.y();
        } else {
            return;
        }

        static QPoint sLastLoggedCursor(-1000000, -1000000);
        const QPoint thisCursor(cursorX, cursorY);
        const bool farMoved = std::abs(thisCursor.x() - sLastLoggedCursor.x()) > 64
            || std::abs(thisCursor.y() - sLastLoggedCursor.y()) > 64;
        if (farMoved && lcOverlay().isDebugEnabled()) {
            sLastLoggedCursor = thisCursor;
            qCDebug(lcOverlay) << "selector hit-test:"
                               << "screen=" << cursorScreenId << "cursor(global)=" << thisCursor
                               << "winGeom=" << winGeom << "local=(" << localX << "," << localY << ")";
        }

        writeQmlProperty(slot, QStringLiteral("cursorX"), localX);
        writeQmlProperty(slot, QStringLiteral("cursorY"), localY);

        QVariantList layouts = slot->property("layouts").toList();
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

        // Walk the slot subtree (the shell's contentItem hosts multiple
        // sibling slot Items; rooting traversal at the zone-selector slot
        // limits results to that slot's content).
        QQuickItem* contentRoot = slot;
        if (auto* gridItem = findQmlItemByName(contentRoot, QStringLiteral("zoneSelectorContentGrid"))) {
            QRectF gridRect = gridItem->mapRectToItem(contentRoot, QRectF(0, 0, gridItem->width(), gridItem->height()));
            contentGridX = qRound(gridRect.x());
            contentGridY = qRound(gridRect.y());
            gridActualSize = QSizeF(gridItem->width(), gridItem->height());

            const auto kids = gridItem->childItems();
            for (QQuickItem* cell : kids) {
                if (!cell) {
                    continue;
                }
                if (cell->width() <= 0 || cell->height() <= 0) {
                    continue;
                }
                cellRectsInWindow.append(cell->mapRectToItem(contentRoot, QRectF(0, 0, cell->width(), cell->height())));
                if (cellRectsInWindow.size() >= layouts.size()) {
                    break;
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

            QQuickItem* contentRoot = slot;
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
                int scaledPadding = slot->property("scaledPadding").toInt();
                if (scaledPadding <= 0)
                    scaledPadding = 1;
                int minZoneSize = slot->property("minZoneSize").toInt();
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
                        if (m_selectedLayoutId != layoutId || m_selectedZoneIndex != z) {
                            m_selectedLayoutId = layoutId;
                            m_selectedZoneIndex = z;
                            m_selectedZoneRelGeo = QRectF(rx, ry, rw, rh);
                            writeQmlProperty(slot, QStringLiteral("selectedLayoutId"), layoutId);
                            writeQmlProperty(slot, QStringLiteral("selectedZoneIndex"), z);
                        }
                        return;
                    }
                }
                if (!m_selectedLayoutId.isEmpty() || m_selectedZoneIndex >= 0) {
                    m_selectedLayoutId.clear();
                    m_selectedZoneIndex = -1;
                    m_selectedZoneRelGeo = QRectF();
                    writeQmlProperty(slot, QStringLiteral("selectedLayoutId"), QString());
                    writeQmlProperty(slot, QStringLiteral("selectedZoneIndex"), -1);
                }
                return;
            }
        }

        if (!m_selectedLayoutId.isEmpty()) {
            m_selectedLayoutId.clear();
            m_selectedZoneIndex = -1;
            m_selectedZoneRelGeo = QRectF();
            writeQmlProperty(slot, QStringLiteral("selectedLayoutId"), QString());
            writeQmlProperty(slot, QStringLiteral("selectedZoneIndex"), -1);
        }
    }
}

void OverlayService::createZoneSelectorWindow(const QString& screenId, QScreen* physScreen, const QRect& geom)
{
    // Post-shell-migration: the per-VS PzRoles::ZoneSelector wl_surface
    // is replaced by an Item slot inside the per-screen passive shell.
    // This function is now a thin alias for ensurePassiveShellFor +
    // initial property push; the showZoneSelector show-loop already
    // routes through showOnScreen which calls ensurePassiveShellFor.
    if (!physScreen) {
        qCWarning(lcOverlay) << "createZoneSelectorWindow: null physScreen for screen=" << screenId;
        return;
    }
    auto* state = ensurePassiveShellFor(screenId, physScreen);
    if (!state || !state->passiveShellZoneSelectorSlot) {
        return;
    }
    state->zoneSelectorSurface = state->passiveShellSurface;
    state->zoneSelectorWindow = state->passiveShellWindow;
    state->zoneSelectorPhysScreen = physScreen;

    const QRect screenGeom = geom.isValid() ? geom : physScreen->geometry();
    state->zoneSelectorGeometry = screenGeom;

    auto* slot = state->passiveShellZoneSelectorSlot;
    qreal aspectRatio =
        (screenGeom.height() > 0) ? static_cast<qreal>(screenGeom.width()) / screenGeom.height() : (16.0 / 9.0);
    aspectRatio = qBound(0.5, aspectRatio, 4.0);
    writeQmlProperty(slot, QStringLiteral("screenAspectRatio"), aspectRatio);
    writeQmlProperty(slot, QStringLiteral("screenWidth"), screenGeom.width());
    if (m_settings) {
        writeQmlProperty(slot, QStringLiteral("zonePadding"), m_settings->zonePadding());
        writeQmlProperty(slot, QStringLiteral("zoneBorderWidth"), m_settings->borderWidth());
        writeQmlProperty(slot, QStringLiteral("zoneBorderRadius"), m_settings->borderRadius());
    }
    const ZoneSelectorConfig config =
        m_settings ? m_settings->resolvedZoneSelectorConfig(screenId) : defaultZoneSelectorConfig();
    writeQmlProperty(slot, QStringLiteral("selectorPosition"), config.position);
    writeQmlProperty(slot, QStringLiteral("selectorLayoutMode"), config.layoutMode);
    writeQmlProperty(slot, QStringLiteral("selectorGridColumns"), config.gridColumns);
    writeQmlProperty(slot, QStringLiteral("previewWidth"), config.previewWidth);
    writeQmlProperty(slot, QStringLiteral("previewHeight"), config.previewHeight);
    writeQmlProperty(slot, QStringLiteral("previewLockAspect"), config.previewLockAspect);
    // zoneSelected signal wiring lives in ensurePassiveShellFor so it's
    // installed once per shell rather than per screen-state init pass.
}

void OverlayService::destroyZoneSelectorWindow(const QString& screenId)
{
    auto it = m_screenStates.find(screenId);
    if (it != m_screenStates.end()) {
        // Slot lifetime is the shell's; clearing the legacy alias fields
        // is enough — destroying the shell (via destroyNotificationWindow)
        // tears the slot down with it.
        it->zoneSelectorSurface = nullptr;
        it->zoneSelectorWindow = nullptr;
        it->zoneSelectorPhysScreen = nullptr;
        it->zoneSelectorGeometry = QRect();
    }
}

void OverlayService::onZoneSelectorSlotHideCompleted(const QString& effectiveId)
{
    auto it = m_screenStates.find(effectiveId);
    if (it == m_screenStates.end() || !it->passiveShellZoneSelectorSlot) {
        return;
    }
    it->passiveShellZoneSelectorSlot->setVisible(false);
    writeQmlProperty(it->passiveShellZoneSelectorSlot, QStringLiteral("loaded"), false);
    syncPassiveShellSurfaceState(effectiveId);
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
    // The zoneSelected signal is forwarded by the shell window, so
    // sender() is the shell QQuickWindow. The shell hosts every
    // kbd-None overlay slot for one screen; matching to its
    // passiveShellWindow yields the screen id.
    QString screenId;
    auto* senderWindow = qobject_cast<QQuickWindow*>(sender());
    if (senderWindow) {
        for (auto it = m_screenStates.constBegin(); it != m_screenStates.constEnd(); ++it) {
            if (it.value().passiveShellWindow == senderWindow) {
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
        auto* slot = it_.value().passiveShellZoneSelectorSlot;
        if (slot) {
            QMetaObject::invokeMethod(slot, "applyScrollDelta", Q_ARG(QVariant, angleDeltaY));
        }
    }
}

} // namespace PlasmaZones
