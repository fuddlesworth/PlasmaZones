// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

/**
 * @brief The theme-fallback colour control: a swatch, the hex, and a reset.
 *
 * The inner control ThemeFallbackColorRow wraps in SettingsRow chrome,
 * extracted so hosts with different chrome (the rules action editor's
 * delegates) can present the same follow-the-system affordance: a stored
 * SENTINEL value means "follow", the swatch previews the colour actually
 * drawn while following, and Reset clears back to the sentinel.
 *
 * The picker is passed in rather than owned here — page-level, shared, with
 * transient self-disconnecting handlers — for the reasons documented on
 * ThemeFallbackColorRow.
 */
RowLayout {
    id: root

    /// The stored value: a hex string, or `sentinel` for "follow the theme".
    property string storedColor: ""
    /// The stored value that means "follow the system colour".
    property string sentinel: ""
    /// What the label shows while the sentinel is stored, naming the system
    /// colour being followed.
    property string fallbackLabel: i18n("Color scheme")
    /// What the Reset button announces and its tooltip says.
    property string resetAccessibleName: i18n("Reset to the color scheme")
    property string resetToolTip: i18n("Follow the color scheme")
    /// Whether the Reset button exists at all. A host whose value has no
    /// meaningful "follow" state (a rules colour param on an action without
    /// the accent concept) hides it rather than offering a reset that would
    /// write a sentinel nothing resolves.
    property bool showReset: true
    /// What the indicator actually draws while storedColor is the sentinel.
    /// Shown as the swatch so the control previews the real result rather
    /// than a blank.
    property color themeColor: Kirigami.Theme.highlightColor
    /// The page-level ColorDialog. Deliberately `var` rather than a typed
    /// `Dialog`: the control duck-types anything exposing accepted /
    /// rejected / selectedColor / open(), so a host can substitute its own
    /// picker without this file importing QtQuick.Dialogs.
    property var picker: null
    /// What a screen reader announces for the swatch.
    property string swatchAccessibleName: i18nc("@action:button", "Color")

    /// Emitted with the chosen `#AARRGGBB`, or the sentinel on reset.
    signal colorChosen(string hex)

    readonly property bool _followsTheme: root.storedColor === root.sentinel

    // Qt's color.toString() drops the alpha channel when fully opaque and
    // keeps it otherwise, so the hex label's width would jump between 6 and 8
    // digits. Always emit the full 8-digit form — the canonical #AARRGGBB
    // spelling every colour setting stores.
    //
    // Takes a COLOR, not a string. Handing it `storedColor` reads `.a`/`.r`
    // off a QML string, which are undefined, and `Math.round(undefined * 255)`
    // is NaN — the label rendered a literal "#NANNANNANNAN". Anything already
    // in string form wants _displayHex below instead.
    function _toHexArgb(c) {
        function pad(v) {
            return Math.round(v * 255).toString(16).padStart(2, '0');
        }
        return ("#" + pad(c.a) + pad(c.r) + pad(c.g) + pad(c.b)).toUpperCase();
    }

    /// The stored value as the label shows it. Picker and reset writes are
    /// hex via _toHexArgb, so this usually only normalises case — but the
    /// read-side validator accepts ANY valid colour NAME, so a hand-edited
    /// config can carry a named colour ("red") or a short form ("#RGB");
    /// both are shown VERBATIM (uppercased) rather than silently rewritten
    /// into a spelling the user never typed. The swatch still paints the
    /// real colour either way.
    function _displayHex(s) {
        return s.toUpperCase();
    }

    // The transient picker handlers below outlive this control if it is
    // destroyed while the dialog is open (a rules-list rebuild destroying a
    // delegate is an EXPECTED path — it is why the picker is page-level).
    // Track the live pair so destruction can disconnect them instead of
    // leaving stale closures on the shared dialog's signals.
    property var _disconnectPending: null

    Component.onDestruction: {
        if (root._disconnectPending)
            root._disconnectPending();
    }

    spacing: Kirigami.Units.smallSpacing

    ColorButton {
        id: swatch

        color: root._followsTheme ? root.themeColor : root.storedColor
        Accessible.name: root.swatchAccessibleName
        accessibleDescription: root._followsTheme ? i18n("Following the color scheme") : i18n("Current color: %1", swatch.color.toString())
        onClicked: {
            if (!root.picker)
                return;
            // The picker is SHARED across the controls on the page. If a
            // second control could open it while the first is pending, both
            // controls' handlers would be connected and one accept would write
            // the chosen colour into two settings. Refusing while it is up is
            // what actually makes the sharing safe, rather than relying on the
            // dialog's modality.
            if (root.picker.visible)
                return;
            var picker = root.picker;

            function acceptedHandler() {
                root._disconnectPending = null;
                picker.accepted.disconnect(acceptedHandler);
                picker.rejected.disconnect(rejectedHandler);
                root.colorChosen(root._toHexArgb(picker.selectedColor));
            }

            function rejectedHandler() {
                root._disconnectPending = null;
                picker.accepted.disconnect(acceptedHandler);
                picker.rejected.disconnect(rejectedHandler);
            }

            picker.accepted.connect(acceptedHandler);
            picker.rejected.connect(rejectedHandler);
            root._disconnectPending = function () {
                // The page-level dialog can be destroyed before this control
                // during a page teardown; a null picker has nothing left to
                // disconnect.
                if (!picker)
                    return;
                picker.accepted.disconnect(acceptedHandler);
                picker.rejected.disconnect(rejectedHandler);
            };
            // Seed imperatively: ColorDialog writes selectedColor itself
            // as the user drags, and that JS-side write would sever a
            // declarative binding after the first edit.
            picker.selectedColor = swatch.color;
            picker.open();
        }
    }

    QQC2.Label {
        text: root._followsTheme ? root.fallbackLabel : root._displayHex(root.storedColor)
        color: root._followsTheme ? Kirigami.Theme.disabledTextColor : Kirigami.Theme.textColor
        font.family: root._followsTheme ? Kirigami.Theme.defaultFont.family : Kirigami.Theme.fixedWidthFont.family
        // Content-sized up to a cap, not a fixed width: the SettingsRow slot
        // hosts this in a Row POSITIONER that never resizes its child, so
        // every pixel of fixed width the label doesn't need pushes the Reset
        // button toward the clip edge on a narrow window.
        Layout.preferredWidth: Math.min(implicitWidth, Kirigami.Units.gridUnit * 6)
        elide: Text.ElideRight
        // A hex value is an LTR machine token, not prose: pin the alignment
        // (Text mirrors an explicitly-set horizontalAlignment under
        // LayoutMirroring, so disabling mirroring here is what makes the
        // AlignLeft stick) so RTL locales neither flip the visual edge the
        // value hangs from nor elide the wrong end.
        horizontalAlignment: Text.AlignLeft
        LayoutMirroring.enabled: false
    }

    QQC2.Button {
        visible: root.showReset
        icon.name: "edit-reset"
        // Nothing to reset while the colour already follows the theme.
        enabled: !root._followsTheme
        text: i18n("Reset")
        display: QQC2.AbstractButton.IconOnly
        Accessible.name: root.resetAccessibleName
        onClicked: {
            root.colorChosen(root.sentinel);
            // The button disables itself the moment the sentinel lands
            // (enabled tracks _followsTheme), which would drop keyboard
            // focus on the floor — re-home it on the swatch.
            swatch.forceActiveFocus();
        }

        QQC2.ToolTip.visible: hovered
        QQC2.ToolTip.text: root.resetToolTip
    }
}
