// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
// Phosphor.Picker.Picker, the wallpaper and theme strip (A3 §8).
//
// A 96 px strip along the bottom screen edge: wallpaper candidates on
// the left, a hairline, theme tiles, a hairline, then Apply / Apply on
// all screens and the fan-out count. The one idea is that there is no
// preview pane: hovering a candidate runs matugen on it and retints the
// live PaletteStore, so the whole shell (bar, surfaces, this strip) takes
// the palette for as long as the pointer rests there. Leaving without
// Apply puts the previous palette back, on the Theme's own binding
// update. Escape closes.
//
// The wallpaper follows the same rule: the shell owns the wallpaper
// surface (WallpaperSurface, one per output), so hovering a candidate
// also asks WallpaperService to preview it on this strip's output, and
// the surface crossfades to it under the windows. Leaving clears the
// preview; Apply persists the path and the preview clears onto it.
//
// The host mounts one strip per output and binds `open`; the strip
// rescans and takes focus on open, and restores the palette on close.
//
//   Picker {
//       screenName: surface.screen.name
//       wallpaper: PhosphorShell.wallpaper
//       open: PickerRegistry.openScreen === screenName
//       onClosed: PickerRegistry.hide()
//   }

import QtQuick
import QtQuick.Layouts
import Phosphor.Theme
import Phosphor.Widgets

FocusScope {
    id: picker

    // The output this strip is on: "Apply" targets it alone.
    property string screenName: ""
    // The wallpaper service (PhosphorShell.wallpaper in the shell, a
    // fake in tests): a `path` property, `setPath(path, screenName)`,
    // `setPreview(path, screenName)` and `clearPreview(screenName)`.
    // Null means the wallpaper cannot be set or previewed and the strip
    // works the palette alone.
    property var wallpaper: null
    // How many surfaces take the palette: the "N targets" figure.
    property int targetCount: 0
    property bool open: false

    readonly property alias candidates: candidates
    readonly property alias presets: presets
    readonly property alias retint: retint
    readonly property string selectedPath: priv.selectedPath
    readonly property string selectedPreset: priv.selectedPreset
    // Whether Apply has something to do.
    readonly property bool canApply: priv.selectedPreset !== "" || (priv.selectedPath !== "" && priv.selectedPath !== priv.currentPath)

    signal applied(string path, bool allScreens)
    signal closed

    implicitHeight: 96

    Accessible.role: Accessible.Pane
    Accessible.name: qsTr("Wallpaper and theme picker")

    // The strip drives the runner in the palette's own mode, so a light
    // shell gets light matugen output.
    MatugenRunner {
        id: matugen

        mode: Theme.isDark ? "dark" : "light"
    }

    RetintController {
        id: retint

        runner: matugen
        store: Theme.paletteStore
        persistPath: retint.defaultPersistPath
    }

    WallpaperCandidates {
        id: candidates
    }

    ThemePresets {
        id: presets
    }

    QtObject {
        id: priv

        property string selectedPath: ""
        property string selectedPreset: ""
        property string currentPath: ""

        // What the desktop should show given hover and selection: the
        // hovered candidate wins, else the selection, else nothing. The
        // wallpaper preview follows the candidate half of that: a hovered
        // candidate, else a selected one that is not already current.
        function settle(): void {
            const wallpaperPath = hoveredPath !== "" ? hoveredPath : (selectedPath !== currentPath ? selectedPath : "");
            if (wallpaperPath !== "")
                previewWallpaper(wallpaperPath);
            else
                clearWallpaperPreview();
            if (hoveredPreset !== "") {
                retint.previewTokens(presets.tokensFor(hoveredPreset), hoveredPreset);
            } else if (hoveredPath !== "") {
                retint.preview(hoveredPath);
            } else if (selectedPreset !== "") {
                retint.previewTokens(presets.tokensFor(selectedPreset), selectedPreset);
            } else if (selectedPath !== "" && selectedPath !== currentPath) {
                retint.preview(selectedPath);
            } else {
                retint.clearPreview();
            }
        }

        property string hoveredPath: ""
        property string hoveredPreset: ""

        function readCurrentPath(): string {
            const w = picker.wallpaper;
            if (!w)
                return "";
            if (typeof w.configuredPath === "function")
                return String(w.configuredPath(picker.screenName));
            if (typeof w.effectivePath === "function")
                return String(w.effectivePath(picker.screenName));
            return w.path !== undefined ? String(w.path) : "";
        }

        function previewWallpaper(path: string): void {
            if (picker.wallpaper && typeof picker.wallpaper.setPreview === "function")
                picker.wallpaper.setPreview(path, picker.screenName);
        }

        function clearWallpaperPreview(): void {
            if (picker.wallpaper && typeof picker.wallpaper.clearPreview === "function")
                picker.wallpaper.clearPreview(picker.screenName);
        }
    }

    // A candidate under the pointer, or no longer: the wallpaper and the
    // palette both follow. The thumbs call this; a host or a test can too.
    function hoverCandidate(path: string, hovered: bool): void {
        priv.hoveredPath = hovered ? path : (priv.hoveredPath === path ? "" : priv.hoveredPath);
        priv.settle();
    }

    // Select a candidate, clearing any theme tile.
    function selectCandidate(path: string): void {
        priv.selectedPath = path;
        priv.selectedPreset = "";
        priv.settle();
    }

    function reset(): void {
        // This screen's own configured path, not the process-wide `path`:
        // a per-screen Apply must read back as current on that screen.
        priv.currentPath = priv.readCurrentPath();
        candidates.currentPath = priv.currentPath;
        candidates.rescan();
        presets.rescan();
        priv.selectedPath = priv.currentPath;
        priv.selectedPreset = "";
        priv.hoveredPath = "";
        priv.hoveredPreset = "";
        candidateList.positionViewAtBeginning();
        picker.forceActiveFocus();
    }

    function close(): void {
        priv.hoveredPath = "";
        priv.hoveredPreset = "";
        priv.clearWallpaperPreview();
        retint.clearPreview();
        picker.closed();
    }

    // Apply: the palette that is live stays (a run still in flight lands
    // and stays too), the wallpaper is set on this screen or every one.
    // The preview clears AFTER the set, so the surface resolves to the
    // same path it is already showing and nothing flickers.
    function apply(allScreens: bool): void {
        const path = priv.selectedPreset !== "" ? "" : priv.selectedPath;
        if (path !== "" && path !== priv.currentPath && picker.wallpaper && typeof picker.wallpaper.setPath === "function")
            picker.wallpaper.setPath(path, allScreens ? "" : picker.screenName);
        priv.clearWallpaperPreview();
        retint.commit();
        priv.hoveredPath = "";
        priv.hoveredPreset = "";
        picker.applied(path, allScreens);
        picker.closed();
    }

    onOpenChanged: {
        if (open) {
            reset();
        } else {
            priv.clearWallpaperPreview();
            retint.clearPreview();
        }
    }

    Keys.onEscapePressed: close()
    Keys.onReturnPressed: event => apply((event.modifiers & Qt.ShiftModifier) !== 0)
    Keys.onEnterPressed: event => apply((event.modifiers & Qt.ShiftModifier) !== 0)
    Keys.onLeftPressed: candidateList.decrementCurrentIndex()
    Keys.onRightPressed: candidateList.incrementCurrentIndex()

    // Navy glass at 0.72 with the 1 px cyan top edge: the picker's
    // material, no radius, no inset (it is the screen edge).
    Rectangle {
        anchors.fill: parent
        color: Theme.surface
        opacity: 0.72
    }

    Rectangle {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        height: 1
        color: Spectrum.resting
    }

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: Tokens.spacing_l
        anchors.rightMargin: Tokens.spacing_l
        anchors.topMargin: Tokens.spacing_s
        anchors.bottomMargin: Tokens.spacing_s
        spacing: Tokens.spacing_l

        // Wallpapers.
        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: Tokens.spacing_xs

            Text {
                text: qsTr("Wallpaper").toUpperCase()
                color: Theme.on_surface_variant
                font.family: Tokens.font_family_ui
                font.pixelSize: Tokens.font_size_label_s
                font.letterSpacing: Tokens.font_size_label_s * 0.08
            }

            ListView {
                id: candidateList

                Layout.fillWidth: true
                Layout.preferredHeight: 68
                orientation: ListView.Horizontal
                spacing: Tokens.spacing_s
                clip: true
                model: candidates.candidates
                currentIndex: -1
                highlightFollowsCurrentItem: true
                boundsBehavior: Flickable.StopAtBounds

                onCurrentIndexChanged: {
                    if (currentIndex >= 0 && currentIndex < count) {
                        priv.selectedPath = String(model[currentIndex].path);
                        priv.selectedPreset = "";
                        priv.settle();
                    }
                }

                delegate: CandidateThumb {
                    required property var modelData
                    required property int index

                    source: "file://" + modelData.path
                    name: modelData.name
                    current: modelData.path === priv.currentPath
                    selected: modelData.path === priv.selectedPath && priv.selectedPreset === ""
                    onHoveredChanged2: hovered => picker.hoverCandidate(modelData.path, hovered)
                    onClicked: {
                        candidateList.currentIndex = index;
                        picker.selectCandidate(modelData.path);
                        picker.forceActiveFocus();
                    }
                    onDoubleClicked: picker.apply(false)
                }

                // A wheel over the strip scrolls it sideways: the strip is
                // one row, so either axis of the wheel means "along".
                WheelHandler {
                    target: null
                    onWheel: event => {
                        const delta = event.angleDelta.y !== 0 ? event.angleDelta.y : event.angleDelta.x;
                        const max = Math.max(0, candidateList.contentWidth - candidateList.width);
                        candidateList.contentX = Math.max(0, Math.min(max, candidateList.contentX - delta));
                    }
                }
            }
        }

        Rectangle {
            Layout.preferredWidth: 1
            Layout.preferredHeight: 68
            Layout.alignment: Qt.AlignBottom
            color: Theme.outline_variant
            opacity: Tokens.stroke_resting
        }

        // Themes.
        ColumnLayout {
            Layout.preferredWidth: Math.min(presetList.contentWidth, 4 * 96 + 3 * Tokens.spacing_s)
            Layout.fillHeight: true
            spacing: Tokens.spacing_xs

            Text {
                text: qsTr("Theme").toUpperCase()
                color: Theme.on_surface_variant
                font.family: Tokens.font_family_ui
                font.pixelSize: Tokens.font_size_label_s
                font.letterSpacing: Tokens.font_size_label_s * 0.08
            }

            ListView {
                id: presetList

                Layout.fillWidth: true
                Layout.preferredHeight: 68
                orientation: ListView.Horizontal
                spacing: Tokens.spacing_s
                clip: true
                model: presets.presets
                boundsBehavior: Flickable.StopAtBounds

                delegate: PaletteTile {
                    required property var modelData

                    name: modelData.name
                    swatches: modelData.swatches
                    selected: modelData.name === priv.selectedPreset
                    onHoveredChanged2: hovered => {
                        priv.hoveredPreset = hovered ? modelData.name : (priv.hoveredPreset === modelData.name ? "" : priv.hoveredPreset);
                        priv.settle();
                    }
                    onClicked: {
                        priv.selectedPreset = modelData.name;
                        priv.settle();
                        picker.forceActiveFocus();
                    }
                    onDoubleClicked: picker.apply(false)
                }

                WheelHandler {
                    target: null
                    onWheel: event => {
                        const delta = event.angleDelta.y !== 0 ? event.angleDelta.y : event.angleDelta.x;
                        const max = Math.max(0, presetList.contentWidth - presetList.width);
                        presetList.contentX = Math.max(0, Math.min(max, presetList.contentX - delta));
                    }
                }
            }
        }

        Rectangle {
            Layout.preferredWidth: 1
            Layout.preferredHeight: 68
            Layout.alignment: Qt.AlignBottom
            color: Theme.outline_variant
            opacity: Tokens.stroke_resting
        }

        // Actions.
        ColumnLayout {
            Layout.alignment: Qt.AlignVCenter
            spacing: Tokens.spacing_s

            PickerAction {
                text: picker.canApply ? qsTr("Apply") : qsTr("Close")
                primary: true
                onActivated: picker.canApply ? picker.apply(false) : picker.close()
            }

            PickerAction {
                text: qsTr("Apply on all screens")
                visible: picker.canApply
                onActivated: picker.apply(true)
            }

            TabularText {
                text: retint.busy ? qsTr("%n target(s) · retinting", "", picker.targetCount) : qsTr("%n target(s) · Esc returns", "", picker.targetCount)
                color: Theme.on_surface_variant
                font.pixelSize: Tokens.font_size_label_m
            }
        }
    }
}
