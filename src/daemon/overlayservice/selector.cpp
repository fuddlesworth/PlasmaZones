// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "internal.h"
#include "daemon/overlayservice.h"
#include "core/platform/logging.h"
#include "phosphor_slot_keys.h"
#include <PhosphorOverlay/ShellHost.h>
#include <PhosphorSurfaces/SurfaceManager.h>
#include <PhosphorZones/Layout.h>
#include <PhosphorZones/LayoutRegistry.h>
#include <PhosphorZones/Zone.h>
#include <PhosphorZones/LayoutUtils.h>
#include "core/utils/geometryutils.h"
#include <PhosphorScreens/Manager.h>
#include "core/utils/utils.h"
#include <PhosphorScreens/VirtualScreen.h>
#include "core/types/zoneselectorlayout.h"
#include "config/configdefaults.h"
#include <QCursor>
#include <QHash>
#include <QScreen>
#include <QQuickWindow>
#include <QQuickItem>
#include <QQmlEngine>
#include <QVector>
#include <algorithm>
#include <utility>

#include <PhosphorLayer/Surface.h>
#include "phosphor_roles.h"
#include <PhosphorScreens/ScreenIdentity.h>

namespace PlasmaZones {

namespace {

/// A layout card the QML layout pass has positioned, as read back from the
/// scene graph. `rect` is in the zone-selector slot's coordinates, matching the
/// cursor coordinates updateSelectorPosition hit-tests with.
struct LaidOutCard
{
    QQuickItem* item = nullptr;
    QRectF rect;
};

} // namespace

bool OverlayService::selectorEnabledForScreen(const QString& screenId) const
{
    const bool stripScreen = isStripSelectorScreen(screenId);
    // Strip-selector screens are governed by the SCROLLING selector enable;
    // everything else by the snapping one.
    const bool toggle =
        !m_settings || (stripScreen ? m_settings->scrollingZoneSelectorEnabled() : m_settings->zoneSelectorEnabled());
    // The snapping MASTER switch, ANDed outside the rule fold exactly as the
    // drag adaptor's checkZoneSelectorTrigger does: a classic pick commits
    // inside dragStopped, which a snapping-disabled drag never reaches, so
    // offering (or keeping) the popup would discard the choice on release —
    // that is a "pick could not be committed" gate no SetDragSelectorEnabled
    // rule may override. Strip screens are exempt for the adaptor's reason
    // too: their drop commits through the scroll engine, not a snap.
    const bool masterAllowed = stripScreen || !m_settings || m_settings->snappingEnabled();
    if (!m_layoutManager) {
        return toggle && masterAllowed;
    }
    // A SetDragSelectorEnabled rule overrides the toggle in either direction;
    // no rule filling the slot means the toggle decides.
    return m_layoutManager
               ->resolveContextDragSelectorEnabled(screenId, currentVirtualDesktopForScreen(screenId),
                                                   m_currentActivity)
               .value_or(toggle)
        && masterAllowed;
}

void OverlayService::showZoneSelector(const QString& targetScreenId)
{
    if (m_zoneSelectorVisible) {
        return;
    }

    // With an explicit target the folded verdict (rule over toggle) gates
    // here; with no target the per-screen check in the show loops below
    // decides screen by screen, so a force-on rule can still show a popup
    // while both global toggles are off.
    if (!targetScreenId.isEmpty() && !selectorEnabledForScreen(targetScreenId)) {
        return;
    }

    if (m_zoneSelectorRecreationPending) {
        return;
    }

    auto* mgr = m_screenManager;
    QScreen* targetScreen = nullptr;
    if (!targetScreenId.isEmpty()) {
        targetScreen = mgr ? mgr->physicalScreenFor(targetScreenId).qscreen
                           : PhosphorScreens::ScreenIdentity::findByIdOrName(targetScreenId);
        if (!targetScreen && (!mgr || !mgr->effectiveScreenIds().contains(targetScreenId))) {
            // An explicit target that resolves to NOTHING must not fall
            // through to the every-screen path — a selector popping up on all
            // monitors because one id went stale is worse than not showing.
            qCWarning(lcOverlay) << "showZoneSelector: target screen unresolvable:" << targetScreenId;
            return;
        }
    }

    const QStringList effectiveIds = mgr ? mgr->effectiveScreenIds() : QStringList();

    // Set the logical-visibility flag only if at least one screen actually
    // shows; a flag flipped on with zero shells shown wedges the guard at
    // function entry (every later show early-returns "already visible").
    bool shownAny = false;
    // Tracks whether ANY screen passed the enable/engine gates: with every
    // screen legitimately disabled (both toggles off, no force-on rule) the
    // silent return below is the expected outcome, not a fault worth a
    // warning.
    bool anyEligible = false;
    auto showOnScreen = [this, &shownAny](const QString& screenId, QScreen* physScreen, const QRect& targetGeom) {
        auto* state = ensurePassiveShellFor(screenId, physScreen);
        if (!state || !state->shell || !state->shell->shellSurface() || !state->zoneSelectorSlot()) {
            return;
        }
        shownAny = true;
        state->zoneSelectorPhysScreen = physScreen;
        state->zoneSelectorGeometry = targetGeom;
        if (state->shell->shellWindow()) {
            assertWindowOnScreen(state->shell->shellWindow(), physScreen, targetGeom);
            state->shell->shellWindow()->setWidth(targetGeom.width());
            state->shell->shellWindow()->setHeight(targetGeom.height());
        }
        updateZoneSelectorWindow(screenId);
        auto* slot = state->zoneSelectorSlot();
        // Stage d: resolve + push the zone-selector surface-shader decoration
        // (same SurfaceDecoration host the OSD uses, retargeted to the
        // "popup.zoneSelector" surface path). Empty source = no decoration.
        applyDecoration(slot, QStringLiteral("popup.zoneSelector"));
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
            // Strip-selector screens are exempt from BOTH engine-screen
            // gates: isSnappingContextDisabled answers true for a live
            // Templates scrolling screen, and m_excludedScreens carries the
            // engine-owned set — on a strip screen the popup is exactly the
            // engine surface those gates exist to protect. They still gate
            // per-screen enable via the folded rule-over-toggle verdict; the
            // engine-screen gates stay outside that fold (a rule cannot
            // override a gate that exists because a pick could not commit).
            if (!selectorEnabledForScreen(screenId)) {
                continue;
            }
            if (!isStripSelectorScreen(screenId)) {
                if (isSnappingContextDisabled(screenId)) {
                    continue;
                }
                if (m_excludedScreens.contains(screenId)) {
                    continue;
                }
            }
            const QRect geom = mgr->screenGeometry(screenId);
            const QRect targetGeom = geom.isValid() ? geom : physScreen->geometry();
            anyEligible = true;
            showOnScreen(screenId, physScreen, targetGeom);
        }
    } else {
        for (auto* screen : Utils::allScreens()) {
            if (targetScreen && screen != targetScreen) {
                continue;
            }
            QString screenId = PhosphorScreens::ScreenIdentity::identifierFor(screen);
            // Same strip-screen exemption and rule fold as the managed loop above.
            if (!selectorEnabledForScreen(screenId)) {
                continue;
            }
            if (!isStripSelectorScreen(screenId)) {
                if (isSnappingContextDisabled(screenId)) {
                    continue;
                }
                if (m_excludedScreens.contains(screenId)) {
                    continue;
                }
            }
            auto* smgr = m_screenManager;
            QRect geom = (smgr && smgr->screenGeometry(screenId).isValid()) ? smgr->screenGeometry(screenId)
                                                                            : screen->geometry();
            anyEligible = true;
            showOnScreen(screenId, screen, geom);
        }
    }

    if (!shownAny) {
        if (anyEligible) {
            qCWarning(lcOverlay) << "showZoneSelector: no screen could show the selector";
        }
        return;
    }
    m_zoneSelectorVisible = true;
    Q_EMIT zoneSelectorVisibilityChanged(true);
}

void OverlayService::hideZoneSelector()
{
    if (!m_zoneSelectorVisible) {
        return;
    }

    m_zoneSelectorVisible = false;

    // Selected zone NOT cleared here - drag-end snap path needs it.
    // The STRIP target IS cleared: once the popup is hidden no hit-test
    // updates it, and the drag adaptor's settle path prefers a stored strip
    // pick over the cursor-derived bands — so a target surviving a mid-drag
    // hide (a toggle/rule flip, a desktop switch) would keep driving the
    // preview and the drop against a card list nobody can see. Ownership
    // hands back to the drop bands, which is the documented contract.
    m_selectedStripTarget = {};
    m_selectedStripScreenId.clear();
    // The popup only exists inside a drag, so a full hide also ends the
    // memo's useful life — the strip is free to reshape before the next
    // show, and dropping the cache here (rather than only at the next
    // drag's setActiveDragWindowId) keeps no stale entry across sessions
    // (stop() routes through here).
    m_stripCardFractionsCache.clear();
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
        if (!slot) {
            continue;
        }
        // Blank the strip highlight triple on every slot, visible or not: the
        // C++ target was just cleared above, and updateStripSelectorHit's
        // changed-gate compares against that cleared target, so an invalid
        // first hit after the next show would early-return and leave a stale
        // highlight painted (slot properties survive the Loader recycle).
        writeQmlProperty(slot, QStringLiteral("selectedStripColumn"), -1);
        writeQmlProperty(slot, QStringLiteral("selectedStripGap"), -1);
        writeQmlProperty(slot, QStringLiteral("selectedStripHalf"), -1);
        if (!slot->isVisible()) {
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

    // Clear selection highlight on all OTHER zone selector slots when cursor
    // moves to a different VS, preventing stale highlights from previous VS.
    // Runs BEFORE the excluded-screen bail below: a hop onto an excluded
    // screen still makes every other screen's painted selection stale, and
    // returning first would leave it (and the C++ STRIP mirror — the zone
    // triple is deliberately kept for the drag-end snap path) alive.
    for (auto it = m_screenStates.begin(); it != m_screenStates.end(); ++it) {
        // screensMatch, not raw !=, matching the mirror clear below and the
        // drop guard's convention. ASSUMPTION: one screen never coexists
        // under two key spellings in m_screenStates while the cursor id
        // resolves to a third — the exact constFind below would then miss
        // the aliased entry this loop just spared.
        if (!PhosphorScreens::ScreenIdentity::screensMatch(it.key(), cursorScreenId) && it.value().zoneSelectorSlot()) {
            writeQmlProperty(it.value().zoneSelectorSlot(), QStringLiteral("selectedLayoutId"), QString());
            writeQmlProperty(it.value().zoneSelectorSlot(), QStringLiteral("selectedZoneIndex"), -1);
            writeQmlProperty(it.value().zoneSelectorSlot(), QStringLiteral("selectedStripColumn"), -1);
            writeQmlProperty(it.value().zoneSelectorSlot(), QStringLiteral("selectedStripGap"), -1);
            writeQmlProperty(it.value().zoneSelectorSlot(), QStringLiteral("selectedStripHalf"), -1);
            // Keep the C++ mirror in step with the blanked slot: a strip
            // target keyed to a screen whose highlight was just cleared
            // would otherwise survive invisibly and still win the drop.
            // screensMatch, not raw ==, per the drop guard's convention.
            if (PhosphorScreens::ScreenIdentity::screensMatch(it.key(), m_selectedStripScreenId)) {
                m_selectedStripTarget = {};
                m_selectedStripScreenId.clear();
            }
        }
    }

    // Skip excluded screens (autotile-managed) - matches showZoneSelector
    // exclusion. Strip-selector screens are exempt for the same reason they
    // are in the show path: the popup IS the engine surface there.
    if (m_excludedScreens.contains(cursorScreenId) && !isStripSelectorScreen(cursorScreenId)) {
        return;
    }

    auto cursorStateIt = m_screenStates.constFind(cursorScreenId);
    if (cursorStateIt != m_screenStates.constEnd() && cursorStateIt->zoneSelectorSlot()) {
        auto* slot = cursorStateIt->zoneSelectorSlot();
        // A slot the shell has hidden (a modal overlay claimed the screen,
        // a hide is animating out) must not keep hit-testing: the geometry
        // walk below reads rendered rects that are simply invisible, and a
        // hit written here arms BOTH selection families that the drop path
        // (drop.cpp, `hasSelectedZone()` branch) prefers over the release
        // cursor — so an invisible popup could drive the drop. A bare
        // return is not enough: the LAST visible tick's pick would stay
        // armed for the same drop. Clear everything, which also blanks the
        // painted highlights so the next show cannot trip the hit-tests'
        // changed-gates against stale slot properties.
        if (!slot->isVisible()) {
            clearSelectedZone();
            return;
        }
        auto* window = cursorStateIt->shell ? cursorStateIt->shell->shellWindow() : nullptr;
        // Convert global cursor position to window-local coordinates.
        int localX, localY;
        const QRect& storedGeom = cursorStateIt->zoneSelectorGeometry;
        const QRect winGeom = storedGeom.isValid() ? storedGeom : (window ? window->geometry() : QRect());
        if (winGeom.isValid()) {
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

        // Strip-mode screens speak the drag-insert vocabulary, not zones.
        if (isStripSelectorScreen(cursorScreenId)) {
            updateStripSelectorHit(slot, localX, localY, cursorScreenId);
            return;
        }

        QVariantList layouts = slot->property("layouts").toList();
        if (layouts.isEmpty()) {
            return;
        }

        // Where QML actually rendered each card, keyed by the delegate's model
        // index. Computing positions from (cellWidth + indicatorSpacing) is
        // wrong: when the GridLayout's explicit width (= scrollContentWidth,
        // sized for `gridColumns` columns) exceeds the natural content width
        // (when fewer cards than gridColumns are populated), Qt distributes the
        // slack across the populated columns. Cards 1..N-1 then drift further
        // right than the C++ math predicts, with cumulative error per card.
        // Reading the rendered geometry back is the only way to track this
        // exactly, and it has to be the CLIPPED geometry: the ScrollView clips
        // once the list overflows, and a card scrolled out of the viewport keeps
        // a position that would otherwise still match the cursor.
        //
        // Keyed by each delegate's own `index` rather than by traversal
        // position: the grid's children are the Repeater plus its delegates, so
        // a positional walk has to guess which children are cards, and any
        // delegate the layout pass has not sized yet would shift every
        // subsequent card onto the wrong entry in `layouts` — selecting a zone
        // from one layout while recording another layout's id. Reading `index`
        // removes the guess. The Repeater itself carries no `index` and drops
        // out for free.
        QHash<int, LaidOutCard> cards;
        cards.reserve(layouts.size());

        // Walk the slot subtree (the shell's contentItem hosts multiple
        // sibling slot Items; rooting traversal at the zone-selector slot
        // limits results to that slot's content).
        QQuickItem* contentRoot = slot;
        if (auto* gridItem = findQmlItemByName(contentRoot, QStringLiteral("zoneSelectorContentGrid"))) {
            const auto kids = gridItem->childItems();
            for (QQuickItem* cell : kids) {
                if (!cell) {
                    continue;
                }
                bool indexOk = false;
                const int cardIndex = cell->property("index").toInt(&indexOk);
                if (!indexOk || cardIndex < 0 || cardIndex >= layouts.size()) {
                    continue;
                }
                cards.insert(cardIndex, LaidOutCard{cell, mapVisibleRectToItem(cell, contentRoot)});
            }
        }

        if (farMoved && lcOverlay().isDebugEnabled()) {
            qCDebug(lcOverlay) << "selector cards:" << cards.size() << "laid out of" << layouts.size();
            for (int k = 0; k < std::min<int>(layouts.size(), 8); ++k) {
                const auto dumpIt = cards.constFind(k);
                if (dumpIt == cards.constEnd()) {
                    qCDebug(lcOverlay) << "  card[" << k << "] not laid out";
                } else {
                    qCDebug(lcOverlay) << "  card[" << k << "]" << dumpIt->rect;
                }
            }
        }

        // Check each layout card. The card is the coarse filter; the zones
        // inside it are hit-tested precisely further down.
        for (int i = 0; i < layouts.size(); ++i) {
            // Only cards QML has actually laid out can be hit. Deriving a card
            // box from layout math instead drifted twice over: LayoutCard offsets
            // the preview from the card's top by Kirigami.Units.gridUnit, which
            // tracks the user's font metrics rather than the 18 the C++ side used
            // to hardcode, and the GridLayout redistributes slack across populated
            // columns (see the card walk above). Before the first layout pass
            // nothing is painted, so there is correctly nothing to select.
            const auto cardIt = cards.constFind(i);
            if (cardIt == cards.constEnd()) {
                continue;
            }
            const QRectF& cardRect = cardIt->rect;

            if (cardRect.contains(localX, localY)) {
                QVariantMap layoutMap = layouts[i].toMap();
                QString layoutId = layoutMap[QStringLiteral("id")].toString();

                // Skip non-active layouts when screen is locked — a LockContext
                // rule (checked first) or a manual lock on either mode.
                if (m_settings && m_layoutManager) {
                    int curDesktop = currentVirtualDesktopForScreen(cursorScreenId);
                    // m_currentActivity, not m_layoutManager->currentActivity():
                    // every other context resolve in this class reads the mirror,
                    // and mixing sources lets the lock hit-test transiently
                    // disagree with the cards drawn from the mirror.
                    bool locked =
                        isAnyModeLocked(m_settings, m_layoutManager, cursorScreenId, curDesktop, m_currentActivity);
                    if (locked) {
                        // Only allow zone selection from the active layout
                        PhosphorZones::Layout* activeLayout = m_layoutManager->resolveLayoutForScreen(cursorScreenId);
                        if (activeLayout && layoutId != activeLayout->id().toString()) {
                            continue; // Skip this non-active layout entirely
                        }
                    }
                }

                // Per-zone hit testing against the rects QML actually rendered.
                //
                // Replaying ZonePreview's layout math here drifted from what the
                // user sees, because LayoutCard fits `previewBackground` to the
                // layout's aspectRatioClass (letterboxing it inside the indicator
                // box), insets ZonePreview by smallSpacing in card mode, applies
                // edgeGap only at the preview's outer edges (zonePadding/2
                // between neighbours), and drops both gaps entirely for
                // overlapping layouts. Reading the delegates keeps this in step
                // with QML by construction — the same reason the card walk above
                // reads back rendered geometry instead of predicting origins.
                QVariantList zones = layoutMap[QStringLiteral("zones")].toList();

                // Keyed by each delegate's own `index`, for the same reason the
                // card walk above is: collectQmlItemsByName descends the whole
                // card subtree, so the order it returns is an artifact of that
                // traversal rather than model order.
                QHash<int, QRectF> zoneRectsInSlot;
                zoneRectsInSlot.reserve(zones.size());
                QVector<QQuickItem*> zoneItems;
                collectQmlItemsByName(cardIt->item, QStringLiteral("zonePreviewZone"), zoneItems);
                for (QQuickItem* zoneItem : std::as_const(zoneItems)) {
                    bool zoneIndexOk = false;
                    const int zoneIndex = zoneItem->property("index").toInt(&zoneIndexOk);
                    if (!zoneIndexOk || zoneIndex < 0) {
                        continue;
                    }
                    zoneRectsInSlot.insert(zoneIndex, mapVisibleRectToItem(zoneItem, contentRoot));
                }

                for (int z = 0; z < zones.size(); ++z) {
                    // No delegate for this index means nothing is painted there
                    // yet (first frame not laid out). Nothing to highlight.
                    const auto zoneRectIt = zoneRectsInSlot.constFind(z);
                    if (zoneRectIt == zoneRectsInSlot.constEnd()) {
                        continue;
                    }
                    if (!zoneRectIt->contains(localX, localY)) {
                        continue;
                    }

                    QVariantMap zoneMap = zones[z].toMap();
                    // Relative geometry for m_selectedZoneRelGeo, which backs
                    // getSelectedZoneGeometry's fallback path at drop time.
                    //
                    // LayoutPreview serializes zones with flat x/y/width/height
                    // (layoutpreviewserialize.cpp::zoneMap). The legacy
                    // zonesToVariantList path produced a nested relativeGeometry
                    // sub-map. Match ZonePreview.qml's resolution order: prefer
                    // flat keys, fall back to nested. Reading nested-only made
                    // every rx/ry/rw/rh come out as 0 once LayoutPreview became
                    // the canonical wire format, handing the drop path a
                    // zero-sized zone rect.
                    const QVariantMap relGeo = zoneMap.value(QStringLiteral("relativeGeometry")).toMap();
                    auto coord = [&](QLatin1String flatKey, QLatin1String nestedKey) {
                        const QVariant flat = zoneMap.value(flatKey);
                        if (flat.isValid() && !flat.isNull()) {
                            return flat.toReal();
                        }
                        return relGeo.value(nestedKey).toReal();
                    };
                    const qreal rx = coord(QLatin1String("x"), QLatin1String("x"));
                    const qreal ry = coord(QLatin1String("y"), QLatin1String("y"));
                    const qreal rw = coord(QLatin1String("width"), QLatin1String("width"));
                    const qreal rh = coord(QLatin1String("height"), QLatin1String("height"));

                    if (m_selectedLayoutId != layoutId || m_selectedZoneIndex != z) {
                        m_selectedLayoutId = layoutId;
                        m_selectedZoneIndex = z;
                        m_selectedZoneRelGeo = QRectF(rx, ry, rw, rh);
                        // Exactly one selection family at a time: a zone write
                        // invalidates any strip target a cross-screen hop left
                        // behind (the mirror of the strip arm's zone clear).
                        m_selectedStripTarget = {};
                        m_selectedStripScreenId.clear();
                        // The strip family's PAINTED half too, or a screen
                        // that flipped from strip mode to layout mode keeps
                        // the old card highlight drawn under the zone cards
                        // (the strip arm blanks the zone props the same way).
                        // Gated on a read so the common case writes nothing.
                        if (slot->property("selectedStripColumn").toInt() >= 0
                            || slot->property("selectedStripGap").toInt() >= 0) {
                            writeQmlProperty(slot, QStringLiteral("selectedStripColumn"), -1);
                            writeQmlProperty(slot, QStringLiteral("selectedStripGap"), -1);
                            writeQmlProperty(slot, QStringLiteral("selectedStripHalf"), -1);
                        }
                        writeQmlProperty(slot, QStringLiteral("selectedLayoutId"), layoutId);
                        writeQmlProperty(slot, QStringLiteral("selectedZoneIndex"), z);
                    }
                    return;
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

// createZoneSelectorWindow was removed: after the shell migration it was a
// caller-less thin alias for ensurePassiveShellFor + a property seed that
// updateZoneSelectorWindow re-writes on every show path anyway.

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
        // A strip target keyed to the destroyed screen can no longer be
        // repainted or re-hit-tested, so it would keep driving the preview
        // and win the drop against a card list nobody can see — the same
        // hazard hideZoneSelector's clear documents. hideDisabledAndRefresh
        // routes context-disable teardowns here without going through
        // hideZoneSelector, so the clear must live here too.
        if (PhosphorScreens::ScreenIdentity::screensMatch(m_selectedStripScreenId, screenId)) {
            m_selectedStripTarget = {};
            m_selectedStripScreenId.clear();
        }
        // The memoized card fractions go with the target, for the same reason
        // the rekey path drops both: this screen's popup is gone, so the cached
        // row shape describes cards nobody can see, and a screen that comes
        // back must rebuild it from the live strip.
        m_stripCardFractionsCache.remove(screenId);
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
    // Do not restore underneath a modal that replaced the one whose hide
    // completion routed here: the modals dismiss each other (snap-assist ↔
    // picker), and the dismissed one's ANIMATED hide completion lands after
    // the replacement has already hidden this screen's selector for its own
    // show — an unguarded restore would re-show it under the live modal.
    if ((m_snapAssistVisible && m_snapAssistScreenId == effectiveId)
        || (m_layoutPickerVisible && m_layoutPickerScreenId == effectiveId)) {
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
    // Release the backdrop stand-in, matching onOsdSlotHideCompleted: a hidden
    // slot draws none of it, the image is wallpaper-sized, and every show runs
    // applyDecoration again, which rewrites it.
    writeQmlProperty(it->zoneSelectorSlot(), QStringLiteral("backdropTexture"), QVariant());
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
    // Both selection families: every existing call site (screen hops, VS
    // reconfigure, drop teardown) means "no selector selection survives",
    // and a stale strip target is exactly as dangerous as a stale zone.
    m_selectedStripTarget = {};
    m_selectedStripScreenId.clear();
    // Blank the painted highlights too, BOTH families. Slot properties
    // survive the Loader recycle, and with the C++ mirrors just emptied the
    // hit-tests' changed-gates compare equal on an invalid first hit after
    // the next show — so a highlight left painted here (e.g. across a
    // stop()->start() cycle) has no other repair path. Consistent by
    // construction: mirrors empty + display blank cannot disagree.
    for (auto it = m_screenStates.begin(); it != m_screenStates.end(); ++it) {
        auto* slot = it.value().zoneSelectorSlot();
        if (!slot) {
            continue;
        }
        writeQmlProperty(slot, QStringLiteral("selectedLayoutId"), QString());
        writeQmlProperty(slot, QStringLiteral("selectedZoneIndex"), -1);
        writeQmlProperty(slot, QStringLiteral("selectedStripColumn"), -1);
        writeQmlProperty(slot, QStringLiteral("selectedStripGap"), -1);
        writeQmlProperty(slot, QStringLiteral("selectedStripHalf"), -1);
    }
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
