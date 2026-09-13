// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
import QtQuick
import QtQuick.Layouts
import Phosphor.Theme
import Phosphor.Widgets
import Phosphor.Shell

PanelFrame {
    id: root
    title: String(clock.hours).padStart(2, "0") + ":" + String(clock.minutes).padStart(2, "0")
    subtitle: Qt.formatDate(clock.date, Qt.locale().dateFormat(Locale.LongFormat))
    iconName: "view-calendar"
    panelWidth: Appearance.panelWidth
    maxBodyHeight: 440
    SystemClock {
        id: clock
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
    readonly property int firstWeekday: Qt.locale().firstDayOfWeek % 7
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

    RowLayout {
        width: parent ? parent.width : 0
        Text {
            Layout.fillWidth: true
            text: Qt.formatDate(root.displayedMonth, "MMMM yyyy")
            color: Appearance.text
            font.family: Tokens.font_family_ui
            font.pixelSize: 16
            font.weight: Font.DemiBold
        }
        ShellButton {
            iconName: "go-previous"
            label: qsTr("Previous month")
            onClicked: root.browseMonth(-1)
        }
        ShellButton {
            iconName: "go-next"
            label: qsTr("Next month")
            onClicked: root.browseMonth(1)
        }
    }
    Item {
        width: 1
        height: 10
    }
    GridLayout {
        width: parent ? parent.width : 0
        columns: 7
        columnSpacing: 2
        rowSpacing: 3
        Repeater {
            model: 7
            delegate: Text {
                required property int index
                Layout.fillWidth: true
                Layout.preferredHeight: 24
                text: Qt.locale().standaloneDayName((root.firstWeekday + index) % 7, Locale.ShortFormat)
                horizontalAlignment: Text.AlignHCenter
                color: Appearance.muted
                font.family: Tokens.font_family_ui
                font.pixelSize: 10
            }
        }
        Repeater {
            model: 42
            delegate: ShellButton {
                id: day
                required property int index
                readonly property date dateValue: new Date(root.displayedMonth.getFullYear(), root.displayedMonth.getMonth(), index - root.lead + 1)
                Layout.fillWidth: true
                Layout.preferredHeight: Appearance.compact ? 32 : 37
                implicitWidth: 32
                flat: true
                text: dateValue.getDate()
                label: Qt.formatDate(dateValue, Qt.locale().dateFormat(Locale.LongFormat))
                highlighted: root.sameDay(dateValue, root.selectedDate)
                opacity: dateValue.getMonth() === root.displayedMonth.getMonth() ? 1 : 0.4
                onClicked: root.selectDate(dateValue)
                Rectangle {
                    visible: root.sameDay(day.dateValue, root.today)
                    width: 4
                    height: 4
                    radius: 2
                    color: Appearance.accent
                    anchors.horizontalCenter: parent.horizontalCenter
                    anchors.bottom: parent.bottom
                    anchors.bottomMargin: 3
                }
            }
        }
    }
    Item {
        width: 1
        height: 12
    }
    RowLayout {
        width: parent ? parent.width : 0
        Text {
            Layout.fillWidth: true
            text: Qt.formatDate(root.selectedDate, "ddd, MMM d")
            color: Appearance.muted
            font.family: Tokens.font_family_ui
            font.pixelSize: 12
        }
        ShellButton {
            text: qsTr("Today")
            onClicked: root.selectDate(root.today)
        }
    }
}
