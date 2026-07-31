// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorScrollEngine/ScrollEngine.h>

#include <PhosphorEngine/ICrossSurfaceResolver.h>
#include <PhosphorEngine/PerScreenKeys.h>
#include <PhosphorScreens/Manager.h>
#include <PhosphorScrollEngine/IScrollSettings.h>

#include "scrollenginelogging.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

namespace PhosphorScrollEngine {

namespace {

/// Distance a parked window sits beyond the screen edge. Small on purpose:
/// "just outside the nearest output" keeps coordinates sane for KWin and
/// gives scroll animations a believable enter/leave origin, and it is the
/// structural fix for the stuck-off-screen-window folklore (extreme
/// coordinates are never committed).
// Parked rects on the RIGHT all share one x; on the LEFT each sits its own
// width beyond the edge, so a wider column parks further out. Neither side
// spreads by distance — the margin only keeps them clear of edge-snap
// heuristics.
constexpr int kParkMargin = 16;

} // namespace

ScrollLayoutParams ScrollEngine::layoutParamsForScreen(const QString& screenId) const
{
    ScrollLayoutParams params;
    QRect area = m_screenManager ? m_screenManager->screenAvailableGeometry(screenId)
                                 : (m_availableGeometryProvider ? m_availableGeometryProvider(screenId) : QRect());
    int innerGap = 0;
    // The strip reads the shared Tiling.Gaps model through IScrollSettings'
    // forwarding accessors; outer gaps shrink the work area, the inner gap
    // separates columns and stacked tiles. Context gap rules (resolved by
    // the daemon-injected provider, PerScreenKeys-shaped) win per slot.
    int top = 0;
    int bottom = 0;
    int left = 0;
    int right = 0;
    if (auto* gaps = qobject_cast<PhosphorEngine::IScrollSettings*>(engineSettings())) {
        innerGap = qMax(0, gaps->scrollingInnerGap());
        if (gaps->scrollingUsePerSideOuterGap()) {
            top = gaps->scrollingOuterGapTop();
            bottom = gaps->scrollingOuterGapBottom();
            left = gaps->scrollingOuterGapLeft();
            right = gaps->scrollingOuterGapRight();
        } else {
            top = bottom = left = right = gaps->scrollingOuterGap();
        }
    }
    if (m_contextGapProvider) {
        namespace PSK = PhosphorEngine::PerScreenKeys;
        const QVariantMap overrides = m_contextGapProvider(screenId);
        if (const auto it = overrides.constFind(PSK::InnerGap); it != overrides.constEnd()) {
            innerGap = qMax(0, it->toInt());
        }
        const bool perSide = overrides.value(PSK::UsePerSideOuterGap, false).toBool();
        if (const auto it = overrides.constFind(PSK::OuterGap); it != overrides.constEnd() && !perSide) {
            top = bottom = left = right = it->toInt();
        }
        if (perSide) {
            top = overrides.value(PSK::OuterGapTop, top).toInt();
            bottom = overrides.value(PSK::OuterGapBottom, bottom).toInt();
            left = overrides.value(PSK::OuterGapLeft, left).toInt();
            right = overrides.value(PSK::OuterGapRight, right).toInt();
        }
    }
    // Outer gaps must never invert the rect: an unknown/removed screen
    // yields a null area, and adjust() on it (or oversized gaps on a small
    // screen) would drive the width/height negative. Downstream consumers
    // only check isValid() on the apply path; strip math runs on every
    // params consumer, so clamp here.
    const QRect adjusted = area.adjusted(qMax(0, left), qMax(0, top), -qMax(0, right), -qMax(0, bottom));
    params.workArea = (adjusted.width() > 0 && adjusted.height() > 0) ? adjusted : QRect();
    params.gap = innerGap;
    params.presetColumnWidths = m_presetColumnWidths;
    params.presetWindowHeights = m_presetWindowHeights;
    params.centerFocusedColumn = effectiveCenterFocusedColumn(screenId);
    params.alwaysCenterSingleColumn = m_alwaysCenterSingleColumn;
    params.defaultColumnWidth = effectiveDefaultColumnWidth(screenId);
    return params;
}

QVector<ScrollEngine::VisibleTile> ScrollEngine::visibleTiles(const QString& screenId) const
{
    // The state check comes first so an unmanaged screen never pays the
    // ScreenManager query plus context-gap-provider call for its params.
    const ScrollState* state = m_states.stateForKey(m_context.currentKeyForScreen(screenId));
    if (!state || state->strip().isEmpty()) {
        return {};
    }
    return visibleTiles(screenId, layoutParamsForScreen(screenId));
}

QVector<ScrollEngine::VisibleTile> ScrollEngine::visibleTiles(const QString& screenId,
                                                              const ScrollLayoutParams& params) const
{
    const ScrollState* state = m_states.stateForKey(m_context.currentKeyForScreen(screenId));
    if (!state || state->strip().isEmpty() || !params.workArea.isValid()) {
        return {};
    }
    const ResolvedStrip resolved = state->strip().relayout(params);
    QVector<VisibleTile> out;
    int resolvedTiles = 0;
    for (const ResolvedColumn& column : resolved.columns) {
        resolvedTiles += column.tiles.size();
    }
    out.reserve(resolvedTiles);
    // THE zone-number walk (see VisibleTile): sequential over what is on
    // screen, columns left to right, tiles top to bottom. Every consumer of
    // the number space — preview labels, Snap-to-Zone digits, the
    // navigation OSD's per-window number, the cross-mode entry window —
    // derives from this list, so a change to the walk changes all of them
    // together and they always address the same tiles. The number is
    // STAMPED here rather than re-derived from each consumer's own loop
    // index: the two agree today only because the projections are strictly
    // order-preserving, which is a property a future filter could quietly
    // break in one consumer and not the others.
    int zoneNumber = 0;
    for (const ResolvedColumn& column : resolved.columns) {
        for (const ResolvedTile& tile : column.tiles) {
            if (tile.hidden) {
                continue;
            }
            // Clip rather than drop partially-visible columns: the cut-off
            // edge is what tells the viewer the strip continues off-screen.
            const QRect clipped = tile.rect.intersected(params.workArea);
            if (!clipped.isEmpty()) {
                out.append(VisibleTile{tile.windowId, column.columnIndex, ++zoneNumber, clipped});
            }
        }
    }
    return out;
}

QVector<QRect> ScrollEngine::visibleTileRects(const QString& screenId) const
{
    QVector<QRect> out;
    const QVector<VisibleTile> tiles = visibleTiles(screenId);
    out.reserve(tiles.size());
    for (const VisibleTile& tile : tiles) {
        out.append(tile.rect);
    }
    return out;
}

int ScrollEngine::visibleTileNumberForWindow(const QString& screenId, const QString& windowId) const
{
    const QString canonical = canonicalizeForLookup(windowId);
    for (const VisibleTile& tile : visibleTiles(screenId)) {
        if (tile.windowId == canonical) {
            return tile.zoneNumber;
        }
    }
    return -1;
}

QVector<QRectF> ScrollEngine::visibleTileRectsRelative(const QString& screenId) const
{
    // State check first, like visibleTiles: an unmanaged or empty screen must
    // not pay the ScreenManager query plus context-gap-provider call that
    // resolving the params costs.
    const ScrollState* state = m_states.stateForKey(m_context.currentKeyForScreen(screenId));
    if (!state || state->strip().isEmpty()) {
        return {};
    }
    const ScrollLayoutParams params = layoutParamsForScreen(screenId);
    if (!params.workArea.isValid()) {
        return {};
    }
    // Normalized against the FULL screen geometry, not the gap-inset work
    // area the tiles are clipped to. Every renderer of these fractions draws
    // them into a box shaped like the whole screen (the settings app's strip
    // thumbnail, and the daemon's own OSD card via its twin renorm in
    // stripzones.h) — work-area fractions in a screen-shaped box stretch the
    // strip by the panel's share of the output. Falls back to the work area
    // when no screen rect is resolvable (headless without a provider), same
    // as the parking-bounds resolution in applyLayout.
    QRect area = m_screenManager ? m_screenManager->screenGeometry(screenId)
                                 : (m_screenGeometryProvider ? m_screenGeometryProvider(screenId) : QRect());
    if (!area.isValid()) {
        area = params.workArea;
    }
    // The params are already resolved, so the walk reuses them rather than
    // sending visibleTiles back to layoutParamsForScreen for the same values.
    const QVector<VisibleTile> tiles = visibleTiles(screenId, params);
    QVector<QRectF> out;
    out.reserve(tiles.size());
    for (const VisibleTile& tile : tiles) {
        const QRect& r = tile.rect;
        out.append(QRectF(
            static_cast<qreal>(r.x() - area.x()) / area.width(), static_cast<qreal>(r.y() - area.y()) / area.height(),
            static_cast<qreal>(r.width()) / area.width(), static_cast<qreal>(r.height()) / area.height()));
    }
    return out;
}

void ScrollEngine::applyLayout(const QString& screenId, bool focusWindowAfter)
{
    ScrollState* state = stateForKey(currentKeyForScreen(screenId), false);
    if (!state) {
        // No state for the CURRENT context (fresh desktop, or the state was
        // just pruned) — the previous context's indicator must not stay
        // painted; the overlay is driven solely by tabStripsChanged, so
        // this bail is its only chance to clear.
        clearTabStripsForScreen(screenId);
        return;
    }
    const ScrollLayoutParams params = layoutParamsForScreen(screenId);
    if (!params.workArea.isValid()) {
        // Screen went away or gaps swallowed it: the indicator must not
        // stay painted, and a scheduled-retile storm must not spam a
        // warning per tick (debug level; the first clear is the signal).
        clearTabStripsForScreen(screenId);
        qCDebug(lcScrollEngine) << "applyLayout: no valid work area for screen" << screenId;
        return;
    }
    // Re-apply the centering policy before resolving: a work-area change
    // (resolution, panels, outer gaps) or a centering-settings flip leaves
    // the stored anchor relative to the OLD width, and nothing else
    // re-derives it until the next focus move. Idempotent for a settled
    // strip (a fully-visible column stays put under Never/OnOverflow).
    const int anchorBefore = state->strip().viewAnchor();
    state->strip().updateViewForFocus(params);
    // The anchor is PERSISTED state (serializeStripState) and
    // placementChanged is the only producer of DirtyScrollStrips. The
    // focus-moving verbs all emit it themselves, but the re-anchor here also
    // fires on retile-ONLY entry points (work-area change, a settings flip, a
    // scheduled retile), which have no emit of their own — so without this a
    // re-anchor that is the session's last strip event is never saved and the
    // strip comes back scrolled to the pre-change view. Emitted at the tail,
    // once the geometry has actually been applied.
    const bool anchorMoved = state->strip().viewAnchor() != anchorBefore;
    const ResolvedStrip resolved = state->strip().relayout(params);
    if (resolved.columns.isEmpty()) {
        // The strip just emptied (last window closed / floated / released).
        // The tab-strip clear must still run — returning before it would
        // leave the indicator painted on an empty screen forever.
        clearTabStripsForScreen(screenId);
        return;
    }

    // Parking bounds come from the FULL screen geometry, not the work area —
    // a rect just outside the work area could still sit on-screen over a
    // panel. Fall back to the work area when the screen rect is unknown.
    QRect screenRect = m_screenManager ? m_screenManager->screenGeometry(screenId)
                                       : (m_screenGeometryProvider ? m_screenGeometryProvider(screenId) : QRect());
    if (!screenRect.isValid()) {
        screenRect = params.workArea;
    }

    // Park-side topology: "just outside the screen edge" is only off-screen
    // when no OTHER output sits there — with a monitor to the right, a
    // right-parked column lands visibly ON that monitor, KWin reassigns the
    // window's output, and a later mode change keeps it stranded on the
    // neighbour (the dolphin-on-DP-3 bug). Prefer the natural side (it is
    // the believable enter/leave animation origin); when an output occupies
    // it, fall to the opposite side, then below/above the screen. A screen
    // boxed in on all four sides keeps the natural side — no off-screen
    // spot exists, and the effect's tracked-screen override still routes
    // the window correctly. No resolver (headless/tests) counts as free.
    const auto sideFree = [this, &screenId](const char* direction) {
        return !m_crossSurfaceResolver
            || m_crossSurfaceResolver->neighborOutputInDirection(screenId, QLatin1String(direction)).isEmpty();
    };
    const bool leftFree = sideFree("left");
    const bool rightFree = sideFree("right");
    const bool downFree = sideFree("down");
    const bool upFree = sideFree("up");
    const auto parkLeft = [&](QRect& rect) {
        rect.moveLeft(screenRect.left() - rect.width() - kParkMargin);
    };
    const auto parkRight = [&](QRect& rect) {
        rect.moveLeft(screenRect.right() + 1 + kParkMargin);
    };
    // A vertically-parked rect keeps its strip-derived x, which for a column
    // scrolled far off the side is well outside this screen and lands over a
    // horizontal neighbour — the very placement the vertical fallback was
    // chosen to avoid. Pull x back inside the screen's own span (as close to
    // the strip-derived value as fits, so the enter animation still comes
    // from the right side); the rect is off-screen by its y either way.
    const auto clampXIntoScreen = [&](QRect& rect) {
        const int maxLeft = qMax(screenRect.left(), screenRect.right() + 1 - rect.width());
        rect.moveLeft(qBound(screenRect.left(), rect.left(), maxLeft));
    };
    const auto parkHorizontal = [&](QRect& rect, bool naturalLeft) {
        // Natural side first, opposite side second, vertical third; a fully
        // boxed-in screen keeps the natural side (least-bad; the effect's
        // tracked-screen override still routes the window correctly).
        if (naturalLeft ? leftFree : rightFree) {
            naturalLeft ? parkLeft(rect) : parkRight(rect);
        } else if (naturalLeft ? rightFree : leftFree) {
            naturalLeft ? parkRight(rect) : parkLeft(rect);
        } else if (downFree) {
            clampXIntoScreen(rect);
            rect.moveTop(screenRect.bottom() + 1 + kParkMargin);
        } else if (upFree) {
            clampXIntoScreen(rect);
            rect.moveTop(screenRect.top() - rect.height() - kParkMargin);
        } else {
            naturalLeft ? parkLeft(rect) : parkRight(rect);
        }
    };

    QJsonArray arr;
    bool anyRectMoved = false;
    for (const ResolvedColumn& column : resolved.columns) {
        for (const ResolvedTile& tile : column.tiles) {
            QRect rect = tile.rect;
            if (tile.hidden) {
                // Non-active tile of a tabbed column: parked off-canvas so it
                // cannot steal input from the visible tab (hit-testing uses
                // real geometry only). The side follows the COLUMN's own
                // position, not a fixed right: a column parked off the left
                // edge must keep its hidden tiles on the left or their
                // enter/leave origin comes from the wrong side of the screen.
                parkHorizontal(rect, column.rect.right() < params.workArea.left());
            } else if (rect.right() < params.workArea.left()) {
                parkHorizontal(rect, true);
            } else if (rect.left() > params.workArea.right()) {
                parkHorizontal(rect, false);
            }
            // A partially-visible edge column keeps its TRUE rect, overhang
            // included. Clamping it to the work area here was tried and
            // rejected: it resized the window instead of clipping its
            // drawing. The overhang must not RENDER on the neighbouring
            // output, but that is the compositor's job — the effect skips
            // strip windows in foreign outputs' paint passes
            // (paint_pipeline.cpp), so the window keeps its full size and
            // its paint stops at the monitor boundary.

            QJsonObject obj;
            obj[QLatin1String("windowId")] = tile.windowId;
            obj[QLatin1String("screenId")] = screenId;
            obj[QLatin1String("x")] = rect.x();
            obj[QLatin1String("y")] = rect.y();
            obj[QLatin1String("width")] = rect.width();
            obj[QLatin1String("height")] = rect.height();
            arr.append(obj);
            const auto lastIt = m_lastAppliedRect.constFind(tile.windowId);
            if (lastIt == m_lastAppliedRect.constEnd() || *lastIt != rect) {
                anyRectMoved = true;
            }
            m_lastAppliedRect.insert(tile.windowId, rect);
        }
    }
    if (arr.isEmpty()) {
        // Unreachable today, kept as the belt: resolved.columns is non-empty
        // by the bail above, and relayout never emits a column with no
        // tiles, so every surviving column contributes at least one entry.
        clearTabStripsForScreen(screenId);
        return;
    }
    // Emit-on-change: a relayout that resolved every window to the exact
    // rect already applied (focus move under Never-centering, redundant
    // scheduled retile) must not re-feed the compositor's apply path.
    if (anyRectMoved) {
        Q_EMIT windowsTiled(QString::fromUtf8(QJsonDocument(arr).toJson(QJsonDocument::Compact)));
    }

    // Tab-strip indicator model: one entry per VISIBLE tabbed column.
    // Change-gated: the payload is re-derived on every relayout, but an
    // unchanged strip set (the common focus-move / plain-retile case) must
    // not re-drive an overlay repaint — and an empty state is announced
    // exactly once (clearTabStripsForScreen latches on the tracked set).
    QJsonArray strips;
    for (const ResolvedColumn& column : resolved.columns) {
        if (!column.tabbed || !column.rect.intersects(params.workArea)) {
            continue;
        }
        QJsonObject strip;
        strip[QLatin1String("x")] = column.rect.x();
        strip[QLatin1String("y")] = column.rect.y();
        strip[QLatin1String("width")] = column.rect.width();
        QJsonArray tabs;
        // 0 is a safe seed, not a fallback: a resolved tabbed column always
        // carries exactly one non-hidden tile (relayout marks every tile
        // but the column's active one hidden), so the loop below always
        // overwrites it. An all-hidden column cannot be resolved — a
        // fully-minimized column is skipped before ResolvedColumns are
        // built.
        int activeIndex = 0;
        for (int i = 0; i < column.tiles.size(); ++i) {
            tabs.append(column.tiles.at(i).windowId);
            if (!column.tiles.at(i).hidden) {
                activeIndex = i;
            }
        }
        strip[QLatin1String("activeIndex")] = activeIndex;
        strip[QLatin1String("tabs")] = tabs;
        strips.append(strip);
    }
    if (!strips.isEmpty()) {
        const QString payload = QString::fromUtf8(QJsonDocument(strips).toJson(QJsonDocument::Compact));
        if (m_lastTabStripPayload.value(screenId) != payload) {
            m_lastTabStripPayload.insert(screenId, payload);
            m_screensWithTabStrips.insert(screenId);
            Q_EMIT tabStripsChanged(screenId, payload);
        }
    } else {
        clearTabStripsForScreen(screenId);
    }

    if (focusWindowAfter) {
        const QString active = state->strip().activeWindowId();
        if (!active.isEmpty()) {
            Q_EMIT activateWindowRequested(active);
        }
    }
    if (anchorMoved) {
        // See the anchorBefore capture above. Callers that already emit for
        // their own mutation get a second, harmless mark — the dirty flag is
        // idempotent, and gating on a real anchor move keeps this rare.
        Q_EMIT placementChanged(screenId);
    }
}

} // namespace PhosphorScrollEngine
