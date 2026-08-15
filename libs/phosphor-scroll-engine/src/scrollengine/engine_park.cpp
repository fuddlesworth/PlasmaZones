// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include "scrollpark_p.h"

#include <QtGlobal>

namespace PhosphorScrollEngine::Detail {

void parkRect(QRect& rect, const QRect& screenRect, int parkTop)
{
    const int maxLeft = qMax(screenRect.left(), screenRect.right() + 1 - rect.width());
    rect.moveLeft(qBound(screenRect.left(), rect.left(), maxLeft));
    rect.moveTop(parkTop);
}

ParkResult resolveTilePlacement(const ParkInputs& in, const QString& remembered)
{
    ParkResult out;
    out.rect = in.tileRect;
    QRect& rect = out.rect;

    // ── Off the viewport ────────────────────────────────────────────────
    //
    // Which screen edge this tile's motion is anchored to, decided from the
    // STRIP, before parking touches the rect. That ordering is the fix: the
    // departure side is a fact about the strip, and reading it back off a
    // parked rect (as the old code effectively did, by encoding it in the park
    // position) makes it a fact about whichever side happened to be safe to
    // park on. Empty for a tile that is on screen — nothing to anchor.
    if (in.hidden) {
        // Non-active tile of a tabbed column: parked off-canvas so it cannot
        // steal input from the visible tab (hit-testing uses real geometry
        // only). It follows its COLUMN's side, so a column off the left edge
        // keeps its hidden tiles anchored left. A column that is ON screen
        // records NO edge: its hidden tabs are parked for input reasons, not
        // because the strip scrolled them away, and there is no departure to
        // animate back from. Defaulting them to "right" made the next tab
        // switch slide the newly-active tab in from off the right edge.
        if (in.columnRect.right() < in.workArea.left()) {
            out.emittedEdge = QStringLiteral("left");
        } else if (in.columnRect.left() > in.workArea.right()) {
            out.emittedEdge = QStringLiteral("right");
        }
        parkRect(rect, in.screenRect, in.parkTop);
        out.parked = true;
    } else if (rect.right() < in.workArea.left()) {
        out.emittedEdge = QStringLiteral("left");
        parkRect(rect, in.screenRect, in.parkTop);
        out.parked = true;
    } else if (rect.left() > in.workArea.right()) {
        out.emittedEdge = QStringLiteral("right");
        parkRect(rect, in.screenRect, in.parkTop);
        out.parked = true;
    }

    // ── The remembered-edge contract ────────────────────────────────────
    //
    // The dispatch keys on parked, not on the edge being empty: a hidden tab
    // of an on-screen column parks WITHOUT an edge, and treating that as "on
    // screen" would consume a remembered departure it is not making.
    if (out.parked) {
        if (out.emittedEdge.isEmpty()) {
            // Parked with no departure direction. Any edge remembered from an
            // earlier genuine departure is stale now that the column has come
            // back, and leaving it would make the next activation of this tab
            // slide in from a side it has already returned from.
            out.rememberedEdge = std::nullopt;
        } else {
            out.rememberedEdge = out.emittedEdge;
        }
    } else {
        // On screen. If it was parked until now, it is arriving, and the edge
        // it went out by is the edge it must come back in from — the whole
        // reason that edge is remembered rather than read back off the parked
        // rect. CONSUMED here: the window is on screen, so there is no longer
        // a departure to remember, and leaving the entry would re-anchor a
        // later unrelated move to a stale side.
        out.emittedEdge = remembered;
        out.rememberedEdge = std::nullopt;
    }

    // ── The main-axis straddle clamp ────────────────────────────────────
    //
    // A partially-visible edge column is CLAMPED at BOTH screen edges,
    // adjacent output or not.
    //
    // Keeping the TRUE rect everywhere and clipping the overhang at render
    // time was the previous contract, and it is not enforceable: the effect's
    // per-output skip works only while the window is composited through the GL
    // scene, and the compositor is free to present an idle-but-damaged surface
    // directly on a hardware plane, bypassing the effect chain entirely — at
    // which point the full frame appears on the neighbouring monitor with every
    // effect-side guard behaving perfectly. Committed geometry is the only
    // contract every present path honours, so the rect itself must stop at the
    // boundary. The window resizes rather than being visually cropped; that is
    // the accepted cost, and it recurs on every scroll step.
    //
    // The clamp bound is the SCREEN edge, not the work area's: the band between
    // them is this monitor's own panel area, not the neighbour.
    //
    // Unconditional, AFTER every positioning decision above. The first version
    // ran only for never-parked tiles, which missed the commonest straddler: a
    // previously-parked neighbour scrolling back into partial view consumes its
    // remembered departure edge, arrives with an edge set, and skipped the
    // clamp — committing its full rect across the boundary.
    //
    // Crop mode keeps the TRUE rect for a main-axis straddler and lets the
    // effect crop the overhang; it forces GL composition via
    // blocksDirectScanout while scrolling screens exist, which closes the
    // hardware-plane bypass that made render-side cropping unsafe as a default.
    // Crop mode covers only what the setting names: the side-edge straddle.
    // Cross-axis stack overflow is enforced unconditionally further down.
    if (!in.cropStraddlers && !out.parked) {
        const bool straddleRight = rect.right() > in.screenRect.right() && rect.left() <= in.screenRect.right();
        // Both predicates read the PRE-mutation rect, and the left one is
        // consumed after the right branch may have called setRight. Safe only
        // because the two are mutually exclusive: a column cannot straddle both
        // edges, since columnExtentPx caps every column at the work area's main
        // extent, which is never larger than the screen's. That is a non-local
        // invariant holding this code up, so it is stated here rather than left
        // to be rediscovered.
        const bool straddleLeft = rect.left() < in.screenRect.left() && rect.right() >= in.screenRect.left();
        if (straddleRight || straddleLeft) {
            // Under respectMinimumSize the peek floor rises to the client's
            // declared minimum: committing a clamped extent the client refuses
            // makes KWin regrow the frame from its origin, pushing it straight
            // back across the boundary (X11 clients have no Wayland-side repair
            // for this). Such a column parks instead of peeking. The floor is
            // capped at the screen's own extent, so a column whose declared
            // minimum exceeds the screen still peeks at full screen extent
            // instead of parking forever. For exactly that oversized-minimum
            // case the regrow hazard the floor exists to avoid is knowingly
            // re-accepted — a straddle beats a permanently invisible window.
            const int peekFloor = qMin(in.screenRect.width(), qMax(kMinVisiblePeekPx, in.tileMin.width()));
            if (straddleRight) {
                const int visible = in.screenRect.right() + 1 - rect.left();
                if (visible >= peekFloor) {
                    rect.setRight(in.screenRect.right());
                } else {
                    out.emittedEdge = QStringLiteral("right");
                    out.rememberedEdge = out.emittedEdge;
                    parkRect(rect, in.screenRect, in.parkTop);
                    out.parked = true;
                }
            }
            if (!out.parked && straddleLeft) {
                const int visible = rect.right() + 1 - in.screenRect.left();
                if (visible >= peekFloor) {
                    out.clampPinnedMain = rect.left() != in.screenRect.left();
                    rect.setLeft(in.screenRect.left());
                } else {
                    out.emittedEdge = QStringLiteral("left");
                    out.rememberedEdge = out.emittedEdge;
                    parkRect(rect, in.screenRect, in.parkTop);
                    out.parked = true;
                }
            }
        }
    }

    // ── The cross-axis stack-overflow clamp ─────────────────────────────
    //
    // The same enforcement, in BOTH modes: a stacked column whose minimum
    // extents overflow the work area lays its trailing tiles out beyond it
    // (relayout documents the overflow as standing), and on a stacked monitor
    // topology that commits geometry onto the neighbouring output. That is
    // stack layout, not strip motion, so it is not what crop mode opts into and
    // it carries no arrival direction. A tile entirely beyond the screen parks;
    // a straddling one clamps or parks by the same floor logic as the sides. A
    // main-axis departure edge already consumed this pass is put back and NOT
    // emitted: this park has no side to animate from, and the memory must
    // survive for the eventual main-axis arrival.
    if (!out.parked && rect.top() > in.screenRect.bottom()) {
        if (!out.emittedEdge.isEmpty()) {
            out.rememberedEdge = out.emittedEdge;
            out.emittedEdge.clear();
        }
        parkRect(rect, in.screenRect, in.parkTop);
        out.parked = true;
    } else if (!out.parked && rect.bottom() > in.screenRect.bottom()) {
        const int peekFloor = qMin(in.screenRect.height(), qMax(kMinVisiblePeekPx, in.tileMin.height()));
        const int visible = in.screenRect.bottom() + 1 - rect.top();
        if (visible >= peekFloor) {
            rect.setBottom(in.screenRect.bottom());
        } else {
            if (!out.emittedEdge.isEmpty()) {
                out.rememberedEdge = out.emittedEdge;
                out.emittedEdge.clear();
            }
            parkRect(rect, in.screenRect, in.parkTop);
            out.parked = true;
        }
    }

    return out;
}

} // namespace PhosphorScrollEngine::Detail
