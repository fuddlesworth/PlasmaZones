// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "internal.h"
#include "../overlayservice.h"
#include "../../core/logging.h"
#include "phosphor_slot_keys.h"
#include <PhosphorOverlay/ShellHost.h>
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
#include "phosphor_roles.h"
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
        targetScreen = mgr ? mgr->physicalScreenFor(targetScreenId).qscreen
                           : PhosphorScreens::ScreenIdentity::findByIdOrName(targetScreenId);
    }

    const QStringList effectiveIds = mgr ? mgr->effectiveScreenIds() : QStringList();

    auto showOnScreen = [this](const QString& screenId, QScreen* physScreen, const QRect& targetGeom) {
        auto* state = ensurePassiveShellFor(screenId, physScreen);
        if (!state || !state->shell || !state->shell->shellSurface() || !state->zoneSelectorSlot()) {
            return;
        }
        state->zoneSelectorPhysScreen = physScreen;
        state->zoneSelectorGeometry = targetGeom;
        if (state->shell->shellWindow()) {
            assertWindowOnScreen(state->shell->shellWindow(), physScreen, targetGeom);
            state->shell->shellWindow()->setWidth(targetGeom.width());
            state->shell->shellWindow()->setHeight(targetGeom.height());
        }
        updateZoneSelectorWindow(screenId);
        auto* slot = state->zoneSelectorSlot();
        // OSD-style content lifecycle: toggle `loaded` false→true so the
        // Loader re-instantiates ZoneSelectorContent fresh per show.
        writeQmlProperty(slot, QStringLiteral("loaded"), false);
        writeQmlProperty(slot, QStringLiteral("loaded"), true);
        cancelSurfacePrime(state->shell->shellSurface());
        if (!state->shell->shellSurface()->isLogicallyShown()) {
            state->shell->shellSurface()->show();
        }
        slot->setVisible(true);
        m_surfaceAnimator->beginShow(state->shell->shellSurface(), slot, PhosphorRoles::ZoneSelector, []() { });
        // Zone selector is purely visual during a drag (KWin owns the
        // drag stream and pushes cursor coords via D-Bus
        // updateSelectorPosition). Sync the input region so a stale
        // shell grab from a prior modal-up state can't bleed across.
        syncPassiveShellSurfaceStateForSurface(state->shell->shellSurface());
    };

    if (mgr && !effectiveIds.isEmpty()) {
        for (const QString& screenId : effectiveIds) {
            QScreen* physScreen = mgr->physicalScreenFor(screenId).qscreen;
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
            QString screenId = PhosphorScreens::ScreenIdentity::identifierFor(screen);
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

    // Selected zone NOT cleared here - drag-end snap path needs it.
    //
    // Snapshot keys before iterating so the completion lambda
    // (potentially fired synchronously by ShellHost::hideSlot on a
    // benign no-op path) cannot invalidate the loop's iterators by
    // inserting into m_screenStates (rehash) via any indirect call
    // path. operator[] on a key already present is safe; an insert is
    // not. The snapshot makes the iteration robust against any future
    // completion-path edits that may add screens.
    const QStringList screenKeys = m_screenStates.keys();
    for (const QString& screenId : screenKeys) {
        auto it = m_screenStates.find(screenId);
        if (it == m_screenStates.end()) {
            continue;
        }
        auto* slot = it->zoneSelectorSlot();
        if (!slot || !slot->isVisible()) {
            continue;
        }
        QMetaObject::invokeMethod(slot, "resetCursorState");
        m_shellHost->hideSlot(screenId, PhosphorSlotKeys::ZoneSelector(), [this, screenId]() {
            onZoneSelectorSlotHideCompleted(screenId);
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

    // Skip excluded screens (autotile-managed) - matches showZoneSelector exclusion
    if (m_excludedScreens.contains(cursorScreenId)) {
        return;
    }

    auto* mgr = m_screenManager;
    // Clear selection highlight on all OTHER zone selector slots when cursor
    // moves to a different VS, preventing stale highlights from previous VS.
    for (auto it = m_screenStates.begin(); it != m_screenStates.end(); ++it) {
        if (it.key() != cursorScreenId && it.value().zoneSelectorSlot()) {
            writeQmlProperty(it.value().zoneSelectorSlot(), QStringLiteral("selectedLayoutId"), QString());
            writeQmlProperty(it.value().zoneSelectorSlot(), QStringLiteral("selectedZoneIndex"), -1);
        }
    }

    auto cursorStateIt = m_screenStates.constFind(cursorScreenId);
    if (cursorStateIt != m_screenStates.constEnd() && cursorStateIt->zoneSelectorSlot()) {
        auto* slot = cursorStateIt->zoneSelectorSlot();
        auto* window = cursorStateIt->shell ? cursorStateIt->shell->shellWindow() : nullptr;
        // Convert global cursor position to window-local coordinates.
        int localX, localY;
        const QRect& storedGeom = cursorStateIt->zoneSelectorGeometry;
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

            // contentRoot is the same value resolved at line 251 above.
            // Re-using that binding inside the debug block keeps the cell-
            // dump traversal rooted at the slot Item.
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
                    bool locked = isAnyModeLocked(m_settings, m_layoutManager, cursorScreenId, curDesktop, curActivity);
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
    // Post-shell-migration: the per-VS PhosphorRoles::ZoneSelector wl_surface
    // is replaced by an Item slot inside the per-screen passive shell.
    // This function is now a thin alias for ensurePassiveShellFor +
    // initial property push; the showZoneSelector show-loop already
    // routes through showOnScreen which calls ensurePassiveShellFor.
    if (!physScreen) {
        qCWarning(lcOverlay) << "createZoneSelectorWindow: null physScreen for screen=" << screenId;
        return;
    }
    auto* state = ensurePassiveShellFor(screenId, physScreen);
    if (!state || !state->zoneSelectorSlot()) {
        return;
    }
    state->zoneSelectorPhysScreen = physScreen;

    const QRect screenGeom = geom.isValid() ? geom : physScreen->geometry();
    state->zoneSelectorGeometry = screenGeom;

    auto* slot = state->zoneSelectorSlot();
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
    // No signal wiring: the slot is input-transparent by design — hit-testing
    // and commit both happen in C++ (updateSelectorPosition + drop.cpp).
}

void OverlayService::destroyZoneSelectorWindow(const QString& screenId)
{
    auto it = m_screenStates.find(screenId);
    if (it != m_screenStates.end()) {
        // Slot lifetime is the shell's - destroyPassiveShell tears the
        // slot Item down with it. But if the slot is currently
        // animating-visible at the moment of context-disable
        // (e.g. user disabled snapping mid-drag), we need to drive
        // the visible→hidden transition on this screen now so the
        // slot doesn't stay painted on the still-mapped shell.
        // hideZoneSelectorSlotOnScreen is a no-op when slot is
        // already invisible.
        hideZoneSelectorSlotOnScreen(screenId);
        it->zoneSelectorPhysScreen = nullptr;
        it->zoneSelectorGeometry = QRect();
        // If this screen was the active zone-selector source, clear the
        // global visible flag so a fresh show after hot-plug isn't
        // gated out by the early-return at showZoneSelector's top.
        if (m_zoneSelectorVisible) {
            bool anyOtherVisible = false;
            for (auto sit = m_screenStates.constBegin(); sit != m_screenStates.constEnd(); ++sit) {
                if (sit.key() == screenId) {
                    continue;
                }
                if (sit.value().zoneSelectorSlot() && sit.value().zoneSelectorSlot()->isVisible()) {
                    anyOtherVisible = true;
                    break;
                }
            }
            if (!anyOtherVisible) {
                m_zoneSelectorVisible = false;
                Q_EMIT zoneSelectorVisibilityChanged(false);
            }
        }
    }
}

void OverlayService::hideZoneSelectorSlotOnScreen(const QString& effectiveId)
{
    auto it = m_screenStates.find(effectiveId);
    if (it == m_screenStates.end()) {
        return;
    }
    auto* slot = it->zoneSelectorSlot();
    if (!slot || !slot->isVisible()) {
        return;
    }
    // Reset hover/cursor state BEFORE the animator-driven hide so the
    // slot's interactive bits don't keep responding to pointer events
    // while it fades out. PZ-specific QML invocation; the animator-leg
    // mechanism itself routes through ShellHost::hideSlot.
    QMetaObject::invokeMethod(slot, "resetCursorState");
    m_shellHost->hideSlot(effectiveId, PhosphorSlotKeys::ZoneSelector(), [this, effectiveId]() {
        onZoneSelectorSlotHideCompleted(effectiveId);
    });
}

void OverlayService::showZoneSelectorSlotOnScreen(const QString& effectiveId, QScreen* physScreen,
                                                  const QRect& targetGeom)
{
    if (!physScreen) {
        return;
    }
    // Short-circuit before ensurePassiveShellFor: if the slot is
    // already visible AND the cached (physScreen, geom) match the
    // request, we have nothing to do. If the cached values differ
    // (mid-flight monitor hot-plug, geometry update), fall through
    // to refresh - silently dropping the new args would leave the
    // slot painted with stale geometry.
    {
        auto existing = m_screenStates.find(effectiveId);
        if (existing != m_screenStates.end() && existing->zoneSelectorSlot()
            && existing->zoneSelectorSlot()->isVisible() && existing->zoneSelectorPhysScreen == physScreen
            && existing->zoneSelectorGeometry == targetGeom) {
            return;
        }
    }
    auto* state = ensurePassiveShellFor(effectiveId, physScreen);
    if (!state || !state->shell || !state->shell->shellSurface() || !state->zoneSelectorSlot()) {
        return;
    }
    auto* slot = state->zoneSelectorSlot();
    state->zoneSelectorPhysScreen = physScreen;
    state->zoneSelectorGeometry = targetGeom;
    if (state->shell->shellWindow()) {
        assertWindowOnScreen(state->shell->shellWindow(), physScreen, targetGeom);
        state->shell->shellWindow()->setWidth(targetGeom.width());
        state->shell->shellWindow()->setHeight(targetGeom.height());
    }
    updateZoneSelectorWindow(effectiveId);
    writeQmlProperty(slot, QStringLiteral("loaded"), false);
    writeQmlProperty(slot, QStringLiteral("loaded"), true);
    cancelSurfacePrime(state->shell->shellSurface());
    if (!state->shell->shellSurface()->isLogicallyShown()) {
        state->shell->shellSurface()->show();
    }
    slot->setVisible(true);
    m_surfaceAnimator->beginShow(state->shell->shellSurface(), slot, PhosphorRoles::ZoneSelector, []() { });
    syncPassiveShellSurfaceState(effectiveId);
}

void OverlayService::restoreZoneSelectorAfterHide(const QString& effectiveId)
{
    auto it = m_screenStates.find(effectiveId);
    if (it == m_screenStates.end()) {
        return;
    }
    // The drag may still be active (m_zoneSelectorVisible stays true
    // across temporary slot-hides), and the screen may retain its
    // captured (physScreen, geometry) - re-show in that case.
    if (m_zoneSelectorVisible && it->zoneSelectorPhysScreen && it->zoneSelectorGeometry.isValid()) {
        showZoneSelectorSlotOnScreen(effectiveId, it->zoneSelectorPhysScreen, it->zoneSelectorGeometry);
    }
}

void OverlayService::onZoneSelectorSlotHideCompleted(const QString& effectiveId)
{
    auto it = m_screenStates.find(effectiveId);
    if (it == m_screenStates.end() || !it->zoneSelectorSlot()) {
        return;
    }
    it->zoneSelectorSlot()->setVisible(false);
    writeQmlProperty(it->zoneSelectorSlot(), QStringLiteral("loaded"), false);
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
    const PhosphorScreens::PhysicalScreen physInfo =
        mgr ? mgr->physicalScreenFor(screenId) : PhosphorScreens::PhysicalScreen{};
    QScreen* physScreen = mgr ? physInfo.qscreen : PhosphorScreens::ScreenIdentity::findByIdOrName(screenId);

    // Primary path: use layout/zone geometry pipeline with virtual screen bounds
    if (m_layoutManager && !m_selectedLayoutId.isEmpty()) {
        PhosphorZones::Layout* selectedLayout = m_layoutManager->layoutById(QUuid::fromString(m_selectedLayoutId));
        if (selectedLayout && m_selectedZoneIndex >= 0
            && m_selectedZoneIndex < static_cast<int>(selectedLayout->zones().size())) {
            PhosphorZones::Zone* zone = selectedLayout->zones().at(m_selectedZoneIndex);
            if (zone) {
                QRect result = GeometryUtils::getZoneGeometryForScreen(m_screenManager, zone, physScreen, screenId,
                                                                       selectedLayout, m_settings, m_layoutManager);
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
        // physInfo is valid only when it came from the manager; gate on that
        // directly rather than `mgr` so the contract is self-evident here.
        areaGeom =
            physInfo.isValid() ? m_screenManager->actualAvailableGeometry(physInfo) : physScreen->availableGeometry();
    }
    if (!areaGeom.isValid()) {
        return QRect();
    }

    QRectF geom(areaGeom.x() + m_selectedZoneRelGeo.x() * areaGeom.width(),
                areaGeom.y() + m_selectedZoneRelGeo.y() * areaGeom.height(),
                m_selectedZoneRelGeo.width() * areaGeom.width(), m_selectedZoneRelGeo.height() * areaGeom.height());
    return GeometryUtils::snapToRect(geom);
}

void OverlayService::scrollZoneSelector(int angleDeltaY)
{
    if (!m_zoneSelectorVisible) {
        return;
    }
    for (auto it_ = m_screenStates.constBegin(); it_ != m_screenStates.constEnd(); ++it_) {
        auto* slot = it_.value().zoneSelectorSlot();
        if (slot) {
            QMetaObject::invokeMethod(slot, "applyScrollDelta", Q_ARG(QVariant, angleDeltaY));
        }
    }
}

} // namespace PlasmaZones
