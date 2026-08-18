// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

import QtQuick
import QtQuick.Layouts
import QtQuick.Controls as QQC2
import org.kde.kirigami as Kirigami

/**
 * Centered busy indicator for content that builds asynchronously (page
 * Loaders, deferred models). Drive `loading` from the host's load state;
 * the spinner only appears after `graceDelay` ms of CONTINUOUS loading,
 * so fast loads never flash a spinner, and it fades in rather than
 * popping so a page that finishes just past the grace window still
 * feels smooth.
 *
 * Fills whatever area it is anchored to and takes no input, so it can
 * sit above the (still-invisible) incubating content.
 */
Item {
    id: root

    /** Whether the hosted content is currently loading. */
    property bool loading: false
    /** Continuous-loading time in ms before the spinner appears. */
    property int graceDelay: 200
    /** Caption under the spinner; empty hides the label. */
    property string text: qsTr("Loading…")

    // Grace latch: armed by the timer, dropped the instant loading ends.
    property bool _pastGrace: false

    visible: root.loading && root._pastGrace
    opacity: visible ? 1 : 0

    onLoadingChanged: {
        if (root.loading) {
            graceTimer.restart();
        } else {
            graceTimer.stop();
            root._pastGrace = false;
        }
    }
    // A host that is already loading when this component instantiates
    // never fires onLoadingChanged for that initial value — arm the
    // grace timer explicitly.
    Component.onCompleted: if (root.loading)
        graceTimer.restart()

    Behavior on opacity {
        NumberAnimation {
            duration: Kirigami.Units.shortDuration
        }
    }

    Timer {
        id: graceTimer

        interval: root.graceDelay
        onTriggered: root._pastGrace = true
    }

    ColumnLayout {
        anchors.centerIn: parent
        spacing: Kirigami.Units.largeSpacing

        QQC2.BusyIndicator {
            Layout.alignment: Qt.AlignHCenter
            running: root.visible
            Accessible.name: root.text.length > 0 ? root.text : qsTr("Loading…")
        }

        QQC2.Label {
            Layout.alignment: Qt.AlignHCenter
            visible: root.text.length > 0
            text: root.text
            opacity: 0.7
        }
    }
}
