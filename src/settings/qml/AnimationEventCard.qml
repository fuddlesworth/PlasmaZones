// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Window
import org.kde.kirigami as Kirigami

/**
 * @brief Reusable card for per-event animation configuration.
 *
 * Each card edits one event in the `PhosphorAnimation::ProfilePaths`
 * taxonomy (e.g. `zone.snapIn`, `osd.show`). The override toggle
 * creates/clears one Profile JSON file under
 * `~/.local/share/plasmazones/profiles/`; the daemon's existing
 * `ProfileLoader` watches that dir and live-reloads the registry.
 *
 * Phase 3 scope: timing-mode (Easing/Spring), curve thumbnail with
 * "Customize…" dialog, duration slider, inheritance breadcrumb. The
 * Animation-style combo + shader-param editor + scale-start slider
 * land in Phase 6 alongside the shader-picker controller.
 *
 * Required properties:
 *   - eventPath:  full path string from `ProfilePaths::` (e.g. "zone.snapIn")
 *   - eventLabel: human-readable label
 *
 * Optional properties:
 *   - isParentNode: bool — flips the inheritance banner copy
 *   - alwaysEnabled: bool — hides the override toggle (the global root)
 *   - collapsible: bool — header click collapses the body
 */
Item {
    id: root

    required property string eventPath
    required property string eventLabel
    property bool isParentNode: false
    property bool alwaysEnabled: false
    property bool collapsible: false
    // ── Internal state — one source of truth per UX axis ────────────
    // The on-disk schema is `Profile::toJson()` — a single `curve`
    // string that's either an easing wire format ("x1,y1,x2,y2",
    // "elastic-out:amp,per", etc.) or a spring ("spring:omega,zeta").
    // The card unpacks it on read into the working state below, and
    // re-packs on write. Easing and spring values are remembered
    // independently across timing-mode toggles so the user doesn't
    // lose their easing curve when previewing spring physics.
    property bool overrideEnabled: false
    property int currentTimingMode: CurvePresets.timingModeEasing
    property int currentDuration: 150
    property string currentEasingCurve: "0.33,1.00,0.68,1.00"
    property real currentSpringOmega: 12
    property real currentSpringZeta: 1
    readonly property string currentCurveString: {
        if (currentTimingMode === CurvePresets.timingModeSpring)
            return "spring:" + currentSpringOmega.toFixed(2) + "," + currentSpringZeta.toFixed(2);

        return currentEasingCurve;
    }

    // ── Inheritance summary (italic "Current: …" line when override off) ─
    function inheritSummaryText() {
        var r = settingsController.animationsPage.resolvedProfile(root.eventPath);
        var curve = r.curve || "0.33,1.00,0.68,1.00";
        var dur = r.duration !== undefined ? r.duration : 150;
        if (typeof curve === "string" && curve.indexOf("spring:") === 0) {
            var parts = curve.substring(7).split(",");
            var w = parseFloat(parts[0]);
            var z = parseFloat(parts[1]);
            if (isFinite(w) && isFinite(z))
                return i18n("Spring · ω=%1 · ζ=%2", w.toFixed(1), z.toFixed(2));

            return i18n("Spring · Custom");
        }
        return i18n("%1 · %2 ms", CurvePresets.curveDisplayName(curve), Math.round(dur));
    }

    function parentChainText() {
        var chain = settingsController.animationsPage.parentChain(root.eventPath);
        // Drop chain[0] (self) — show only ancestors as "zone ← global"
        if (chain.length <= 1)
            return "";

        return chain.slice(1).join(" ← ");
    }

    function summaryDescription() {
        if (root.currentTimingMode === CurvePresets.timingModeSpring) {
            var si = CurvePresets.springPresetIndex(root.currentSpringOmega, root.currentSpringZeta);
            if (si >= 0)
                return i18n("Spring · %1", CurvePresets.springPresets[si].label);

            return i18n("Spring · Custom");
        }
        var idx = CurvePresets.findIndices(root.currentEasingCurve);
        if (idx.styleIndex >= 0)
            return CurvePresets.easingStyles[idx.styleIndex].label + " · " + CurvePresets.easingDirections[idx.dirIndex].label;

        return i18n("Easing · Custom");
    }

    function summarySecondary() {
        if (root.currentTimingMode === CurvePresets.timingModeSpring)
            return i18n("ω=%1 · ζ=%2", root.currentSpringOmega.toFixed(1), root.currentSpringZeta.toFixed(2));

        return i18n("%1 ms", root.currentDuration);
    }

    function refreshFromTree() {
        var raw = settingsController.animationsPage.rawProfile(root.eventPath);
        var resolved = settingsController.animationsPage.resolvedProfile(root.eventPath);
        var hasRaw = raw && Object.keys(raw).length > 0;
        root.overrideEnabled = hasRaw;
        // Effective values feed the controls. When override is off the
        // controls preview "what would happen if you turned it on" =
        // the resolved profile from the parent chain. When on, the
        // raw fields decide.
        var effective = hasRaw ? raw : resolved;
        var curve = effective.curve;
        if (typeof curve === "string" && curve.indexOf("spring:") === 0) {
            var parts = curve.substring(7).split(",");
            var w = parseFloat(parts[0]);
            var z = parseFloat(parts[1]);
            if (isFinite(w) && isFinite(z)) {
                root.currentTimingMode = CurvePresets.timingModeSpring;
                root.currentSpringOmega = w;
                root.currentSpringZeta = z;
            }
        } else {
            root.currentTimingMode = CurvePresets.timingModeEasing;
            if (typeof curve === "string" && curve.length > 0)
                root.currentEasingCurve = curve;

        }
        root.currentDuration = effective.duration !== undefined ? effective.duration : 150;
    }

    // Build the on-disk profile object from the working state and
    // commit it through the controller. The controller stamps the
    // `name` field automatically.
    function commitOverride() {
        var profile = {
            "curve": root.currentCurveString,
            "duration": root.currentDuration
        };
        settingsController.animationsPage.setOverride(root.eventPath, profile);
    }

    implicitHeight: card.implicitHeight
    Layout.fillWidth: true
    Component.onCompleted: refreshFromTree()

    // Pick up changes from any path in the tree — could be this event
    // (user toggled override) or an ancestor (we're inheriting from it
    // and the inherited value just changed). The signal is per-path
    // but it's cheap to just refresh.
    Connections {
        function onOverrideChanged(path) {
            root.refreshFromTree();
        }

        target: settingsController.animationsPage
    }

    SettingsCard {
        id: card

        anchors.fill: parent
        headerText: root.eventLabel
        showToggle: !root.alwaysEnabled
        toggleChecked: root.alwaysEnabled || root.overrideEnabled
        collapsible: root.collapsible
        onToggleClicked: function(checked) {
            if (checked)
                root.commitOverride();
            else
                settingsController.animationsPage.clearOverride(root.eventPath);
        }

        contentItem: ColumnLayout {
            spacing: Kirigami.Units.smallSpacing

            // ── Inheritance info ──────────────────────────────────────
            Kirigami.InlineMessage {
                Layout.fillWidth: true
                type: Kirigami.MessageType.Information
                visible: !root.alwaysEnabled && (root.isParentNode ? root.overrideEnabled : !root.overrideEnabled)
                text: {
                    if (root.isParentNode)
                        return i18n("Settings here apply to all child events unless individually overridden.");

                    var chain = root.parentChainText();
                    if (chain.length > 0)
                        return i18n("Inheriting from: %1", chain);

                    return i18n("Using library defaults");
                }
            }

            Label {
                visible: !root.alwaysEnabled && !root.overrideEnabled
                text: i18n("Current: %1", root.inheritSummaryText())
                font.italic: true
                color: Kirigami.Theme.disabledTextColor
            }

            // ── Override controls ─────────────────────────────────────
            ColumnLayout {
                visible: root.alwaysEnabled || root.overrideEnabled
                spacing: Kirigami.Units.smallSpacing

                // Curve summary row: thumbnail + description + "Customize…"
                RowLayout {
                    Layout.fillWidth: true
                    spacing: Kirigami.Units.largeSpacing

                    CurveThumbnail {
                        id: curveThumbnail

                        implicitWidth: Kirigami.Units.gridUnit * 6
                        implicitHeight: Kirigami.Units.gridUnit * 4
                        curve: root.currentEasingCurve
                        timingMode: root.currentTimingMode
                        omega: root.currentSpringOmega
                        zeta: root.currentSpringZeta
                        onClicked: curveDialog.open()
                    }

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: Math.round(Kirigami.Units.smallSpacing / 2)

                        Label {
                            Layout.fillWidth: true
                            text: root.summaryDescription()
                            elide: Text.ElideRight
                        }

                        Label {
                            Layout.fillWidth: true
                            text: root.summarySecondary()
                            font.pointSize: Kirigami.Theme.smallFont.pointSize
                            color: Kirigami.Theme.disabledTextColor
                            elide: Text.ElideRight
                        }

                    }

                    Button {
                        text: i18n("Customize…")
                        icon.name: "configure"
                        Accessible.name: i18n("Customize curve for %1", root.eventLabel)
                        onClicked: curveDialog.open()
                    }

                }

                SettingsSeparator {
                }

                // ── Timing mode ───────────────────────────────────────
                SettingsRow {
                    title: i18n("Timing mode")

                    WideComboBox {
                        id: timingModeCombo

                        Accessible.name: i18n("Timing mode")
                        model: [i18n("Easing"), i18n("Spring")]
                        currentIndex: root.currentTimingMode
                        onActivated: function(index) {
                            root.currentTimingMode = index;
                            root.commitOverride();
                        }
                    }

                }

                // ── Duration (easing only — spring derives its own settle) ─
                SettingsSeparator {
                    visible: root.currentTimingMode === CurvePresets.timingModeEasing
                }

                SettingsRow {
                    visible: root.currentTimingMode === CurvePresets.timingModeEasing
                    title: i18n("Duration")

                    SettingsSlider {
                        from: settingsController.generalPage.animationDurationMin
                        to: settingsController.generalPage.animationDurationMax
                        stepSize: 10
                        valueSuffix: " ms"
                        Accessible.name: i18n("Animation duration")
                        labelWidth: Kirigami.Units.gridUnit * 4
                        value: root.currentDuration
                        onMoved: function(value) {
                            root.currentDuration = Math.round(value);
                            root.commitOverride();
                        }
                    }

                }

            }

        }

    }

    // Pop the editor as a window-level dialog so it doesn't get clipped
    // by the scrolling Flickable that hosts the card.
    CurveEditorDialog {
        id: curveDialog

        parent: root.Window.window ? root.Window.window.contentItem : root
        eventLabel: root.eventLabel
        timingMode: root.currentTimingMode
        easingCurve: root.currentEasingCurve
        springOmega: root.currentSpringOmega
        springZeta: root.currentSpringZeta
        onCurveApplied: function(curve) {
            root.currentEasingCurve = curve;
            root.currentTimingMode = CurvePresets.timingModeEasing;
            root.commitOverride();
        }
        onSpringApplied: function(omega, zeta) {
            root.currentSpringOmega = omega;
            root.currentSpringZeta = zeta;
            root.currentTimingMode = CurvePresets.timingModeSpring;
            root.commitOverride();
        }
    }

}
