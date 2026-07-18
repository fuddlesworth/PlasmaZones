// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import org.phosphor.animation
import org.phosphor.control as PhosphorUi
import org.plasmazones.common as QFZCommon

// Top-level settings window. Chrome (sidebar, breadcrumbs, apply/discard
// footer, "select a page" placeholder) comes from
// PhosphorUi.SettingsAppWindow — driven by the PageRegistry that
// SettingsController.app exposes. This file owns the PlasmaZones-specific
// glue that surrounds the chrome: the layout-context popup menu QML pages
// reach via `window.layoutContextMenu`, the three-way unsaved-changes
// dialog (Apply / Discard / Cancel — richer than the lib's plain
// Discard/Keep prompt), the toast, the shortcut overlay, and the
// What's-New banner that pops on first launch after an update.
PhosphorUi.SettingsAppWindow {
    // Aspect-ratio labels consumed by the layout context menu's
    // submenu — kept at window scope rather than inside the Menu so
    // future consumers (Per-Screen Override picker, Layouts page) can
    // bind to the same canonical i18n strings without duplicating them.

    id: window

    // ── Public API used by per-page QML files ───────────────────────
    // Pages reach the layout-context popup via window.layoutContextMenu.
    readonly property alias layoutContextMenu: layoutContextMenu
    // GeneralPage's "Reset to Defaults" button reaches the chrome-owned
    // confirmation dialog through this alias. The dialog lives in Main.qml
    // because `_navShortcutsEnabled` (declared later in this file) consults
    // its visibility to gate Ctrl+PgUp/PgDown; without the alias, the
    // GeneralPage button's `defaultsConfirmDialog.open()` would resolve
    // against the page's own scope (Loader breaks file-id lookup),
    // throwing `ReferenceError: defaultsConfirmDialog is not defined`
    // at runtime and leaving the user with no path to reset defaults.
    readonly property alias defaultsConfirmDialog: defaultsConfirmDialog
    // Kept as an object literal for back-compat with any QML reader that
    // expects index-by-key access (a Repeater iterates the keys / a
    // delegate looks up by aspect-ratio key). Reading any value here
    // forces the dependent binding via the QtObject's per-property
    // bindings, so locale changes still propagate.
    //
    // WARNING: Consumers MUST NOT cache the object literal into a
    // local `var` and then read fields off the cache — the cache
    // captures the QtObject member values at the moment of
    // assignment and is NOT re-evaluated when the user changes
    // language. Read fields off `window.aspectRatioLabels` directly
    // (or call `window.aspectRatioLabel(key)`) at the point of use so
    // QML's property-binding machinery sees the dependency and
    // re-evaluates when the underlying i18n() strings refresh. The
    // outer `readonly property var` IS itself a binding, so reading
    // it inline is fine; caching it into a non-binding context is
    // not.
    readonly property var aspectRatioLabels: ({
            "any": aspectRatioLabelsObject.allMonitors,
            "standard": aspectRatioLabelsObject.standard,
            "ultrawide": aspectRatioLabelsObject.ultrawide,
            "super-ultrawide": aspectRatioLabelsObject.superUltrawide,
            "portrait": aspectRatioLabelsObject.portrait
        })
    // Keyboard-shortcut overlay state.
    property bool _showShortcuts: false

    // ── Public functions (called from per-page QML): ───────────────
    //   - aspectRatioLabel(key): translate an aspect-ratio key to its
    //     localized label (mirrors the `aspectRatioLabels` lookup map
    //     for consumers that prefer the function form).
    //   - showWhatsNew(): pop the WhatsNewPage dialog. Used by the
    //     AboutPage "What's New" link and by post-update auto-pop.
    //   - showToast(msg): surface a transient pill notification at
    //     the bottom of the window. Used by every page for inline
    //     success / failure feedback.
    function aspectRatioLabel(key) {
        switch (key) {
        case "any":
            return aspectRatioLabelsObject.allMonitors;
        case "standard":
            return aspectRatioLabelsObject.standard;
        case "ultrawide":
            return aspectRatioLabelsObject.ultrawide;
        case "super-ultrawide":
            return aspectRatioLabelsObject.superUltrawide;
        case "portrait":
            return aspectRatioLabelsObject.portrait;
        default:
            return key;
        }
    }

    function showWhatsNew() {
        whatsNewDialog.open();
    }

    function showToast(msg) {
        toast.show(msg);
    }

    // Page-controller toasts, wired once here rather than per page. A refusal can
    // be raised from a controller while a completely different page is loaded (a
    // Reset blocked by an in-flight discard, say), and a page-scoped Connections
    // would drop it on the floor.
    //
    // The per-page ShaderSetStore bridges (each page controller's setsBridge) are
    // NOT wired here, on purpose: every one of their toasts is raised by a user
    // action on the sets page itself, so the page's own Connections is the right
    // scope and the shell would just duplicate it.
    Connections {
        target: settingsController.animationsPage

        function onToastRequested(text) {
            window.showToast(text);
        }
    }

    Connections {
        target: settingsController.decorationPage

        function onToastRequested(text) {
            window.showToast(text);
        }
    }

    Connections {
        target: settingsController.snappingShadersPage

        function onToastRequested(text) {
            window.showToast(text);
        }
    }

    controller: settingsController.app
    title: i18n("PlasmaZones Settings")
    // Sized in Kirigami grid units so the window scales with the
    // user's gridUnit setting (HiDPI / large-text themes). Was a
    // hardcoded 1200x800 that ignored both.
    width: Kirigami.Units.gridUnit * 60
    height: Kirigami.Units.gridUnit * 40
    // Use the lib's 3-action close prompt (Apply / Discard / Cancel)
    // — same UX as the legacy hand-rolled unsavedChangesDialog, but
    // the framework owns the dialog and the close orchestration.
    closePromptShowsApply: true
    // Gate the chrome's back/forward history inputs (Alt+Left /
    // Alt+Right, mouse back/forward buttons) behind the same guard as
    // the Ctrl+PgUp/PgDown page-step shortcuts, so history navigation
    // can't drag the user off the page while a confirm dialog, the
    // shortcut overlay, a page-owned modal, or the search dropdown is
    // open.
    navigationShortcutsEnabled: window._navShortcutsEnabled

    // Global search in the header toolbar (headerExtras slot). It supersedes the
    // in-sidebar page-tree filter, which is disabled in Component.onCompleted.
    headerExtras: Component {
        GlobalSearchField {
            // SettingsAppWindow paints the header band itself with the Header
            // color set + background, so this slot content must match the band
            // it sits on (wallpaper-aware schemes give Header its own hue)
            // rather than inherit the Window set.
            Kirigami.Theme.colorSet: Kirigami.Theme.Header
            Kirigami.Theme.inherit: false
            // Declared inline in Main.qml, so it can reach `window` to feed the
            // page-step shortcut guard while the results dropdown is open.
            onSearchOpenChanged: window._searchOpen = searchOpen
        }
    }

    // Daemon status, right-aligned on the search row: pulsing colored dot
    // (positive when running, negative when stopped) + Running/Stopped label +
    // enable/disable switch.
    headerTrailing: Component {
        RowLayout {
            // Sits on the same SettingsAppWindow-painted Header band as the
            // search field — same Header color set so text/dot roles match it.
            Kirigami.Theme.colorSet: Kirigami.Theme.Header
            Kirigami.Theme.inherit: false
            spacing: Kirigami.Units.smallSpacing

            Rectangle {
                id: daemonDot

                Layout.alignment: Qt.AlignVCenter
                width: Kirigami.Units.smallSpacing * 1.5
                height: Kirigami.Units.smallSpacing * 1.5
                radius: width / 2
                color: settingsController.daemonRunning ? Kirigami.Theme.positiveTextColor : Kirigami.Theme.negativeTextColor

                SequentialAnimation on opacity {
                    loops: Animation.Infinite
                    running: settingsController.daemonRunning
                    // The pulse can stop mid-cycle (daemon toggled off), which
                    // would freeze the dot at a partial opacity.
                    onRunningChanged: if (!running)
                        daemonDot.opacity = 1

                    PhosphorMotionAnimation {
                        from: 1
                        to: 0.4
                        profile: "widget.pulse.slow"
                    }

                    PhosphorMotionAnimation {
                        from: 0.4
                        to: 1
                        profile: "widget.pulse.slow"
                    }
                }
            }

            Label {
                text: settingsController.daemonRunning ? i18n("Running") : i18n("Stopped")
                opacity: 0.7
                Layout.alignment: Qt.AlignVCenter
            }

            SettingsSwitch {
                Layout.alignment: Qt.AlignVCenter
                checked: settingsController.daemonRunning
                enabled: !settingsController.daemonController.busy
                accessibleName: i18n("Toggle daemon")
                // The switch is fully controlled (checked is bound to
                // daemonRunning and never self-toggles), so it stays visually
                // "on" until the daemon actually stops. Turning OFF kills tiling
                // + snapping for the whole session, so confirm first; turning ON
                // applies immediately.
                // Routes through the root-level daemonStopConfirm (declared
                // beside the other inline confirm dialogs) so the page-nav
                // shortcut guard can see its `visible` state.
                onToggled: function (newValue) {
                    if (newValue)
                        settingsController.daemonController.setEnabled(true);
                    else
                        daemonStopConfirm.open();
                }
            }
        }
    }

    // Per-page overflow (kebab) in the breadcrumb row: Reset this page to
    // defaults / Discard this page's unsaved changes. Shown on any page that
    // supports at least one action. The two items query pageSupportsReset /
    // pageSupportsDiscard separately so a future discard-only page can show just
    // Discard; today every resettable page is also discardable, so the two
    // predicates match. Both route through a window-scoped confirm.
    breadcrumbTrailing: Component {
        ToolButton {
            id: pageActionsButton

            // Re-evaluates on activePageChanged (activePage is referenced), so
            // the button appears/disappears as the user moves between pages.
            visible: settingsController.pageSupportsReset(settingsController.activePage) || settingsController.pageSupportsDiscard(settingsController.activePage)
            icon.name: "overflow-menu"
            display: AbstractButton.IconOnly
            text: i18n("Page actions")
            Accessible.name: i18n("Page actions")

            onClicked: pageActionsMenu.popup()

            Menu {
                id: pageActionsMenu

                MenuItem {
                    text: i18n("Reset page to defaults")
                    Accessible.name: text
                    icon.name: "document-revert"
                    visible: settingsController.pageSupportsReset(settingsController.activePage)
                    // Disabled while a global Save/Discard batch is in flight: a
                    // per-page reset mid-batch could race the async revert (e.g.
                    // the animation controller's async file restore) and leave a
                    // partial reset.
                    enabled: !settingsController.app.applying && !settingsController.app.discarding
                    onTriggered: resetPageConfirmDialog.open()
                }

                MenuItem {
                    text: i18n("Discard changes on this page")
                    Accessible.name: text
                    icon.name: "edit-undo"
                    visible: settingsController.pageSupportsDiscard(settingsController.activePage)
                    // Enabled only when the page carries unsaved edits and no
                    // global Save/Discard batch is in flight. `dirtyPages` is
                    // referenced purely to re-run this binding on
                    // dirtyPagesChanged (isPageDirty itself is a function call
                    // whose only QML dependency would otherwise be activePage).
                    enabled: {
                        void settingsController.dirtyPages;
                        return !settingsController.app.applying && !settingsController.app.discarding && settingsController.isPageDirty(settingsController.activePage);
                    }
                    onTriggered: discardPageConfirmDialog.open()
                }
            }
        }
    }

    Component.onCompleted: {
        // Zone previews app-wide must show the EFFECTIVE zone colors (the
        // same settings pipeline the daemon pushes into its overlays). The
        // ZoneColorDefaults singleton cannot resolve the appSettings context
        // property itself (compiled-module singletons have no root-context
        // chain), so inject it once here.
        QFZCommon.ZoneColorDefaults.settingsSource = appSettings;

        // The header search supersedes the sidebar's page-tree search.
        window.sidebar.searchEnabled = false;

        var geo = settingsController.loadWindowGeometry();
        if (geo.width > 0 && geo.height > 0) {
            window.width = geo.width;
            window.height = geo.height;
        }
        if (geo.hasPosition) {
            window.x = geo.x;
            window.y = geo.y;
        }
        // Restore sidebar drill state: walk the parent chain from the
        // restored activePage. The legacy file did a 90-line traversal
        // over _mainItems / _childItems for this — the framework now
        // exposes the same lookup as one Q_INVOKABLE on the controller.
        window._drillIntoActivePage();
    }

    // Drill into the deepest non-collapsible ancestor of the current
    // activePage so the sub-sidebar opens at the right level; inline-
    // collapsible ancestors stay where they live (the sidebar will
    // expand them via the default-true `expandedCategories` map). Used
    // both at restore-time (Component.onCompleted) AND when activePage
    // changes externally (CLI --page, daemon broadcast, shortcut). DRY
    // the chain-walk so future tweaks happen in one place.
    function _drillIntoActivePage() {
        const chain = settingsController.app.parentChainFor(settingsController.activePage);
        for (let i = chain.length - 1; i >= 0; --i) {
            const entry = settingsController.app.registry.pageData(chain[i]);
            if (entry && entry.id && entry.isCollapsible !== true) {
                if (window.sidebar.currentParentId !== chain[i])
                    window.sidebar.drillInto(chain[i]);
                return;
            }
        }
        // No non-collapsible ancestor — page lives under main mode.
        if (window.sidebar.currentParentId !== "")
            window.sidebar.drillOut();
    }

    // Translated labels for aspect-ratio classes. Backed by a QtObject
    // so each property is its own binding — bindings re-evaluate when
    // QML's language-change signal fires, while an object-literal
    // declared with `property var = ({...})` is frozen at construction
    // time and would freeze the labels at the language active during
    // first instantiation. The "super-ultrawide" key contains a hyphen
    // so it lives under the `superUltrawide` member of this QtObject;
    // the aspectRatioLabel(key) accessor function (above) translates
    // back to the hyphenated key consumers use.
    QtObject {
        // `any` (instead of e.g. `allMonitors`) was the original
        // property name — qmlformat 6.11 silently fails on a property
        // named `any` (same shadow-class bug that hits `id`). Renamed
        // to `allMonitors` and the consumer reads below follow.
        id: aspectRatioLabelsObject

        readonly property string allMonitors: i18n("All Monitors")
        readonly property string standard: i18n("Standard (16:9)")
        readonly property string ultrawide: i18n("Ultrawide (21:9)")
        readonly property string superUltrawide: i18n("Super-Ultrawide (32:9)")
        readonly property string portrait: i18n("Portrait (9:16)")
    }

    // The lib's onClosing handles the dirty-state prompt. We just
    // need to add a window-geometry save alongside it — Connections
    // adds to the closing signal rather than overriding the base
    // handler, so the framework's prompt still fires.
    Connections {
        function onClosing(close) {
            // Skip when the framework's dirty-state prompt cancelled the
            // close — geometry should only persist on an actual close, not
            // on every cancelled-by-dialog attempt.
            if (!close.accepted)
                return;

            settingsController.saveWindowGeometry(window.x, window.y, window.width, window.height);
        }

        // Wire the lib's apply-on-close failure signal to a toast so
        // the user sees WHY the window didn't close, instead of getting
        // re-prompted with the same discard dialog. The lib hands us
        // the ids of pages still dirty after applyAll(); resolve them
        // to titles via the registry for a readable message.
        function onApplyOnCloseFailed(dirtyPageIds, errors) {
            const reg = settingsController.app.registry;
            const titles = [];
            for (let i = 0; i < dirtyPageIds.length; ++i) {
                const data = reg.pageData(dirtyPageIds[i]);
                if (data && data.title)
                    titles.push(data.title);
                else if (dirtyPageIds[i])
                    titles.push(dirtyPageIds[i]);
            }
            // Prefer the per-domain error string when the controller
            // surfaced one — "permission denied on /etc/foo" is more
            // actionable than "page X failed". Fall back to the page
            // titles list when the error array is empty (older
            // domains that don't emit per-domain text).
            if (errors && errors.length > 0)
                window.showToast(i18n("Save did not complete: %1", errors.join("; ")));
            else if (titles.length === 0)
                window.showToast(i18n("Save did not complete. Some pages still have unsaved changes."));
            else
                window.showToast(i18n("Save did not complete. Still unsaved on: %1", titles.join(", ")));
        }

        function onDiscardOnCloseFailed(errors) {
            // Toast before the deferred close fires so the user sees
            // the message even though the window is about to close.
            // SettingsAppWindow already gates the emit on a non-empty
            // errors array, but mirror the apply-on-close guard shape
            // here so a future library refactor that loosens that
            // check can't surface a `null.join(...)` runtime error.
            const detail = (errors && errors.length > 0) ? errors.join("; ") : i18n("(no details)");
            window.showToast(i18n("Discard did not complete: %1", detail));
        }

        target: window
    }

    // Mirror activePage changes back into the sidebar's drill scope —
    // external navigation (a CLI --page arg, a daemon broadcast, a
    // shortcut) should still land in the right drill view rather than
    // leaving the sidebar showing a stale top-level / wrong-parent list.
    Connections {
        function onActivePageChanged() {
            window._drillIntoActivePage();
        }

        target: settingsController
    }

    // Auto-drill-out when a feature is disabled while inside its sub-sidebar.
    Connections {
        function onSnappingEnabledChanged() {
            if (!appSettings.snappingEnabled && window.sidebar.currentParentId === "snapping")
                window.sidebar.drillOut();
        }

        function onAutotileEnabledChanged() {
            if (!appSettings.autotileEnabled && window.sidebar.currentParentId === "tiling")
                window.sidebar.drillOut();
        }

        target: appSettings
    }

    // ── Ctrl+PgUp / Ctrl+PgDown — step through navigable pages ──────
    // Guarded: page navigation must not fire while any of the inline
    // confirm dialogs (whatsNewDialog,
    // defaultsConfirmDialog, sectionToggleDiscardConfirm, daemonStopConfirm,
    // resetPageConfirmDialog, discardPageConfirmDialog),
    // the shortcut
    // overlay, the active page's own modal stack (RulesPage's
    // forceSaveConfirm / addRuleWizard / ruleEditorSheet /
    // windowPickerDialog), OR a native child window (QtQuick FileDialog,
    // system color picker, etc.) is open. The `window.active` check
    // covers native child windows — when they grab focus the main window
    // goes inactive. The inline-dialog checks cover Kirigami.PromptDialog
    // overlays that don't change the window's active state. Without the
    // combined guard the user could navigate the underlying page state
    // while interacting with any of these prompts.
    //
    // Page-level modals are surfaced via an optional `anyModalOpen`
    // property on the active page item — the active page lives inside
    // the framework-owned PageHost Loader, so we read it via the
    // settingsApp activeFocusItem chain. Pages that haven't opted into
    // the property contribute false; the guard stays correct.
    /// Cross-cutting flag that pages opt into by writing through
    /// `window._pageOwnedModalOpen` when they open / close their own
    /// modal stack. RulesPage publishes its
    /// addRuleWizard / windowPickerDialog / ruleEditorSheet /
    /// forceSaveConfirm aggregate state here so page-navigation
    /// shortcuts (Ctrl+PgUp / PgDown) cannot drag the user off the
    /// page while a destructive modal is open. Pages without modals
    /// never touch this property and contribute false by default.
    /// Declared BEFORE `_navShortcutsEnabled` so a top-down reader
    /// sees the property's purpose before the guard that consumes it.
    property bool _pageOwnedModalOpen: false
    /// True while the global search dropdown is open — suppresses page-step
    /// shortcuts so ↑/↓/Enter drive the results list, not page navigation.
    property bool _searchOpen: false
    // Shared enable-guard for page-navigation shortcuts. Hoisted from
    // the two identical inline expressions so a future dialog addition
    // doesn't drift between Ctrl+PgUp / Ctrl+PgDown.
    readonly property bool _navShortcutsEnabled: window.active && !whatsNewDialog.visible && !defaultsConfirmDialog.visible && !resetPageConfirmDialog.visible && !discardPageConfirmDialog.visible && !sectionToggleDiscardConfirm.visible && !daemonStopConfirm.visible && !layoutContextMenu.visible && !window._showShortcuts && !window._pageOwnedModalOpen && !window._searchOpen

    Shortcut {
        sequence: "Ctrl+PgUp"
        enabled: window._navShortcutsEnabled
        onActivated: settingsController.app.gotoPreviousPage()
    }

    Shortcut {
        sequence: "Ctrl+PgDown"
        enabled: window._navShortcutsEnabled
        onActivated: settingsController.app.gotoNextPage()
    }

    // ── Help-overlay shortcut ───────────────────────────────────────
    Shortcut {
        sequence: "?"
        enabled: {
            // Don't toggle the overlay while the user is typing in a
            // text field — `?` is a legitimate character there. The
            // Accessible.role fallback catches TextField/TextArea
            // (Qt Quick Controls 2 wrappers) which subclass neither
            // TextInput nor TextEdit directly but report EditableText
            // / PasswordText through their AT-SPI role.
            var item = window.activeFocusItem;
            if (!item)
                return true;

            if (item instanceof TextInput || item instanceof TextEdit)
                return false;

            var role = item.Accessible.role;
            if (role === Accessible.EditableText || role === Accessible.PasswordText)
                return false;

            return true;
        }
        onActivated: window._showShortcuts = !window._showShortcuts
    }

    // ── Toast ───────────────────────────────────────────────────────
    // Pill notification, anchored to the bottom of the window's
    // contentItem so it floats above the chrome.
    Toast {
        id: toast

        parent: window.contentItem
    }

    // ── Layout context menu (extracted to LayoutContextMenu.qml; instantiated
    // here, outside any Loader, to avoid Qt6 SIGSEGV on Menu destruction) ──
    LayoutContextMenu {
        id: layoutContextMenu

        settingsController: settingsController
        appSettings: appSettings
        aspectRatioLabels: window.aspectRatioLabels
    }
    Kirigami.PromptDialog {
        id: defaultsConfirmDialog

        title: i18n("Restore Defaults")
        subtitle: i18n("Are you sure you want to reset all settings to their default values?")
        standardButtons: Kirigami.Dialog.NoButton
        customFooterActions: [
            Kirigami.Action {
                text: i18n("Restore Defaults")
                icon.name: "document-revert"
                onTriggered: {
                    defaultsConfirmDialog.close();
                    settingsController.defaults();
                }
            },
            Kirigami.Action {
                text: i18n("Cancel")
                icon.name: "dialog-cancel"
                onTriggered: defaultsConfirmDialog.close()
            }
        ]
    }

    // Per-page Reset — restores just the active page's settings to their
    // defaults, staged for Save/Discard (opened from the breadcrumb kebab).
    Kirigami.PromptDialog {
        id: resetPageConfirmDialog

        title: i18n("Reset Page to Defaults")
        subtitle: i18n("Reset the settings on this page to their default values? You can still review the result and Save or Discard afterwards.")
        standardButtons: Kirigami.Dialog.NoButton
        customFooterActions: [
            Kirigami.Action {
                text: i18n("Reset Page")
                icon.name: "document-revert"
                onTriggered: {
                    resetPageConfirmDialog.close();
                    settingsController.resetPage(settingsController.activePage);
                }
            },
            Kirigami.Action {
                text: i18n("Cancel")
                icon.name: "dialog-cancel"
                onTriggered: resetPageConfirmDialog.close()
            }
        ]
    }

    // Per-page Discard — reverts just the active page's unsaved edits to the
    // last-saved values, leaving other pages' changes intact.
    Kirigami.PromptDialog {
        id: discardPageConfirmDialog

        title: i18n("Discard Page Changes")
        subtitle: i18n("Discard the unsaved changes on this page? Changes on other pages are kept.")
        standardButtons: Kirigami.Dialog.NoButton
        customFooterActions: [
            Kirigami.Action {
                text: i18n("Discard")
                icon.name: "edit-undo"
                onTriggered: {
                    discardPageConfirmDialog.close();
                    settingsController.discardPage(settingsController.activePage);
                }
            },
            Kirigami.Action {
                text: i18n("Cancel")
                icon.name: "dialog-cancel"
                onTriggered: discardPageConfirmDialog.close()
            }
        ]
    }

    // Confirm dialog for the sidebar's inline snapping/tiling toggle when
    // the relevant page has unsaved edits. Disabling the section through
    // beginExternalEdit/endExternalEdit commits the *_Enabled flag plus
    // whatever the page has staged dirty — without this gate the user
    // could silently apply a partial edit by flipping the sidebar toggle.
    Kirigami.PromptDialog {
        id: sectionToggleDiscardConfirm

        // Set by the trailing-delegate SettingsSwitch before open(); the
        // confirm action reads them to know which section to commit and
        // what value to set.
        property string pendingSection: ""
        property bool pendingValue: false

        title: i18n("Discard unsaved changes?")
        subtitle: pendingSection === "snapping" ? i18n("Disabling Snapping will discard your unsaved Snapping changes. Continue?") : i18n("Disabling Tiling will discard your unsaved Tiling changes. Continue?")
        standardButtons: Kirigami.Dialog.NoButton
        customFooterActions: [
            Kirigami.Action {
                text: i18n("Discard and Disable")
                icon.name: "edit-undo"
                onTriggered: {
                    const section = sectionToggleDiscardConfirm.pendingSection;
                    const value = sectionToggleDiscardConfirm.pendingValue;
                    sectionToggleDiscardConfirm.close();
                    // Discard the section's staged edits first, THEN flip the
                    // enable flag — otherwise the inline beginExternalEdit /
                    // endExternalEdit pair would surface the still-staged edits
                    // alongside the disable. discardPage("snapping"/"tiling")
                    // reverts every manifest-backed leaf under that mode back to
                    // the committed baseline (the framework PageAdapter.discard()
                    // for these virtual parents is a no-op, so the old
                    // registry.controller(section).discard() call did nothing).
                    settingsController.discardPage(section);
                    settingsController.beginExternalEdit(section);
                    if (section === "snapping")
                        appSettings.snappingEnabled = value;
                    else
                        appSettings.autotileEnabled = value;
                    settingsController.endExternalEdit();
                }
            },
            Kirigami.Action {
                text: i18n("Cancel")
                icon.name: "dialog-cancel"
                onTriggered: sectionToggleDiscardConfirm.close()
            }
        ]
    }

    // Confirm before stopping the daemon from the header toggle. Declared at the
    // window root (not in the headerTrailing Component) so the page-nav shortcut
    // guard `_navShortcutsEnabled` can read its `visible` state; the header
    // SettingsSwitch opens it via outer-scope reference.
    Kirigami.PromptDialog {
        id: daemonStopConfirm

        title: i18n("Stop daemon?")
        subtitle: i18n("Stopping the PlasmaZones daemon disables window tiling and snapping until you start it again.")
        standardButtons: Kirigami.Dialog.Cancel
        customFooterActions: [
            Kirigami.Action {
                text: i18n("Stop daemon")
                icon.name: "system-shutdown"
                onTriggered: {
                    settingsController.daemonController.setEnabled(false);
                    daemonStopConfirm.close();
                }
            }
        ]
    }

    // ── Keyboard-shortcut overlay ───────────────────────────────────
    KeyboardShortcutOverlay {
        parent: window.contentItem
        // `appSettings` is the context property exposed by main.cpp;
        // pass it explicitly through the new required property so the
        // overlay no longer relies on the implicit context-name match.
        appSettings: appSettings
        shown: window._showShortcuts
        onDismiss: window._showShortcuts = false
    }

    // ── What's New dialog ──────────────────────────────────────────
    WhatsNewPage {
        id: whatsNewDialog
    }

    // Brief delay before auto-popping the What's-New dialog on first
    // launch after an update — lets the main window finish its
    // first-paint motion before the modal steals focus. Kirigami's
    // `veryLongDuration` is the canonical "noticeable but unhurried"
    // timing token (currently 400 ms across themes), close enough to
    // the legacy hand-picked 500 ms and theme-tracking.
    Timer {
        interval: Kirigami.Units.veryLongDuration
        running: settingsController.hasUnseenWhatsNew
        onTriggered: whatsNewDialog.open()
    }

    // Per-row sidebar trailing content — a Row with two slots:
    //   1. Pulsing dirty badge that's visible when the row's page (or
    //      one of its descendants for a collapsed category header) is
    //      dirty. The user sees pending edits hiding inside categories
    //      without having to drill in.
    //   2. Inline SettingsSwitch on the snapping / tiling rows so a
    //      whole feature can be disabled without drilling in.
    sidebar.trailingDelegate: Component {
        RowLayout {
            id: trailingRow

            // No colorSet override here: the sidebar is Window-set chrome, and
            // a Header-set delegate would mismatch its own row highlight.

            // SidebarRow's trailingLoader exposes the row's role data via
            // `modelData`. The lib renamed the row-identifier role from
            // "id" to "pageId" (the prior name shadowed the QML id:
            // directive); read `entry.pageId` accordingly.
            readonly property var entry: parent ? parent.modelData : null
            readonly property bool isSnapping: entry && entry.pageId === "snapping"
            readonly property bool isTiling: entry && entry.pageId === "tiling"
            readonly property bool isCollapsibleHeader: entry && entry._isCollapsibleHeader === true
            readonly property bool isCollapsibleExpanded: isCollapsibleHeader && entry._isExpanded === true
            property int _dirtyTick: 0

            spacing: Kirigami.Units.smallSpacing

            // ── Unsaved-changes badge ────────────────────────────────
            // `_dirtyTick` bumps once per `dirtyPagesChanged` emit. We
            // bind the badge's `visible` to it (read once via the
            // ternary) and to `isPageDirty()` directly — vs. the
            // earlier `settingsController.dirtyPages` read, which
            // materialised a fresh QStringList per row per emit.
            Connections {
                function onDirtyPagesChanged() {
                    trailingRow._dirtyTick++;
                }

                target: settingsController
            }

            Rectangle {
                id: dirtyBadge

                width: Kirigami.Units.smallSpacing * 1.5
                height: Kirigami.Units.smallSpacing * 1.5
                radius: width / 2
                color: Kirigami.Theme.neutralTextColor
                Layout.alignment: Qt.AlignVCenter
                visible: {
                    if (!trailingRow.entry)
                        return false;

                    // For inline-collapsible headers, only show the
                    // badge when the category is COLLAPSED — expanded
                    // categories show their dirty children's own
                    // badges so the header badge is redundant.
                    if (trailingRow.isCollapsibleHeader && trailingRow.isCollapsibleExpanded)
                        return false;

                    trailingRow._dirtyTick; // re-evaluate when dirty state changes
                    return settingsController.isPageDirty(trailingRow.entry.pageId);
                }

                SequentialAnimation on opacity {
                    loops: Animation.Infinite
                    running: dirtyBadge.visible

                    PhosphorMotionAnimation {
                        from: 1
                        to: 0.4
                        profile: "widget.pulse"
                    }

                    PhosphorMotionAnimation {
                        from: 0.4
                        to: 1
                        profile: "widget.pulse"
                    }
                }
            }

            // ── Snapping / Tiling toggle ────────────────────────────
            SettingsSwitch {
                id: sectionToggle

                visible: trailingRow.isSnapping || trailingRow.isTiling
                checked: trailingRow.isSnapping ? appSettings.snappingEnabled : (trailingRow.isTiling ? appSettings.autotileEnabled : false)
                accessibleName: trailingRow.entry ? trailingRow.entry.title : ""
                onToggled: function (newValue) {
                    // Disabling from the sidebar is a destructive shortcut
                    // when the page underneath has unsaved edits — those
                    // edits would silently apply through the
                    // beginExternalEdit/endExternalEdit pair (which commits
                    // the snapping/tiling enable flag plus whatever dirty
                    // state the page has staged). Prompt before clobbering.
                    // Enabling is safe — it doesn't discard anything — so
                    // we only gate the disable path.
                    if (!newValue && trailingRow.entry && settingsController.isPageDirty(trailingRow.entry.pageId)) {
                        sectionToggleDiscardConfirm.pendingSection = trailingRow.isSnapping ? "snapping" : "tiling";
                        sectionToggleDiscardConfirm.pendingValue = newValue;
                        sectionToggleDiscardConfirm.open();
                        // Snap the toggle visual back to its checked state
                        // — the binding to appSettings.* keeps it in sync
                        // automatically once the user makes a decision.
                        return;
                    }
                    settingsController.beginExternalEdit(trailingRow.isSnapping ? "snapping" : "tiling");
                    if (trailingRow.isSnapping)
                        appSettings.snappingEnabled = newValue;
                    else
                        appSettings.autotileEnabled = newValue;
                    settingsController.endExternalEdit();
                }
            }
        }
    }

    // Sticky sidebar-footer profile switcher — activate a settings profile from
    // anywhere, mirroring the per-row Activate on the Profiles page. Selecting
    // one STAGES it into the Save footer (applies on Save, reverts on Discard).
    // Collapses to nothing until at least one profile exists, and (being a
    // non-compact-aware footer consumer) hides in the narrow compact rail.
    // Sticky sidebar-HEADER profile switcher — activate a settings profile from
    // anywhere, mirroring the per-row Activate on the Profiles page. Selecting
    // one STAGES it into the Save footer (applies on Save, reverts on Discard).
    // Collapses to nothing until at least one profile exists, and (being a
    // non-compact-aware header consumer) hides in the narrow compact rail.
    sidebar.headerContent: Component {
        Item {
            id: profileHeader

            // Declaring `compact` opts this slot into staying visible in the
            // icon-only rail (the Sidebar hides unaware consumers there) and
            // makes it receive the live value. In the rail there is no room for
            // the name field, so only the mark is shown — see switcherRow.
            property bool compact: false

            readonly property var profilesBridge: settingsController.profilesPage ? settingsController.profilesPage.bridge : null
            property var profileRows: profilesBridge ? profilesBridge.availableProfiles() : []
            readonly property int activeIndex: {
                for (let i = 0; i < profileRows.length; ++i) {
                    if (profileRows[i].active)
                        return i;
                }
                return -1;
            }
            readonly property var activeRow: activeIndex >= 0 ? profileRows[activeIndex] : null

            // Zero-height when there are no profiles, so the band disappears.
            implicitHeight: profileRows.length > 0 ? switcherRow.implicitHeight + Kirigami.Units.smallSpacing * 2 : 0

            Connections {
                function onProfilesChanged() {
                    profileHeader.profileRows = profileHeader.profilesBridge.availableProfiles();
                }

                target: profileHeader.profilesBridge
            }

            // A settings edit doesn't fire profilesChanged but can flip the
            // active profile's `modified` state — re-read so the marker updates.
            Connections {
                function onSettingsChanged() {
                    profileHeader.profileRows = profileHeader.profilesBridge ? profileHeader.profilesBridge.availableProfiles() : [];
                }

                target: appSettings
            }

            // Anchored rather than a RowLayout: the mark sits BESIDE the field
            // when expanded but CENTRED OVER it in the compact rail, and only
            // anchors can express both. The mark is never inside the field —
            // the Desktop style neither honours leftPadding for the text nor
            // tolerates a replaced contentItem (it calls positionToRectangle on
            // that item), so an in-field mark lands on top of the name.
            Item {
                id: switcherRow

                visible: profileHeader.profileRows.length > 0
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                anchors.leftMargin: Kirigami.Units.smallSpacing
                anchors.rightMargin: Kirigami.Units.smallSpacing
                implicitHeight: profileCombo.implicitHeight

                Item {
                    id: markSlot

                    visible: profileHeader.activeRow !== null
                    anchors.verticalCenter: parent.verticalCenter
                    // Beside the field when expanded; centred over it in the rail.
                    // Positioned with `x`, NOT by swapping anchors: assigning
                    // `undefined` to an anchor does not reliably clear it, so
                    // toggling compact left both `left` and `horizontalCenter`
                    // live, and Qt sized the item to satisfy both (left 0 +
                    // centre w/2 ⇒ width = row width) — anchors beat an explicit
                    // `width`, so the mark stretched across the whole row.
                    x: profileHeader.compact ? Math.round((parent.width - width) / 2) : 0
                    width: Kirigami.Units.iconSizes.smallMedium
                    height: Kirigami.Units.iconSizes.smallMedium
                    // Non-interactive on purpose: with no input handlers, clicks
                    // fall through to the ComboBox beneath, so tapping the mark
                    // in the rail opens the profile list.
                    z: 1

                    ProfileSignature {
                        anchors.fill: parent
                        signature: profileHeader.activeRow ? profileHeader.activeRow.signature : ""
                    }

                    // Modified badge — the settings have moved on from the
                    // profile this mark represents.
                    Rectangle {
                        visible: profileHeader.activeRow !== null && profileHeader.activeRow.modified
                        anchors.right: parent.right
                        anchors.top: parent.top
                        anchors.rightMargin: -1
                        anchors.topMargin: -1
                        width: Kirigami.Units.smallSpacing * 1.5
                        height: width
                        radius: width / 2
                        color: Kirigami.Theme.neutralTextColor
                        border.width: 1
                        border.color: Kirigami.Theme.backgroundColor
                    }
                }

                ComboBox {
                    id: profileCombo

                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.leftMargin: profileHeader.compact ? 0 : markSlot.width + Kirigami.Units.smallSpacing
                    anchors.verticalCenter: parent.verticalCenter
                    // In the rail the field itself is hidden but stays laid out
                    // and interactive, so the mark on top of it still opens a
                    // correctly-anchored popup. opacity (not visible) because an
                    // invisible item takes no input.
                    opacity: profileHeader.compact ? 0 : 1
                    model: profileHeader.profileRows
                    textRole: "name"
                    // Bound to the active profile; `activated` fires only on a
                    // real user pick (not this binding), so there is no loop.
                    currentIndex: profileHeader.activeIndex
                    displayText: profileHeader.activeRow ? profileHeader.activeRow.name : i18n("No profile")
                    Accessible.name: i18n("Active profile")
                    onActivated: function (index) {
                        const row = profileHeader.profileRows[index];
                        if (row && profileHeader.profilesBridge)
                            profileHeader.profilesBridge.activateProfile(row.id);
                    }

                    // Deliberately no ToolTip: this control sits over the page
                    // list, and a hover tip here covers the very dropdown the
                    // user is reaching for. The Profiles page spells the state
                    // out in words instead.

                    // Each entry carries its own identicon, so the dropdown
                    // reads as a gallery of profiles, not a plain name list.
                    // Styling follows LayoutComboBox's popup delegate, the app's
                    // dropdown convention: View colour set, a 0.15-alpha
                    // highlight wash over an opaque row, and a leading checkmark
                    // (with a spacer when absent) marking the current entry
                    // rather than a second highlight band.
                    delegate: ItemDelegate {
                        id: profileEntry

                        required property var modelData
                        required property int index

                        readonly property bool isCurrentSelection: profileCombo.currentIndex === profileEntry.index

                        Kirigami.Theme.colorSet: Kirigami.Theme.View
                        Kirigami.Theme.inherit: false

                        // Follow the popup's list, NOT the field: in the compact
                        // rail the field is only as wide as the rail, and sizing
                        // entries to it squeezes the names out of existence.
                        // Reserve the scrollbar gutter so the row ends at its
                        // edge rather than running under it.
                        width: {
                            const view = profileEntry.ListView.view;
                            if (!view)
                                return profileCombo.width;
                            const bar = view.ScrollBar ? view.ScrollBar.vertical : null;
                            return view.width - (bar && bar.visible ? bar.width : 0);
                        }
                        // Only the hovered / keyboard-navigated row highlights;
                        // the current one is marked by the checkmark instead.
                        highlighted: profileCombo.highlightedIndex === profileEntry.index

                        background: Rectangle {
                            color: profileEntry.highlighted ? Qt.rgba(Kirigami.Theme.highlightColor.r, Kirigami.Theme.highlightColor.g, Kirigami.Theme.highlightColor.b, 0.15) : Kirigami.Theme.backgroundColor
                        }

                        contentItem: RowLayout {
                            spacing: Kirigami.Units.smallSpacing

                            Kirigami.Icon {
                                visible: profileEntry.isCurrentSelection
                                source: "checkmark"
                                Layout.alignment: Qt.AlignVCenter
                                Layout.preferredWidth: Kirigami.Units.iconSizes.small
                                Layout.preferredHeight: Kirigami.Units.iconSizes.small
                                color: Kirigami.Theme.textColor
                            }

                            // Keeps the marks and names aligned on unchecked rows.
                            Item {
                                visible: !profileEntry.isCurrentSelection
                                Layout.preferredWidth: Kirigami.Units.iconSizes.small
                                Layout.preferredHeight: Kirigami.Units.iconSizes.small
                            }

                            ProfileSignature {
                                signature: profileEntry.modelData.signature
                                Layout.alignment: Qt.AlignVCenter
                                Layout.preferredWidth: Kirigami.Units.iconSizes.smallMedium
                                Layout.preferredHeight: Kirigami.Units.iconSizes.smallMedium
                            }

                            Label {
                                Layout.fillWidth: true
                                Layout.alignment: Qt.AlignVCenter
                                text: profileEntry.modelData.name
                                elide: Text.ElideRight
                                color: Kirigami.Theme.textColor
                            }
                        }
                    }
                }

                // The style sizes the popup from the delegate's implicit width,
                // which a custom delegate does not supply — without this the
                // list collapses to a sliver. Pin it to the field.
                Binding {
                    target: profileCombo.popup
                    property: "width"
                    // Floor it so the list stays readable when the field itself
                    // is only as wide as the compact rail.
                    value: Math.max(profileCombo.width, Kirigami.Units.gridUnit * 12)
                }
            }
        }
    }
}
