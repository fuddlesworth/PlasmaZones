// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
// The placement map's matched-edge morph (A2 §2.1, §2.3). The pure
// reconcile logic is checked directly through MiniatureEdges.js, then
// the same contract is driven through a PlacementMiniature over a fake
// model so the persistent edge model and `morphing` are covered too.
//
// The load-bearing cases: a 50/50 split shared by the old and the new
// geometry RETARGETS rather than re-entering (the edge row keeps its
// key and never leaves the live set), and at most two generations are
// drawn (a third geometry arriving mid-release drops the releasing edges
// of the oldest generation at once).

import QtQuick
import QtTest
import Phosphor.Widgets
import "qrc:/qt/qml/Phosphor/Widgets/MiniatureEdges.js" as Edges

TestCase {
    id: testCase

    name: "MiniatureEdges"
    when: windowShown

    // A fake PlacementMapScreen: whatever the miniature reads.
    Component {
        id: fakeModel

        QtObject {
            property int mode: 0
            property real aspect: 16 / 9
            property var cells: []
            property var lens: null
            property int overflowLeft: 0
            property int overflowRight: 0
            property int stripExtentPx: 0
            property int desktopCount: 1
            property int currentDesktop: 0
            signal changed
        }
    }

    Component {
        id: miniComp

        PlacementMiniature {
            width: 200
            height: 112
        }
    }

    function cell(id, x, y, w, h, occupied) {
        return {
            "id": id,
            "x": x,
            "y": y,
            "w": w,
            "h": h,
            "t": x + w / 2,
            "occupied": occupied === undefined ? true : occupied,
            "focused": false,
            "urgent": false,
            "label": id
        };
    }

    // Two halves: a 50/50 vertical split.
    function halves() {
        return [cell("L", 0, 0, 0.5, 1), cell("R", 0.5, 0, 0.5, 1)];
    }

    // Left half split in two, right half kept: shares the 50/50 edge.
    function leftStack() {
        return [cell("a", 0, 0, 0.5, 0.5), cell("b", 0, 0.5, 0.5, 0.5), cell("c", 0.5, 0, 0.5, 1)];
    }

    // Thirds: no edge in common with the halves but the frame.
    function thirds() {
        return [cell("p", 0, 0, 1 / 3, 1), cell("q", 1 / 3, 0, 1 / 3, 1), cell("r", 2 / 3, 0, 1 / 3, 1)];
    }

    function keys(list) {
        return list.map(e => e.key);
    }

    function findEdge(edges, o, pos) {
        for (let i = 0; i < edges.length; ++i) {
            if (edges[i].o === o && Math.abs(edges[i].pos - pos) < 1e-6)
                return edges[i];
        }
        return null;
    }

    function rows(mini) {
        const out = [];
        for (let i = 0; i < mini.edgeModel.count; ++i)
            out.push(mini.edgeModel.get(i));
        return out;
    }

    function test_edgesFor_merges_shared_edges() {
        const edges = Edges.edgesFor(halves());
        // Frame (4) plus the one shared vertical split: the two cells'
        // coincident x = 0.5 edges merge, as do their top and bottom
        // lines into one full-width edge each.
        compare(edges.length, 5);
        const split = findEdge(edges, "v", 0.5);
        verify(split !== null);
        compare(split.start, 0);
        compare(split.end, 1);
        verify(split.occupied);
        const top = findEdge(edges, "h", 0);
        compare(top.start, 0);
        compare(top.end, 1);
        // Reading order: top edge first.
        compare(edges[0].o, "h");
        compare(edges[0].pos, 0);
    }

    function test_shared_split_retargets_rather_than_reentering() {
        const first = Edges.edgesFor(halves()).map(e => Object.assign({
                "releasing": false
            }, e));
        const r = Edges.reconcile(first, Edges.edgesFor(leftStack()));
        // The 50/50 split and the frame are kept; only the new horizontal
        // half-line enters; nothing releases.
        const splitKey = findEdge(first, "v", 0.5).key;
        verify(keys(r.retarget).indexOf(splitKey) >= 0);
        compare(r.retarget.length, 5);
        compare(r.enter.length, 1);
        compare(r.enter[0].o, "h");
        compare(r.enter[0].pos, 0.5);
        compare(r.release.length, 0);
        compare(r.drop.length, 0);
        verify(r.morph);
    }

    function test_near_edge_within_tolerance_retargets() {
        const first = Edges.edgesFor(halves()).map(e => Object.assign({
                "releasing": false
            }, e));
        // A 55/45 split: within 6 % of the 50/50 edge.
        const next = Edges.edgesFor([cell("L", 0, 0, 0.55, 1), cell("R", 0.55, 0, 0.45, 1)]);
        const r = Edges.reconcile(first, next);
        const splitKey = findEdge(first, "v", 0.5).key;
        const moved = r.retarget.filter(t => t.key === splitKey);
        compare(moved.length, 1);
        compare(moved[0].pos, 0.55);
        compare(r.enter.length, 0);
        compare(r.release.length, 0);
        // A 60/40 split is past the tolerance: the old edge releases and
        // the new one enters.
        const far = Edges.reconcile(first, Edges.edgesFor([cell("L", 0, 0, 0.6, 1), cell("R", 0.6, 0, 0.4, 1)]));
        compare(far.release, [splitKey]);
        compare(far.enter.length, 1);
    }

    function test_enter_stagger_is_capped() {
        const many = [];
        for (let i = 0; i < 12; ++i)
            many.push(cell("c" + i, i / 12, 0, 1 / 12, 1));
        const r = Edges.reconcile([], Edges.edgesFor(many));
        compare(r.enter[0].delay, 0);
        compare(r.enter[1].delay, 15);
        compare(r.enter[r.enter.length - 1].delay, 150);
    }

    function test_two_generations_max() {
        // Generation 1 live, generation 2 arrives: the thirds' inner
        // edges enter and the halves' split releases.
        const g1 = Edges.edgesFor(halves()).map(e => Object.assign({
                "releasing": false
            }, e));
        const r2 = Edges.reconcile(g1, Edges.edgesFor(thirds()));
        const splitKey = findEdge(g1, "v", 0.5).key;
        compare(r2.release, [splitKey]);
        compare(r2.drop.length, 0);
        // The rendered state mid-release: the split still there, releasing.
        const byKey = {};
        g1.forEach(e => byKey[e.key] = e);
        const mid = r2.retarget.map(t => Object.assign({}, byKey[t.key], t, {
                "releasing": false
            })).concat(r2.enter.map(e => Object.assign({}, e, {
                "releasing": false
            }))).concat([Object.assign({}, byKey[splitKey], {
                "releasing": true
            })]);
        // Generation 3 arrives (quarters): the still-releasing split of
        // generation 1 is DROPPED at once, and the thirds' edges become
        // the new releasing generation.
        const quarters = [cell("w", 0, 0, 0.25, 1), cell("x", 0.25, 0, 0.25, 1), cell("y", 0.5, 0, 0.25, 1), cell("z", 0.75, 0, 0.25, 1)];
        const r3 = Edges.reconcile(mid, Edges.edgesFor(quarters));
        verify(r3.morph);
        // The 0.5 quarter edge re-adopts the releasing 50/50 split rather
        // than entering: a releasing edge within tolerance is still a
        // match. So it is retargeted, not dropped.
        verify(keys(r3.retarget).indexOf(splitKey) >= 0);
        compare(r3.drop.length, 0);
        // The thirds' inner edges (1/3, 2/3) are farther than 6 % from
        // any quarter edge, so they release now.
        compare(r3.release.length, 2);

        // Same again but with a releasing edge nothing matches: it is
        // dropped instead of lingering as a third generation.
        const stale = mid.concat([
            {
                "key": "v:20:9",
                "o": "v",
                "pos": 0.1,
                "start": 0,
                "end": 1,
                "occupied": true,
                "releasing": true
            }
        ]);
        const r3b = Edges.reconcile(stale, Edges.edgesFor(quarters));
        compare(r3b.drop, ["v:20:9"]);
    }

    function test_no_morph_keeps_releasing_generation() {
        const g1 = Edges.edgesFor(halves()).map(e => Object.assign({
                "releasing": false
            }, e));
        const stale = g1.concat([
            {
                "key": "old",
                "o": "h",
                "pos": 0.3,
                "start": 0,
                "end": 1,
                "occupied": true,
                "releasing": true
            }
        ]);
        // Same geometry again (an occupancy-only change): nothing enters
        // or releases, so the releasing edge is left to finish.
        const r = Edges.reconcile(stale, Edges.edgesFor(halves()));
        verify(!r.morph);
        compare(r.drop.length, 0);
        compare(r.enter.length, 0);
    }

    // The miniature over a fake model: rows persist across the morph.
    function test_miniature_keeps_the_shared_split_row() {
        const model = createTemporaryObject(fakeModel, testCase, {});
        const mini = createTemporaryObject(miniComp, testCase, {
            "model": model
        });
        model.cells = halves();
        model.changed();
        compare(mini.edgeModel.count, 5);
        compare(mini.liveCount, 2);
        verify(mini.morphing);
        // Let the enters finish (180 ms plus stagger).
        tryVerify(() => !mini.morphing, 2000);
        const before = rows(mini);
        const split = before.filter(e => e.o === "v" && Math.abs(e.pos - 0.5) < 1e-6);
        compare(split.length, 1);
        compare(split[0].phase, "live");

        model.cells = leftStack();
        model.changed();
        // The split row is the same row, still live; one edge is entering.
        const after = rows(mini);
        const same = after.filter(e => e.key === split[0].key);
        compare(same.length, 1);
        compare(same[0].phase, "live");
        compare(after.filter(e => e.phase === "enter").length, 1);
        compare(after.filter(e => e.phase === "release").length, 0);
        verify(mini.morphing);
        tryVerify(() => !mini.morphing, 2000);
        compare(mini.edgeModel.count, 6);
    }

    function test_miniature_drops_oldest_generation() {
        const model = createTemporaryObject(fakeModel, testCase, {});
        const mini = createTemporaryObject(miniComp, testCase, {
            "model": model
        });
        model.cells = halves();
        model.changed();
        tryVerify(() => !mini.morphing, 2000);
        model.cells = thirds();
        model.changed();
        // The 50/50 split is releasing (720 ms).
        compare(rows(mini).filter(e => e.phase === "release").length, 1);
        // A third geometry before the release finishes: a layout with no
        // edge near 0.5, so the releasing split is dropped immediately
        // and only the thirds' inner edges are releasing.
        model.cells = [cell("one", 0, 0, 0.2, 1), cell("two", 0.2, 0, 0.8, 1)];
        model.changed();
        const releasing = rows(mini).filter(e => e.phase === "release");
        compare(releasing.length, 2);
        verify(releasing.every(e => Math.abs(e.pos - 0.5) > 1e-6));
        tryVerify(() => !mini.morphing, 3000);
        // Only the live geometry remains: the frame plus the 0.2 split.
        compare(mini.edgeModel.count, 5);
    }
}
