// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// BarHost's pane state machine: the three pane sources (the host pane, the
// map pane and the engine-placed external pane) collapse into the `_*Eff`
// properties every visual in the bar reads, and the focus latch has to be
// recaptured whenever any of them moves. That machinery had no coverage at
// all, which is where both of this file's highest-severity defects lived.
//
// BarHost is a PanelWindow. It is never shown here: the properties under
// test are plain QML state, and showing it would need a live compositor.

import QtQuick
import QtTest
import Phosphor.Bar

TestCase {
    id: testCase

    name: "BarPaneState"

    // A PlacementMapScreen stand-in. BarHost only asks it for the focused
    // cell, so the latch can be driven by writing this.
    Component {
        id: fakeMapComp

        QtObject {
            property string focused: "cell-a"

            // PlacementMapScreen emits this whenever its cells move; BarHost
            // has a Connections block on it, which warns if the target does
            // not carry the signal.
            signal changed

            function focusedCellId() {
                return focused;
            }
        }
    }

    Component {
        id: hostComp

        BarHost {}
    }

    function makeHost() {
        const map = createTemporaryObject(fakeMapComp, testCase);
        const host = createTemporaryObject(hostComp, testCase, {
            "placementMap": map
        });
        verify(host, "BarHost instantiates");
        return {
            "host": host,
            "map": map
        };
    }

    function test_either_pane_source_opens_the_effective_pane() {
        const host = makeHost().host;
        verify(!host._paneOpenEff, "closed to begin with");
        host.paneOpen = true;
        verify(host._paneOpenEff);
        host.paneOpen = false;
        verify(!host._paneOpenEff);
        host.mapPaneOpen = true;
        verify(host._paneOpenEff, "the map pane opens it too");
        host.mapPaneOpen = false;
        verify(!host._paneOpenEff);
    }

    function test_the_map_pane_is_never_external_and_owns_the_anchor() {
        const host = makeHost().host;
        host.paneAnchor = "chip";
        host.paneExternal = true;
        host.paneWidth = 300;
        host.mapPaneWidth = 700;
        compare(host._paneExternalEff, true);
        compare(host._paneAnchorEff, "chip");
        compare(host._paneWidthEff, 300);
        // The map pane is drawn inline under its chip, so it overrides all
        // three no matter what the host pane asked for.
        host.mapPaneOpen = true;
        compare(host._paneExternalEff, false, "the map pane is never a toplevel");
        compare(host._paneAnchorEff, "placementmap");
        compare(host._paneWidthEff, 700);
    }

    function test_opening_the_host_pane_closes_the_map_pane() {
        const host = makeHost().host;
        host.mapPaneOpen = true;
        host.paneOpen = true;
        verify(!host.mapPaneOpen, "the two panes are the same surface, so only one is up");
        verify(host._paneOpenEff);
    }

    function test_the_focus_latch_is_recaptured_on_every_pane_move() {
        // The latch records which cell had focus when the pane opened. On a
        // map-pane to host-pane swap the surface stays up, so `_paneOpenEff`
        // never moves and the latch would keep the cell from the pane before
        // it unless each source recaptures on its own.
        const s = makeHost();
        const host = s.host;
        s.map.focused = "cell-a";
        host.mapPaneOpen = true;
        compare(host._focusAtOpen, "cell-a");

        // Focus moves on while the map pane is up, then the host pane takes
        // over the same surface.
        s.map.focused = "cell-b";
        host.paneOpen = true;
        verify(!host.mapPaneOpen, "the swap happened");
        verify(host._paneOpenEff, "the surface never went down");
        compare(host._focusAtOpen, "cell-b", "the latch followed the swap");

        // Becoming an engine-placed toplevel is another move of the same pane.
        s.map.focused = "cell-c";
        host.paneExternal = true;
        compare(host._focusAtOpen, "cell-c");

        // Closing clears it rather than leaving the last cell latched.
        host.paneOpen = false;
        compare(host._focusAtOpen, "");
    }

    function test_an_external_pane_adopts_a_cell_focused_after_it_opened() {
        // The pane follows the cell the user focuses WHILE it is open, but
        // only once, and only when it is a real toplevel: the inline pane is
        // drawn under its chip and has no cell to ride.
        const s = makeHost();
        const host = s.host;
        s.map.focused = "cell-a";
        host.paneExternal = true;
        host.paneOpen = true;
        compare(host._focusAtOpen, "cell-a");
        compare(host._paneCellId, "", "the cell focused AT open is not adopted");

        s.map.focused = "cell-b";
        s.map.changed();
        compare(host._paneCellId, "cell-b");

        // Already latched: a later move does not drag the pane along.
        s.map.focused = "cell-c";
        s.map.changed();
        compare(host._paneCellId, "cell-b", "the pane stays on the cell it took");
    }

    function test_an_inline_pane_never_adopts_a_cell() {
        const s = makeHost();
        const host = s.host;
        s.map.focused = "cell-a";
        host.paneExternal = false;
        host.paneOpen = true;
        s.map.focused = "cell-b";
        s.map.changed();
        compare(host._paneCellId, "", "an inline pane rides no cell");
    }

    function test_the_latch_is_empty_without_a_map() {
        const host = createTemporaryObject(hostComp, testCase);
        verify(host);
        host.paneOpen = true;
        compare(host._focusAtOpen, "", "no map means nothing to latch, not a crash");
    }
}
