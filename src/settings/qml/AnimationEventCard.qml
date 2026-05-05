// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import QtQuick.Window
import org.kde.kirigami as Kirigami
import org.plasmazones.common as PZCommon

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
    property int currentDuration: CurvePresets.defaultDurationMs
    property string currentEasingCurve: CurvePresets.defaultEasingCurve
    property real currentSpringOmega: CurvePresets.defaultSpringOmega
    property real currentSpringZeta: CurvePresets.defaultSpringZeta
    // ── Shader assignment (independent of timing override) ──────────
    // Per-event shader override is independent of the motion override
    // toggle: a user can pick a shader for an event without overriding
    // its timing. Persistence routes through Settings::shaderProfile-
    // Tree (NOT the Profile-loader pipeline), so signal handling for
    // refreshes is separate.
    property string currentShaderEffectId: ""
    // Initialized from Q_INVOKABLE resolvedShaderProfile() — refresh on
    // shaderProfileChanged via the Connections block at the bottom.
    property var currentShaderParams: ({
    })
    readonly property string currentCurveString: {
        if (currentTimingMode === CurvePresets.timingModeSpring)
            return "spring:" + currentSpringOmega.toFixed(2) + "," + currentSpringZeta.toFixed(2);

        return currentEasingCurve;
    }
    // Cached resolved-profile lookup. The C++ Q_INVOKABLE walks the
    // parent chain on every call; before this cache, the inheritance-
    // banner Label re-evaluated `inheritSummaryText()` on every
    // `currentTimingMode` / `currentDuration` / `currentEasingCurve`
    // change (each binding dependency), so a single keystroke on the
    // duration slider drove N round-trips into C++ where N is the
    // number of cards on the page. The revision tick (_inheritRev)
    // invalidates only when the profile chain actually changes — see
    // the Connections block below that bumps it on overrideChanged.
    property int _inheritRev: 0
    readonly property var _inheritResolved: {
        _inheritRev;
        return settingsController.animationsPage.resolvedProfile(root.eventPath);
    }
    // True only for event paths the daemon's overlay service actually
    // consumes as a shader-leg surface. Gates the shader picker, the
    // inline param editor, and the inheritance "Shader: X" banner so
    // a user can't pick a shader on an unsupported path (e.g. the
    // "All Panel Events" parent or `panel.slideIn`) and silently
    // persist a dead override that the daemon resolver would shadow
    // any user-intended setting with via deeper-leaf-wins overlay.
    // Source-of-truth list: `src/core/animationshadersupportedpaths.h`.
    readonly property bool _shaderLegSupported: settingsController.animationsPage.supportsShaderLeg(root.eventPath)
    // Number of shader overrides on paths strictly DEEPER than this card's
    // eventPath. Only meaningful for parent-node cards: a stale leaf
    // override (e.g. `panel.popup.layoutPicker.show = "dissolve"` set in
    // a previous session) silently wins the deeper-leaf-overlay merge in
    // `ShaderProfileTree::resolve` and shadows the parent's value at
    // runtime. Surfaced via the warning banner below with a one-click
    // "Clear shadowing children" button. Refreshed on any
    // shaderProfileChanged signal (see Connections at line 222+).
    property int _shadowingChildrenCount: 0
    // Bumped on every `shaderEffectsChanged` so any binding that reads
    // a shader-registry Q_INVOKABLE (`availableShaderEffects()`,
    // `shaderParameters()`, etc.) can become reactive to registry
    // mutations by mentioning this revision tick. The Q_INVOKABLE return
    // values are not observed by QML's binding engine — without a
    // tracked dependency on this tick, the bindings would evaluate once
    // and stick at the initial (often empty, mid-warmup) result.
    property int _shaderRegistryRev: 0

    // ── Inheritance summary (italic "Current: …" line when override off) ─
    function inheritSummaryText() {
        var r = root._inheritResolved;
        var curve = r.curve || CurvePresets.defaultEasingCurve;
        var dur = r.duration !== undefined ? r.duration : CurvePresets.defaultDurationMs;
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

    // `effectId` is explicit so callers can snapshot it at user-action
    // time (e.g. when the color dialog opens) rather than reading
    // `root.currentShaderEffectId` at write time. Without that snapshot,
    // a registry refresh that fires while the dialog is open could
    // silently retarget the write at a different effect's param map.
    function _writeShaderParam(effectId, paramId, value) {
        if (!effectId)
            return ;

        // Bail if the user navigated to a different effect while the
        // dialog (color picker / etc.) was open. Calling
        // `setShaderOverride` with the stale effect id would silently
        // reassign the eventPath to the OLD effect, undoing the user's
        // navigation and reviving a dropped param map. Better to drop the
        // late accept than to clobber state the user explicitly changed.
        if (effectId !== root.currentShaderEffectId)
            return ;

        var next = Object.assign({
        }, root.currentShaderParams || {
        });
        next[paramId] = value;
        root.currentShaderParams = next;
        settingsController.animationsPage.setShaderOverride(root.eventPath, effectId, next);
    }

    function refreshShaderFromTree() {
        var resolved = settingsController.animationsPage.resolvedShaderProfile(root.eventPath);
        root.currentShaderEffectId = (resolved && resolved.effectId) ? resolved.effectId : "";
        root.currentShaderParams = (resolved && resolved.parameters) ? resolved.parameters : ({
        });
        // Recompute deeper-override count on every shader-tree update —
        // the warning banner below depends on it. Cheap (O(N) over
        // overriddenPaths). Only meaningful for parent-node cards but
        // we always refresh so the binding stays consistent.
        root._shadowingChildrenCount = settingsController.animationsPage.shaderOverrideDescendantCount(root.eventPath);
    }

    function refreshFromTree() {
        var raw = settingsController.animationsPage.rawProfile(root.eventPath);
        var resolved = settingsController.animationsPage.resolvedProfile(root.eventPath);
        var hasRaw = raw && Object.keys(raw).length > 0;
        // The card's "Override" toggle reflects ANY direct override at
        // this path — timing curve OR shader assignment. Without the
        // shader half, a user could see an event toggle "off" while a
        // matrix shader was actively firing on every fire of that event
        // (timing override clear, shader override still set), which
        // exactly matches the user-reported "I turned this off but
        // shaders still animate" bug. Reading rawShaderProfile here
        // makes the toggle's checked state honest about both axes.
        // rawShaderProfile returns {} when there's no direct override at
        // this path; any non-empty map (effectId set, parameters set, etc.)
        // indicates a direct override. Mirrors the rawProfile check above.
        var rawShader = settingsController.animationsPage.rawShaderProfile(root.eventPath);
        var hasShader = rawShader && Object.keys(rawShader).length > 0;
        root.overrideEnabled = hasRaw || hasShader;
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
        root.currentDuration = effective.duration !== undefined ? effective.duration : CurvePresets.defaultDurationMs;
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

    // Pick up changes that affect THIS card's path — the path itself
    // (user toggled the override here) or an ancestor (we're inheriting
    // from it and the inherited value just changed). Filter on path so
    // a settings burst on an unrelated row doesn't drive every other
    // card on the page through a refresh + Q_INVOKABLE round-trip.
    // Without the filter, drag-editing a slider on one row drives
    // N redundant `_inheritResolved` recompute kicks (where N = number
    // of cards on the page) — defeats the cache the `_inheritRev`
    // pattern was added to provide.
    function _pathAffectsThisCard(path) {
        return path === root.eventPath || root.eventPath.startsWith(path + ".");
    }

    implicitHeight: card.implicitHeight
    Layout.fillWidth: true
    Component.onCompleted: {
        refreshFromTree();
        refreshShaderFromTree();
    }

    Connections {
        function onOverrideChanged(path) {
            if (!root._pathAffectsThisCard(path))
                return ;

            root.refreshFromTree();
            // The signal is per-path but the resolved profile depends on
            // the entire ancestor chain, so any change at-or-above this
            // path can shift the inheritance banner. Bump the revision
            // tick to invalidate _inheritResolved.
            root._inheritRev++;
        }

        function onShaderProfileChanged(path) {
            // Empty-string path is the controller's "tree fully reloaded"
            // broadcast — set/clear/clearDescendants all route through
            // `Settings::setShaderProfileTree` which the controller
            // relays as a single path-agnostic emit (see
            // `animationspagecontroller.cpp` — `Q_EMIT
            // shaderProfileChanged(QString())`). Filtering it out the
            // way per-path emits get filtered would mean NO card ever
            // refreshes its shader state for ANY shader-tree mutation
            // — the exact bugs this Connections block was added to
            // catch. Treat "" as "refresh me unconditionally".
            if (path !== "" && !root._pathAffectsThisCard(path))
                return ;

            root.refreshShaderFromTree();
            // The card's "Override" toggle now reflects whether either
            // a timing OR a shader override exists at this path, so a
            // shader-only change has to re-flip refreshFromTree's
            // overrideEnabled binding too. Without this, clearing the
            // shader on a path with no timing override would leave the
            // toggle visually "on" until something else triggered a
            // refreshFromTree call.
            root.refreshFromTree();
        }

        function onShaderEffectsChanged() {
            // One tick invalidates every Q_INVOKABLE-derived shader binding
            // on this card (picker's `_effects`, param editor's `_paramSchema`).
            root._shaderRegistryRev++;
        }

        target: settingsController.animationsPage
    }

    SettingsCard {
        // ── Shader effect picker (independent of timing override) ─
        // Independent of timing override — users can drop a shader on
        // an event without touching its timing. The visibility gate
        // `root._shaderLegSupported` is declared on the card root so
        // it's reachable from every nested binding below; declaring
        // it here would scope it to this ColumnLayout and the outer
        // `root.<id>` references would silently resolve to undefined
        // (defaulting `visible:` to true and showing the picker on
        // every event regardless of daemon support).

        id: card

        anchors.fill: parent
        headerText: root.eventLabel
        showToggle: !root.alwaysEnabled
        toggleChecked: root.alwaysEnabled || root.overrideEnabled
        collapsible: root.collapsible
        onToggleClicked: function(checked) {
            if (checked) {
                root.commitOverride();
            } else {
                // Clear BOTH timing and shader at this path. A user who
                // toggles "All Notifications" or "Show" off expects
                // every override at that path to clear, not just the
                // timing curve — without the paired shader clear, a
                // previously-assigned matrix shader keeps firing even
                // though the toggle reads "off". clearShaderOverride is
                // a no-op when no shader is set, so paths that only
                // had timing overrides see no behavior change.
                settingsController.animationsPage.clearOverride(root.eventPath);
                settingsController.animationsPage.clearShaderOverride(root.eventPath);
            }
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

            // ── Shadowing-children warning (parent-node cards only) ───
            // ShaderProfileTree::resolve walks parent → leaf and overlays
            // each level's `effectId` if engaged; deeper leaves win. So
            // a stale per-leg override from an earlier session (e.g. an
            // old "Layout Picker — Show = dissolve" left over after the
            // user switches to a parent "All Popups = morph") silently
            // overrides the parent at runtime — even though the parent
            // card visually shows "morph" and the user never sees the
            // shadowing leaf. Surface it explicitly with one-click
            // remediation; without the button, the only fix is to find
            // each shadowing leaf manually and clear its override.
            Kirigami.InlineMessage {
                Layout.fillWidth: true
                type: Kirigami.MessageType.Warning
                visible: root.isParentNode && root._shadowingChildrenCount > 0
                text: i18np("%n descendant event has a shader override that shadows this parent.", "%n descendant events have shader overrides that shadow this parent.", root._shadowingChildrenCount)
                actions: [
                    Kirigami.Action {
                        text: i18n("Clear shadowing children")
                        icon.name: "edit-clear-all"
                        onTriggered: {
                            settingsController.animationsPage.clearShaderOverrideDescendants(root.eventPath);
                        }
                    }
                ]
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

            SettingsSeparator {
                visible: root._shaderLegSupported
            }

            SettingsRow {
                visible: root._shaderLegSupported
                title: i18n("Shader effect")
                description: i18n("Apply a shader transition to this event")

                PZCommon.ShaderPickerButton {
                    // `availableShaderEffects()` is a Q_INVOKABLE — QML's
                    // binding engine can't observe internal state of an
                    // opaque function call. The card's root-level
                    // `_shaderRegistryRev` (ticked on `shaderEffectsChanged`)
                    // is the dependency that makes this binding reactive
                    // (mirrors the stickyHandlingCombo pattern in
                    // SnappingBehaviorPage).
                    id: shaderPicker

                    readonly property var _effects: {
                        root._shaderRegistryRev; // re-evaluate when the registry signals
                        return settingsController.animationsPage.availableShaderEffects();
                    }

                    // The picker provides its own `Accessible.name` derived
                    // from the current selection's display text — leaving it
                    // overrides that with a generic label and loses the
                    // "currently selected: X" context for screen readers.
                    Layout.fillWidth: true
                    shaders: _effects
                    currentShaderId: root.currentShaderEffectId
                    // Explicit empty `noneShaderId` — the picker emits
                    // `shaderSelected("")` for the synthetic None entry, so
                    // `id.length === 0` below is the canonical clear-check.
                    // Pinning the prop to "" defends against any future
                    // default change in the shared component.
                    noneShaderId: ""
                    includeNoneEntry: true
                    placeholderText: i18nc("@action:button", "Select shader…")
                    onShaderSelected: function(id) {
                        if (id.length === 0) {
                            settingsController.animationsPage.clearShaderOverride(root.eventPath);
                            return ;
                        }
                        // Re-pick of the same shader is a no-op: skip the
                        // D-Bus round-trip to avoid bumping dirty-tracking
                        // for a non-change.
                        if (id === root.currentShaderEffectId)
                            return ;

                        // Switching to a DIFFERENT effect: drop the previous
                        // effect's parameter map — the new effect's schema is
                        // unrelated, so carrying old keys persists dead values
                        // on disk that the daemon can't validate.
                        settingsController.animationsPage.setShaderOverride(root.eventPath, id, ({
                        }));
                    }
                }

            }

            // Inline parameter editor surfaces only when an effect is
            // assigned and that effect declares parameters. Shared with
            // the editor's ShaderSettingsDialog — same row delegates,
            // same accordion behaviour, but with locking/randomize/image
            // disabled since animation packs don't use those affordances.
            PZCommon.ShaderParameterEditor {
                id: animationParamEditor

                // `shaderParameters()` is a Q_INVOKABLE — invalidate the
                // schema on registry mutations via the card's shared
                // `_shaderRegistryRev` tick. `currentShaderEffectId` is
                // already a tracked dependency through the binding body,
                // so a shader switch reloads the schema automatically.
                readonly property var _paramSchema: {
                    root._shaderRegistryRev; // dependency for reactivity
                    return root.currentShaderEffectId.length > 0 ? settingsController.animationsPage.shaderParameters(root.currentShaderEffectId) : [];
                }

                Layout.fillWidth: true
                visible: root._shaderLegSupported && root.currentShaderEffectId.length > 0 && _paramSchema.length > 0
                parameters: _paramSchema
                currentValues: root.currentShaderParams
                enableLocking: false
                enableRandomize: false
                enableGroups: true
                enableImage: false
                compact: true
                onValueChanged: function(paramId, value) {
                    root._writeShaderParam(root.currentShaderEffectId, paramId, value);
                }
                onRequestColorPicker: function(paramId, paramName, current) {
                    // Snapshot the effect id at dialog-open time so the
                    // accept handler writes back to the SAME effect even
                    // if the registry refreshes mid-pick.
                    animationColorDialog.effectId = root.currentShaderEffectId;
                    animationColorDialog.paramId = paramId;
                    animationColorDialog.paramName = paramName;
                    animationColorDialog.selectedColor = current;
                    animationColorDialog.open();
                }
            }

        }

    }

    // Pop the editor as a window-level dialog so it doesn't get clipped
    // by the scrolling Flickable that hosts the card. Fall back to
    // `null` (not `root`) when the Window context is unavailable: the
    // fallback path only fires during teardown / early initialisation
    // when nothing is opening the dialog anyway, and `null` keeps the
    // dialog detached from the card so a future code path that opens
    // it pre-realisation can't re-introduce the Flickable clip bug
    // this assignment exists to avoid.
    CurveEditorDialog {
        id: curveDialog

        parent: root.Window.window ? root.Window.window.contentItem : null
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

    // QtQuick.Dialogs.ColorDialog is a wrapper around the OS-native color
    // picker — it runs in its own platform window, not as a QML overlay,
    // so the in-QML use-after-free class that bites visual Kirigami
    // dialogs (CurveEditorDialog, the editor's image FileDialog) does
    // not apply here. No `parent:` assignment is needed (or accepted —
    // ColorDialog has no `parent` property).
    ColorDialog {
        id: animationColorDialog

        property string effectId: ""
        property string paramId: ""
        property string paramName: ""

        title: paramName.length > 0 ? i18nc("@title:window", "Choose %1", paramName) : i18nc("@title:window", "Pick color")
        onAccepted: {
            if (animationColorDialog.paramId === "" || animationColorDialog.effectId === "")
                return ;

            root._writeShaderParam(animationColorDialog.effectId, animationColorDialog.paramId, selectedColor.toString());
        }
    }

}
