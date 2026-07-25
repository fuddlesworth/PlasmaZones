// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
// Behaviour tests for Slot: it mounts every widget id across its groups
// through the injected registry, skips an unresolvable id (the registry
// returned null), and mounts nothing when no registry is provided or the
// group list is empty. A fake registry stands in for the C++ BarController
// so the slot's mounting logic is exercised without the shell process or
// real services.

import QtQuick
import QtTest
import Phosphor.Bar

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

    // Registry-free provider: counts creations and records ids so the
    // mounting contract is observable. failFor simulates an unregistered
    // id (the real BarController returns null for those).
    QtObject {
        id: fakeRegistry

        property int created: 0
        property var mountedIds: []
        property string failFor: ""

        function createWidgetFor(id, parent) {
            if (id === fakeRegistry.failFor)
                return null;
            fakeRegistry.created++;
            fakeRegistry.mountedIds = fakeRegistry.mountedIds.concat([id]);
            return fakeWidget.createObject(parent, {});
        }
    }

    Component {
        id: slotComp

        Slot {
            registry: fakeRegistry
        }
    }

    function init() {
        fakeRegistry.created = 0;
        fakeRegistry.mountedIds = [];
        fakeRegistry.failFor = "";
    }

    function test_mounts_every_id_across_groups() {
        const s = createTemporaryObject(slotComp, testCase, {
            "groups": [["clock", "battery"], ["tray"]]
        });
        compare(fakeRegistry.created, 3, "every id across all groups is mounted");
        compare(fakeRegistry.mountedIds, ["clock", "battery", "tray"], "ids mounted in order");
    }

    function test_unresolvable_id_is_skipped() {
        fakeRegistry.failFor = "missing";
        const s = createTemporaryObject(slotComp, testCase, {
            "groups": [["clock", "missing", "battery"]]
        });
        compare(fakeRegistry.created, 2, "only the resolvable ids mount");
        compare(fakeRegistry.mountedIds, ["clock", "battery"], "the unresolvable id is skipped");
    }

    function test_no_registry_mounts_nothing() {
        const s = createTemporaryObject(slotComp, testCase, {
            "registry": null,
            "groups": [["clock"]]
        });
        compare(fakeRegistry.created, 0, "nothing mounts without a registry");
    }

    function test_empty_groups_mount_nothing() {
        const s = createTemporaryObject(slotComp, testCase, {
            "groups": []
        });
        compare(fakeRegistry.created, 0, "an empty group list mounts nothing");
    }
}
