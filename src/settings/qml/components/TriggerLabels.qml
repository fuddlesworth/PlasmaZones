// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

// `pragma Singleton` pairs with the QT_QML_SINGLETON_TYPE source property set
// in CMakeLists BEFORE qt_add_qml_module — both are required, exactly as for
// CurvePresets (see the rationale at that file's declaration).
pragma Singleton

import QtQuick

/**
 * @brief User-facing labels for an activation trigger.
 *
 * A trigger is stored as `{ modifier, mouseButton }`, both raw Qt bitmasks.
 * Anywhere one is shown to a reader it has to become "Alt + Right", and the
 * bit → label tables are the kind of thing that rots once it exists twice, so
 * they live here: the trigger editor renders its chips from them and the
 * profile diff resolves stored values through them.
 */
QtObject {
    // Qt::KeyboardModifier bits — bit shifts rather than hex literals so
    // qmlformat cannot mangle them (0x02000000 ShiftModifier, and so on).
    readonly property int shiftFlag: (1 << 25)
    readonly property int ctrlFlag: (1 << 26)
    readonly property int altFlag: (1 << 27)
    readonly property int metaFlag: (1 << 28)

    readonly property var modifiers: [
        {
            "bit": shiftFlag,
            "label": i18n("Shift")
        },
        {
            "bit": ctrlFlag,
            "label": i18n("Ctrl")
        },
        {
            "bit": altFlag,
            "label": i18n("Alt")
        },
        {
            "bit": metaFlag,
            "label": i18n("Meta")
        }
    ]

    // Qt::MouseButton bits. The "Extra 3/4/5" naming matches the trigger
    // editor's chips (the kcfg spells the same buttons Extra1/2/3).
    readonly property var mouseButtons: [
        {
            "bit": 2,
            "label": i18n("Right")
        },
        {
            "bit": 4,
            "label": i18n("Middle")
        },
        {
            "bit": 8,
            "label": i18n("Back")
        },
        {
            "bit": 16,
            "label": i18n("Forward")
        },
        {
            "bit": 32,
            "label": i18n("Extra 3")
        },
        {
            "bit": 64,
            "label": i18n("Extra 4")
        },
        {
            "bit": 128,
            "label": i18n("Extra 5")
        }
    ]

    /// "Alt + Right" for the given bitmasks, or @p emptyText when neither is
    /// set. Modifiers lead, then mouse buttons, matching the editor's ordering.
    function label(modifier, mouseButton, emptyText) {
        const parts = [];
        for (let i = 0; i < modifiers.length; ++i) {
            if ((modifier & modifiers[i].bit) !== 0)
                parts.push(modifiers[i].label);
        }
        for (let j = 0; j < mouseButtons.length; ++j) {
            if ((mouseButton & mouseButtons[j].bit) !== 0)
                parts.push(mouseButtons[j].label);
        }
        return parts.length > 0 ? parts.join(" + ") : emptyText;
    }
}
