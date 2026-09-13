// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
import QtQuick
import QtTest
import Phosphor.Bar

TestCase {
    id: tests
    name: "CalendarNavigation"
    when: windowShown
    Component {
        id: calendar
        CalendarPanel {}
    }
    Component {
        id: appearance
        AppearancePanel {}
    }
    function test_monthEndAndLeapYear() {
        const view = createTemporaryObject(calendar, tests);
        verify(view);
        view.selectDate(new Date(2024, 0, 31));
        view.browseMonth(1);
        compare(view.selectedDate.getMonth(), 1);
        compare(view.selectedDate.getDate(), 29);
        view.stepDay(1);
        compare(view.selectedDate.getMonth(), 2);
        compare(view.selectedDate.getDate(), 1);
        view.stepDay(-1);
        compare(view.selectedDate.getDate(), 29);
        verify(!view.sameDay(new Date(2024, 1, 12), new Date(2024, 2, 12)));
    }
    function test_todayUsesTheLocalCalendarDate() {
        const view = createTemporaryObject(calendar, tests);
        verify(view);
        const now = new Date();
        compare(view.today.getFullYear(), now.getFullYear());
        compare(view.today.getMonth(), now.getMonth());
        compare(view.today.getDate(), now.getDate());
        compare(view.title, String(now.getHours()).padStart(2, "0") + ":" + String(now.getMinutes()).padStart(2, "0"));
    }
    function test_keyboardNavigation() {
        const view = createTemporaryObject(calendar, tests);
        verify(view);
        view.selectDate(new Date(2026, 11, 31));
        view.forceActiveFocus();
        keyClick(Qt.Key_Right);
        compare(view.selectedDate.getFullYear(), 2027);
        compare(view.selectedDate.getMonth(), 0);
        compare(view.selectedDate.getDate(), 1);
        keyClick(Qt.Key_Home);
        verify(view.sameDay(view.selectedDate, view.today));
    }
    function test_agendaFollowsTheSelectedLocalDate() {
        const provider = {
            name: "Calendar",
            eventsForDate: date => date.getDate() === 12 ? [
                    {
                        time: "11:00",
                        title: "Review"
                    }
                ] : []
        };
        const view = createTemporaryObject(calendar, tests, {
            agenda: provider
        });
        view.selectDate(new Date(2026, 8, 12, 12));
        compare(view.events.length, 1);
        verify(view.hasEvents(view.selectedDate));
        view.stepDay(1);
        compare(view.events.length, 0);
        verify(!view.hasEvents(view.selectedDate));
        view.clock = {
            hours: 10,
            minutes: 24,
            date: new Date(2026, 8, 12, 12),
            timeZoneName: "Kolkata",
            timeZoneAbbreviation: "IST",
            utcOffsetMinutes: 330
        };
        compare(view.utcOffset, "UTC +05:30");
    }
    function test_appearanceControlsLoad() {
        const view = createTemporaryObject(appearance, tests);
        verify(view);
        verify(view.implicitWidth > 0);
        verify(view.implicitHeight > 0);
    }
}
