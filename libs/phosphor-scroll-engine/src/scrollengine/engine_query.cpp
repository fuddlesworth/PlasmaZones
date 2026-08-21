// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

// Read-only layout queries, split out of engine_apply.cpp along their own
// seam: the per-screen params resolver (layoutParamsForScreen and the axis
// resolvers it leans on) and the visible-tile accessor family the daemon's
// selector/overlay surfaces read. Nothing here mutates engine state; the
// batch emitter (applyLayout and its park/emit pipeline) stays in
// engine_apply.cpp.

#include <PhosphorScrollEngine/ScrollEngine.h>

#include <PhosphorEngine/GapResolution.h>
#include <PhosphorEngine/ICrossSurfaceResolver.h>
#include <PhosphorEngine/PerScreenKeys.h>
#include <PhosphorScreens/Manager.h>
#include <PhosphorScrollEngine/IScrollSettings.h>

namespace PhosphorScrollEngine {

ScrollLayoutParams ScrollEngine::layoutParamsForScreen(const QString& screenId, int columnCountOverride) const
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
    int settingsUniformOuter = 0;
    if (auto* gaps = qobject_cast<PhosphorEngine::IScrollSettings*>(engineSettings())) {
        innerGap = qMax(0, gaps->scrollingInnerGap());
        settingsUniformOuter = gaps->scrollingOuterGap();
        if (gaps->scrollingUsePerSideOuterGap()) {
            top = gaps->scrollingOuterGapTop();
            bottom = gaps->scrollingOuterGapBottom();
            left = gaps->scrollingOuterGapLeft();
            right = gaps->scrollingOuterGapRight();
        } else {
            top = bottom = left = right = settingsUniformOuter;
        }
    }
    if (m_contextGapProvider) {
        namespace PSK = PhosphorEngine::PerScreenKeys;
        namespace GR = PhosphorEngine::GapResolution;
        const QVariantMap overrides = m_contextGapProvider(screenId);
        // The shared atomic-layer resolution both sibling pipelines use (the
        // snap-side GeometryUtils and the autotile PerScreenConfigResolver):
        // an override map that carries outer-gap info wins WHOLESALE, and
        // missing sides of a partial per-side map fall back to the map's own
        // uniform OuterGap, then to the settings uniform. Scrolling consumes
        // raw values (identity normalize) like the snapping side; the
        // rect-inversion belt below is the only clamp.
        const auto identity = [](int v) {
            return v;
        };
        if (const auto inner = GR::gapFromOverrideMap(overrides, PSK::InnerGap, identity)) {
            innerGap = qMax(0, *inner);
        }
        if (const auto outer = GR::outerGapsFromOverrideMap(overrides, settingsUniformOuter, identity)) {
            top = outer->top;
            bottom = outer->bottom;
            left = outer->left;
            right = outer->right;
        }
    }
    // Smart gaps: a single-COLUMN strip drops the outer gaps wholesale —
    // settings values AND context-rule overrides, matching autotile's bypass
    // of its whole gap resolve. Deliberately column-count keyed, NOT the
    // window-count gate autotile uses: a lone column with a stacked pair
    // still fills the strip edge-to-edge, which is the look smart gaps are
    // for, while a window-count gate would keep outer gaps around it. The
    // inner gap stays as-is: with one column nothing consumes it between
    // columns, and stacked tiles keep their separation.
    //
    // Resolved HERE rather than passed in, so every consumer of these params
    // agrees: the geometry producers and the pure-math verbs (navigation,
    // anchor math, the maximize compare) all have to see the same work area
    // or the strip resolves against one rect and applies another. The basis
    // is the screen's CURRENT-context strip, which is the one applyLayout
    // ever puts on screen; a background context resolving params for the
    // same screen borrows that verdict rather than inventing a second one.
    // Accepted quirk: while a drag-insert preview holds the dragged window
    // DETACHED, a two-column strip counts as one here, so the outer gaps
    // drop for the duration of the hold and snap back at commit/cancel.
    // That is one settle each way, consistent with the neighbours-close-up
    // settle detach-once already makes, and gating on the preview would
    // give the opposite artifact (gaps around a visually single column).
    // The override map is fetched here rather than at its old site further
    // down: smart gaps is now a per-screen rule slot too, and its gate runs
    // before the rest of the effective* reads. One fetch still serves them
    // all — the later reads take this same map.
    const QVariantMap overrides = overridesForScreen(screenId);
    // The Auto axis basis, captured BEFORE smart gaps may zero the outer
    // gaps. Smart-gap zeroing is CONTENT-driven — it reads the strip's live
    // column count — and folding it into the axis basis coupled the resolved
    // axis to how many columns happen to exist: on a near-square monitor
    // with asymmetric per-side outer gaps, a 1↔2 column transition could
    // flip the engine's own axis while the effect's published membership
    // (which refreshes on mode/rules/geometry passes, never on a window
    // open/close) stayed stale — the exact daemon/effect split
    // stripAxisForScreen's contract note calls the worst possible failure
    // mode. The axis still measures the outer-gap-ADJUSTED rect, so the
    // near-square asymmetric-gap resolve the comment below defends is
    // unchanged; it just no longer depends on the strip's contents.
    const QRect axisBasisRaw = area.adjusted(qMax(0, left), qMax(0, top), -qMax(0, right), -qMax(0, bottom));
    const QRect axisBasis = (axisBasisRaw.width() > 0 && axisBasisRaw.height() > 0) ? axisBasisRaw : QRect();
    if (effectiveSmartGaps(overrides)) {
        if (columnCountOverride >= 0) {
            if (columnCountOverride == 1) {
                top = bottom = left = right = 0;
            }
        } else {
            const ScrollState* state = m_states.stateForKey(m_context.currentKeyForScreen(screenId));
            if (state && state->strip().columnCount() == 1) {
                top = bottom = left = right = 0;
            }
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
    // The override map was resolved ONCE above (before the smart-gaps gate)
    // and is threaded through every effective* read here — the accessors'
    // screenId wrappers would otherwise re-fetch it per call on this
    // per-relayout path.
    // Resolved BEFORE the defaults below and against the axisBasis captured
    // above, and both halves of that are load-bearing. The basis is the
    // outer-gap-adjusted rect: resolving Auto against an unadjusted rect
    // would disagree on a near-square monitor with asymmetric outer gaps.
    // It is NOT params.workArea, which additionally folds in the smart-gaps
    // zeroing and would couple the axis to the live column count (see the
    // basis capture above). The default window height reads the axis,
    // because a height fraction resolves against the work area's CROSS
    // extent.
    //
    // Resolved PER CALL and never cached: under Auto two screens with no
    // per-screen key at all resolve differently, so a cached verdict would
    // hand one monitor the other's axis.
    params.axis = effectiveStripAxis(overrides, axisBasis);
    params.respectMinimumSize = effectiveRespectMinimumSize(overrides);
    params.cropStraddlers = effectiveCropStraddlers(overrides);
    // Each template preset VOCABULARY is likewise parsed once and threaded
    // through: the two default resolvers below resolve a Preset kind against
    // the same list the params already carry, so the plain map-taking
    // overloads would validate the override list a second time per relayout.
    params.presetColumnWidths = effectivePresetColumnWidths(overrides);
    params.presetWindowHeights = effectivePresetWindowHeights(overrides);
    params.defaultWindowHeight =
        effectiveDefaultWindowHeight(overrides, params.workArea, params.axis, params.presetWindowHeights);
    params.centerFocusedColumn = effectiveCenterFocusedColumn(overrides);
    params.alwaysCenterSingleColumn = effectiveAlwaysCenterSingleColumn(overrides);
    params.defaultColumnWidth = effectiveDefaultColumnWidth(overrides, params.presetColumnWidths);
    params.tabIndicator = effectiveTabIndicator(overrides);
    return params;
}

StripAxis ScrollEngine::resolveStripAxis(const QRect& workArea) const
{
    // Auto: a work area taller than it is wide runs the strip vertically. The
    // rule itself lives in PhosphorProtocol::autoScrollAxisFor so the editor's
    // template preview resolves the identical answer for a screen it is not
    // laying out; see the note there for why there is no threshold knob and
    // why a degenerate rect resolves horizontal (engine_apply nulls
    // params.workArea whenever the gap-adjusted rect degenerates, so that is a
    // live path here).
    //
    // Measured against the WORK AREA rather than the screen rect, so a tall
    // reserved panel is accounted for consistently with everything else the
    // strip measures.
    return StripAxis(PhosphorProtocol::autoScrollAxisFor(workArea.width(), workArea.height()));
}

StripAxis ScrollEngine::stripAxisForScreen(const QString& screenId) const
{
    // Public accessor for consumers outside the layout path — the daemon
    // publishes this to the effect, and the strip selector draws with it.
    // Both MUST take it from here rather than deriving an aspect ratio of
    // their own: a second derivation can disagree with this one on a
    // near-square monitor, and a daemon/engine disagreement about the axis is
    // the worst possible failure mode — intermittent, geometry-dependent, and
    // invisible in tests.
    //
    // Resolves the work area LIVE, through layoutParamsForScreen, rather than
    // reading a snapshot from the last relayout. The engine subscribes to no
    // ScreenManager signal, so a cached work area would still be the
    // PRE-rotation one at the moment the daemon asks, and the published axis
    // would be correctly ordered but stale.
    return layoutParamsForScreen(screenId).axis;
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
    // screen, columns in strip order, tiles in within-column order (which
    // reads as left to right and top to bottom on a horizontal strip, and is
    // orientation-neutral otherwise). Every consumer of
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
        // The column's tab indicator, for the previews that draw one (the
        // settings app's strip thumbnail and the daemon's OSD strip card).
        // Gated on the resolved rect, the SAME single gate the compositor's
        // tab-strip payload uses (engine_apply.cpp): it already folds in the
        // master switch, the single-tab skip and "not tabbed", so a preview
        // cannot draw an indicator the screen does not, or miss one it does.
        //
        // Derived per COLUMN, not per tile: a tabbed column resolves exactly
        // one non-hidden tile, so the walk below stamps the data onto that one
        // tile and re-deriving it inside the tile loop would just repeat this
        // scan for every tile of every normal column.
        const bool drawsIndicator = column.tabbed && !column.tabIndicatorRect.isNull();
        int activeTabIndex = -1;
        qreal tabLengthProportion = 0.0;
        if (drawsIndicator) {
            for (int i = 0; i < column.tiles.size(); ++i) {
                if (!column.tiles.at(i).hidden) {
                    activeTabIndex = i;
                }
            }
            // Measured off the resolved rects rather than read back off the
            // params: the resolve rounds the proportion and floors it at one
            // pixel, and a preview that re-applied the raw setting would draw
            // a length the screen does not have. The column extent is the
            // basis indicatorRectFor used, so the two divide out exactly.
            const bool vertical = isVerticalTabIndicator(column.tabIndicatorPosition);
            const int axisExtent = vertical ? column.rect.height() : column.rect.width();
            const int indicatorExtent = vertical ? column.tabIndicatorRect.height() : column.tabIndicatorRect.width();
            if (axisExtent > 0) {
                tabLengthProportion =
                    qBound(0.0, static_cast<qreal>(indicatorExtent) / static_cast<qreal>(axisExtent), 1.0);
            }
        }
        const int tabCount = drawsIndicator ? column.tiles.size() : 0;
        for (const ResolvedTile& tile : column.tiles) {
            if (tile.hidden) {
                continue;
            }
            // Clip rather than drop partially-visible columns: the cut-off
            // edge is what tells the viewer the strip continues off-screen.
            const QRect clipped = tile.rect.intersected(params.workArea);
            if (!clipped.isEmpty()) {
                out.append(VisibleTile{tile.windowId, column.columnIndex, ++zoneNumber, clipped, tabCount,
                                       activeTabIndex, column.tabIndicatorPosition, tabLengthProportion});
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

QVector<ScrollEngine::VisibleTileWithRect> ScrollEngine::visibleTilesWithRects(const QString& screenId) const
{
    // The single resolve behind BOTH relative-rect surfaces
    // (visibleTileRectsRelative is a projection over this). State check
    // first, like visibleTiles: an unmanaged or empty screen must not pay
    // the ScreenManager query plus context-gap-provider call the params
    // resolve costs. The basis is the FULL screen geometry, not the
    // gap-inset work area the tiles are clipped to: every renderer of these
    // fractions draws them into a box shaped like the whole screen (the
    // settings app's strip thumbnail, and the daemon's own OSD card via its
    // twin renorm in stripzones.h) — work-area fractions in a screen-shaped
    // box stretch the strip by the panel's share of the output. Falls back
    // to the work area when no screen rect is resolvable (headless without
    // a provider), same as the parking-bounds resolution in applyLayout.
    const ScrollState* state = m_states.stateForKey(m_context.currentKeyForScreen(screenId));
    if (!state || state->strip().isEmpty()) {
        return {};
    }
    const ScrollLayoutParams params = layoutParamsForScreen(screenId);
    if (!params.workArea.isValid()) {
        return {};
    }
    QRect area = m_screenManager ? m_screenManager->screenGeometry(screenId)
                                 : (m_screenGeometryProvider ? m_screenGeometryProvider(screenId) : QRect());
    if (!area.isValid()) {
        area = params.workArea;
    }
    const QVector<VisibleTile> tiles = visibleTiles(screenId, params);
    QVector<VisibleTileWithRect> out;
    out.reserve(tiles.size());
    for (const VisibleTile& tile : tiles) {
        const QRect& r = tile.rect;
        out.append(
            {tile,
             QRectF(static_cast<qreal>(r.x() - area.x()) / area.width(),
                    static_cast<qreal>(r.y() - area.y()) / area.height(), static_cast<qreal>(r.width()) / area.width(),
                    static_cast<qreal>(r.height()) / area.height())});
    }
    return out;
}

QVector<QRectF> ScrollEngine::visibleTileRectsRelative(const QString& screenId) const
{
    // A projection over visibleTilesWithRects, not a second copy of its
    // resolve: the state check, the params and basis resolution (full screen
    // geometry, work-area fallback — see the paired walk for the rationale)
    // and the normalization formula must all stay in lockstep, and two
    // hand-maintained copies of that 30-line body already drifted apart once
    // in comments alone.
    const QVector<VisibleTileWithRect> paired = visibleTilesWithRects(screenId);
    QVector<QRectF> out;
    out.reserve(paired.size());
    for (const VisibleTileWithRect& entry : paired) {
        out.append(entry.relativeRect);
    }
    return out;
}

} // namespace PhosphorScrollEngine
