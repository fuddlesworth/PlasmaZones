// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
import QtQuick
import QtTest
import Phosphor.Polkit
import Phosphor.Theme

Item {
    id: root
    width: 1440
    height: 900
    property var savedSettings: ({})

    Component {
        id: requestComponent
        QtObject {
            property string message: qsTr("Authentication is required to install system updates.")
            property string actionId: "org.freedesktop.packagekit.system-update"
            property string iconName: "system-software-update"
            property var details: ({
                    "program": "/usr/bin/pkexec"
                })
            property var identities: ["test-user"]
            property int selectedIdentity: 0
            property string prompt: qsTr("Password: ")
            property bool echo: false
        }
    }
    Component {
        id: agentComponent
        QtObject {
            property var activeRequest: null
            property string phase: "prompt"
            property bool inputReady: true
            property bool busy: false
            property bool canRetry: false
            property string info: ""
            property string lastError: ""
            property string requesterProgram: "/usr/bin/pkexec"
            property string requesterResource: ""
            property var responses: []
            property bool deferResponseState: false
            property var identitySelections: []
            property int cancels: 0
            property int retries: 0
            signal promptRequested(string prompt, bool echo)

            function respond(text) {
                responses = responses.concat([text]);
                if (deferResponseState)
                    return;
                inputReady = false;
                busy = true;
                phase = "checking";
            }
            function cancel() {
                ++cancels;
                activeRequest = null;
                inputReady = false;
                busy = false;
                phase = "cancelled";
            }
            function retry() {
                ++retries;
                canRetry = false;
                inputReady = false;
                busy = true;
                phase = "starting";
            }
            function selectIdentity(index) {
                identitySelections = identitySelections.concat([index]);
                activeRequest.selectedIdentity = index;
                inputReady = false;
                busy = true;
                phase = "starting";
            }
            function issuePrompt(text, echo) {
                activeRequest.prompt = text;
                activeRequest.echo = echo;
                phase = "prompt";
                busy = false;
                inputReady = true;
                promptRequested(text, echo);
            }
        }
    }
    Component {
        id: keyboardComponent
        QtObject {
            property string layoutName: "US"
            property bool capsLock: false
            property bool canCycle: false
        }
    }
    Component {
        id: promptComponent
        PolkitPrompt {
            width: implicitWidth
            height: implicitHeight
            errorText: agent ? agent.lastError : ""
        }
    }
    Component {
        id: surfaceComponent
        PolkitSurface {
            width: 1440
            height: 900
            errorText: agent ? agent.lastError : ""
        }
    }

    TestCase {
        id: tests
        name: "PolkitAuthentication"
        when: windowShown

        SignalSpy {
            id: submittedSpy
            signalName: "submitted"
        }
        SignalSpy {
            id: cancelledSpy
            signalName: "cancelled"
        }

        function initTestCase() {
            root.savedSettings = JSON.parse(JSON.stringify(AppearanceStore.values));
        }
        function init() {
            verify(AppearanceStore.applyPreset("phosphor"));
            verify(AppearanceStore.setValue("motion", false));
            verify(AppearanceStore.setValue("textScale", 100));
        }
        function cleanup() {
            submittedSpy.target = null;
            cancelledSpy.target = null;
            AppearanceStore.setValues(root.savedSettings);
        }
        function child(owner, name) {
            const item = findChild(owner, name);
            verify(!!item, "Object exists");
            return item;
        }
        function scene(component, requestProperties, itemProperties) {
            const request = createTemporaryObject(requestComponent, root, requestProperties || {});
            verify(!!request, "Object exists");
            const agent = createTemporaryObject(agentComponent, root, {
                activeRequest: request
            });
            verify(!!agent, "Object exists");
            const keyboard = createTemporaryObject(keyboardComponent, root);
            verify(!!keyboard, "Object exists");
            const item = createTemporaryObject(component || promptComponent, root, Object.assign({
                agent: agent,
                request: request,
                requester: "Package installer",
                keyboard: keyboard
            }, itemProperties || {}));
            verify(!!item, "Component exists");
            waitForRendering(item);
            return {
                item: item,
                agent: agent,
                request: request,
                keyboard: keyboard
            };
        }
        function focusField(prompt) {
            const field = child(prompt, "polkitField");
            field.focus = true;
            prompt.focusInput();
            tryCompare(field, "activeFocus", true);
            return field;
        }

        function test_enterSubmitsOnceAndClearsTheSecret() {
            const s = scene();
            focusField(s.item);
            submittedSpy.target = s.item;
            submittedSpy.clear();
            const secret = "påsswörd-42! Ω-\u00e9\u0301-\ud83d\udd11";
            s.item.password = secret;
            keyClick(Qt.Key_Return);
            compare(s.agent.responses, [secret]);
            compare(s.item.password, "");
            compare(submittedSpy.count, 1);
            keyClick(Qt.Key_Return);
            s.item.submit();
            compare(s.agent.responses.length, 1);
        }
        function test_escapeCancelsFromTheFocusedField() {
            const s = scene();
            focusField(s.item);
            cancelledSpy.target = s.item;
            cancelledSpy.clear();
            s.item.password = "cancelled-secret";
            keyClick(Qt.Key_Escape);
            compare(s.agent.cancels, 1);
            compare(s.item.password, "");
            compare(cancelledSpy.count, 1);
            compare(s.agent.responses.length, 0);
        }
        function test_enterActivatesFocusedActions() {
            const s = scene();
            focusField(s.item);
            s.item.password = "ready-to-submit";
            keyClick(Qt.Key_Backtab, Qt.ShiftModifier);
            tryCompare(child(s.item, "polkitSubmit"), "activeFocus", true);
            keyClick(Qt.Key_Return);
            compare(s.agent.responses, ["ready-to-submit"]);
            compare(s.item.password, "");
            child(s.item, "polkitCancel").forceActiveFocus();
            keyClick(Qt.Key_Enter);
            compare(s.agent.cancels, 1);
        }
        function test_duplicateSubmitWaitsForANewPamPrompt() {
            const s = scene();
            s.agent.deferResponseState = true;
            s.item.password = "first-answer";
            s.item.submit();
            s.item.password = "duplicate-answer";
            s.item.submit();
            compare(s.agent.responses, ["first-answer"]);
            s.agent.issuePrompt(s.request.prompt, false);
            compare(s.item.password, "");
            s.item.password = "second-prompt-answer";
            s.item.submit();
            compare(s.agent.responses, ["first-answer", "second-prompt-answer"]);
        }
        function test_cancelRemainsUsableWhileChecking() {
            const s = scene();
            s.item.password = "answer";
            s.item.submit();
            const cancel = child(s.item, "polkitCancel");
            cancel.focus = true;
            cancel.forceActiveFocus();
            tryCompare(cancel, "activeFocus", true);
            keyClick(Qt.Key_Space);
            compare(s.agent.cancels, 1);
            compare(s.item.password, "");
        }
        function test_unavailableRequestWithoutRetryCannotSubmit() {
            const s = scene();
            s.agent.phase = "unavailable";
            s.agent.inputReady = false;
            s.agent.busy = false;
            s.item.password = "not-an-answer";
            s.item.submit();
            compare(s.agent.responses.length, 0);
            compare(child(s.item, "polkitSubmit").enabled, false);
        }
        function test_staleCardCannotActOnTheNextRequest() {
            const s = scene();
            const next = createTemporaryObject(requestComponent, root, {
                actionId: "org.example.next"
            });
            verify(!!next, "Object exists");
            s.agent.activeRequest = next;
            s.item.password = "old-request-secret";
            s.item.submit();
            s.item.cancel();
            compare(s.agent.responses.length, 0);
            compare(s.agent.cancels, 0);
            compare(s.agent.activeRequest, next);
        }
        function test_requestReplacementClearsSecretAndReveal() {
            const s = scene();
            const field = focusField(s.item);
            const reveal = child(s.item, "polkitReveal");
            s.item.password = "old-request-secret";
            mouseClick(reveal);
            tryCompare(field, "echoMode", TextInput.Normal);
            const next = createTemporaryObject(requestComponent, root, {
                actionId: "org.example.next"
            });
            verify(!!next, "Object exists");
            s.agent.activeRequest = next;
            s.item.request = next;
            tryCompare(s.item, "password", "");
            tryCompare(field, "echoMode", TextInput.Password);
        }
        function test_repeatedPamPromptClearsSecretAndReveal() {
            const s = scene();
            const field = focusField(s.item);
            const reveal = child(s.item, "polkitReveal");
            for (let attempt = 0; attempt < 2; ++attempt) {
                s.item.password = "partial-answer-" + attempt;
                mouseClick(reveal);
                tryCompare(field, "echoMode", TextInput.Normal);
                // An unchanged prompt property must still start fresh input.
                s.agent.issuePrompt(s.request.prompt, false);
                tryCompare(s.item, "password", "");
                tryCompare(field, "echoMode", TextInput.Password);
                tryCompare(field, "activeFocus", true);
            }
        }
        function test_pamEchoAndErrorDoNotLeakEarlierInput() {
            const s = scene();
            const field = focusField(s.item);
            s.item.password = "secret";
            s.agent.issuePrompt(qsTr("Verification code: "), true);
            compare(s.item.password, "");
            compare(field.echoMode, TextInput.Normal);
            s.item.password = "123456";
            s.agent.lastError = qsTr("The verification code was rejected.");
            compare(s.item.password, "");
            s.agent.issuePrompt(qsTr("Password: "), false);
            compare(field.echoMode, TextInput.Password);
        }
        function test_identityChangeClearsThePreviousIdentitySecret() {
            const s = scene(promptComponent, {
                identities: ["test-user", "administrator"]
            });
            const field = focusField(s.item);
            s.item.password = "first-identity-secret";
            mouseClick(child(s.item, "polkitReveal"));
            tryCompare(field, "echoMode", TextInput.Normal);
            const identity = child(s.item, "polkitIdentity");
            identity.focus = true;
            identity.forceActiveFocus();
            tryCompare(identity, "activeFocus", true);
            keyClick(Qt.Key_Down);
            tryCompare(s.agent, "identitySelections", [1]);
            tryCompare(s.item, "password", "");
            tryCompare(field, "echoMode", TextInput.Password);
            compare(s.agent.responses.length, 0);
        }
        function test_retryRestartsAuthenticationWithoutSendingASecret() {
            const s = scene();
            s.agent.phase = "unavailable";
            s.agent.inputReady = false;
            s.agent.canRetry = true;
            const submit = child(s.item, "polkitSubmit");
            mouseClick(submit);
            tryCompare(s.agent, "retries", 1);
            tryCompare(s.agent, "responses", []);
            tryCompare(submit, "enabled", false);
        }
        function test_tabCycleStaysInsideTheDialog() {
            const s = scene();
            const field = focusField(s.item);
            s.item.password = "ready-to-submit";
            const order = ["polkitReveal", "polkitDetails", "polkitCancel", "polkitSubmit", "polkitField"];
            for (const name of order) {
                keyClick(Qt.Key_Tab);
                tryCompare(child(s.item, name), "activeFocus", true);
            }
            keyClick(Qt.Key_Backtab, Qt.ShiftModifier);
            tryCompare(child(s.item, "polkitSubmit"), "activeFocus", true);
            keyClick(Qt.Key_Tab);
            tryCompare(field, "activeFocus", true);
        }
        function test_multipleIdentitiesJoinTheTabCycleBeforeTheField() {
            const s = scene(promptComponent, {
                identities: ["test-user", "administrator"]
            });
            const field = focusField(s.item);
            s.item.password = "ready-to-submit";
            keyClick(Qt.Key_Backtab, Qt.ShiftModifier);
            tryCompare(child(s.item, "polkitIdentity"), "activeFocus", true);
            keyClick(Qt.Key_Backtab, Qt.ShiftModifier);
            tryCompare(child(s.item, "polkitSubmit"), "activeFocus", true);
            keyClick(Qt.Key_Tab);
            tryCompare(child(s.item, "polkitIdentity"), "activeFocus", true);
            keyClick(Qt.Key_Tab);
            tryCompare(field, "activeFocus", true);
        }
        function test_policyAndPamStringsRemainPlainText() {
            const s = scene(promptComponent, {
                message: "<b>Grant root?</b><img src='http://example.invalid/x.png'>",
                prompt: "<u>Password</u>: ",
                actionId: "<i>org.example.action</i>"
            }, {
                requester: "<em>Requester</em>"
            });
            s.agent.lastError = "<s>Authentication failed</s>";
            for (const name of ["polkitRequester", "polkitDescription", "polkitMessage", "polkitFieldLabel"])
                compare(child(s.item, name).textFormat, Text.PlainText);
            compare(child(s.item, "polkitDescription").text, s.request.message);
            compare(child(s.item, "polkitRequester").text, s.item.requester);
            compare(child(s.item, "polkitMessage").text, s.agent.lastError);
        }
        function test_keyboardLayoutAndCapsLockFollowTheProvider() {
            const s = scene();
            const layout = child(s.item, "polkitKeyboard");
            const caps = child(s.item, "polkitCaps");
            compare(layout.text, "US");
            compare(caps.visible, false);
            s.keyboard.layoutName = "DE";
            s.keyboard.capsLock = true;
            compare(layout.text, "DE");
            compare(caps.visible, true);
            s.keyboard.capsLock = false;
            compare(caps.visible, false);
        }
        function test_statusReservesSpaceForErrorsAndChecking() {
            const s = scene();
            const details = child(s.item, "polkitDetails");
            const message = child(s.item, "polkitMessage");
            const detailsY = details.mapToItem(s.item, 0, 0).y;
            const cardHeight = s.item.height;
            s.agent.lastError = qsTr("Authentication failed.\nPlease try again.");
            tryCompare(message, "text", s.agent.lastError);
            waitForRendering(s.item);
            fuzzyCompare(details.mapToItem(s.item, 0, 0).y, detailsY, 1);
            fuzzyCompare(s.item.height, cardHeight, 1);
            s.agent.lastError = "";
            s.agent.info = qsTr("Touch your security key to continue.");
            compare(message.text, s.agent.info);
            s.agent.info = "";
            s.item.password = "answer";
            s.item.submit();
            waitForRendering(s.item);
            fuzzyCompare(details.mapToItem(s.item, 0, 0).y, detailsY, 1);
            fuzzyCompare(s.item.height, cardHeight, 1);
            compare(s.agent.responses, ["answer"]);
        }
        function test_surfaceUsesCenteredCardInsteadOfRequesterGeometry() {
            const s = scene(surfaceComponent);
            const card = child(s.item, "polkitPrompt");
            tryCompare(card, "width", 460);
            compare(s.item.fullScreen, true);
            const origin = card.mapToItem(s.item, 0, 0);
            fuzzyCompare(origin.x + card.width / 2, s.item.width / 2, 1);
            fuzzyCompare(origin.y + card.height / 2, s.item.height * 0.45, 1);
        }
        function test_shortDisplayScrollsLongDetailsAndKeepsActionsVisible() {
            verify(AppearanceStore.setValue("textScale", 115));
            const s = scene(surfaceComponent, {
                message: qsTr("This application needs authorization to update protected system settings. ").repeat(12),
                actionId: "org.example." + "long-policy-identifier-".repeat(18)
            }, {
                width: 800,
                height: 600
            });
            s.agent.requesterProgram = "/usr/libexec/" + "long-program-directory/".repeat(12) + "helper";
            const card = child(s.item, "polkitPrompt");
            const content = child(card, "polkitContent");
            const cancel = child(card, "polkitCancel");
            const submit = child(card, "polkitSubmit");
            tryVerify(() => content.contentHeight > content.height);
            const origin = card.mapToItem(s.item, 0, 0);
            verify(origin.y >= 0);
            verify(origin.y + card.height <= s.item.height);
            const cancelY = cancel.mapToItem(s.item, 0, 0).y;
            const submitY = submit.mapToItem(s.item, 0, 0).y;
            verify(cancelY >= 0 && cancelY + cancel.height <= s.item.height);
            verify(submitY >= 0 && submitY + submit.height <= s.item.height);
            content.contentY = content.contentHeight - content.height;
            const oldContentHeight = content.contentHeight;
            mouseClick(child(card, "polkitDetails"));
            tryVerify(() => content.contentHeight > oldContentHeight);
            fuzzyCompare(cancel.mapToItem(s.item, 0, 0).y, cancelY, 1);
            fuzzyCompare(submit.mapToItem(s.item, 0, 0).y, submitY, 1);
            mouseClick(cancel);
            tryCompare(s.agent, "cancels", 1);
        }
        function test_narrowDisplayKeepsTheCardAndActionsWithinTheOutput() {
            const s = scene(surfaceComponent, {}, {
                width: 360,
                height: 640
            });
            const card = child(s.item, "polkitPrompt");
            tryVerify(() => card.width > 0 && card.width < s.item.width);
            for (const control of [card, child(card, "polkitCancel"), child(card, "polkitSubmit")]) {
                const point = control.mapToItem(s.item, 0, 0);
                verify(point.x >= 0 && point.x + control.width <= s.item.width);
                verify(point.y >= 0 && point.y + control.height <= s.item.height);
            }
            mouseClick(child(card, "polkitCancel"));
            tryCompare(s.agent, "cancels", 1);
        }
    }
}
