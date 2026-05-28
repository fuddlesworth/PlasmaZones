// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

// PerScreen — instantiate one delegate per row of a `ScreenModel`-shaped
// model, keying live delegates on the row's `screen` (QScreen*) identity
// so a model reset (`beginResetModel` / `endResetModel`, which is how
// PhosphorShell::ScreenModel signals hot-plug) does NOT tear down and
// recreate delegates for screens that survived the reset.
//
// Why not a plain `Instantiator { model: PhosphorShell.screens }`?
// `Instantiator` keys delegates by row index, not by the QScreen*
// payload. When the model resets, every delegate is destroyed and
// rebuilt — fine for the trivial demo case (a small window per
// monitor), but production consumers (per-monitor wallpapers,
// overlays, panels, layer-shell surfaces) rely on delegate identity
// surviving hot-plugs to keep their state (textures, mid-flight
// animations, open menus, accumulated graphs) intact.
//
// Contract for the `delegate` Component:
//   - Receives initial properties `screen` (QScreen*), `name` (string),
//     `index` (int), `isPrimary` (bool) on creation.
//   - If the delegate root is a `Window`, the `screen` initial-property
//     sets `Window.screen`, anchoring the window to the right monitor
//     without the caller wiring geometry plumbing.
//   - To consume the other fields, declare them as `required property`s
//     on the delegate root.
//   - The delegate is destroyed when its screen leaves the model.
//
// Lifetime: delegates are parented to the PerScreen item. Destroying
// the PerScreen tears down all delegates. Consumers MUST NOT call
// `destroy()` on delegates themselves; the PerScreen's reset diff would
// then dereference a dangling pointer.

import QtQml
import QtQuick

Item {
    id: root

    // QAbstractItemModel exposing the PhosphorShell::ScreenModel role
    // shape (screen / name / width / height / isPrimary). Consumers in
    // a phosphor-shell context typically pass `PhosphorShell.screens`;
    // the helper does NOT default to the context property so the QML
    // file is usable in tests and non-shell processes without
    // resolving `PhosphorShell` lazily.
    property var model: null

    // Component instantiated once per surviving screen.
    property Component delegate: null

    // Number of currently instantiated delegates. Matches model.rowCount()
    // under steady state; tests assert it across hot-plug operations.
    // Updated explicitly from `_rebuild` / `_teardownAll` because JS
    // Map.size doesn't trigger QML binding re-evaluation when entries
    // are added or removed (Map.set / Map.delete mutate the same
    // object instance, so the property tracker never sees a change).
    property int count: 0

    // Non-visual: size to zero and hide so a PerScreen placed inside a
    // visual parent (test windows, demo layouts) doesn't influence
    // layout. Delegates are top-level objects parented to PerScreen,
    // not visual children.
    visible: false
    width: 0
    height: 0

    // Internal: Map<QScreen*, QObject> keyed by the screen reference.
    // JS Map (NOT a plain object literal) so QObject* identity is the
    // key rather than a stringified pointer — surviving hot-plugs
    // depends on `screen === screen` comparison, which Map provides
    // and `{}` does not.
    property var _instances: new Map()

    // ScreenModel role values, mirrored from PhosphorShell/ScreenModel.h.
    // Hard-coded because the ScreenModel::Role enum is not exposed to
    // QML (the model is consumed via roles by name in Repeater/etc.,
    // but our explicit data() calls need the integer values). If
    // ScreenModel reorders the enum, this must be updated in lockstep
    // — the test fixture pins the same values so a drift is caught.
    readonly property int _roleScreen: Qt.UserRole + 1
    readonly property int _roleName: Qt.UserRole + 2
    readonly property int _roleIsPrimary: Qt.UserRole + 5

    Component.onCompleted: root._rebuild()
    Component.onDestruction: root._teardownAll()

    onModelChanged: {
        root._teardownAll();
        root._rebuild();
    }

    Connections {
        target: root.model

        // Hot-plug add / remove arrives as a reset (ScreenModel's
        // implementation) but row-incremental signals are supported in
        // case a future provider becomes incremental.
        function onModelReset() {
            root._rebuild();
        }
        function onRowsInserted(parent, first, last) {
            root._rebuild();
        }
        function onRowsRemoved(parent, first, last) {
            root._rebuild();
        }

        // Primary-screen swap: only isPrimary / index can have changed.
        // Update affected delegates in place — recreating them would
        // defeat the entire reuse contract.
        function onDataChanged(topLeft, bottomRight, roles) {
            if (!root.model)
                return;
            const rowCount = root.model.rowCount();
            for (let i = 0; i < rowCount; ++i) {
                const idx = root.model.index(i, 0);
                const screen = root.model.data(idx, root._roleScreen);
                const inst = root._instances.get(screen);
                if (!inst)
                    continue;
                inst.index = i;
                inst.isPrimary = root.model.data(idx, root._roleIsPrimary);
            }
        }
    }

    // Diff the current row set against the live delegate map; destroy
    // delegates for departed screens, update properties on survivors,
    // construct delegates for new arrivals. Idempotent — calling it
    // with no changes is cheap (a single pass over the model).
    function _rebuild() {
        if (!root.model || !root.delegate)
            return;
        // Pass 1: collect screens currently in the model with their row
        // index. Map preserves insertion order, which matches the
        // model's row order — useful for stable `index` assignment.
        const wanted = new Map();
        const rowCount = root.model.rowCount();
        for (let i = 0; i < rowCount; ++i) {
            const idx = root.model.index(i, 0);
            wanted.set(root.model.data(idx, root._roleScreen), i);
        }

        // Pass 2: destroy delegates whose screen no longer appears.
        // Collect first, then mutate, so we don't iterate _instances
        // while deleting from it.
        const toDestroy = [];
        root._instances.forEach((inst, screen) => {
            if (!wanted.has(screen))
                toDestroy.push(screen);
        });
        for (const screen of toDestroy) {
            root._instances.get(screen).destroy();
            root._instances.delete(screen);
        }
        // Pass 3: update survivors in place; construct delegates for
        // new screens via createObject + initialProperties.
        for (const [screen, i] of wanted) {
            const idx = root.model.index(i, 0);
            const name = root.model.data(idx, root._roleName);
            const isPrimary = root.model.data(idx, root._roleIsPrimary);
            const existing = root._instances.get(screen);
            if (existing) {
                existing.index = i;
                existing.isPrimary = isPrimary;
                // `screen` and `name` are per-QScreen invariants — a
                // given QScreen* doesn't switch identity nor rename.
            } else {
                const inst = root.delegate.createObject(root, {
                    "screen": screen,
                    "name": name,
                    "index": i,
                    "isPrimary": isPrimary
                });
                if (inst)
                    root._instances.set(screen, inst);
            }
        }
        root.count = root._instances.size;
    }

    function _teardownAll() {
        root._instances.forEach(inst => inst.destroy());
        root._instances.clear();
        root.count = 0;
    }
}
