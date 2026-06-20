// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Window
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

/**
 * @brief Pack-agnostic shader browser page.
 *
 * Read-only listing with drop-zone install card, filter bar, grouped
 * card grid, and a detail dialog. Drives both the Animations → Shaders
 * page (for `data/animations/` packs) and the Snapping → Shaders page
 * (for `data/shaders/` overlay packs).
 *
 * The host supplies a `bridge` QObject implementing the contract:
 *
 *   QVariantList availableShaderEffects()
 *   bool         installShaderPack(const QString& url)
 *   void         openUserShaderDirectory()
 *   QVariantList shaderEffectUsages(const QString& effectId)
 *
 *   signal shaderEffectsChanged()
 *   signal shaderProfileChanged(const QString& path)  // optional
 *
 * Plus a few labels the host can override to suit its domain (e.g.
 * "Browse installed snapping overlay shaders" vs the animation copy).
 *
 * ## Layout
 *
 *   • Optional info banner (`infoBannerText`).
 *   • User shaders card — drop zone for installing user shader packs +
 *     "Open Folder" button.
 *   • Filter bar — text search + per-category multi-select pills +
 *     built-in / user toggles.
 *   • Installed shaders card — packs grouped by category as a card grid;
 *     each section headed with the category name and the count.
 *     Card click opens ShaderBrowserDetailDialog.
 */
SettingsFlickable {
    id: root

    required property var bridge
    // ── Domain-tuned copy ────────────────────────────────────────────────
    property string infoBannerText: ""
    property string userShadersDescription: i18n("User-installed shader packs live under your data directory. Drop a shader pack folder here to make it available to PlasmaZones.")
    property string dropZoneIdleText: i18nc("@info drop-zone idle label", "Drop a shader pack folder here")
    property string dropZoneHoverText: i18nc("@info drop-zone hover label", "Release to install shader pack")
    property string emptyCatalogueText: i18n("No shader effects installed.")
    /// Closure plumbed through to the detail dialog. Hosts pass an
    /// `i18ncp(..., count)` closure so the plural form is selected with
    /// the LIVE count — pre-baking singular/plural strings here would
    /// break locales with >2 plural forms (Polish, Russian, Arabic, …)
    /// and would lie about `%n` (it would substitute the wrapper's
    /// hard-coded count instead of `_usages.length`).
    property var usageHeaderTextFn: function (count) {
        return i18ncp("@info shader usage section header", "Used in %n event", "Used in %n events", count);
    }
    /// Closure plumbed through to each ShaderBrowserCard's footer chip.
    /// Same plural-form / live-count rationale as `usageHeaderTextFn`,
    /// and lets the card chip stay consistent with the dialog header
    /// (animations: "event"; snapping: "layout"; default: generic "use").
    property var usageChipTextFn: function (count) {
        return i18ncp("@info shader usage count", "%n use", "%n uses", count);
    }
    // Loaded from a Q_INVOKABLE; Connections below manually refreshes it
    // on `shaderEffectsChanged`. Q_INVOKABLE results aren't reactive
    // across the QML binding boundary.
    property var effectList: bridge ? bridge.availableShaderEffects() : []
    // ── Filter state ────────────────────────────────────────────────────
    property string filterText: ""
    /// Active category filter; "" = show all (single-select, with an "All" chip).
    property string selectedCategory: ""
    property bool showBuiltIn: true
    property bool showUser: true
    readonly property bool _hasActiveFilters: filterText.length > 0 || selectedCategory.length > 0 || !showBuiltIn || !showUser
    // ── Derived: category index (sorted, with counts) ───────────────────
    readonly property var _allCategories: {
        var counts = {};
        for (var i = 0; i < effectList.length; i++) {
            var cat = (effectList[i] && effectList[i].category) ? effectList[i].category : "";
            if (cat.length === 0)
                continue;

            counts[cat] = (counts[cat] || 0) + 1;
        }
        var keys = Object.keys(counts);
        keys.sort(function (a, b) {
            return a.localeCompare(b);
        });
        var result = [];
        for (var k = 0; k < keys.length; k++)
            result.push({
                "name": keys[k],
                "count": counts[keys[k]]
            });
        return result;
    }
    // ── Derived: filtered + grouped effects ─────────────────────────────
    readonly property var _filteredEffects: {
        var needle = root.filterText.trim().toLowerCase();
        var out = [];
        for (var i = 0; i < effectList.length; i++) {
            var e = effectList[i];
            if (!e)
                continue;

            var isUser = !!e.isUserEffect;
            if (isUser && !root.showUser)
                continue;

            if (!isUser && !root.showBuiltIn)
                continue;

            if (root.selectedCategory.length > 0 && (e.category || "") !== root.selectedCategory)
                continue;

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
    /// inside each group keep their bridge-emitted order.
    readonly property var _groupedEffects: {
        var groups = {};
        var order = [];
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
        order.sort(function (a, b) {
            if (a === uncategorisedKey)
                return 1;

            if (b === uncategorisedKey)
                return -1;

            return a.localeCompare(b);
        });
        var result = [];
        for (var k = 0; k < order.length; k++)
            result.push({
                "category": order[k],
                "effects": groups[order[k]]
            });
        return result;
    }
    // Per-row "Used in:" labels resolve via Q_INVOKABLE. QML can't
    // observe the result of an invokable across mutations, so each row
    // re-evaluates against this tick whenever any usage source mutates.
    property int _usagesRev: 0

    contentHeight: content.implicitHeight
    clip: true

    Connections {
        function onShaderEffectsChanged() {
            root.effectList = root.bridge ? root.bridge.availableShaderEffects() : [];
        }

        function onShaderProfileChanged(path) {
            ++root._usagesRev;
        }

        // Surface bridge-emitted toast requests through the shell
        // `window.showToast`. Without this, the controller's
        // toastRequested signal goes to /dev/null and the install
        // drop zone falls back to the generic InlineMessage above
        // (which can't carry the concrete failure reason).
        function onToastRequested(text) {
            if (window && window.showToast)
                window.showToast(text);
        }

        target: root.bridge
        // `ignoreUnknownSignals` is tolerated implicitly by Connections —
        // bridges that don't expose `shaderProfileChanged` /
        // `toastRequested` simply never fire them; the functions are
        // still defined so the signal handler index resolves correctly
        // when present.
        ignoreUnknownSignals: true
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
            visible: root.infoBannerText.length > 0
            text: root.infoBannerText
        }

        SettingsCard {
            Layout.fillWidth: true
            headerText: i18n("User shaders")

            contentItem: ColumnLayout {
                spacing: Kirigami.Units.smallSpacing

                Label {
                    Layout.fillWidth: true
                    text: root.userShadersDescription
                    wrapMode: Text.WordWrap
                    color: Kirigami.Theme.disabledTextColor
                }

                Rectangle {
                    id: dropZone

                    readonly property bool _highlight: dropArea.containsDrag

                    Layout.fillWidth: true
                    Layout.preferredHeight: Kirigami.Units.gridUnit * 5
                    radius: Kirigami.Units.smallSpacing
                    color: _highlight ? Qt.rgba(Kirigami.Theme.highlightColor.r, Kirigami.Theme.highlightColor.g, Kirigami.Theme.highlightColor.b, 0.12) : Qt.rgba(Kirigami.Theme.textColor.r, Kirigami.Theme.textColor.g, Kirigami.Theme.textColor.b, 0.04)
                    border.width: Math.max(1, Math.round(Screen.devicePixelRatio))
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
                            text: dropZone._highlight ? root.dropZoneHoverText : root.dropZoneIdleText
                            color: dropZone._highlight ? Kirigami.Theme.highlightColor : Kirigami.Theme.disabledTextColor
                            font.italic: !dropZone._highlight
                        }
                    }

                    DropArea {
                        id: dropArea

                        anchors.fill: parent
                        keys: ["text/uri-list"]
                        onDropped: function (drop) {
                            var urls = drop.urls;
                            if (!urls || urls.length === 0) {
                                drop.accepted = false;
                                return;
                            }
                            var ok = root.bridge ? root.bridge.installShaderPack(String(urls[0])) : false;
                            installResult.show(ok, urls[0]);
                            drop.accepted = true;
                        }
                    }
                }

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
                            text = i18nc("@info shader install failure", "Could not install “%1”. The folder must contain a metadata.json and not collide with an existing pack.", basename);
                        }
                        visible = true;
                        autoHideTimer.restart();
                    }

                    Layout.fillWidth: true
                    Layout.leftMargin: Kirigami.Units.largeSpacing
                    Layout.rightMargin: Kirigami.Units.largeSpacing
                    visible: false
                    showCloseButton: true

                    Timer {
                        id: autoHideTimer

                        interval: 6000
                        onTriggered: installResult.visible = false
                    }
                }

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
                        onClicked: {
                            if (root.bridge)
                                root.bridge.openUserShaderDirectory();
                        }
                    }
                }
            }
        }

        // ── Search row ──────────────────────────────────────────────────
        // Full-width search + trailing source/reset icons, mirroring the
        // Window Rules page's search row.
        RowLayout {
            Layout.fillWidth: true
            spacing: Kirigami.Units.smallSpacing

            Kirigami.SearchField {
                id: searchField

                Layout.fillWidth: true
                placeholderText: i18nc("@info:placeholder shader search", "Search shaders…")
                Accessible.name: i18n("Search shaders")
                onTextChanged: searchDebounce.restart()
            }

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
                    root.selectedCategory = "";
                    root.showBuiltIn = true;
                    root.showUser = true;
                }
            }
        }

        // ── Filter chips ────────────────────────────────────────────────
        // "Filter:" label + single-select category chips ("All" first),
        // matching the Window Rules filter row.
        RowLayout {
            Layout.fillWidth: true
            spacing: Kirigami.Units.smallSpacing

            Label {
                text: i18n("Filter:")
                opacity: 0.7
            }

            Flow {
                Layout.fillWidth: true
                spacing: Kirigami.Units.smallSpacing

                Button {
                    text: i18nc("@action:button show every shader category", "All")
                    checkable: true
                    checked: root.selectedCategory === ""
                    Accessible.name: i18n("Show all shader categories")
                    onClicked: root.selectedCategory = ""
                }

                Repeater {
                    model: root._allCategories

                    delegate: Button {
                        required property var modelData

                        text: i18nc("@action:button category filter chip", "%1 (%2)", modelData.name, modelData.count)
                        checkable: true
                        checked: root.selectedCategory === modelData.name
                        Accessible.name: i18n("Filter shaders by category: %1", modelData.name)
                        onClicked: root.selectedCategory = modelData.name
                    }
                }
            }
        }

        // ── Empty states ────────────────────────────────────────────────
        Kirigami.PlaceholderMessage {
            Layout.fillWidth: true
            Layout.topMargin: Kirigami.Units.gridUnit * 2
            visible: root.effectList.length === 0
            icon.name: "image-missing"
            text: root.emptyCatalogueText
        }

        Kirigami.PlaceholderMessage {
            Layout.fillWidth: true
            Layout.topMargin: Kirigami.Units.gridUnit * 2
            visible: root.effectList.length > 0 && root._filteredEffects.length === 0
            icon.name: "view-filter"
            text: i18n("No shaders match the current filter")
            explanation: i18n("Try a different filter or search term.")
        }

        // ── Grouped shader catalogue ────────────────────────────────────
        // Each category renders as its own collapsible SettingsCard — the same
        // grouped-section treatment as the Window Rules page — with the
        // category as the header and the shader count as the trailing hint.
        Repeater {
            model: root._groupedEffects

            delegate: SettingsCard {
                required property var modelData

                Layout.fillWidth: true
                headerText: modelData.category
                collapsible: true
                headerTrailingText: i18np("%n shader", "%n shaders", modelData.effects.length)

                contentItem: ColumnLayout {
                    spacing: Kirigami.Units.smallSpacing

                    // Cards in a Flow wrap to the next row when out of
                    // horizontal space — 3-4 cards per row at typical
                    // settings-window widths. The inner delegate declares its
                    // own `required property var modelData` so the section
                    // delegate's identically-named `modelData` doesn't shadow
                    // the Repeater's auto-injection.
                    Flow {
                        Layout.fillWidth: true
                        Layout.leftMargin: Kirigami.Units.smallSpacing
                        Layout.rightMargin: Kirigami.Units.smallSpacing
                        spacing: Kirigami.Units.smallSpacing

                        Repeater {
                            model: modelData.effects

                            delegate: ShaderBrowserCard {
                                required property var modelData

                                effect: modelData
                                bridge: root.bridge
                                usagesRev: root._usagesRev
                                usageChipTextFn: root.usageChipTextFn
                                onShowDetails: function (e) {
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

    ShaderBrowserDetailDialog {
        id: detailDialog

        bridge: root.bridge
        usagesRev: root._usagesRev
        usageHeaderTextFn: root.usageHeaderTextFn
    }
}
