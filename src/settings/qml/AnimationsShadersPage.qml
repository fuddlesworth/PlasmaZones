// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

/**
 * @brief Animations → Shaders — installed shader effect browser.
 *
 * Read-only listing. Per-event shader assignment lives in each event
 * card (AnimationEventCard's shader picker section); this page exists
 * so users can survey what's installed, see parameter metadata, and
 * jump to the user shader directory to drop in their own packs.
 *
 * ## Layout
 *
 *   • User shaders card — drop zone for installing user shader packs +
 *     "Open Folder" button (matches the layouts / autotile-algorithms
 *     toolbar pattern; no path label — the file manager surfaces the
 *     location far better than a truncated string).
 *   • Filter bar — text search + per-category multi-select pills +
 *     built-in / user toggles.
 *   • Installed shaders card — packs grouped by category as a card grid;
 *     each section headed with the category name and the count.
 *     Card click opens AnimationsShaderDetailDialog for the full view
 *     (preview, description, "Used in:" reverse-lookup, parameter list).
 */
Flickable {
    id: root

    // Loaded from a Q_INVOKABLE; Connections below manually refreshes it
    // on `shaderEffectsChanged`. Mirrors the picker pattern in
    // AnimationEventCard (Q_INVOKABLE results aren't reactive across the
    // QML binding boundary).
    property var effectList: settingsController.animationsPage.availableShaderEffects()
    // ── Filter state ────────────────────────────────────────────────────
    property string filterText: ""
    /// Map of `{ categoryName: true }`. Empty = show all categories.
    /// Toggling any pill makes the map non-empty and switches into "show
    /// only the explicitly-selected categories" mode.
    property var selectedCategories: ({
    })
    property bool showBuiltIn: true
    property bool showUser: true
    readonly property bool _hasActiveFilters: filterText.length > 0 || Object.keys(selectedCategories).length > 0 || !showBuiltIn || !showUser
    // ── Derived: category index (sorted, with counts) ───────────────────
    readonly property var _allCategories: {
        var counts = {
        };
        for (var i = 0; i < effectList.length; i++) {
            var cat = (effectList[i] && effectList[i].category) ? effectList[i].category : "";
            if (cat.length === 0)
                continue;

            counts[cat] = (counts[cat] || 0) + 1;
        }
        var keys = Object.keys(counts);
        keys.sort(function(a, b) {
            return a.localeCompare(b);
        });
        var result = [];
        for (var k = 0; k < keys.length; k++) result.push({
            "name": keys[k],
            "count": counts[keys[k]]
        })
        return result;
    }
    // ── Derived: filtered + grouped effects ─────────────────────────────
    readonly property var _filteredEffects: {
        var needle = root.filterText.trim().toLowerCase();
        var anyCategorySelected = Object.keys(root.selectedCategories).length > 0;
        var out = [];
        for (var i = 0; i < effectList.length; i++) {
            var e = effectList[i];
            if (!e)
                continue;

            // Built-in / user gate.
            var isUser = !!e.isUserEffect;
            if (isUser && !root.showUser)
                continue;

            if (!isUser && !root.showBuiltIn)
                continue;

            // Category gate — only when at least one pill is active.
            if (anyCategorySelected) {
                var cat = e.category || "";
                if (!root.selectedCategories[cat])
                    continue;

            }
            // Text search across name / id / description / category / author.
            if (needle.length > 0) {
                var hay = (String(e.name || "") + " " + String(e.id || "") + " " + String(e.description || "") + " " + String(e.category || "") + " " + String(e.author || "")).toLowerCase();
                if (hay.indexOf(needle) === -1)
                    continue;

            }
            out.push(e);
        }
        return out;
    }
    /// `[{category, effects}]` sorted alphabetically by category. Effects
    /// inside each group keep their controller-emitted order.
    readonly property var _groupedEffects: {
        var groups = {
        };
        var order = [];
        // Use a sentinel for uncategorised effects so they cluster
        // together rather than spreading across the catalogue.
        var uncategorisedKey = i18nc("@title:group fallback for shaders without a category", "Uncategorised");
        for (var i = 0; i < _filteredEffects.length; i++) {
            var e = _filteredEffects[i];
            var cat = (e.category && e.category.length > 0) ? e.category : uncategorisedKey;
            if (!groups[cat]) {
                groups[cat] = [];
                order.push(cat);
            }
            groups[cat].push(e);
        }
        order.sort(function(a, b) {
            // Always sort the uncategorised bucket last so the named
            // categories take visual precedence.
            if (a === uncategorisedKey)
                return 1;

            if (b === uncategorisedKey)
                return -1;

            return a.localeCompare(b);
        });
        var result = [];
        for (var k = 0; k < order.length; k++) result.push({
            "category": order[k],
            "effects": groups[order[k]]
        })
        return result;
    }
    // Per-row "Used in:" labels resolve via Q_INVOKABLE
    // `shaderEffectUsages(id)`. QML can't observe the result of an
    // invokable across mutations, so each row re-evaluates against this
    // tick whenever any shader override changes anywhere in the tree.
    property int _usagesRev: 0

    contentHeight: content.implicitHeight
    clip: true

    Connections {
        function onShaderEffectsChanged() {
            root.effectList = settingsController.animationsPage.availableShaderEffects();
        }

        function onShaderProfileChanged(path) {
            ++root._usagesRev;
        }

        target: settingsController.animationsPage
    }

    Timer {
        id: searchDebounce

        interval: 150
        onTriggered: root.filterText = searchField.text
    }

    ColumnLayout {
        id: content

        width: parent.width
        spacing: Kirigami.Units.largeSpacing

        Kirigami.InlineMessage {
            Layout.fillWidth: true
            type: Kirigami.MessageType.Information
            visible: true
            text: i18n("Browse installed animation shaders. Assign shaders to specific events using the shader picker on any per-event sub-page (Window, Zone, OSD, etc.).")
        }

        SettingsCard {
            Layout.fillWidth: true
            headerText: i18n("User shaders")

            contentItem: ColumnLayout {
                spacing: Kirigami.Units.smallSpacing

                Label {
                    Layout.fillWidth: true
                    text: i18n("User-installed shader packs live under your data directory. Drop a shader pack folder here to make it available to PlasmaZones.")
                    wrapMode: Text.WordWrap
                    color: Kirigami.Theme.disabledTextColor
                }

                // ── Drop target ───────────────────────────────────────
                // Visible affordance for the drag-folder import the copy
                // above describes. Hover-state highlight + icon, success/
                // failure feedback via a transient Kirigami.InlineMessage.
                Rectangle {
                    id: dropZone

                    readonly property bool _highlight: dropArea.containsDrag

                    Layout.fillWidth: true
                    Layout.preferredHeight: Kirigami.Units.gridUnit * 5
                    radius: Kirigami.Units.smallSpacing
                    color: _highlight ? Qt.rgba(Kirigami.Theme.highlightColor.r, Kirigami.Theme.highlightColor.g, Kirigami.Theme.highlightColor.b, 0.12) : Qt.rgba(Kirigami.Theme.textColor.r, Kirigami.Theme.textColor.g, Kirigami.Theme.textColor.b, 0.04)
                    border.width: Math.max(1, Math.round(Kirigami.Units.devicePixelRatio))
                    // Dashed-look approximation via dimmed border colour
                    // when idle, theme highlight when active.
                    border.color: _highlight ? Kirigami.Theme.highlightColor : Qt.rgba(Kirigami.Theme.textColor.r, Kirigami.Theme.textColor.g, Kirigami.Theme.textColor.b, 0.25)

                    RowLayout {
                        anchors.centerIn: parent
                        spacing: Kirigami.Units.largeSpacing

                        Kirigami.Icon {
                            source: dropZone._highlight ? "folder-add" : "folder-download"
                            implicitWidth: Kirigami.Units.iconSizes.large
                            implicitHeight: Kirigami.Units.iconSizes.large
                            color: dropZone._highlight ? Kirigami.Theme.highlightColor : Kirigami.Theme.disabledTextColor
                        }

                        Label {
                            text: dropZone._highlight ? i18nc("@info drop-zone hover label", "Release to install shader pack") : i18nc("@info drop-zone idle label", "Drop a shader pack folder here")
                            color: dropZone._highlight ? Kirigami.Theme.highlightColor : Kirigami.Theme.disabledTextColor
                            font.italic: !dropZone._highlight
                        }

                    }

                    DropArea {
                        id: dropArea

                        anchors.fill: parent
                        // Accept folder drops from file managers (text/uri-list
                        // is the standard MIME type for file/folder URLs).
                        keys: ["text/uri-list"]
                        onDropped: function(drop) {
                            // Multi-folder drops aren't useful here — install
                            // the first folder URL and ignore the rest. The
                            // controller validates that each URL is actually
                            // a directory containing metadata.json.
                            var urls = drop.urls;
                            if (!urls || urls.length === 0) {
                                drop.accepted = false;
                                return ;
                            }
                            var ok = settingsController.animationsPage.installShaderPack(String(urls[0]));
                            installResult.show(ok, urls[0]);
                            drop.accepted = true;
                        }
                    }

                }

                // Transient success/failure banner under the drop zone.
                // Hidden by default; populated by `installResult.show(ok, url)`.
                Kirigami.InlineMessage {
                    id: installResult

                    function show(ok, url) {
                        var basename = String(url).split("/").pop();
                        if (basename.length === 0)
                            basename = String(url);

                        if (ok) {
                            type = Kirigami.MessageType.Positive;
                            text = i18nc("@info shader install success", "Installed shader pack “%1”.", basename);
                        } else {
                            type = Kirigami.MessageType.Error;
                            // Keep the message generic — the controller logs
                            // the specific reason (missing metadata.json,
                            // collision, copy failure) to journalctl.
                            text = i18nc("@info shader install failure", "Could not install “%1”. The folder must contain a metadata.json and not collide with an existing pack.", basename);
                        }
                        visible = true;
                        autoHideTimer.restart();
                    }

                    Layout.fillWidth: true
                    visible: false
                    showCloseButton: true

                    Timer {
                        id: autoHideTimer

                        interval: 6000
                        onTriggered: installResult.visible = false
                    }

                }

                // "Open Folder" — matches the layouts / autotile-algorithms
                // toolbar pattern (see LayoutToolbar.qml). Path display is
                // intentionally omitted: the file manager surfaces the
                // location far better than an elided path string ever does.
                RowLayout {
                    Layout.fillWidth: true

                    Item {
                        Layout.fillWidth: true
                    }

                    Button {
                        text: i18n("Open Folder")
                        icon.name: "folder-open"
                        flat: true
                        Accessible.name: i18n("Open user shader directory")
                        onClicked: settingsController.animationsPage.openUserShaderDirectory()
                    }

                }

            }

        }

        // ── Filter bar ──────────────────────────────────────────────────
        RowLayout {
            Layout.fillWidth: true
            spacing: Kirigami.Units.smallSpacing

            TextField {
                id: searchField

                Layout.preferredWidth: Kirigami.Units.gridUnit * 14
                placeholderText: i18nc("@info:placeholder shader search", "Search shaders…")
                inputMethodHints: Qt.ImhNoPredictiveText
                rightPadding: clearSearchButton.visible ? clearSearchButton.width + Kirigami.Units.smallSpacing : Kirigami.Units.smallSpacing
                Accessible.name: i18n("Search shaders")
                onTextChanged: searchDebounce.restart()

                ToolButton {
                    id: clearSearchButton

                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    visible: searchField.text.length > 0
                    icon.name: "edit-clear"
                    icon.width: Kirigami.Units.iconSizes.small
                    icon.height: Kirigami.Units.iconSizes.small
                    Accessible.name: i18nc("@action:button", "Clear search")
                    onClicked: {
                        searchField.clear();
                        searchDebounce.stop();
                        root.filterText = "";
                    }
                }

            }

            // Category pill row — one ToolButton per known category.
            // Click toggles inclusion; a non-empty selection switches
            // into "show only selected" mode (any-of, not all-of).
            Flow {
                Layout.fillWidth: true
                spacing: Kirigami.Units.smallSpacing

                Repeater {
                    model: root._allCategories

                    delegate: ToolButton {
                        required property var modelData
                        readonly property bool isActive: root.selectedCategories[modelData.name] === true

                        text: i18nc("@action:button category filter pill", "%1 (%2)", modelData.name, modelData.count)
                        checkable: true
                        checked: isActive
                        Accessible.name: isActive ? i18nc("@action:button", "Hide %1 shaders", modelData.name) : i18nc("@action:button", "Show only %1 shaders", modelData.name)
                        onClicked: {
                            // Replace-the-map mutation pattern — QML reactivity
                            // requires a new map identity, not in-place edits.
                            var next = Object.assign({
                            }, root.selectedCategories);
                            if (next[modelData.name])
                                delete next[modelData.name];
                            else
                                next[modelData.name] = true;
                            root.selectedCategories = next;
                        }
                    }

                }

            }

            // Built-in / User toggle — single ToolButton with a popup so
            // it doesn't dominate the bar at narrow widths.
            ToolButton {
                id: sourceFilterButton

                icon.name: "view-filter"
                checkable: false
                checked: !root.showBuiltIn || !root.showUser
                Accessible.name: (root.showBuiltIn && root.showUser) ? i18nc("@action:button", "Source filter") : i18nc("@action:button", "Source filter (active)")
                ToolTip.text: Accessible.name
                ToolTip.visible: hovered
                ToolTip.delay: Kirigami.Units.toolTipDelay
                onClicked: sourceFilterMenu.popup()

                Menu {
                    id: sourceFilterMenu

                    title: i18nc("@title:menu", "Source")

                    MenuItem {
                        text: i18nc("@option:check", "Built-in")
                        checkable: true
                        checked: root.showBuiltIn
                        onToggled: root.showBuiltIn = checked
                    }

                    MenuItem {
                        text: i18nc("@option:check", "User-installed")
                        checkable: true
                        checked: root.showUser
                        onToggled: root.showUser = checked
                    }

                }

            }

            // Reset — only enabled when something to reset.
            ToolButton {
                icon.name: "edit-reset"
                enabled: root._hasActiveFilters
                Accessible.name: i18nc("@action:button", "Reset filters")
                ToolTip.text: Accessible.name
                ToolTip.visible: hovered
                ToolTip.delay: Kirigami.Units.toolTipDelay
                onClicked: {
                    searchField.clear();
                    searchDebounce.stop();
                    root.filterText = "";
                    root.selectedCategories = ({
                    });
                    root.showBuiltIn = true;
                    root.showUser = true;
                }
            }

        }

        SettingsCard {
            Layout.fillWidth: true
            headerText: root._hasActiveFilters ? i18nc("@title:group filtered shader catalogue", "Installed shaders (%1 of %2)", root._filteredEffects.length, root.effectList.length) : i18nc("@title:group full shader catalogue", "Installed shaders (%1)", root.effectList.length)

            contentItem: ColumnLayout {
                spacing: Kirigami.Units.smallSpacing

                Label {
                    Layout.fillWidth: true
                    visible: root.effectList.length === 0
                    text: i18n("No shader effects installed.")
                    color: Kirigami.Theme.disabledTextColor
                    font.italic: true
                }

                Label {
                    Layout.fillWidth: true
                    visible: root.effectList.length > 0 && root._filteredEffects.length === 0
                    text: i18n("No shaders match the current filter.")
                    color: Kirigami.Theme.disabledTextColor
                    font.italic: true
                }

                // ── Grouped sections ──────────────────────────────────
                Repeater {
                    model: root._groupedEffects

                    delegate: ColumnLayout {
                        required property var modelData

                        Layout.fillWidth: true
                        // Bump vertical spacing between successive
                        // sections so the headers visually separate
                        // their card grids — `largeSpacing` topMargin
                        // gives a clearer "new category" cue than the
                        // previous tight `smallSpacing`.
                        Layout.topMargin: Kirigami.Units.largeSpacing
                        spacing: Math.round(Kirigami.Units.smallSpacing / 2)

                        // Section header
                        RowLayout {
                            Layout.fillWidth: true
                            spacing: Kirigami.Units.smallSpacing

                            Label {
                                text: modelData.category
                                font.weight: Font.DemiBold
                            }

                            Label {
                                text: i18np("%n shader", "%n shaders", modelData.effects.length)
                                font: Kirigami.Theme.smallFont
                                color: Kirigami.Theme.disabledTextColor
                            }

                            // Hairline divider that fills the rest of the row.
                            Rectangle {
                                Layout.fillWidth: true
                                Layout.alignment: Qt.AlignVCenter
                                height: Math.max(1, Math.round(Kirigami.Units.devicePixelRatio))
                                color: Qt.rgba(Kirigami.Theme.textColor.r, Kirigami.Theme.textColor.g, Kirigami.Theme.textColor.b, 0.15)
                            }

                        }

                        // Effects in this section. Cards in a Flow wrap
                        // to next row when they run out of horizontal
                        // space — gives 3-4 cards per row at typical
                        // settings-window widths. AnimationsShaderCard
                        // declares `required property var modelData`
                        // (which it aliases to `effect`) so the Repeater's
                        // auto-injection wires the per-effect map directly
                        // — without that, the outer group delegate's
                        // identically-named `modelData` would shadow the
                        // inner Repeater's auto-inject.
                        Flow {
                            Layout.fillWidth: true
                            Layout.leftMargin: Kirigami.Units.smallSpacing
                            Layout.topMargin: Math.round(Kirigami.Units.smallSpacing / 2)
                            spacing: Kirigami.Units.smallSpacing

                            Repeater {
                                model: modelData.effects

                                delegate: AnimationsShaderCard {
                                    required property var modelData

                                    effect: modelData
                                    usagesRev: root._usagesRev
                                    onShowDetails: function(e) {
                                        detailDialog.effect = e;
                                        detailDialog.open();
                                    }
                                }

                            }

                        }

                    }

                }

            }

        }

    }

    // ── Detail dialog (single instance; populated on card click) ────────
    AnimationsShaderDetailDialog {
        id: detailDialog

        usagesRev: root._usagesRev
    }

}
