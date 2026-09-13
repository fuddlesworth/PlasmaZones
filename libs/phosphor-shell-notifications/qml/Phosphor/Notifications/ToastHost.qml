// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
import QtQuick
import QtQuick.Controls
import Phosphor.Theme

Item {
    id: host
    property int maxVisible: 3
    property int defaultTimeout: 5000
    property real spacing: 12
    property real margins: 22
    property real topInset: Appearance.bottom ? 24 : Appearance.barHeight + 28
    property real bottomInset: Appearance.bottom ? Appearance.barHeight + 28 : 24
    property bool bottomEdge: Appearance.bottom
    property bool previews: Appearance.notificationPreviews
    property var backend: null
    property var rules: null
    property Component decoration: null
    property string screenName: ""
    property var queue: []
    property int nextId: 1
    readonly property int activeCount: activeModel.count
    readonly property int queuedCount: queue.length
    readonly property var inputRects: activeCount ? [Qt.rect(list.x, list.y, list.width, list.height)] : []
    signal toastDismissed(int id)
    signal openCenterRequested
    // All arrivals share the screen's status edge. Stable notification IDs
    // replace existing cards; a bounded queue handles bursts without overflow.
    function show(toast) {
        const row = Object.assign({}, toast);
        if (rules) {
            const result = rules.evaluate(row);
            if (result && result.suppress)
                return -1;
            if (result && result.timeout !== undefined && !row.managed)
                row.timeout = result.timeout;
        }
        const id = row.id === undefined ? nextId++ : Number(row.id);
        nextId = Math.max(nextId, id + 1);
        row.id = id;
        if (row.timeout === undefined)
            row.timeout = defaultTimeout;
        if (updateNotification(row))
            return id;
        if (activeCount < maxVisible)
            activeModel.insert(0, {
                toastId: id,
                payload: row
            });
        else {
            queue = queue.concat([row]).slice(-50);
        }
        return id;
    }
    function updateNotification(toast) {
        for (let i = 0; i < activeModel.count; ++i) {
            if (activeModel.get(i).toastId === Number(toast.id)) {
                activeModel.setProperty(i, "payload", toast);
                return true;
            }
        }
        const pending = queue.slice();
        for (let i = 0; i < pending.length; ++i) {
            if (pending[i].id === Number(toast.id)) {
                pending[i] = toast;
                queue = pending;
                return true;
            }
        }
        return false;
    }
    function dismiss(id) {
        id = Number(id);
        for (let i = 0; i < activeModel.count; ++i) {
            if (activeModel.get(i).toastId === id) {
                activeModel.remove(i);
                host.toastDismissed(id);
                promote();
                return;
            }
        }
        const remaining = queue.filter(row => row.id !== id);
        if (remaining.length !== queue.length) {
            queue = remaining;
            host.toastDismissed(id);
        }
    }
    function promote() {
        while (queue.length && activeModel.count < maxVisible) {
            const row = queue[0];
            queue = queue.slice(1);
            activeModel.append({
                toastId: row.id,
                payload: row
            });
        }
    }
    function clear() {
        const ids = queue.map(row => row.id);
        for (let i = 0; i < activeModel.count; ++i)
            ids.push(activeModel.get(i).toastId);
        queue = [];
        activeModel.clear();
        ids.forEach(id => host.toastDismissed(id));
    }
    ListModel {
        id: activeModel
        dynamicRoles: true
    }
    ListView {
        id: list
        objectName: "arrivalStack"
        x: Math.max(0, host.width - width - host.margins)
        y: host.bottomEdge ? host.height - host.bottomInset - height : host.topInset
        width: Math.min(396, host.width - host.margins * 2)
        height: Math.max(0, Math.min(contentHeight, host.height - host.topInset - host.bottomInset))
        model: activeModel
        spacing: host.spacing
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        interactive: contentHeight > height
        verticalLayoutDirection: host.bottomEdge ? ListView.BottomToTop : ListView.TopToBottom
        ScrollBar.vertical: ScrollBar {
            policy: list.contentHeight > list.height ? ScrollBar.AsNeeded : ScrollBar.AlwaysOff
        }
        add: Transition {
            NumberAnimation {
                property: "opacity"
                from: 0
                to: 1
                duration: Appearance.motion ? 140 : 0
            }
        }
        delegate: Toast {
            required property var payload
            required property int toastId
            width: list.width
            height: implicitHeight
            notification: payload
            backend: host.backend
            previews: host.previews
            decoration: host.decoration
            onPauseRequested: paused => {
                if (host.backend && payload.managed)
                    host.backend.setExpiryPaused(toastId, paused);
            }
            onDismissed: {
                if (host.backend && payload.managed)
                    host.backend.dismissPopup(toastId);
                else
                    host.dismiss(toastId);
            }
            onOpenCenterRequested: host.openCenterRequested()
        }
    }
}
