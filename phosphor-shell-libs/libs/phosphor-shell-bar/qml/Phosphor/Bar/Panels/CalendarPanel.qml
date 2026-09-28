// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Phosphor.Theme
import Phosphor.Widgets
import Phosphor.Shell

FocusScope {
    id: root
    property var clock: systemClock
    // Calendar providers return {time, title, detail, color} for a local date.
    property var agenda: null
    readonly property var events: {
        if (!agenda)
            return [];
        void agenda.revision;
        return agenda.eventsForDate(selectedDate) || [];
    }
    readonly property string title: String(clock.hours).padStart(2, "0") + ":" + String(clock.minutes).padStart(2, "0")
    readonly property string utcOffset: {
        const offset = clock.utcOffsetMinutes;
        return "UTC " + (offset < 0 ? "−" : "+") + String(Math.floor(Math.abs(offset) / 60)).padStart(2, "0") + ":" + String(Math.abs(offset) % 60).padStart(2, "0");
    }
    signal closeRequested
    implicitWidth: 410
    implicitHeight: layout.implicitHeight + (Appearance.padding + 1) * 2
    SystemClock {
        id: systemClock
        precision: SystemClock.Minutes
    }
    // QDate crosses into JavaScript at UTC midnight. Rebuild a local
    // calendar date explicitly so western time zones do not select yesterday.
    readonly property date today: {
        const parts = Qt.formatDate(clock.date, "yyyy-MM-dd").split("-");
        return new Date(Number(parts[0]), Number(parts[1]) - 1, Number(parts[2]), 12);
    }
    property date selectedDate: new Date(today.getFullYear(), today.getMonth(), today.getDate())
    property date displayedMonth: new Date(selectedDate.getFullYear(), selectedDate.getMonth(), 1)
    property int firstWeekday: 1
    readonly property int lead: (displayedMonth.getDay() - firstWeekday + 7) % 7

    function sameDay(a: date, b: date): bool {
        return a.getFullYear() === b.getFullYear() && a.getMonth() === b.getMonth() && a.getDate() === b.getDate();
    }
    function browseMonth(delta: int): void {
        displayedMonth = new Date(displayedMonth.getFullYear(), displayedMonth.getMonth() + delta, 1);
        const last = new Date(displayedMonth.getFullYear(), displayedMonth.getMonth() + 1, 0).getDate();
        selectedDate = new Date(displayedMonth.getFullYear(), displayedMonth.getMonth(), Math.min(selectedDate.getDate(), last));
    }
    function selectDate(value: date): void {
        selectedDate = value;
        displayedMonth = new Date(value.getFullYear(), value.getMonth(), 1);
    }
    function stepDay(delta: int): void {
        selectDate(new Date(selectedDate.getFullYear(), selectedDate.getMonth(), selectedDate.getDate() + delta));
    }
    focus: true
    Component.onCompleted: forceActiveFocus()
    Keys.onLeftPressed: stepDay(-1)
    Keys.onRightPressed: stepDay(1)
    Keys.onUpPressed: stepDay(-7)
    Keys.onDownPressed: stepDay(7)
    Keys.onPressed: event => {
        if (event.key === Qt.Key_Home) {
            selectDate(today);
            event.accepted = true;
        } else if (event.key === Qt.Key_PageUp) {
            browseMonth(-1);
            event.accepted = true;
        } else if (event.key === Qt.Key_PageDown) {
            browseMonth(1);
            event.accepted = true;
        }
    }

    Keys.onEscapePressed: closeRequested()
    function hasEvents(date) {
        if (!agenda)
            return false;
        void agenda.revision;
        return (agenda.eventsForDate(date) || []).length > 0;
    }
    ShellSurface {
        anchors.fill: parent
        accented: true
    }
    Flickable {
        anchors.fill: parent
        anchors.margins: Appearance.padding + 1
        contentWidth: width
        contentHeight: layout.implicitHeight
        clip: true
        interactive: contentHeight > height
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: ScrollBar {}
        ColumnLayout {
            id: layout
            width: parent.width
            spacing: 0
            Item {
                Layout.fillWidth: true
                Layout.preferredHeight: 103
                Text {
                    y: 5
                    width: parent.width - 40
                    text: Qt.formatDate(root.today, "dddd, MMMM d").toUpperCase()
                    color: Appearance.muted
                    font.family: Tokens.font_family_ui
                    font.pixelSize: Math.round((9) * Appearance.textScale)
                    font.letterSpacing: 1.8
                    font.weight: Font.DemiBold
                    elide: Text.ElideRight
                }
                Text {
                    y: 27
                    height: 71
                    text: root.title.replace(":", '<span style="color:' + Appearance.stops[2] + '">:</span>')
                    textFormat: Text.RichText
                    color: Appearance.text
                    font.family: Tokens.font_family_ui
                    font.pixelSize: Math.round((62) * Appearance.textScale)
                    font.letterSpacing: -3
                    font.weight: Font.Medium
                    verticalAlignment: Text.AlignVCenter
                }
                ShellButton {
                    anchors.right: parent.right
                    text: "×"
                    label: qsTr("Close date and time")
                    labelSize: 17
                    implicitWidth: 30
                    flat: true
                    outlined: true
                    onClicked: root.closeRequested()
                }
            }
            RowLayout {
                Layout.fillWidth: true
                Layout.topMargin: 8
                Layout.preferredHeight: 14
                Text {
                    Layout.fillWidth: true
                    text: root.clock.timeZoneName + " · " + root.clock.timeZoneAbbreviation
                    color: Appearance.muted
                    font.family: Tokens.font_family_ui
                    font.pixelSize: Math.round((10) * Appearance.textScale)
                    elide: Text.ElideRight
                }
                Text {
                    text: root.utcOffset
                    color: Appearance.muted
                    font.family: Tokens.font_family_ui
                    font.pixelSize: Math.round((10) * Appearance.textScale)
                }
            }
            Rectangle {
                Layout.fillWidth: true
                Layout.topMargin: 21
                height: 1
                color: Appearance.outline
            }
            RowLayout {
                Layout.fillWidth: true
                Layout.topMargin: 18
                Layout.preferredHeight: 29
                spacing: 5
                Text {
                    Layout.fillWidth: true
                    text: Qt.formatDate(root.displayedMonth, "MMMM yyyy")
                    color: Appearance.text
                    font.family: Tokens.font_family_ui
                    font.pixelSize: Math.round((15) * Appearance.textScale)
                    font.weight: Font.Medium
                }
                ShellButton {
                    text: "‹"
                    label: qsTr("Previous month")
                    implicitWidth: 29
                    implicitHeight: 29
                    labelSize: 16
                    flat: true
                    onClicked: root.browseMonth(-1)
                }
                ShellButton {
                    text: qsTr("Today")
                    implicitWidth: 44
                    implicitHeight: 29
                    labelSize: 10
                    onClicked: root.selectDate(root.today)
                }
                ShellButton {
                    text: "›"
                    label: qsTr("Next month")
                    implicitWidth: 29
                    implicitHeight: 29
                    labelSize: 16
                    flat: true
                    onClicked: root.browseMonth(1)
                }
            }
            Row {
                Layout.fillWidth: true
                Layout.topMargin: 15
                Layout.preferredHeight: 13
                spacing: 3
                Repeater {
                    model: 7
                    Text {
                        required property int index
                        width: (parent.width - 18) / 7
                        height: 13
                        text: Qt.locale().standaloneDayName((root.firstWeekday + index) % 7, Locale.NarrowFormat)
                        horizontalAlignment: Text.AlignHCenter
                        color: Appearance.muted
                        font.family: Tokens.font_family_ui
                        font.pixelSize: Math.round((9) * Appearance.textScale)
                    }
                }
            }
            Grid {
                Layout.fillWidth: true
                Layout.topMargin: 9
                columns: 7
                columnSpacing: 3
                rowSpacing: 3
                Repeater {
                    model: 42
                    AbstractButton {
                        id: day
                        required property int index
                        readonly property date dateValue: new Date(root.displayedMonth.getFullYear(), root.displayedMonth.getMonth(), index - root.lead + 1, 12)
                        readonly property bool selected: root.sameDay(dateValue, root.selectedDate)
                        width: (parent.width - 18) / 7
                        height: Appearance.compact ? 31 : 36
                        Accessible.name: Qt.formatDate(dateValue, Qt.locale().dateFormat(Locale.LongFormat))
                        Accessible.role: Accessible.Button
                        checkable: true
                        autoExclusive: true
                        checked: selected
                        opacity: selected || dateValue.getMonth() === root.displayedMonth.getMonth() ? 1 : 0.45
                        onClicked: root.selectDate(dateValue)
                        background: Rectangle {
                            radius: Appearance.radius * 0.4
                            color: day.selected ? Qt.tint(Appearance.recess, Qt.alpha(Appearance.stops[2], 0.24)) : day.hovered ? Appearance.card : "transparent"
                            border.width: 1
                            border.color: day.selected || day.visualFocus ? Appearance.text : root.sameDay(day.dateValue, root.today) ? Appearance.accent : "transparent"
                        }
                        contentItem: Text {
                            text: day.dateValue.getDate()
                            font.family: Tokens.font_family_mono
                            font.pixelSize: Math.round((12) * Appearance.textScale)
                            color: day.dateValue.getMonth() === root.displayedMonth.getMonth() ? Appearance.text : Appearance.muted
                            horizontalAlignment: Text.AlignHCenter
                            verticalAlignment: Text.AlignVCenter
                        }
                        Rectangle {
                            width: 4
                            height: 3
                            radius: 2
                            anchors.horizontalCenter: parent.horizontalCenter
                            anchors.bottom: parent.bottom
                            anchors.bottomMargin: 3
                            color: Appearance.stops[2]
                            visible: root.hasEvents(day.dateValue)
                        }
                    }
                }
            }
            Rectangle {
                Layout.fillWidth: true
                Layout.topMargin: 18
                height: 1
                color: Appearance.outline
            }
            RowLayout {
                Layout.fillWidth: true
                Layout.topMargin: 17
                Layout.preferredHeight: 16
                Text {
                    Layout.fillWidth: true
                    text: Qt.formatDate(root.selectedDate, "dddd, MMM d")
                    color: Appearance.text
                    font.family: Tokens.font_family_ui
                    font.pixelSize: Math.round((11) * Appearance.textScale)
                    font.weight: Font.Medium
                    elide: Text.ElideRight
                }
                Text {
                    text: root.agenda ? root.agenda.name || qsTr("Agenda") : ""
                    color: Appearance.muted
                    font.family: Tokens.font_family_ui
                    font.pixelSize: Math.round((9) * Appearance.textScale)
                }
            }
            Column {
                Layout.fillWidth: true
                Layout.topMargin: 15
                spacing: 16
                Repeater {
                    model: root.events
                    RowLayout {
                        required property var modelData
                        width: parent.width
                        height: 35
                        spacing: 14
                        Text {
                            Layout.preferredWidth: 40
                            Layout.alignment: Qt.AlignTop
                            Layout.topMargin: 2
                            text: modelData.time
                            color: Appearance.muted
                            font.family: Tokens.font_family_mono
                            font.pixelSize: Math.round((10) * Appearance.textScale)
                        }
                        Rectangle {
                            Layout.fillHeight: true
                            width: 2
                            color: modelData.color || Appearance.accent
                        }
                        Column {
                            Layout.fillWidth: true
                            spacing: 4
                            Text {
                                width: parent.width
                                text: modelData.title
                                font.family: Tokens.font_family_ui
                                font.pixelSize: Math.round((11) * Appearance.textScale)
                                font.weight: Font.Medium
                                color: Appearance.text
                                elide: Text.ElideRight
                            }
                            Text {
                                width: parent.width
                                text: modelData.detail || ""
                                font.family: Tokens.font_family_ui
                                font.pixelSize: Math.round((9) * Appearance.textScale)
                                color: Appearance.muted
                                elide: Text.ElideRight
                            }
                        }
                    }
                }
                Rectangle {
                    width: parent.width
                    height: 46
                    visible: !root.events.length
                    radius: 8
                    color: Appearance.recess
                    Text {
                        anchors.fill: parent
                        anchors.margins: 14
                        text: root.agenda ? qsTr("Nothing scheduled for this day.") : qsTr("No calendar connected.")
                        color: Appearance.muted
                        font.family: Tokens.font_family_ui
                        font.pixelSize: Math.round((11) * Appearance.textScale)
                        verticalAlignment: Text.AlignVCenter
                    }
                }
            }
            RowLayout {
                Layout.fillWidth: true
                Layout.topMargin: 19
                Layout.preferredHeight: 13
                Text {
                    Layout.fillWidth: true
                    text: qsTr("Arrow keys to browse · Esc to close")
                    color: Appearance.muted
                    font.family: Tokens.font_family_ui
                    font.pixelSize: Math.round((9) * Appearance.textScale)
                    elide: Text.ElideRight
                }
                Text {
                    text: qsTr("Local time")
                    color: Appearance.muted
                    font.family: Tokens.font_family_ui
                    font.pixelSize: Math.round((9) * Appearance.textScale)
                }
            }
        }
    }
}
