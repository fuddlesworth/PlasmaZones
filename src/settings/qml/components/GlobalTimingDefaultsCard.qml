// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

/**
 * @brief The Global animation defaults card: the enable toggle plus the
 * shared AnimationProfileEditor wired to the Settings-driven Global profile
 * (animationEasingCurve / animationDuration).
 *
 * Hosted by BOTH the advanced Animations → General page and the simple-mode
 * Animations page, so the spring encode/parse round-trip and the
 * seed/commit guards exist once. Consumers append their own rows below the
 * editor as default children (the General page's sequencing / stagger /
 * minimum-distance rows); the simple page passes none. The timing editor is
 * the FULL one in both hosts (curve summary, Customize dialog,
 * Easing/Spring, Duration) even though simple mode trims the per-event cards
 * below it: the curve is a global choice every card inherits, so simple mode
 * still needs one place to make it.
 *
 * Timing state is cached per axis (`_lastEasingCurve` vs
 * `_lastSpringOmega`/`_lastSpringZeta`) so toggling Easing ↔ Spring
 * round-trips without losing the other axis's tuning, and the cached spring
 * values are the ROUNDED ones — the encoded string is the canonical on-disk
 * form (2-decimal precision), so caching the raw inputs would leave the
 * cache a sub-precision tick off what the next reload parses back.
 */
SettingsCard {
    id: card

    /// The ISettings object holding the global animation profile.
    required property QtObject cardSettings
    /// Extra rows appended below the timing editor. Aliased to `children`
    /// rather than `data` so it agrees with the empty-slot collapse below,
    /// which measures visual children: `data` also accepts non-Item objects
    /// (Timer, Connections) that never reach `children`, which would leave a
    /// populated slot measuring as empty. Both current hosts append only
    /// visual rows, so this is behaviour-preserving, and a consumer that
    /// tries to append a non-visual object now fails loudly at load instead
    /// of silently landing in a slot that stays collapsed.
    default property alias extraContent: extraColumn.children

    readonly property string _springPrefix: "spring:"
    readonly property bool _isSpring: card._isSpringCurve(card.cardSettings.animationEasingCurve)
    readonly property int _currentTimingMode: _isSpring ? CurvePresets.timingModeSpring : CurvePresets.timingModeEasing
    property string _lastEasingCurve: CurvePresets.defaultEasingCurve
    property real _lastSpringOmega: CurvePresets.defaultSpringOmega
    property real _lastSpringZeta: CurvePresets.defaultSpringZeta
    // Set while WE are writing so the change-signal handlers below don't
    // re-seed the editor mid-edit (e.g. during a duration drag).
    property bool _committingEditor: false

    function _isSpringCurve(curveStr) {
        return typeof curveStr === "string" && curveStr.indexOf(card._springPrefix) === 0;
    }

    // Refresh the cached easing/spring values from whichever axis the
    // current curve string describes.
    function _syncCachedValues() {
        const c = card.cardSettings.animationEasingCurve;
        if (typeof c !== "string")
            return;
        if (card._isSpringCurve(c)) {
            // CurvePresets.parseSpring is all-or-nothing (matches the engine's
            // Spring::fromString): a hand-edited "spring:14,abc" seeds the
            // cache with the engine defaults (12, 0.8), the same values a
            // per-event card resolves for the inherited curve, so the two
            // never describe the same spring differently.
            const s = CurvePresets.parseSpring(c);
            card._lastSpringOmega = s.omega;
            card._lastSpringZeta = s.zeta;
        } else if (c.length > 0) {
            card._lastEasingCurve = c;
        }
    }

    // Push the saved profile into the editor. Property assignment doesn't
    // emit the editor's valueChanged, so this never re-enters _commitEditor.
    function _seedEditor() {
        editor.timingMode = card._currentTimingMode;
        editor.easingCurve = card._lastEasingCurve;
        editor.springOmega = card._lastSpringOmega;
        editor.springZeta = card._lastSpringZeta;
        editor.duration = card.cardSettings.animationDuration;
    }

    function _writeSpring(omega, zeta) {
        const omegaRounded = parseFloat(omega.toFixed(2));
        const zetaRounded = parseFloat(zeta.toFixed(2));
        const encoded = card._springPrefix + omegaRounded + "," + zetaRounded;
        // No-op guard before mutating the cache so re-selecting the already
        // active mode/values doesn't churn bindings.
        if (card.cardSettings.animationEasingCurve === encoded)
            return;
        card._lastSpringOmega = omegaRounded;
        card._lastSpringZeta = zetaRounded;
        card.cardSettings.animationEasingCurve = encoded;
    }

    function _writeEasing(curveStr) {
        if (card.cardSettings.animationEasingCurve === curveStr)
            return;
        card._lastEasingCurve = curveStr;
        card.cardSettings.animationEasingCurve = curveStr;
    }

    // Commit the editor's working state back to the Global profile.
    function _commitEditor() {
        // try/finally, not a straight-line set/clear pair: QML has no RAII, and
        // a throw between them (cardSettings resolving undefined during a page
        // teardown, a property write rejecting) would latch the flag TRUE. That
        // is permanent, not transient — both change handlers would stop
        // re-seeding the editor for the rest of the session. Same hazard and
        // same shape as AnimationEventCard._committing.
        card._committingEditor = true;
        try {
            if (editor.timingMode === CurvePresets.timingModeSpring)
                card._writeSpring(editor.springOmega, editor.springZeta);
            else
                card._writeEasing(editor.easingCurve);
            if (card.cardSettings.animationDuration !== editor.duration)
                card.cardSettings.animationDuration = editor.duration;
        } finally {
            card._committingEditor = false;
        }
    }

    Component.onCompleted: {
        card._syncCachedValues();
        card._seedEditor();
    }

    // External writes (a Discard, a profile switch, the other page in a
    // past session) move the profile under us — re-seed, but never while
    // WE are the writer mid-edit.
    Connections {
        function onAnimationEasingCurveChanged() {
            card._syncCachedValues();
            if (!card._committingEditor)
                card._seedEditor();
        }

        function onAnimationDurationChanged() {
            if (!card._committingEditor)
                card._seedEditor();
        }

        target: card.cardSettings
    }

    headerText: i18n("Global animation defaults")
    searchAnchor: "globalAnimationDefaults"
    showToggle: true
    toggleChecked: card.cardSettings.animationsEnabled
    onToggleClicked: function (checked) {
        card.cardSettings.animationsEnabled = checked;
    }

    contentItem: ColumnLayout {
        spacing: Kirigami.Units.largeSpacing

        // Compact curve summary (thumbnail + "Customize…" → dialog) plus the
        // timing-mode and duration rows — the same AnimationProfileEditor the
        // per-event override cards use.
        AnimationProfileEditor {
            id: editor

            Layout.fillWidth: true
            enabled: card.toggleChecked
            // The Global default has no shader leg in this UI — shader
            // overrides live on the per-event and Rules layers.
            shaderLegSupported: false
            eventLabel: i18n("Global animation defaults")
            onValueChanged: card._commitEditor()
        }

        ColumnLayout {
            id: extraColumn

            Layout.fillWidth: true
            spacing: Kirigami.Units.largeSpacing
            // An empty slot still costs the parent's largeSpacing gap, so a
            // consumer that appends nothing (the simple page) would get
            // stray bottom padding the General page's card does not have.
            visible: extraColumn.children.length > 0
        }
    }
}
