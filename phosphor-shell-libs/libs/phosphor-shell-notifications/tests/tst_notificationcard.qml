// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
import QtQuick
import QtTest
import Phosphor.Notifications

TestCase {
    id: testCase
    name: "NotificationCard"
    when: windowShown
    width: 500
    height: 900
    visible: true
    Component {
        id: cardComponent
        NotificationCard {
            width: 396
            height: implicitHeight
            arrival: true
        }
    }
    Component {
        id: backendComponent
        QtObject {
            property int replies: 0
            property string lastReply: ""
            function reply(id, text) {
                replies++;
                lastReply = text;
                return true;
            }
        }
    }
    function payload() {
        return {
            id: 1,
            appName: "Chat",
            appIcon: "mail-message",
            summary: "Private title",
            body: "A message with enough text to wrap. ".repeat(40),
            live: true,
            unread: true,
            actions: [
                {
                    key: "inline-reply",
                    label: "Reply"
                }
            ]
        };
    }
    function test_long_message_expands_without_losing_content() {
        const card = createTemporaryObject(cardComponent, testCase, {
            notification: payload()
        });
        const body = findChild(card, "notificationMessage");
        const toggle = findChild(card, "expandNotification");
        tryVerify(() => body.truncated);
        const collapsed = card.height;
        toggle.clicked();
        verify(card.expanded);
        tryVerify(() => card.height > collapsed);
        compare(body.text, payload().body);
        verify(card.held, "expanded content pauses expiry");
        toggle.clicked();
        tryCompare(card, "height", collapsed);
    }
    function test_hidden_previews_remove_content_and_accessible_text() {
        const card = createTemporaryObject(cardComponent, testCase, {
            notification: payload(),
            previews: false
        });
        compare(findChild(card, "notificationMessage").text, "");
        verify(!card.Accessible.name.includes("Private title"));
        verify(!findChild(card, "notificationReplyAction").visible);
        card.previews = true;
        compare(findChild(card, "notificationMessage").text, payload().body);
    }
    function test_reply_draft_survives_payload_replacement() {
        const backend = createTemporaryObject(backendComponent, testCase);
        const card = createTemporaryObject(cardComponent, testCase, {
            notification: payload(),
            backend: backend
        });
        findChild(card, "notificationReplyAction").clicked();
        const reply = findChild(card, "notificationReply");
        reply.text = "Keep my draft";
        const updated = payload();
        updated.body = "New content";
        card.notification = updated;
        compare(reply.text, "Keep my draft");
        verify(card.replying);
        findChild(card, "sendNotificationReply").clicked();
        compare(backend.replies, 1);
        compare(backend.lastReply, "Keep my draft");
        compare(reply.text, "");
    }
    function test_privacy_clears_an_open_reply() {
        const card = createTemporaryObject(cardComponent, testCase, {
            notification: payload()
        });
        findChild(card, "notificationReplyAction").clicked();
        findChild(card, "notificationReply").text = "Private draft";
        card.previews = false;
        verify(!card.replying);
        compare(findChild(card, "notificationReply").text, "");
    }
}
