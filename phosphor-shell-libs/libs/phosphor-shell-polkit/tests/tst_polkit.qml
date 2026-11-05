// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
// Behaviour tests for the polkit prompt: the anchor resolves a window's
// cell rect through a fake map and tracker and falls back when any link
// is missing; the prompt's Enter / Escape / actions reach a fake agent
// with PolkitAgent's respond / cancel names; an error clears the field;
// the dim leaves the requester's hole clear.

import QtQuick
import QtTest
import Phosphor.Polkit

TestCase {
    id: testCase

    name: "PolkitPrompt"
    when: windowShown
    width: 800
    height: 600

    // A stand-in for PlacementMapScreen: cellRect(id) and changed().
    Component {
        id: fakeMapComp

        QtObject {
            property rect w1Rect: Qt.rect(100, 40, 240, 200)
            signal changed
            function cellRect(id) {
                return id === "w1" ? w1Rect : Qt.rect(0, 0, 0, 0);
            }
        }
    }

    // A stand-in for WindowTracking: findWindowByPid(pid).
    Component {
        id: fakeTrackingComp

        QtObject {
            function findWindowByPid(pid) {
                return pid === 42 ? "w1" : "";
            }
        }
    }

    // A stand-in for PolkitAgent, by name: respond(text) and cancel().
    Component {
        id: fakeAgentComp

        QtObject {
            property var responses: []
            property int cancels: 0
            function respond(text) {
                responses = responses.concat([text]);
            }
            function cancel() {
                ++cancels;
            }
        }
    }

    Component {
        id: fakeRequestComp

        QtObject {
            property string message: "Install a system update?"
            property string actionId: "org.freedesktop.packagekit.system-update"
            property string prompt: "Password: "
            property bool echo: false
        }
    }

    Component {
        id: anchorComp

        PolkitAnchor {}
    }

    Component {
        id: promptComp

        PolkitPrompt {}
    }

    Component {
        id: dimComp

        PolkitDim {
            width: 800
            height: 600
        }
    }

    function test_anchor_resolves_the_requesters_cell() {
        const map = createTemporaryObject(fakeMapComp, testCase);
        const tracking = createTemporaryObject(fakeTrackingComp, testCase);
        const a = createTemporaryObject(anchorComp, testCase, {
            "pid": 42,
            "placementMap": map,
            "windowTracking": tracking
        });
        verify(a.trackingAvailable);
        compare(a.windowId, "w1");
        verify(a.anchored, "a window the map shows anchors the prompt");
        compare(a.rect.x, 100);
        compare(a.rect.y, 40);
        compare(a.rect.width, 240);
        compare(a.rect.height, 200);

        // The window moved: bumping the epoch re-reads the rect.
        map.w1Rect = Qt.rect(300, 80, 240, 200);
        a.epoch++;
        compare(a.rect.x, 300);
        compare(a.rect.y, 80);
    }

    function test_anchor_falls_back_without_a_window() {
        const map = createTemporaryObject(fakeMapComp, testCase);
        const tracking = createTemporaryObject(fakeTrackingComp, testCase);
        // An unknown pid.
        const noWindow = createTemporaryObject(anchorComp, testCase, {
            "pid": 7,
            "placementMap": map,
            "windowTracking": tracking
        });
        compare(noWindow.windowId, "");
        verify(!noWindow.anchored);
        compare(noWindow.rect, null);
        // No tracker at all (the capability latch).
        const noTracking = createTemporaryObject(anchorComp, testCase, {
            "pid": 42,
            "placementMap": map
        });
        verify(!noTracking.trackingAvailable);
        verify(!noTracking.anchored);
        // A tracker that lacks the method.
        const bare = createTemporaryObject(fakeRequestComp, testCase);
        const noMethod = createTemporaryObject(anchorComp, testCase, {
            "pid": 42,
            "placementMap": map,
            "windowTracking": bare
        });
        verify(!noMethod.trackingAvailable);
        // No map.
        const noMap = createTemporaryObject(anchorComp, testCase, {
            "pid": 42,
            "windowTracking": tracking
        });
        compare(noMap.windowId, "w1");
        verify(!noMap.anchored);
        // No pid.
        const noPid = createTemporaryObject(anchorComp, testCase, {
            "placementMap": map,
            "windowTracking": tracking
        });
        verify(!noPid.anchored);
    }

    function test_band_spans_the_window_when_anchored_and_the_card_otherwise() {
        const p = createTemporaryObject(promptComp, testCase, {
            "anchored": true,
            "bandWidth": 900
        });
        compare(p.implicitWidth, 900, "the band spans the window's width");
        p.anchored = false;
        compare(p.implicitWidth, 360, "the fallback band is the card's width");
        verify(p.implicitHeight >= 2 + 8 + 140, "the card is at least 140 tall under its band");
    }

    function test_enter_submits_the_password_to_the_agent_and_clears_it() {
        const agent = createTemporaryObject(fakeAgentComp, testCase);
        const request = createTemporaryObject(fakeRequestComp, testCase);
        const p = createTemporaryObject(promptComp, testCase, {
            "agent": agent,
            "request": request
        });
        p.forceActiveFocus();
        let submitted = 0;
        p.submitted.connect(() => ++submitted);
        p.password = "hunter2";
        keyClick(Qt.Key_Return);
        compare(agent.responses.length, 1, "Enter responds once");
        compare(agent.responses[0], "hunter2");
        compare(p.password, "", "the field is cleared after submit");
        compare(submitted, 1);
        compare(agent.cancels, 0);
    }

    function test_escape_cancels() {
        const agent = createTemporaryObject(fakeAgentComp, testCase);
        const p = createTemporaryObject(promptComp, testCase, {
            "agent": agent,
            "request": createTemporaryObject(fakeRequestComp, testCase)
        });
        p.forceActiveFocus();
        let cancelled = 0;
        p.cancelled.connect(() => ++cancelled);
        p.password = "abc";
        keyClick(Qt.Key_Escape);
        compare(agent.cancels, 1);
        compare(agent.responses.length, 0);
        compare(p.password, "", "a cancelled entry is dropped");
        compare(cancelled, 1);
    }

    function test_submit_and_cancel_by_call() {
        const agent = createTemporaryObject(fakeAgentComp, testCase);
        const p = createTemporaryObject(promptComp, testCase, {
            "agent": agent
        });
        p.password = "x";
        p.submit();
        compare(agent.responses, ["x"]);
        p.cancel();
        compare(agent.cancels, 1);
    }

    function test_error_clears_the_field_and_shows() {
        const agent = createTemporaryObject(fakeAgentComp, testCase);
        const p = createTemporaryObject(promptComp, testCase, {
            "agent": agent,
            "request": createTemporaryObject(fakeRequestComp, testCase)
        });
        p.password = "wrong";
        p.errorText = "Authentication failure";
        compare(p.password, "", "the wrong password is cleared");
        p.errorText = "";
        p.password = "kept";
        compare(p.password, "kept", "clearing the error leaves the field alone");
    }

    function test_prompt_text_follows_the_request() {
        const request = createTemporaryObject(fakeRequestComp, testCase);
        const p = createTemporaryObject(promptComp, testCase, {
            "request": request
        });
        compare(p.message, "Install a system update?");
        compare(p.actionId, "org.freedesktop.packagekit.system-update");
        compare(p.fieldPrompt, "Password", "PAM's trailing colon is dropped");
        verify(!p.echo);
        const none = createTemporaryObject(promptComp, testCase);
        compare(none.fieldPrompt, "Password", "no request means the default label");
    }

    // Collect every descendant carrying a `textFormat`, so the assertions
    // below cover whatever Texts the prompt happens to be built from rather
    // than a list of item ids that would rot.
    function _textItems(item, out) {
        const kids = item.children;
        for (let i = 0; i < kids.length; ++i) {
            const kid = kids[i];
            if (kid.textFormat !== undefined && kid.text !== undefined)
                out.push(kid);
            _textItems(kid, out);
        }
        return out;
    }

    function test_request_strings_render_as_plain_text() {
        // The message, the action id and PAM's prompt all arrive from the
        // requesting process. Rendered as rich text they would let a caller
        // put markup, or an <img> fetch, inside the authentication dialog.
        const request = createTemporaryObject(fakeRequestComp, testCase);
        request.message = "<b>Grant root?</b><img src='http://example.invalid/x.png'>";
        request.actionId = "<i>org.example.evil</i>";
        request.prompt = "<u>Password</u>: ";
        const p = createTemporaryObject(promptComp, testCase, {
            "request": request
        });
        p.errorText = "<s>failed</s>";
        const texts = _textItems(p, []);
        verify(texts.length > 0, "the prompt renders some text");
        let sawMessage = false;
        for (let i = 0; i < texts.length; ++i) {
            const t = texts[i];
            if (String(t.text).indexOf("<") < 0)
                continue;
            // Anything showing caller-supplied angle brackets must show them
            // literally, never parse them.
            compare(t.textFormat, Text.PlainText, "caller text is rendered as plain text: " + t.text);
            if (String(t.text).indexOf("Grant root?") >= 0)
                sawMessage = true;
        }
        verify(sawMessage, "the request message reaches a Text item");
        compare(p.message, request.message, "the message is passed through unaltered");
    }

    function test_repeated_failures_each_clear_the_field() {
        // A wrong password is the common case and it happens more than once.
        const agent = createTemporaryObject(fakeAgentComp, testCase);
        const p = createTemporaryObject(promptComp, testCase, {
            "agent": agent,
            "request": createTemporaryObject(fakeRequestComp, testCase)
        });
        for (let attempt = 0; attempt < 3; ++attempt) {
            p.password = "wrong" + attempt;
            p.submit();
            compare(agent.responses[attempt], "wrong" + attempt);
            p.errorText = "Authentication failure " + attempt;
            compare(p.password, "", "attempt " + attempt + " leaves no stale entry");
            // Clearing the error between attempts must not re-arm anything.
            p.errorText = "";
        }
        compare(agent.responses.length, 3);
        compare(agent.cancels, 0);
    }

    function test_a_unicode_password_reaches_the_agent_unchanged() {
        // Passwords are not ASCII. Anything that normalised or re-encoded on
        // the way through would reject a correct password.
        const agent = createTemporaryObject(fakeAgentComp, testCase);
        const p = createTemporaryObject(promptComp, testCase, {
            "agent": agent,
            "request": createTemporaryObject(fakeRequestComp, testCase)
        });
        const secret = "påsswörd-Ω-\u00e9\u0301-\ud83d\udd11";
        p.password = secret;
        p.submit();
        compare(agent.responses.length, 1);
        compare(agent.responses[0], secret, "byte for byte what was typed");
        compare(agent.responses[0].length, secret.length);
        compare(p.password, "");
    }

    function test_submit_without_an_agent_is_a_no_op() {
        const p = createTemporaryObject(promptComp, testCase);
        p.password = "x";
        p.submit();
        compare(p.password, "x", "nothing to respond to keeps the entry");
        p.cancel();
    }

    function test_dim_leaves_the_requesters_hole() {
        const d = createTemporaryObject(dimComp, testCase, {
            "active": true,
            "hole": Qt.rect(100, 40, 240, 200)
        });
        verify(d.holed);
        // Four panes in declaration order: above, below, left, right.
        const panes = d.children;
        compare(panes.length, 4);
        compare(panes[0].height, 40);
        compare(panes[1].y, 240);
        compare(panes[1].height, 360);
        compare(panes[2].width, 100);
        compare(panes[3].x, 340);
        compare(panes[3].width, 460);

        d.hole = null;
        verify(!d.holed);
        compare(panes[0].height, 600, "without a hole the dim is the whole screen");
        compare(panes[1].height, 0, "the other panes collapse");
        compare(panes[2].width, 0);
        compare(panes[3].width, 0);
    }
}
