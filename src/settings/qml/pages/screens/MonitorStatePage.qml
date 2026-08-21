// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

/**
 * @brief Monitor State dashboard — current mode and layout per monitor.
 *
 * Uses the spatial DisplayMap to pick a monitor, then shows a layout
 * preview with mode toggle and layout/algorithm selector.
 */
SettingsFlickable {
    id: root

    // Bridge for LayoutComboBox — exposes only what it accesses.
    // The `layouts` property binding auto-generates a `layoutsChanged` signal,
    // which LayoutComboBox's Connections target listens for.
    readonly property QtObject _layoutBridge: QtObject {
        readonly property string defaultAutotileAlgorithm: appSettings.defaultAutotileAlgorithm
        readonly property var layouts: settingsController.layouts
        readonly property string defaultLayoutId: appSettings.defaultLayoutId
        // LayoutComboBox's effectiveDefaultLayoutId reads this on its
        // layoutFilter === 2 arm (the scrolling dropdown below is the one
        // filter-2 user in the app); without it the Default row resolved
        // `undefined` and drew no preview or name.
        readonly property string defaultScrollingTemplate: appSettings.defaultScrollingTemplate
        // LayoutComboBox's preview CategoryBadge reads `autoAssignAllLayouts` for
        // the global-auto-assign indicator; expose it so the Monitor State
        // dropdowns light it like the rest of the app.
        readonly property bool autoAssignAllLayouts: appSettings.autoAssignAllLayouts
    }

    property var _screenStates: []
    property string _selectedScreen: ""
    property int _revision: 0
    // Prefix the layout list uses to namespace tiling algorithms apart from
    // zone layouts. Named so the id surgery below never restates its length.
    readonly property string _autotilePrefix: "autotile:"
    // The reserved scrolling-template word meaning "explicitly no template",
    // as opposed to an empty slot, which inherits the configured default.
    // Spelled here the way _autotilePrefix spells its own token; the
    // authoritative declaration is PhosphorZones::NoScrollingTemplate in
    // AssignmentEntry.h.
    readonly property string _noTemplateToken: "none"
    // The reserved word for the snapping/autotile opt-out, meaning
    // "explicitly no layout" or "explicitly no algorithm" for this context.
    // The same spelling as the template token but named separately because
    // it hardcodes different C++ declarations: PhosphorZones::NoSnappingLayout
    // and NoTilingAlgorithm in AssignmentEntry.h.
    readonly property string _noLayoutToken: "none"
    // Aspect ratio of the currently selected screen (for layout preview).
    // With no selection yet, fall back to the SHOWN state's own screen — the
    // same object _currentState returns and _stageCurrentState writes against.
    // Falling back to screens[0] instead let the preview take its shape from a
    // different monitor than the one whose state the page is displaying,
    // because the two lists are ordered independently.
    readonly property real _selectedScreenAspectRatio: {
        void _revision;
        void _screenStates;
        var screens = settingsController.screens;
        var target = _selectedScreen;
        if (!target) {
            var state = _currentState();
            target = state ? (state.screenId || "") : "";
        }

        if (!target)
            return 0;

        // Same two-pass, physical-id-tolerant match _currentState uses, so the
        // preview takes its shape from the screen whose state is on display
        // even when the two lists name it differently (a virtual child on one
        // side, its physical parent on the other). Exact matches win.
        var exact = _screenAspectRatioFor(screens, target, false);
        return exact > 0 ? exact : _screenAspectRatioFor(screens, target, true);
    }

    // Aspect ratio of the screen in `screens` matching `target`, or 0 when
    // none does. With @p byPhysicalId the comparison runs on both sides'
    // physical ids (extractPhysicalId is idempotent for an already-physical
    // id, so this also matches a physical selection against a virtual child).
    function _screenAspectRatioFor(screens, target, byPhysicalId) {
        var wanted = byPhysicalId ? settingsController.physicalScreenId(target) : target;
        for (var i = 0; i < screens.length; i++) {
            var nm = screens[i].name || "";
            var candidate = byPhysicalId ? settingsController.physicalScreenId(nm) : nm;
            if (!candidate || candidate !== wanted)
                continue;

            var w = screens[i].width || 0;
            var h = screens[i].height || 0;
            if (w > 0 && h > 0)
                return w / h;
        }
        return 0;
    }

    // Re-read the per-screen states. BLOCKING D-Bus, so it is gated on
    // visibility: the page host keeps visited pages alive and merely hides
    // them, and the five Connections handlers below fire for events that
    // change nothing a hidden page is showing. onVisibleChanged is the
    // catch-up, so a page coming back is never stale.
    function _refresh() {
        if (!visible)
            return;

        _screenStates = settingsController.getScreenStates();
        _revision++;
    }

    function _currentState() {
        var target = _selectedScreen;
        // No selection yet — fall back to the first reported state.
        if (!target)
            return _screenStates.length > 0 ? _screenStates[0] : null;

        for (var i = 0; i < _screenStates.length; i++) {
            if (_screenStates[i].screenId === target)
                return _screenStates[i];
        }
        // Same physical-id tolerance _screenStillPresent applies, so a
        // selection naming a physically-present output still resolves to the
        // state reported for one of its virtual children. Exact matches win,
        // hence the second pass.
        //
        // Both sides are reduced to their physical id, not just the state's:
        // a one-sided reduction only tolerated a physical SELECTION against a
        // virtual state, and left the mirror case (a virtual child selected
        // while the daemon reports the physical parent) resolving to null with
        // the "unable to retrieve monitor state" warning on a live screen.
        // extractPhysicalId is idempotent for an already-physical id, so the
        // symmetric compare still matches everything the one-sided one did.
        var wanted = settingsController.physicalScreenId(target);
        for (var j = 0; j < _screenStates.length; j++) {
            var sid = _screenStates[j].screenId || "";
            if (sid && settingsController.physicalScreenId(sid) === wanted)
                return _screenStates[j];
        }
        // The selected screen has no reported state. Return null so the
        // "Unable to retrieve monitor state" warning shows, rather than
        // displaying (and staging against) another monitor's context.
        return null;
    }

    function _findLayout(layoutId) {
        if (!layoutId || !_layoutBridge.layouts)
            return null;

        for (var i = 0; i < _layoutBridge.layouts.length; i++) {
            if (_layoutBridge.layouts[i].id === layoutId)
                return _layoutBridge.layouts[i];
        }
        return null;
    }

    // Resolve what one slot (layout or algorithm) carries into a staged entry.
    // Lossless mode toggling: every staged entry carries the SIBLING mode's
    // pick too. The daemon rebuilds the whole context rule from the entry, so
    // staging an empty sibling field would drop a stored layout/algorithm for
    // good — pin to Scrolling and back and the zone layout would be gone. An
    // explicit "Default" pick (@p cleared) deliberately carries empty so the
    // sibling keeps following the global default.
    // Only a TOUCHED slot carries its local value (@p localId). An untouched
    // slot falls back to the daemon's assignment (@p resolvedId) and carries
    // it only when the daemon marked it explicit (@p explicitFlag), so a pure
    // mode switch never freezes a cascade default into an explicit
    // assignment. The local ids cannot stand in for that test: they are
    // pre-filled from the RESOLVED state, so they always look like a pick. A
    // daemon too old to report the marker leaves it undefined, and there the
    // existing assignment is preserved rather than silently dropped.
    function _carrySibling(cleared, touched, localId, explicitFlag, resolvedId) {
        if (cleared)
            return "";

        if (touched)
            return localId;

        if (explicitFlag === undefined || explicitFlag)
            return resolvedId || "";

        return "";
    }

    // Stage the current local state for the selected screen (flushed on Apply).
    // Uses setAssignmentEntry targeting the exact (screen, desktop, activity)
    // context from getScreenStates — most specific context wins.
    function _stageCurrentState() {
        var state = stateView.screenState;
        if (!state)
            return;

        // Key the write off the STATE, exactly as the staged-entry read does
        // (the read at onScreenStateChanged uses screenState.screenId).
        // _selectedScreen must NOT win here: when the state resolved through
        // the physical-id fallback pass, the selection holds a physical id
        // while the state holds the virtual child id, and an entry staged
        // under the selection would never be read back. Deriving both keys
        // from the same object makes read and write agree by construction.
        var target = state.screenId || "";
        if (!target)
            return;

        var desktop = state.virtualDesktop || 0;
        var activity = state.activity || "";
        var snapping = "";
        var tiling = "";
        // The RESOLVED layoutId cannot carry the explicit-none state (the
        // reserved word resolves to no layout, so the daemon reports an
        // empty layoutId beside a raw snappingLayoutId of "none"). Carrying
        // the resolved value there would silently flatten the opt-out back
        // to "inherit the default" on the next mode toggle, so the raw slot
        // wins exactly when it holds the token.
        var resolvedSnapping = (state.snappingLayoutId === root._noLayoutToken) ? root._noLayoutToken : state.layoutId;
        // See _carrySibling for the rule both slots follow.
        var siblingSnapping = root._carrySibling(stateView.localLayoutCleared, stateView.localLayoutTouched, stateView.localLayoutId, state.layoutIdExplicit, resolvedSnapping);
        var siblingAlgo = root._carrySibling(stateView.localAlgorithmCleared, stateView.localAlgorithmTouched, stateView.localAlgorithmId, state.algorithmIdExplicit, state.algorithmId);
        var siblingTiling = siblingAlgo ? root._autotilePrefix + siblingAlgo : "";
        if (stateView.isScrolling) {
            // Scrolling's activeLayoutId is the bare sentinel: the entry
            // carries the mode plus both preserved sibling fields. Its
            // TEMPLATE rides a separate assignment slot that setAssignmentEntry
            // does not carry, so the template dropdown stages through its own
            // call below and an untouched dropdown stages nothing at all,
            // leaving the daemon's slot exactly as it found it.
            settingsController.stageAssignmentEntry(target, desktop, activity, stateView.localMode, siblingSnapping, siblingTiling);
            if (stateView.localTemplateTouched)
                settingsController.stageScrollingTemplate(target, desktop, activity, stateView.localTemplateId);
            return;
        }
        if (stateView.isTiling) {
            // An explicit "Default" pick clears the algorithm slot. Otherwise
            // stage the user's pick, else the currently-resolved algorithm.
            // _carrySibling's rule, for the same reason as the layout slot
            // below: an untouched algorithm carries the resolved value only
            // when the daemon marked it explicit, so a pure mode toggle cannot
            // freeze the global default algorithm onto this screen.
            var algoId = siblingAlgo;
            if (!algoId) {
                // Nothing to pin — either the user picked "Default", or
                // nothing resolved (fresh config, or the context suppresses
                // the default). Stage a MODE-ONLY entry, exactly like the
                // Snapping and Scrolling branches. The wire is mode=1 with
                // an EMPTY algorithm id (the adaptor only validates
                // non-empty ids), so the switch commits, the slot is cleared
                // without touching the mode pin or the sibling, the
                // algorithm keeps FOLLOWING the global default (never frozen
                // to today's value), and the combo honestly keeps showing
                // "Default".
                settingsController.stageAssignmentEntry(target, desktop, activity, stateView.localMode, siblingSnapping, "");
                return;
            }

            tiling = root._autotilePrefix + algoId;
            snapping = siblingSnapping;
        } else {
            // Same rule the SIBLING slot follows (see _carrySibling): an
            // untouched slot carries the daemon's resolved value only when the
            // daemon marked it EXPLICIT. Falling back to state.layoutId
            // unconditionally froze a cascade default into an explicit
            // assignment — toggle Snapping to Scrolling and back, hit Apply,
            // and the screen now pins today's global default and stops
            // following it. The entered mode is not exempt from that rule.
            var layoutId = siblingSnapping;
            if (!layoutId) {
                // Nothing to pin — either the user picked "Default", or
                // nothing resolved (the context suppresses the default
                // layout, so state.layoutId is empty). The MODE change must
                // still commit — leaving from Scrolling used to fall into an
                // unstage here and silently drop the switch while the button
                // group showed Snapping. Stage a mode-only entry; it clears
                // the layout slot while keeping the mode pin and the
                // sibling, and the daemon accepts a bare mode exactly as it
                // does for Scrolling above.
                settingsController.stageAssignmentEntry(target, desktop, activity, stateView.localMode, "", siblingTiling);
                return;
            }

            snapping = layoutId;
            tiling = siblingTiling;
        }
        settingsController.stageAssignmentEntry(target, desktop, activity, stateView.localMode, snapping, tiling);
    }

    contentHeight: content.implicitHeight
    clip: true
    // Pick the monitor BEFORE the first state read. _autoSelectScreen reads
    // only settingsController.screens, so it needs nothing from _refresh, and
    // selecting afterwards moved the shown state a second time — a second
    // blocking round of the staged-entry and strip-preview reads at startup.
    Component.onCompleted: {
        if (!_selectedScreen && settingsController.screens.length > 0)
            _autoSelectScreen();

        _refresh();
    }

    // The recovery half of the visibility gates below: any event a hidden
    // cached page skipped (a global Discard, a dirty-set flip) is caught up
    // in one re-read when the page comes back.
    onVisibleChanged: {
        if (visible)
            _refresh();
    }

    // Auto-select primary monitor, fallback to first.
    function _autoSelectScreen() {
        var screens = settingsController.screens || [];
        for (var i = 0; i < screens.length; i++) {
            if (screens[i].isPrimary) {
                _selectedScreen = screens[i].name || "";
                return;
            }
        }
        if (screens.length > 0)
            _selectedScreen = screens[0].name || "";
    }

    // True if `id` is still a connected output (physical-id aware, so a
    // physically-present screen with virtual children still matches).
    function _screenStillPresent(id) {
        var arr = settingsController.screens || [];
        for (var i = 0; i < arr.length; i++) {
            var nm = arr[i].name || "";
            if (nm === id || settingsController.physicalScreenId(nm) === id)
                return true;
        }
        return false;
    }

    Connections {
        target: settingsController

        function onScreensChanged() {
            // Drop a selection whose output was unplugged, then re-pick.
            if (root._selectedScreen !== "" && !root._screenStillPresent(root._selectedScreen))
                root._selectedScreen = "";
            if (root._selectedScreen === "" && settingsController.screens.length > 0)
                root._autoSelectScreen();
            root._refresh();
        }

        function onLayoutsChanged() {
            // _layoutBridge.layouts is bound to settingsController.layouts and refreshes on
            // its own layoutsChanged; only the dependent view needs a nudge.
            root._refresh();
        }

        function onVirtualDesktopsChanged() {
            root._refresh();
        }

        function onActivitiesChanged() {
            root._refresh();
        }

        function onScreenLayoutChanged() {
            root._refresh();
        }

        // A global Discard or a factory reset drops the staged entries without
        // touching this page's local mode/layout state, so the discarded pick
        // stayed on screen and the touched flags carried it straight back into
        // the next staged write. Re-reading the states re-fires
        // onScreenStateChanged, which re-initializes both the local values and
        // the flags from whatever survived. Same handler VirtualScreensPage
        // uses for the same reason.
        function onDirtyPagesChanged() {
            root._refresh();
        }

        // The empty-strip sketch is drawn along the selected screen's resolved
        // strip axis, and a per-monitor StripAxis override changes that answer
        // without touching anything else this page reads.
        function onPerScreenOverridesChanged() {
            root._revision++;
        }

        // The daemon going away mid-session leaves the last read on screen
        // with nothing saying it is stale, and coming back leaves it stale the
        // other way. Either transition is worth one re-read (which _refresh
        // skips while hidden, like every other read here).
        function onDaemonRunningChanged() {
            root._refresh();
        }
    }

    ColumnLayout {
        id: content

        width: parent.width
        spacing: Kirigami.Units.largeSpacing

        Kirigami.InlineMessage {
            Layout.fillWidth: true
            type: Kirigami.MessageType.Information
            text: i18n("View and change the active mode and layout for each monitor.")
            // Kirigami.InlineMessage defaults to visible: false.
            visible: true
        }

        // Monitor picker (spatial map; always a specific monitor, no "All")
        DisplayMap {
            Layout.fillWidth: true
            appSettings: settingsController
            showAll: false
            physicalOnly: false
            selectedScreenName: root._selectedScreen
            // An entry with no name would clear the selection, which this page
            // reads as "no pick yet" and answers by showing the first reported
            // state — silently moving the user to another monitor. Ignore it
            // and keep the current pick.
            onScreenPicked: name => {
                if (name)
                    root._selectedScreen = name;
            }
        }

        // Nothing to configure, and nothing to blame the daemon for: the map
        // above draws an empty area and the state warning below would
        // misattribute it.
        Kirigami.InlineMessage {
            Layout.fillWidth: true
            type: Kirigami.MessageType.Information
            text: i18n("No monitors are connected.")
            visible: settingsController.screens.length === 0
        }

        // Daemon offline / no state message
        Kirigami.InlineMessage {
            Layout.fillWidth: true
            type: Kirigami.MessageType.Warning
            text: i18n("Unable to retrieve monitor state. Make sure the daemon is running.")
            // daemonRunning covers the daemon dying with a last good read still
            // on screen: the states survive the death, so screenState alone
            // would keep the page looking live.
            visible: settingsController.screens.length > 0 && (stateView.screenState === null || !settingsController.daemonRunning)
        }

        // Current state for selected monitor
        ColumnLayout {
            id: stateView

            // The two `void` reads state the binding's dependencies at the
            // binding site. QML does capture property reads made inside a
            // plain function the binding calls (only an INVOKABLE C++ call is
            // opaque to it), so _currentState() already carries them; naming
            // them here keeps the trigger visible and keeps the binding
            // correct if the reads ever move out of that function.
            // _revision is the refresh counter and is not read there at all.
            property var screenState: {
                void root._revision;
                void root._selectedScreen;
                return root._currentState();
            }
            // Local state — initialized from D-Bus, changed by user
            property int localMode: 0
            property string localLayoutId: ""
            property string localAlgorithmId: ""
            // True after the user explicitly picks "Default" in the layout
            // selector. Tracked separately from localLayoutId because the
            // combo reports Default as an empty value, which is otherwise
            // indistinguishable from the not-yet-touched state.
            property bool localLayoutCleared: false
            // Same tracking for the algorithm selector's "Default" pick: the
            // combo reports it as an empty value, indistinguishable from the
            // not-yet-touched state without this flag.
            property bool localAlgorithmCleared: false
            // True once this session has actually assigned the slot — a
            // selector pick, a toggle into the slot's mode, or a restored
            // staged entry. localLayoutId / localAlgorithmId are pre-filled
            // from the RESOLVED state, so they can never answer "did the
            // user set this?" on their own; the sibling carry in
            // _stageCurrentState needs that answer to avoid freezing a
            // cascade default into an explicit assignment.
            property bool localLayoutTouched: false
            property bool localAlgorithmTouched: false
            property bool isTiling: localMode === 1
            // The scrolling engine is mode 2. It picks neither a layout nor an
            // algorithm, so the preview, the selectors and the staged entry all
            // branch on this instead of treating "not tiling" as snapping.
            property bool isScrolling: localMode === 2
            property bool isSnapping: localMode === 0
            // The scrolling template governing this screen, by name. EMPTY
            // when the screen has none of its own: the daemon reports the RAW
            // assignment field here rather than the resolved one (see the
            // adaptor's getScreenStates), so an empty string means "follows
            // the default template", never "no template applies". The preview
            // title and the message below it both branch on that distinction,
            // and neither may claim a screen is template-free.
            readonly property string scrollingTemplateName: (screenState && screenState.scrollingTemplateName) || ""
            // The raw slot beside the resolved name, so the note can tell the
            // explicit-none token from an empty slot. The name is empty for
            // both, which is exactly the pair that must not read alike.
            readonly property string scrollingTemplateId: (screenState && screenState.scrollingTemplateId) || ""
            // Template seed progress, refreshed by refreshScrollingStrip on the
            // live beat. Zero total means "nothing to say" and covers all three
            // silent cases at once (not scrolling, no blueprint, daemon down),
            // so the readout below is gated on it rather than on isScrolling:
            // a screen whose template declares no starting columns has no
            // progress to report either.
            property int blueprintTotal: 0
            property int blueprintUsed: 0
            // The template slot the dropdown edits. Three values, matching the
            // assignment field: a template UUID, "" (inherit the configured
            // default) and root._noTemplateToken (explicitly none). Touched is
            // tracked separately because "" is a legitimate pick, so it cannot
            // double as "untouched".
            property string localTemplateId: ""
            property bool localTemplateTouched: false
            // Resolved layout object for LayoutThumbnail
            property var currentLayout: root._findLayout(localLayoutId)
            // One preview box shape for all three thumbnails, and the width the
            // messages below them are clamped to, so the column reads as a
            // single stack instead of three independently sized cards.
            //
            // The width budget is capped to what the page actually offers: the
            // thumbnails size themselves from these bounds and are centred, not
            // filled, so at the window's minimum width a fixed budget drew a
            // box wider than the page and clipped it on both sides.
            readonly property real _previewHeight: Kirigami.Units.gridUnit * 14
            readonly property real _previewMaxWidth: Math.max(Kirigami.Units.gridUnit * 6, Math.min(Kirigami.Units.gridUnit * 32, content.width - _previewChromeWidth))
            // Side padding LayoutThumbnail reserves around its preview box
            // (1 gridUnit per side). The messages clamp to box + chrome so
            // their edges line up with the thumbnail's, not with the box
            // inside it.
            readonly property real _previewChromeWidth: Kirigami.Units.gridUnit * 2
            readonly property real _messageMaxWidth: _previewMaxWidth + _previewChromeWidth
            // Settle beat after an event-driven read came back empty, and the
            // slow beat the live preview polls on. Named so the comments below
            // can talk about them without restating the numbers.
            readonly property int _stripSettleIntervalMs: 400
            readonly property int _stripLiveIntervalMs: 2000
            // Live strip zones for the scrolling preview, refreshed with the
            // selection context. Empty when the screen is not scrolling right
            // now (mode staged but not applied, no windows, daemon down).
            // Deliberately retained while the page is hidden rather than
            // cleared: stripLiveRefresh's triggeredOnStart shot re-reads when
            // the page comes back, so at worst the previous array is on
            // screen for the one queued-event beat before that shot lands,
            // and dropping it on hide would only cost an extra read there.
            // onScreenStateChanged clears it whenever the selected context's
            // daemon mode is not scrolling, so what is retained is always
            // THIS context's last strip.
            property var scrollingStripZones: []
            // Which way the selected screen's strip runs, resolved through the
            // same ladder the engine walks (per-screen override, global value,
            // then the Auto rule). The invokable is opaque to the binding, so
            // the global setting is read here explicitly and the per-monitor
            // override arrives through the _revision bump on
            // perScreenOverridesChanged.
            readonly property bool stripVertical: {
                void root._revision;
                void appSettings.scrollingStripAxis;
                // The shown-state fallback _selectedScreenAspectRatio uses,
                // for the same reason: an empty selection (or one naming the
                // other list's spelling) would miss the override AND make the
                // Auto arm resolve against no screen, sketching a horizontal
                // strip beside a preview box that already shows portrait.
                var target = root._selectedScreen;
                if (!target) {
                    var state = root._currentState();
                    target = state ? (state.screenId || "") : "";
                }
                return settingsController.scrollingStripVerticalForScreen(target);
            }
            // Representative endless strip: a full column mid-view with a
            // clipped column at each edge, so the sketch reads as a window
            // onto a longer strip rather than a fixed zone layout. The three
            // fractions and their transposition are the twin of
            // StripZones::sketchRects (src/daemon/daemon/stripzones.h), so the
            // Monitors page and the OSD card draw the same shape for the same
            // empty strip. On a vertical strip the same three bands stack
            // instead of sitting in a row: drawing the horizontal sketch there
            // would depict a direction that screen never takes.
            readonly property var scrollingFallbackZones: {
                const spans = [
                    {
                        "offset": 0,
                        "extent": 0.1
                    },
                    {
                        "offset": 0.115,
                        "extent": 0.5
                    },
                    {
                        "offset": 0.63,
                        "extent": 0.37
                    }
                ];
                const vertical = stateView.stripVertical;
                // Scoped to the screen, 1-based, the way StripZones::
                // sketchZoneMaps namespaces its own ids. Every monitor's
                // sketch drew the same three ids otherwise, which is only
                // harmless as long as nothing downstream keys on zone id.
                const state = stateView.screenState;
                const screenId = state ? (state.screenId || "") : "";
                return spans.map(function (span, index) {
                    return {
                        "id": "strip:" + screenId + ":fallback:" + (index + 1),
                        "name": "",
                        "useCustomColors": false,
                        "relativeGeometry": {
                            "x": vertical ? 0 : span.offset,
                            "y": vertical ? span.offset : 0,
                            "width": vertical ? 1 : span.extent,
                            "height": vertical ? span.extent : 1
                        }
                    };
                });
            }

            // Fetch the live strip for the current selection. The daemon's
            // strip is briefly empty while a mode flip's re-announce batch is
            // adopted (the OSD defers around the same window); a one-shot read
            // landing there returned [] and left the fallback sketch up for
            // good, so an empty strip on a scrolling screen re-reads once.
            // allowRetry MUST be false when called FROM the settle timer. A
            // re-arm from inside its own handler is an unbounded loop: a
            // scrolling screen with no windows legitimately reports an empty
            // strip forever, and the page outlives its visibility (the host
            // keeps visited pages alive and merely hides them), so nothing
            // would stop it — a blocking round trip every settle interval on a
            // page nobody is looking at. Every call site passes the flag
            // explicitly, so none can drift into retrying by omission.
            function refreshScrollingStrip(allowRetry) {
                if (!screenState)
                    return;
                // The read is a BLOCKING D-Bus round trip, so it must not run
                // for a page the user cannot see. The host keeps visited pages
                // alive and merely hides them, and screenState changes on every
                // hotplug / desktop / activity event, so without this a stack
                // of cached Monitors pages would each block the GUI thread on
                // events that change nothing they show. The live timer is the
                // SINGLE caller: triggeredOnStart makes its running transition
                // (visibility and mode both) fire one read immediately rather
                // than leaving the preview stale for a beat, and a selection
                // change nudges it with restart() rather than reading again.
                if (!stateView.visible)
                    return;
                var fresh = settingsController.getScrollingStripPreview(screenState.screenId || "");
                // Assign only on a real change. The reply is a fresh
                // QVariantList every tick, so a plain assignment always
                // signals, and a JS-array model has no diffing: every beat
                // rebuilt the whole delegate tree for an identical strip.
                if (!_stripMatches(fresh, scrollingStripZones))
                    scrollingStripZones = fresh;
                // The settle beat covers the case where the mode just flipped
                // and the re-announce batch briefly reports an empty strip: one
                // _stripSettleIntervalMs shot beats waiting out the live beat.
                // It cannot compound — the retry passes allowRetry=false, so a
                // retry never arms another, and restart() on a one-shot Timer
                // replaces any pending fire. Gated on isScrolling too, matching
                // the live timer: with Snapping staged locally the thumbnail is
                // hidden, and arming a blocking read for it is the poll that
                // gate exists to prevent.
                if (allowRetry && stateView.isScrolling && fresh.length === 0 && (screenState.mode || 0) === 2)
                    stripSettleRetry.restart();
                // Template seed progress on the SAME beat, behind the same
                // visibility gate. One more blocking round trip is the right
                // trade: the daemon answers two ints off state it already
                // holds, and a timer of its own would double the wakeups for a
                // value that cannot change without the strip changing. An
                // empty reply (not scrolling, no blueprint, daemon down)
                // leaves both at zero, which suppresses the line below.
                const progress = settingsController.getScrollingBlueprintProgress(screenState.screenId || "");
                blueprintTotal = (progress && progress.total) || 0;
                blueprintUsed = (progress && progress.used) || 0;
            }

            // True when two strip replies would render identically. Compares
            // what the thumbnail actually draws: the tile count, each tile's
            // number, its geometry rounded to three decimals (relative 0–1
            // values, so that survives D-Bus float round-trips), and the tab
            // indicator its column draws. Same fingerprint-before-assign shape
            // LayoutComboBox uses. The tab fields count because switching tabs
            // moves no rect at all, so a geometry-only compare would hold the
            // stale pills up forever — at the accepted cost of rebuilding every
            // delegate for a switch that moved nothing, a JS-array model having
            // no diffing. The four tab keys are spelled raw here (QML cannot
            // reach PhosphorProtocol's constants), so a rename turns this
            // compare into undefined-vs-undefined and stops the repaint.
            function _stripMatches(a, b) {
                if (!a || !b || a.length !== b.length)
                    return false;

                for (var i = 0; i < a.length; i++) {
                    if (a[i].zoneNumber !== b[i].zoneNumber)
                        return false;

                    var ga = a[i].relativeGeometry || ({});
                    var gb = b[i].relativeGeometry || ({});
                    if (Math.round((ga.x || 0) * 1000) !== Math.round((gb.x || 0) * 1000) || Math.round((ga.y || 0) * 1000) !== Math.round((gb.y || 0) * 1000))
                        return false;

                    if (Math.round((ga.width || 0) * 1000) !== Math.round((gb.width || 0) * 1000) || Math.round((ga.height || 0) * 1000) !== Math.round((gb.height || 0) * 1000))
                        return false;

                    if ((a[i].tabCount || 0) !== (b[i].tabCount || 0) || (a[i].activeTab || 0) !== (b[i].activeTab || 0) || (a[i].tabPosition || 0) !== (b[i].tabPosition || 0) || Math.round((a[i].tabLength || 0) * 1000) !== Math.round((b[i].tabLength || 0) * 1000))
                        return false;
                }
                return true;
            }

            Timer {
                id: stripSettleRetry
                interval: stateView._stripSettleIntervalMs
                repeat: false
                onTriggered: stateView.refreshScrollingStrip(false)
            }

            // The daemon's strip wake-up, coalesced. stripChanged relays
            // placement changes, so a drag fires it per step and each hit taken
            // straight to the read would be a blocking D-Bus call on the GUI
            // thread. One shot once the burst settles is all a thumbnail needs.
            // That is the best case, not the bound: a drip spaced just over the
            // interval fires every time, so the worst case is one read (two
            // blocking round trips) per interval, paid on the visible page.
            Timer {
                id: stripEdgeCoalesce
                interval: stateView._stripSettleIntervalMs
                repeat: false
                // Through the live timer, never a direct read: it owns EVERY
                // strip read, and restart() gives one immediate shot via
                // triggeredOnStart while pushing the periodic beat out.
                // Guarded exactly like the context nudge in
                // onScreenStateChanged. restart() on a STOPPED timer would
                // START it, and the `running` binding below does not re-assert
                // until a dependency changes, so a wake-up on a hidden page
                // would leave it beating against its own gate; a pending start
                // shot already covers this context, so restarting on top of one
                // just queues a second blocking read. Dropping the wake-up
                // costs nothing — a stopped timer re-reads through
                // triggeredOnStart once its gate lets it run again. This is
                // also the only visibility test on the wake-up path, since the
                // Connections below has none of its own.
                onTriggered: {
                    if (stripLiveRefresh.running && !stripLiveRefresh._startShotPending)
                        stripLiveRefresh.restart();
                }
            }

            Connections {
                target: settingsController

                // Only the screen on show: the daemon wakes every screen whose
                // strip moves, and reading for a monitor nobody is looking at
                // is the blocking call this page's gating exists to prevent.
                // Plain equality, unlike the physical-id-tolerant matches
                // elsewhere here, because both sides are the daemon's own
                // screen id — the read sends this same one straight back.
                function onScrollingStripChanged(screenId) {
                    if (!stateView.screenState || screenId !== (stateView.screenState.screenId || ""))
                        return;
                    stripEdgeCoalesce.restart();
                }
            }

            // The strip is live state: the user can open a window, widen a
            // column, or tab two together while this page is up. The wake-up
            // above turns that into a prompt re-read; this beat is the backstop
            // under it, for what the signal misses (an emission while the
            // daemon was down, a daemon too old to send one, a relayout that
            // left the view anchor put). It runs only while the page is on
            // screen showing a scrolling monitor.
            Timer {
                id: stripLiveRefresh
                interval: stateView._stripLiveIntervalMs
                repeat: true
                // Gated on isScrolling, not just the daemon's mode: with the
                // daemon scrolling and Snapping staged locally the thumbnail is
                // hidden, and polling for something nobody is looking at is the
                // one thing a blocking call must not do. daemonRunning is part
                // of the gate too: with the daemon gone every tick is a 500ms
                // timeout on the GUI thread, forever, for a preview that
                // cannot change.
                running: settingsController.daemonRunning && stateView.visible && stateView.isScrolling && stateView.screenState !== null && (stateView.screenState.mode || 0) === 2
                // Fire on the running transition too, so coming back to a
                // hidden-then-shown page (or picking Scrolling in the mode
                // toggle) repaints from one fresh read. This timer owns EVERY
                // strip read: a second path meant two blocking reads in one
                // frame on every such transition.
                triggeredOnStart: true
                // True from the moment the timer starts until the shot
                // triggeredOnStart queues for that start has run. It marks the
                // one tick following an event (a mode flip, the page coming
                // back, a selection change) as opposed to the periodic beats,
                // and only that tick may arm the settle retry: the settle beat
                // exists for a re-announce batch briefly reporting an empty
                // strip, while a legitimately empty one would otherwise have
                // every beat arm another blocking read. It also tells
                // onScreenStateChanged that a start's shot is already pending,
                // so nudging there would queue a second read for one frame.
                property bool _startShotPending: false

                onRunningChanged: _startShotPending = running
                onTriggered: {
                    var allowRetry = _startShotPending;
                    _startShotPending = false;
                    stateView.refreshScrollingStrip(allowRetry);
                }
            }

            Layout.alignment: Qt.AlignHCenter
            spacing: Kirigami.Units.largeSpacing
            visible: screenState !== null
            onScreenStateChanged: {
                if (!screenState)
                    return;

                var desktop = screenState.virtualDesktop || 0;
                var activity = screenState.activity || "";
                // Selection context changed — drop any pending explicit
                // "Default" pick; the state below re-initializes from
                // staged or daemon values.
                localLayoutCleared = false;
                localAlgorithmCleared = false;
                localLayoutTouched = false;
                localAlgorithmTouched = false;
                // Keyed on the STATE's own screen id: before the user clicks a
                // monitor, _selectedScreen is still empty while the state being
                // shown is the first reported one, so reading the staged entry
                // for the empty id would show the wrong monitor's pending edit.
                var staged = settingsController.getStagedAssignment(screenState.screenId || "", desktop, activity);
                if (Object.keys(staged).length > 0) {
                    // A staged entry carries the WHOLE context rule, so every
                    // slot in it is authoritative: a missing id is a pending
                    // slot clear, not an absent opinion. Re-reading the
                    // daemon's resolved value for such a slot would hide the
                    // clear and re-carry the value on the next stage.
                    localMode = staged.mode !== undefined ? staged.mode : (screenState.mode || 0);
                    localLayoutId = staged.layoutId !== undefined ? staged.layoutId : "";
                    localAlgorithmId = staged.algorithmId !== undefined ? staged.algorithmId : "";
                    // A staged empty id collapses to key ABSENCE on the way in,
                    // so an absent key on a staged entry is the echo of a
                    // staged clear. getStagedAssignment's declaration in
                    // settingscontroller.h is the authoritative statement.
                    localLayoutCleared = staged.layoutId === undefined;
                    localAlgorithmCleared = staged.algorithmId === undefined;
                    // Both slots count as touched whenever an entry is staged,
                    // absent key or not. Deriving touched from presence made a
                    // staged clear lose its flag on the next refresh, and the
                    // slot then fell to _carrySibling's explicitFlag arm, which
                    // re-pinned the daemon's resolved value — the very value
                    // the user had just cleared. With touched set, the cleared
                    // slot carries its own empty local id instead.
                    localLayoutTouched = true;
                    localAlgorithmTouched = true;
                    // The template slot does NOT follow the whole-rule reading
                    // above. It is staged independently (stageScrollingTemplate
                    // writes only its own slot), so an absent key here means
                    // "not staged" rather than "staged clear", and the daemon's
                    // value is the honest fallback. Touched tracks presence for
                    // the same reason: an untouched slot must not be rewritten
                    // by an unrelated mode edit.
                    localTemplateTouched = staged.scrollingTemplateId !== undefined;
                    localTemplateId = localTemplateTouched ? staged.scrollingTemplateId : (screenState.scrollingTemplateId || "");
                } else {
                    // No staged changes — use daemon state. The layout slot
                    // seeds from the RAW field exactly when it holds the
                    // explicit-none word: the resolved layoutId is empty for
                    // that state, and seeding empty would seat the selector
                    // on "Default" while the daemon holds the opt-out. The
                    // algorithm slot needs no such split — algorithmId is the
                    // RESOLVED value, and the resolver passes the reserved
                    // word through verbatim rather than flattening it the way
                    // the snapping resolver does.
                    localMode = screenState.mode || 0;
                    localLayoutId = screenState.snappingLayoutId === root._noLayoutToken ? root._noLayoutToken : (screenState.layoutId || "");
                    localAlgorithmId = screenState.algorithmId || "";
                    localTemplateId = screenState.scrollingTemplateId || "";
                    localTemplateTouched = false;
                }

                // A context whose daemon mode is NOT scrolling can never be
                // handed a fresh read (the timer's running binding needs mode
                // 2), so the retained array from the PREVIOUS context must be
                // dropped here or a later local "Scrolling" pick on this
                // monitor renders the other monitor's tiles as its live strip.
                // The blueprint counters ride the same read and go with it:
                // left standing, monitor A's "2 of its 3 starting columns"
                // note appended itself to monitor B's explainer (zero total is
                // the nothing-to-say state). Not a read, so the
                // single-read-path rule holds.
                if ((screenState.mode || 0) !== 2) {
                    scrollingStripZones = [];
                    blueprintTotal = 0;
                    blueprintUsed = 0;
                } else {
                    // Nudge the live preview into re-reading for the new
                    // context. After the local state is initialized so the
                    // timer's gates (isScrolling) see the mode this handler
                    // just settled, and through the timer rather than a direct
                    // read so there is one read path. When the mode change
                    // itself started the timer, its start shot is already
                    // pending and a restart would queue a second blocking read.
                    if (stripLiveRefresh.running && !stripLiveRefresh._startShotPending)
                        stripLiveRefresh.restart();
                }
            }

            // The per-mode preview thumbnails (snapping layout, tiling
            // algorithm, scrolling strip) live in MonitorModePreviews.qml —
            // split out when this page crossed the file-size ceiling. Pure
            // view over the two handles it is given.
            MonitorModePreviews {
                Layout.alignment: Qt.AlignHCenter
                view: stateView
                page: root
            }

            // Mode toggle (below preview)
            SettingsButtonGroup {
                Layout.alignment: Qt.AlignHCenter
                Accessible.name: i18n("Placement mode")
                // Deliberately NOT gated on appSettings.autotileEnabled: an
                // assignment is durable state, and hiding the Tiling button
                // while a screen is already assigned Tiling would make that
                // state unrepresentable here. With the feature disabled the
                // router downgrades a screen carrying a tiling algorithm to
                // Snapping until it is re-enabled, and the algorithm is kept
                // either way. The one shape that keeps its declared mode is
                // the explicit no-algorithm state, which the router honors as
                // the user's standing choice rather than a transition.
                model: [i18nc("tiling mode name", "Snapping"), i18nc("tiling mode name", "Tiling"), i18nc("tiling mode name", "Scrolling")]
                currentIndex: stateView.localMode
                onIndexChanged: function (idx) {
                    stateView.localMode = idx;
                    // Nothing but the mode changes here. Neither cleared flag
                    // is reset: arrow-key traversal activates every option it
                    // passes through, so entering a mode is not evidence the
                    // user meant to abandon that mode's pending "Default" pick,
                    // and dropping the flag made the preview stop showing the
                    // default the pick resolves to.
                    //
                    // touched is deliberately NOT set either. A mode toggle is
                    // not a slot pick, and _carrySibling's touched arm returns
                    // localId — which is pre-filled from the RESOLVED value —
                    // so setting it made every toggle stage the cascade
                    // default as an explicit assignment. Left untouched, the
                    // slot falls to the explicitFlag arm: it carries only
                    // what the daemon marked explicit, so a pure
                    // Snapping-Scrolling-Snapping round trip leaves a
                    // default-following screen following the default.
                    root._stageCurrentState();
                }
            }

            // Layout selector (snapping)
            LayoutComboBox {
                id: snappingSelector

                Layout.alignment: Qt.AlignHCenter
                visible: stateView.isSnapping
                Accessible.name: i18n("Snapping layout")
                appSettings: root._layoutBridge
                currentLayoutId: stateView.localLayoutId
                layoutFilter: 0
                // The explicit opt-out row, same third state the template
                // selector below carries: Default inherits the configured
                // default layout, None uses no layout at all.
                showExplicitNoneOption: true
                explicitNoneValue: root._noLayoutToken
                showPreview: true
                onActivated: function (idx) {
                    var entry = model[idx];
                    var id = entry ? (entry.value || "") : "";
                    stateView.localLayoutId = id;
                    stateView.localLayoutCleared = (id === "");
                    stateView.localLayoutTouched = true;
                    root._stageCurrentState();
                }
            }

            // An id the local layout list does not carry leaves the selector on
            // no row at all (currentIndex -1, an empty field), which reads as if
            // the monitor had no layout while the preview above names one. Say
            // what is actually assigned instead of showing a blank.
            Kirigami.InlineMessage {
                // Same fillWidth + maximum + alignment combination as the
                // scrolling explainer above.
                Layout.fillWidth: true
                Layout.alignment: Qt.AlignHCenter
                Layout.maximumWidth: stateView._messageMaxWidth
                type: Kirigami.MessageType.Information
                text: i18n("This monitor uses %1, which is not in your layout list.", (stateView.screenState && stateView.screenState.layoutName) || stateView.localLayoutId)
                // count > 2: while a layout fetch is in flight the model is
                // just the two leading rows (Default and the explicit None),
                // both present regardless of the fetched list, and every id
                // resolves to -1 — which is not a missing layout and must not
                // raise this alarm.
                visible: stateView.isSnapping && snappingSelector.currentIndex === -1 && snappingSelector.count > 2
            }

            // Algorithm selector (tiling)
            LayoutComboBox {
                id: tilingSelector

                Layout.alignment: Qt.AlignHCenter
                visible: stateView.isTiling
                Accessible.name: i18n("Tiling algorithm")
                appSettings: root._layoutBridge
                // The opt-out word is stored bare, like the algorithm ids,
                // but the explicit-none row is keyed by the bare word too —
                // prefixing it would build "autotile:none", which matches no
                // row and left the selector blank.
                currentLayoutId: stateView.localAlgorithmId === root._noLayoutToken ? root._noLayoutToken : (stateView.localAlgorithmId ? root._autotilePrefix + stateView.localAlgorithmId : "")
                layoutFilter: 1
                // Same third state as the snapping selector above: Default
                // inherits the configured default algorithm, None keeps the
                // screen in autotile mode with nothing tiling it.
                showExplicitNoneOption: true
                explicitNoneValue: root._noLayoutToken
                showPreview: true
                onActivated: function (idx) {
                    var entry = model[idx];
                    var id = entry ? (entry.value || "") : "";
                    if (id.startsWith(root._autotilePrefix))
                        stateView.localAlgorithmId = id.substring(root._autotilePrefix.length);
                    else
                        stateView.localAlgorithmId = id;
                    stateView.localAlgorithmCleared = (id === "");
                    stateView.localAlgorithmTouched = true;
                    root._stageCurrentState();
                }
            }

            // Same blank-selector case as the snapping hint above.
            Kirigami.InlineMessage {
                // Same fillWidth + maximum + alignment combination as the
                // scrolling explainer above.
                Layout.fillWidth: true
                Layout.alignment: Qt.AlignHCenter
                Layout.maximumWidth: stateView._messageMaxWidth
                type: Kirigami.MessageType.Information
                text: i18n("This monitor uses %1, which is not in your algorithm list.", (stateView.screenState && stateView.screenState.algorithmName) || stateView.localAlgorithmId)
                // Same in-flight-fetch guard as the snapping hint above
                // (count > 2: two always-present leading rows).
                visible: stateView.isTiling && tilingSelector.currentIndex === -1 && tilingSelector.count > 2
            }

            // Template selector (scrolling). Third in the same selector band as
            // its two siblings above, in the same order the previews are drawn,
            // so each mode's control sits where the other modes' controls do.
            // It writes scrollingTemplateLayout rather than the entry's layout
            // or algorithm, so it stages through its own call.
            LayoutComboBox {
                Layout.alignment: Qt.AlignHCenter
                visible: stateView.isScrolling
                Accessible.name: i18n("Scrolling template")
                appSettings: root._layoutBridge
                currentLayoutId: stateView.localTemplateId
                layoutFilter: 2
                // The third state this family needs and the other two do not.
                showExplicitNoneOption: true
                explicitNoneValue: root._noTemplateToken
                showPreview: true
                onActivated: function (idx) {
                    var entry = model[idx];
                    stateView.localTemplateId = entry ? (entry.value || "") : "";
                    // Touched even when the pick lands back on Default: that is
                    // a deliberate "inherit again", and without the flag the
                    // stage below would skip the slot and leave the previous
                    // template assigned.
                    stateView.localTemplateTouched = true;
                    root._stageCurrentState();
                }
            }

            // The scrolling family's message slot, holding an explainer rather
            // than the missing-entry alarm its two siblings carry: a template
            // is optional, so there is no "not in your list" state to warn
            // about, and what the mode does needs saying instead.
            Kirigami.InlineMessage {
                // fillWidth with a maximum and an alignment, not alignment
                // alone: an InlineMessage's implicitWidth collapses to its
                // padding, so without fillWidth the text rendered as a sliver.
                Layout.fillWidth: true
                Layout.alignment: Qt.AlignHCenter
                Layout.maximumWidth: stateView._messageMaxWidth
                type: Kirigami.MessageType.Information
                // The numbering sentence is only shown for a LIVE strip: the
                // placeholder sketch above is deliberately unnumbered, so
                // promising numbers over it describes something not on screen.
                // It also promises no more than Snap to Zone delivers — nine
                // digit shortcuts — and says windows are numbered rather than
                // that the numbers are legible, which they are not on a tile
                // too narrow to carry a label.
                // Two sentences joined at runtime rather than four whole
                // strings: the strip half and the template half vary
                // independently, and spelling out the product would leave
                // translators maintaining four near-identical paragraphs.
                // Each half is a complete sentence on its own.
                text: {
                    const strip = stateView.scrollingStripZones.length > 0 ? i18n("Scrolling mode arranges windows in resizable columns on an endless strip. It does not use a zone layout. Windows are numbered in the order they appear on screen, and Snap to Zone reaches the first nine.") : i18n("Scrolling mode arranges windows in resizable columns on an endless strip. It does not use a zone layout.");
                    // The DAEMON's four states, not the dropdown's three. The
                    // note describes what the screen is doing now, so it
                    // keeps reading the applied value until Save, the same
                    // way the preview card above it does.
                    //
                    // The fourth is the one the sibling families surface with
                    // a "not in your list" message: a context pinning an id
                    // the store no longer holds. A non-empty id with an empty
                    // name is exactly that state, and without its own branch
                    // it fell to the inherit-the-default arm and told the
                    // user the opposite of what the daemon was doing.
                    let template;
                    if (stateView.scrollingTemplateId === root._noTemplateToken)
                        template = i18n("This screen is set to use no template, so its columns follow the built-in width and height steps even if a default template is set.");
                    else if (stateView.scrollingTemplateName.length > 0)
                        template = i18n("This screen uses the %1 template, which sets the columns it starts with and the width and height presets the cycling shortcuts step through.", stateView.scrollingTemplateName);
                    else if (stateView.scrollingTemplateId.length > 0) {
                        template = i18n("This screen is pinned to a template that is no longer in your list, so its columns follow the built-in width and height steps.");
                        // The description above is chosen off the DAEMON's
                        // value and so stands until Save. The instruction
                        // below must not: the dropdown writes the LOCAL slot,
                        // so an ungated nudge went on telling the user to pick
                        // a replacement after they had just picked one. Every
                        // other arm here is descriptive, which is why only
                        // this one needs the gate.
                        if (!stateView.localTemplateTouched)
                            template += " " + i18n("Pick another template to replace it.");
                    } else
                        template = i18n("This screen has no template of its own, so it follows the default template from Scrolling → Templates.");
                    if (stateView.blueprintTotal <= 0)
                        return strip + " " + template;

                    // A starting column is spent once a column has taken it, so
                    // this counts up and stays there until the strip empties or
                    // the template changes. Saying so is the point of the line:
                    // it is the only place the "spent" rule is visible.
                    // Both phrasings read correctly whatever the counts are.
                    // "All %1 of its starting columns" and "%1 of its %2
                    // starting columns are in use" both broke at one, and
                    // i18np cannot help here because the varying number is
                    // not the only substitution.
                    // Named subject, not "It": this sentence is appended to
                    // whichever template arm ran, and every one of those ends
                    // on the template, so a bare pronoun read as the template
                    // having used the columns rather than the screen.
                    const seed = stateView.blueprintUsed >= stateView.blueprintTotal ? i18n("Every starting column is in use, so further columns open at the template's own width and display.") : i18n("This screen has used %1 of its %2 starting columns, and the rest shape the next columns you open.", stateView.blueprintUsed, stateView.blueprintTotal);
                    return strip + " " + template + " " + seed;
                }
                visible: stateView.isScrolling
            }
        }
    }
}
