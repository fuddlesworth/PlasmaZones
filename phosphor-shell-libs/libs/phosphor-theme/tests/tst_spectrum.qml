// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Spectrum is the one place the whole shell turns a coordinate into a
// colour, so its interpolation and its clamps are load-bearing for every
// rail, strip and state tint that reads them.

import QtQuick
import QtTest
import Phosphor.Theme

TestCase {
    id: testCase

    name: "Spectrum"

    function test_at_returns_the_stops_at_the_stop_positions() {
        const stops = Spectrum.stops;
        compare(stops.length, 4, "the ramp has four brand stops");
        // t of 0, 1/3, 2/3 and 1 land exactly on the four stops.
        for (let i = 0; i < stops.length; ++i) {
            const c = Spectrum.at(i / (stops.length - 1));
            fuzzyCompare(c.r, stops[i].r, 0.002);
            fuzzyCompare(c.g, stops[i].g, 0.002);
            fuzzyCompare(c.b, stops[i].b, 0.002);
        }
    }

    function test_at_interpolates_between_two_stops() {
        const stops = Spectrum.stops;
        // Halfway along the first segment is the midpoint of stops 0 and 1.
        const mid = Spectrum.at(1 / 6);
        fuzzyCompare(mid.r, (stops[0].r + stops[1].r) / 2, 0.002);
        fuzzyCompare(mid.g, (stops[0].g + stops[1].g) / 2, 0.002);
        fuzzyCompare(mid.b, (stops[0].b + stops[1].b) / 2, 0.002);
        // And it is genuinely between them, not either end.
        verify(mid.b !== stops[0].b || mid.r !== stops[0].r);
    }

    function test_at_clamps_and_never_indexes_past_the_last_stop() {
        const stops = Spectrum.stops;
        const first = Spectrum.at(0);
        const last = Spectrum.at(1);
        // Out of range in both directions, and the non-numeric path, all
        // clamp rather than reading s[i + 1] off the end (which would be
        // undefined and produce a transparent colour).
        compare(Spectrum.at(-4), first);
        compare(Spectrum.at("banana"), first, "a non-numeric t falls back to 0");
        compare(Spectrum.at(9), last);
        fuzzyCompare(last.a, 1, 0.001, "the far end is opaque, not a read past the array");
        fuzzyCompare(last.r, stops[3].r, 0.002);
    }

    function test_tForX_maps_the_edge_and_guards_a_zero_width() {
        fuzzyCompare(Spectrum.tForX(0, 200), 0, 0.0001);
        fuzzyCompare(Spectrum.tForX(100, 200), 0.5, 0.0001);
        fuzzyCompare(Spectrum.tForX(200, 200), 1, 0.0001);
        // Off either end clamps into the ramp.
        fuzzyCompare(Spectrum.tForX(-50, 200), 0, 0.0001);
        fuzzyCompare(Spectrum.tForX(400, 200), 1, 0.0001);
        // A zero or negative width would divide by zero; it answers 0.
        compare(Spectrum.tForX(50, 0), 0);
        compare(Spectrum.tForX(50, -10), 0);
    }

    function test_state_names_are_the_four_stops_in_ramp_order() {
        const stops = Spectrum.stops;
        compare(Spectrum.resting, stops[0]);
        compare(Spectrum.active, stops[1]);
        compare(Spectrum.pending, stops[2]);
        compare(Spectrum.hot, stops[3]);
        // Focus is white and never a position on the ramp (05 R9).
        compare(Spectrum.focus, "#ffffff");
        for (let i = 0; i < stops.length; ++i)
            verify(Spectrum.focus !== stops[i], "focus is not a hue on the axis");
    }
}
