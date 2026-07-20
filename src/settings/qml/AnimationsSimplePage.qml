// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

/**
 * @brief Animations (simple mode) — the pared-down animation surface.
 *
 * Shown ONLY in simple mode (registered SimpleOnly); advanced mode replaces
 * it with the full per-event Animations tree. The page is the SAME
 * viewport-virtualized AnimationEventCardList the advanced pages use — real
 * AnimationEventCards, not forked simplified controls — but each card
 * covers a whole group of analogous events:
 *
 *   - Window opened & closed: the open card, with every edit MIRRORED onto
 *     the close leaf (see the Connections below) so one card sets both.
 *   - Window minimized: the single minimize leaf (the restore direction is
 *     the same effect reversed; there is no separate leaf).
 *   - Window movement: the `window.movement` CASCADE PARENT — maximize,
 *     snap in/out, and layout switch all inherit from it natively, so one
 *     card controls the whole geometry class with no mirroring. A child
 *     override set in advanced mode shadows it; the card's built-in
 *     shadowing-children banner surfaces that.
 *   - Window moved: the drag-time `move` class. Its shader class is
 *     opt-in and never inherited, so it cannot ride the movement parent
 *     and keeps its own card.
 *   - Desktop switched: the desktop.switch leaf (peek stays advanced).
 *
 * The header block leads with the Global animation defaults card (enable
 * toggle + the same curve/timing/duration editor the advanced General page
 * uses) followed by the animation window-filter card; the grouped event
 * cards come after. Everything else (focus, OSDs, overlays, panels,
 * widgets, editor, the library) is advanced-mode depth.
 */
AnimationEventCardList {
    id: simplePage

    readonly property QtObject globalSettings: settingsController.settings
    readonly property var animController: settingsController.animationsPage

    // ── Open ↔ close mirroring ──────────────────────────────────────────
    // The open and close leaves are siblings (their shared parent also
    // covers minimize and focus, so the cascade can't pair them alone).
    // While this page is alive, any edit to the open leaf — timing or
    // shader, set or clear — is copied verbatim onto the close leaf, so
    // the single "Window opened & closed" card genuinely sets both.
    // Mirror writes re-fire the change signals with the MIRROR path, which
    // is not a group key, so recursion terminates; the guard is belt and
    // braces against a future self-referencing group.
    readonly property var _mirrorGroups: ({
            "window.appearance.open": ["window.appearance.close"]
        })
    property bool _mirroring: false

    function _mirrorTiming(path) {
        const mirrors = simplePage._mirrorGroups[path];
        if (!mirrors || simplePage._mirroring)
            return;
        simplePage._mirroring = true;
        if (simplePage.animController.hasOverride(path)) {
            const raw = simplePage.animController.rawProfile(path);
            for (let i = 0; i < mirrors.length; ++i)
                simplePage.animController.setOverride(mirrors[i], raw);
        } else {
            for (let i = 0; i < mirrors.length; ++i)
                simplePage.animController.clearOverride(mirrors[i]);
        }
        simplePage._mirroring = false;
    }

    function _mirrorShader(path) {
        const mirrors = simplePage._mirrorGroups[path];
        if (!mirrors || simplePage._mirroring)
            return;
        simplePage._mirroring = true;
        const raw = simplePage.animController.rawShaderProfile(path);
        // `effectId` present (possibly the "" engaged-empty sentinel) means
        // a direct override exists; an empty map means none.
        if (raw && typeof raw.effectId === "string") {
            for (let i = 0; i < mirrors.length; ++i)
                simplePage.animController.setShaderOverride(mirrors[i], raw.effectId, raw.parameters || ({}));
        } else {
            for (let i = 0; i < mirrors.length; ++i)
                simplePage.animController.clearShaderOverride(mirrors[i]);
        }
        simplePage._mirroring = false;
    }

    Connections {
        function onOverrideChanged(path) {
            simplePage._mirrorTiming(path);
        }

        function onShaderProfileChanged(path) {
            simplePage._mirrorShader(path);
        }

        target: simplePage.animController
    }

    Accessible.name: i18n("Animation essentials")
    simpleTiming: true
    headerText: i18n("The essentials. Each card covers a whole group of events. Switch to Advanced in the header for per-event control, motion depth, and the shader library.")
    // The Global animation defaults card — the SAME AnimationProfileEditor ↔
    // Settings wiring as AnimationsGeneralPage (curve summary + Customize,
    // Easing/Spring, Duration), minus that page's sequencing / min-distance /
    // filter depth. The editor drives the Global profile every card below
    // inherits from; per-card Duration overrides it per event group.
    headerComponent: Component {
        ColumnLayout {
            spacing: Kirigami.Units.smallSpacing

            SettingsCard {
                id: defaultsCard

                Layout.fillWidth: true

                readonly property QtObject cardSettings: simplePage.globalSettings
                readonly property string _springPrefix: "spring:"
                readonly property bool _isSpring: defaultsCard._isSpringCurve(defaultsCard.cardSettings.animationEasingCurve)
                readonly property int _currentTimingMode: _isSpring ? CurvePresets.timingModeSpring : CurvePresets.timingModeEasing
                // Last-seen values per timing axis so toggling Easing ↔ Spring
                // round-trips without losing the other axis's tuning. Mirrors
                // AnimationsGeneralPage.
                property string _lastEasingCurve: CurvePresets.defaultEasingCurve
                property real _lastSpringOmega: CurvePresets.defaultSpringOmega
                property real _lastSpringZeta: CurvePresets.defaultSpringZeta
                property bool _committingEditor: false

                function _isSpringCurve(curveStr) {
                    return typeof curveStr === "string" && curveStr.indexOf(defaultsCard._springPrefix) === 0;
                }

                function _parseSpring(curveStr) {
                    var parts = curveStr.substring(defaultsCard._springPrefix.length).split(",");
                    var w = parseFloat(parts[0]);
                    var z = parseFloat(parts[1]);
                    return {
                        "omega": isFinite(w) ? w : CurvePresets.defaultSpringOmega,
                        "zeta": isFinite(z) ? z : CurvePresets.defaultSpringZeta
                    };
                }

                function _syncCachedValues() {
                    var c = defaultsCard.cardSettings.animationEasingCurve;
                    if (typeof c !== "string")
                        return;
                    if (defaultsCard._isSpringCurve(c)) {
                        var s = defaultsCard._parseSpring(c);
                        defaultsCard._lastSpringOmega = s.omega;
                        defaultsCard._lastSpringZeta = s.zeta;
                    } else if (c.length > 0) {
                        defaultsCard._lastEasingCurve = c;
                    }
                }

                function _seedEditor() {
                    editor.timingMode = defaultsCard._currentTimingMode;
                    editor.easingCurve = defaultsCard._lastEasingCurve;
                    editor.springOmega = defaultsCard._lastSpringOmega;
                    editor.springZeta = defaultsCard._lastSpringZeta;
                    editor.duration = defaultsCard.cardSettings.animationDuration;
                }

                function _commitEditor() {
                    defaultsCard._committingEditor = true;
                    if (editor.timingMode === CurvePresets.timingModeSpring) {
                        var encoded = defaultsCard._springPrefix + parseFloat(editor.springOmega.toFixed(2)) + "," + parseFloat(editor.springZeta.toFixed(2));
                        if (defaultsCard.cardSettings.animationEasingCurve !== encoded) {
                            defaultsCard._lastSpringOmega = parseFloat(editor.springOmega.toFixed(2));
                            defaultsCard._lastSpringZeta = parseFloat(editor.springZeta.toFixed(2));
                            defaultsCard.cardSettings.animationEasingCurve = encoded;
                        }
                    } else if (defaultsCard.cardSettings.animationEasingCurve !== editor.easingCurve) {
                        defaultsCard._lastEasingCurve = editor.easingCurve;
                        defaultsCard.cardSettings.animationEasingCurve = editor.easingCurve;
                    }
                    if (defaultsCard.cardSettings.animationDuration !== editor.duration)
                        defaultsCard.cardSettings.animationDuration = editor.duration;
                    defaultsCard._committingEditor = false;
                }

                Component.onCompleted: {
                    defaultsCard._syncCachedValues();
                    defaultsCard._seedEditor();
                }

                // External writes (a Discard, a profile switch, the advanced
                // General page in a past session) move the profile under us —
                // re-seed, but never while WE are the writer mid-edit.
                Connections {
                    function onAnimationEasingCurveChanged() {
                        defaultsCard._syncCachedValues();
                        if (!defaultsCard._committingEditor)
                            defaultsCard._seedEditor();
                    }

                    function onAnimationDurationChanged() {
                        if (!defaultsCard._committingEditor)
                            defaultsCard._seedEditor();
                    }

                    target: defaultsCard.cardSettings
                }

                headerText: i18n("Animations")
                searchAnchor: "simpleAnimations"
                showToggle: true
                toggleChecked: defaultsCard.cardSettings.animationsEnabled
                onToggleClicked: function (checked) {
                    defaultsCard.cardSettings.animationsEnabled = checked;
                }

                contentItem: ColumnLayout {
                    spacing: Kirigami.Units.smallSpacing

                    AnimationProfileEditor {
                        id: editor

                        Layout.fillWidth: true
                        enabled: defaultsCard.toggleChecked
                        // The Global default has no shader leg — effects are
                        // picked on the per-event cards below.
                        shaderLegSupported: false
                        eventLabel: i18n("Global animation defaults")
                        onValueChanged: defaultsCard._commitEditor()
                    }
                }
            }

            Kirigami.InlineMessage {
                Layout.fillWidth: true
                type: Kirigami.MessageType.Information
                visible: true
                text: i18n("Filtered windows are not animated. Use a Rule to keep a specific application animated even when a filter would exclude it.")
            }

            // The same animation window filter the advanced General page
            // hosts (animation-specific config group, distinct from the
            // snapping/tiling and decoration filters).
            WindowFilterCard {
                Layout.fillWidth: true

                excludeTransient: simplePage.globalSettings.animationExcludeTransientWindows
                transientDescription: i18n("Skip animations for dialogs, popups, tooltips, and dropdown menus")
                transientAccessibleName: i18n("Exclude transient windows from animations")
                onExcludeTransientToggled: value => {
                    simplePage.globalSettings.animationExcludeTransientWindows = value;
                }

                // Animations-only extra row: exclude notifications / OSDs.
                insertAfterTransient: Component {
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: Kirigami.Units.smallSpacing

                        SettingsSeparator {}

                        SettingsRow {
                            title: i18n("Exclude notifications and OSDs")
                            searchAnchor: "simpleExcludeNotificationsAndOsds"
                            description: i18n("Skip animations for notification popups and on-screen displays such as volume and brightness")

                            SettingsSwitch {
                                checked: simplePage.globalSettings.animationExcludeNotificationsAndOsd
                                accessibleName: i18n("Exclude notifications and on-screen displays from animations")
                                onToggled: function (newValue) {
                                    simplePage.globalSettings.animationExcludeNotificationsAndOsd = newValue;
                                }
                            }
                        }
                    }
                }

                minWidth: simplePage.globalSettings.animationMinimumWindowWidth
                minWidthFrom: settingsController.generalPage.animationMinimumWindowWidthMin
                minWidthTo: settingsController.generalPage.animationMinimumWindowWidthMax
                minWidthDescription: i18n("Windows narrower than this will not animate")
                minWidthDisabledDescription: i18n("Disabled. No width threshold.")
                minWidthAccessibleName: i18n("Minimum window width for animations")
                onMinWidthModified: value => {
                    simplePage.globalSettings.animationMinimumWindowWidth = value;
                }

                minHeight: simplePage.globalSettings.animationMinimumWindowHeight
                minHeightFrom: settingsController.generalPage.animationMinimumWindowHeightMin
                minHeightTo: settingsController.generalPage.animationMinimumWindowHeightMax
                minHeightDescription: i18n("Windows shorter than this will not animate")
                minHeightDisabledDescription: i18n("Disabled. No height threshold.")
                minHeightAccessibleName: i18n("Minimum window height for animations")
                onMinHeightModified: value => {
                    simplePage.globalSettings.animationMinimumWindowHeight = value;
                }
            }
        }
    }
    eventModel: [
        {
            "eventPath": "window.appearance.open",
            "eventLabel": i18n("Window opened & closed"),
            "isParentNode": false
        },
        {
            "eventPath": "window.appearance.minimize",
            "eventLabel": i18n("Window minimized"),
            "isParentNode": false
        },
        {
            "eventPath": "window.movement",
            "eventLabel": i18n("Window movement"),
            "isParentNode": true
        },
        {
            "eventPath": "window.movement.move",
            "eventLabel": i18n("Window moved"),
            "isParentNode": false
        },
        {
            "eventPath": "desktop.switch",
            "eventLabel": i18n("Desktop switched"),
            "isParentNode": false
        }
    ]
}
