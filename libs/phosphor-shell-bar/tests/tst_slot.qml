// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
// Behaviour tests for Slot: it mounts every widget id across its groups
// through the injected registry, skips an unresolvable id (the registry
// returned null), and mounts nothing when no registry is provided or the
// group list is empty. The chip cases cover the grouping contract: each
// group renders its own chip, a chip whose widgets all hide themselves
// collapses to nothing, and a chip with one hidden widget among visible
// ones survives. A fake registry stands in for the C++ BarController so
// the slot's mounting logic is exercised without the shell process or
// real services.

import QtQuick
import QtTest
import Phosphor.Bar
import Phosphor.Theme

TestCase {
    id: testCase

    name: "Slot"

    // Trivial bar-widget stand-in with a known implicit size.
    Component {
        id: fakeWidget

        Item {
            implicitWidth: 30
            implicitHeight: 20
        }
    }

    // A widget that hides itself (zero implicit width), like Battery on a
    // desktop or the inert FocusedApp. Its cell collapses.
    Component {
        id: zeroWidget

        Item {
            implicitWidth: 0
            implicitHeight: 20
        }
    }

    // Registry-free provider: counts creations and records ids so the
    // mounting contract is observable. failFor simulates an unregistered
    // id (the real BarController returns null for those); zeroFor simulates
    // a widget that mounts but hides itself.
    QtObject {
        id: fakeRegistry

        property int created: 0
        property var mountedIds: []
        property string failFor: ""
        property string zeroFor: ""

        function createWidgetFor(id, parent) {
            if (id === fakeRegistry.failFor)
                return null;
            fakeRegistry.created++;
            fakeRegistry.mountedIds = fakeRegistry.mountedIds.concat([id]);
            if (id === fakeRegistry.zeroFor)
                return zeroWidget.createObject(parent, {});
            return fakeWidget.createObject(parent, {});
        }
    }

    // The chip delegates are children of the Slot RowLayout (a Repeater
    // reparents its delegates to the Repeater's parent). Chips carry a
    // `hasContent` property; the Repeater itself does not.
    function chips(slot) {
        let result = [];
        for (let i = 0; i < slot.children.length; i++) {
            const c = slot.children[i];
            if (c && c.hasContent !== undefined)
                result.push(c);
        }
        return result;
    }

    // Count chips that occupy layout space, rather than ones that merely
    // report hasContent. Asserting hasContent alone would keep passing if
    // the zero-width arm of implicitWidth were deleted and empty pills
    // started taking up room in the bar.
    //
    // Item.visible is deliberately NOT part of this: it is EFFECTIVE
    // visibility, so every chip reads false while the slot hangs off an
    // offscreen TestCase with no visible ancestor chain, regardless of its
    // own binding. Width is the observable that actually drives collapse.
    function renderedChipCount(slot) {
        let n = 0;
        const c = chips(slot);
        for (let i = 0; i < c.length; i++)
            if (c[i].implicitWidth > 0)
                n++;
        return n;
    }

    // The chip for a group, by index, so a test can assert its geometry.
    function chipAt(slot, index) {
        return chips(slot)[index];
    }

    Component {
        id: slotComp

        Slot {
            registry: fakeRegistry
        }
    }

    // A QObject carrying no createWidgetFor, for the mis-injection case.
    Component {
        id: bareObject

        QtObject {}
    }

    function init() {
        fakeRegistry.created = 0;
        fakeRegistry.mountedIds = [];
        fakeRegistry.failFor = "";
        fakeRegistry.zeroFor = "";
    }

    function test_mounts_every_id_across_groups() {
        createTemporaryObject(slotComp, testCase, {
            "groups": [["clock", "battery"], ["tray"]]
        });
        compare(fakeRegistry.created, 3, "every id across all groups is mounted");
        compare(fakeRegistry.mountedIds, ["clock", "battery", "tray"], "ids mounted in order");
    }

    function test_unresolvable_id_is_skipped() {
        fakeRegistry.failFor = "missing";
        createTemporaryObject(slotComp, testCase, {
            "groups": [["clock", "missing", "battery"]]
        });
        compare(fakeRegistry.created, 2, "only the resolvable ids mount");
        compare(fakeRegistry.mountedIds, ["clock", "battery"], "the unresolvable id is skipped");
    }

    function test_no_registry_mounts_nothing() {
        createTemporaryObject(slotComp, testCase, {
            "registry": null,
            "groups": [["clock"]]
        });
        compare(fakeRegistry.created, 0, "nothing mounts without a registry");
    }

    function test_empty_groups_mount_nothing() {
        createTemporaryObject(slotComp, testCase, {
            "groups": []
        });
        compare(fakeRegistry.created, 0, "an empty group list mounts nothing");
    }

    function test_each_group_is_its_own_chip() {
        const s = createTemporaryObject(slotComp, testCase, {
            "groups": [["clock"], ["audio", "network"], ["power"]]
        });
        // tryVerify re-invokes the closure each poll, so it observes the
        // layout settling; a bound property would cache its first result.
        tryVerify(() => renderedChipCount(s) === 3, 3000, "three groups render three chips");
        compare(fakeRegistry.created, 4, "every id across the three groups mounted");
        // The chip must paint the theme token, not a default.
        //
        // This is a regression guard, not decoration: listing a module under
        // qt_add_qml_module IMPORTS (rather than DEPENDENCIES) injects its
        // unqualified type names into every file here, and Kirigami exports
        // a `Theme` of its own. That shadowed the Phosphor.Theme singleton
        // across the whole bar and made every colour read undefined, which
        // no test noticed because none of them asserted a colour.
        compare(chipAt(s, 0).color, Theme.surface_variant, "the chip paints the theme token, so Theme is not shadowed");
    }

    function test_chip_collapses_when_every_widget_hides() {
        // A chip whose widgets all report zero width (Battery on a desktop,
        // the inert FocusedApp) must collapse so no empty pill shows.
        fakeRegistry.zeroFor = "battery";
        const s = createTemporaryObject(slotComp, testCase, {
            "groups": [["clock"], ["battery"]]
        });
        tryVerify(() => renderedChipCount(s) === 1, 3000, "the all-hidden chip collapses, the other stays");
        compare(fakeRegistry.created, 2, "both widgets were still mounted");
        // Assert the collapsed chip directly, so deleting the zero-width
        // arm or breaking the predicate fails this test rather than only
        // shifting a count.
        const collapsed = chipAt(s, 1);
        compare(collapsed.hasContent, false, "the all-hidden chip reports no content");
        compare(collapsed.implicitWidth, 0, "the collapsed chip reserves no width");
        // And the slot must not keep a spacing slot for it. QQuickLayout
        // honours EXPLICIT visible (unlike Item.visible's effective value),
        // so a broken `visible:` binding leaves a phantom gap that shows up
        // here as extra width even though the chip itself is zero-wide.
        // tryCompare, not compare: the parent RowLayout's own implicitWidth
        // settles on a later polish pass than the child chip's, so a plain
        // compare here races the layout.
        tryCompare(s, "implicitWidth", chipAt(s, 0).implicitWidth, 3000, "the collapsed chip leaves no spacing slot behind");
    }

    function test_chip_survives_a_partially_hidden_group() {
        // One hidden widget in a group must not collapse the whole chip.
        fakeRegistry.zeroFor = "battery";
        const s = createTemporaryObject(slotComp, testCase, {
            "groups": [["battery", "clock"]]
        });
        tryVerify(() => renderedChipCount(s) === 1, 3000, "the chip stays for its remaining visible widget");
        compare(fakeRegistry.created, 2, "both widgets were mounted");
        // The hidden cell must not leave a spacing slot inside the chip
        // either: the chip should be exactly the visible widget plus its
        // own padding.
        // tryCompare for the same reason: the preceding tryVerify is already
        // satisfied while the hidden cell still holds its spacing slot.
        tryCompare(chipAt(s, 0), "implicitWidth", 30 + Tokens.spacing_m * 2, 3000, "the hidden cell leaves no spacing slot inside the chip");
    }

    function test_unusable_group_entries_are_ignored() {
        // Neither a list nor a string: the entry is dropped with a warning
        // rather than mounting anything or throwing.
        const s = createTemporaryObject(slotComp, testCase, {
            "groups": [null, 5, ["clock"]]
        });
        compare(fakeRegistry.created, 1, "only the usable group mounts");
        tryVerify(() => renderedChipCount(s) === 1, 3000, "the unusable entries render no chip");
    }

    function test_registry_without_the_method_mounts_nothing() {
        // The property is typed QtObject, so any QObject assigns cleanly;
        // a mis-injected object lacking createWidgetFor must be refused by
        // the duck-type guard rather than throwing.
        const bare = bareObject.createObject(testCase, {});
        createTemporaryObject(slotComp, testCase, {
            "registry": bare,
            "groups": [["clock"]]
        });
        compare(fakeRegistry.created, 0, "nothing mounts through a registry with no createWidgetFor");
    }

    function test_bare_string_group_is_normalised() {
        // A layout editor writing ["clock"] instead of [["clock"]] must get
        // a one-widget chip, not a Repeater walking the string's characters.
        const s = createTemporaryObject(slotComp, testCase, {
            "groups": ["clock"]
        });
        compare(fakeRegistry.created, 1, "the bare string mounts exactly one widget");
        compare(fakeRegistry.mountedIds, ["clock"], "and it is the id itself, not a character");
    }
}
