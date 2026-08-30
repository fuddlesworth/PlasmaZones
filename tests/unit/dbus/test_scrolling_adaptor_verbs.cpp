// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_scrolling_adaptor_verbs.cpp
 * @brief The wire VERBS of ScrollingAdaptor: the wheel pair and the absolute
 *        width/height setters.
 *
 * Split out of test_scrolling_adaptor.cpp once that file passed the size
 * ceiling; both suites share scrollingadaptortestfixture.h. What this file
 * pins, beyond the contract-sync test's shape check:
 *
 *  1. focusColumn refuses a screen the engine does not own, so a wheel event
 *     aimed at a non-scrolling monitor is never redirected onto the active
 *     scrolling one; refuses for a disabled context, and with no context
 *     gate installed at all (fail-closed); and it maps its delta to the
 *     documented direction.
 *  2. scrollView pans the view by exactly one provider step per call and
 *     never moves focus: no provider, a foreign screen, a bad delta, and a
 *     refusing (or missing) context gate each leave the anchor untouched, a
 *     notch into the strip's end answers no_target, consecutive notches add
 *     up, and the opposite direction walks the same distance back.
 *  3. The four absolute setters share focusColumn's ownership and
 *     per-context gates (each setter is driven through a refusing gate, since
 *     the gate is per-method code), refuse out-of-range and non-finite values
 *     silently (inclusive at both bounds), and write the intent kind each
 *     form documents — width proportion exact, width/height px Fixed, height
 *     proportion a Preset anchor that relayout snaps to the height vocabulary.
 *  4. toggleMaximizeColumn takes the same ownership, empty-screen and
 *     per-context gates as the setters, its toggle round trip returns the
 *     column to the width it started at, and — the one thing no other call
 *     here can see — its windowId argument is FORWARDED rather than dropped.
 *     The engine's parameter is defaulted, so an adaptor that swallowed the
 *     id would compile and silently revert to acting on whichever column
 *     happens to be active, which is the behaviour the wire argument exists
 *     to replace.
 */

#include <QTest>
#include <QSignalSpy>

#include <limits>

#include <PhosphorEngine/PlacementEngineBase.h>
#include <PhosphorScrollEngine/ScrollEngine.h>
#include <PhosphorScrollEngine/ScrollState.h>

#include "dbus/scrollingadaptor/scrollingadaptor.h"
#include "scrollingadaptortestfixture.h"

using namespace PlasmaZones;

class TestScrollingAdaptorVerbs : public QObject, protected ScrollingAdaptorTestFixture
{
    Q_OBJECT

private Q_SLOTS:
    void init()
    {
        setUpFixture();
    }

    void cleanup()
    {
        tearDownFixture();
    }

    // focusColumn's own doc: gated on the engine owning the screen, because
    // the engine's screen fallback would otherwise redirect a wheel event
    // from a non-scrolling monitor onto the active scrolling one.
    void testFocusColumn_ignoresForeignScreenAndBadDelta()
    {
        m_engine->windowOpened(QStringLiteral("app|a"), QStringLiteral("DP-1"), 0, 0);
        m_engine->windowOpened(QStringLiteral("app|b"), QStringLiteral("DP-1"), 0, 0);

        QSignalSpy activateSpy(m_engine, &PhosphorEngine::PlacementEngineBase::activateWindowRequested);

        m_adaptor->focusColumn(QStringLiteral("HDMI-2"), -1); // not ours
        // Boundary hygiene, like the empty-screenId strip case: the ownership
        // gate rejects "" on its own, so this arm does not discriminate the
        // isEmpty check. It pins the documented answer, not the guard.
        m_adaptor->focusColumn(QString(), -1); // no screen at all
        m_adaptor->focusColumn(QStringLiteral("DP-1"), 0); // not a direction
        m_adaptor->focusColumn(QStringLiteral("DP-1"), 2); // not a direction either
        QCOMPARE(activateSpy.count(), 0);

        // The per-context gate, consulted PER CALL and keyed on the screen:
        // a gate that refuses DP-1 stops the wheel there, and a gate missing
        // altogether fails closed (the daemon installs one unconditionally;
        // a build that forgot must not ship an ungated wheel).
        m_adaptor->setContextGateProvider([](const QString& screenId) {
            return screenId == QStringLiteral("DP-1");
        });
        m_adaptor->focusColumn(QStringLiteral("DP-1"), -1);
        QCOMPARE(activateSpy.count(), 0);
        m_adaptor->setContextGateProvider({});
        m_adaptor->focusColumn(QStringLiteral("DP-1"), -1);
        QCOMPARE(activateSpy.count(), 0);
        m_adaptor->setContextGateProvider([](const QString&) {
            return false;
        });

        // Positive control: the same call with a real screen and a real
        // direction DOES move focus, so the refusals above discriminate.
        m_adaptor->focusColumn(QStringLiteral("DP-1"), -1);
        QCOMPARE(activateSpy.count(), 1);
    }

    // Which WAY each delta goes, not just that something moved: -1 is left
    // and +1 is right, per the XML's DocString.
    void testFocusColumn_mapsDeltaToDirection()
    {
        QSignalSpy activateSpy(m_engine, &PhosphorEngine::PlacementEngineBase::activateWindowRequested);

        // An empty strip has nothing to activate, so a well-formed call is
        // still silent — the counts below start from a real zero.
        m_adaptor->focusColumn(QStringLiteral("DP-1"), -1);
        QCOMPARE(activateSpy.count(), 0);

        m_engine->windowOpened(QStringLiteral("app|a"), QStringLiteral("DP-1"), 0, 0);
        m_engine->windowOpened(QStringLiteral("app|b"), QStringLiteral("DP-1"), 0, 0);
        activateSpy.clear();

        // app|b opened last and holds focus, so left lands on app|a.
        m_adaptor->focusColumn(QStringLiteral("DP-1"), -1);
        QCOMPARE(activateSpy.count(), 1);
        QCOMPARE(activateSpy.at(0).at(0).toString(), QStringLiteral("app|a"));

        m_adaptor->focusColumn(QStringLiteral("DP-1"), 1);
        QCOMPARE(activateSpy.count(), 2);
        QCOMPARE(activateSpy.at(1).at(0).toString(), QStringLiteral("app|b"));
    }

    // scrollView, the wheel's view twin: focusColumn's gate plus the step
    // provider, one configured step per call, and focus untouched — which
    // is the whole verb, so it is asserted on the model rather than inferred
    // from the absence of an activation.
    void testScrollView_gatesOnProviderAndPansOneStep()
    {
        // Three over-wide columns overflow the viewport so there is somewhere
        // to pan; the last opened holds focus and the view sits at the END.
        for (const char* id : {"app|a", "app|b", "app|c"}) {
            m_engine->windowOpened(QString::fromLatin1(id), QStringLiteral("DP-1"), 0, 0);
            m_engine->windowFocused(QString::fromLatin1(id), QStringLiteral("DP-1"));
            m_engine->setColumnWidth(PhosphorScrollEngine::ColumnWidth::makeProportion(0.55), QStringLiteral("DP-1"));
        }
        auto* state = static_cast<PhosphorScrollEngine::ScrollState*>(m_engine->stateForScreen(QStringLiteral("DP-1")));
        QVERIFY(state);
        const int anchorBefore = state->strip().viewAnchor();
        QSignalSpy feedback(m_engine, &PhosphorEngine::PlacementEngineBase::navigationFeedback);

        // No provider installed: a silent no-op, not a pan by an invented
        // distance. Every gate below is asserted on the ANCHOR, since the
        // refusals must leave the persisted view exactly where it was.
        m_adaptor->scrollView(QStringLiteral("DP-1"), -1);
        QCOMPARE(state->strip().viewAnchor(), anchorBefore);
        QCOMPARE(feedback.count(), 0);

        m_adaptor->setViewScrollStepProvider([]() {
            return 25;
        });
        m_adaptor->scrollView(QStringLiteral("HDMI-2"), -1); // not ours
        m_adaptor->scrollView(QString(), -1); // no screen at all
        m_adaptor->scrollView(QStringLiteral("DP-1"), 0); // not a direction
        m_adaptor->scrollView(QStringLiteral("DP-1"), 2); // not a direction either
        QCOMPARE(state->strip().viewAnchor(), anchorBefore);
        QCOMPARE(feedback.count(), 0);

        // The per-context gate, same terms as focusColumn's arm: refused for
        // the gated screen, refused with no gate at all, open again after.
        m_adaptor->setContextGateProvider([](const QString& screenId) {
            return screenId == QStringLiteral("DP-1");
        });
        m_adaptor->scrollView(QStringLiteral("DP-1"), -1);
        m_adaptor->setContextGateProvider({});
        m_adaptor->scrollView(QStringLiteral("DP-1"), -1);
        QCOMPARE(state->strip().viewAnchor(), anchorBefore);
        QCOMPARE(feedback.count(), 0);
        m_adaptor->setContextGateProvider([](const QString&) {
            return false;
        });

        // The view starts at the strip's END, so a forward notch is the
        // documented clamp case: the anchor holds, and the verb answers with
        // the no-target feedback rather than silence, the way the XML
        // describes. One feedback per call is the wire's promise.
        m_adaptor->scrollView(QStringLiteral("DP-1"), 1);
        QCOMPARE(state->strip().viewAnchor(), anchorBefore);
        QCOMPARE(feedback.count(), 1);
        QCOMPARE(feedback.last().at(0).toBool(), false);
        QCOMPARE(feedback.last().at(2).toString(), QStringLiteral("no_target"));

        // Positive control, driven TWICE: one call is one step of 25% of the
        // fixture's 1200px extent (a backward pan GROWS the active-relative
        // anchor), and a second notch is a second step, not a re-read of the
        // first. The 1200 is the fixture's main-axis extent, which pins that
        // the step is measured against the viewport rather than the strip
        // (the fixture's available and screen rects share that extent, so
        // work area versus screen is not what this number discriminates;
        // the engine's own verbs test pins the work-area basis).
        // Focus stays on app|c throughout, and every call reports itself.
        const int step = qRound(0.25 * 1200);
        m_adaptor->scrollView(QStringLiteral("DP-1"), -1);
        QCOMPARE(state->strip().viewAnchor(), anchorBefore + step);
        m_adaptor->scrollView(QStringLiteral("DP-1"), -1);
        QCOMPARE(state->strip().viewAnchor(), anchorBefore + 2 * step);
        QCOMPARE(state->strip().activeWindowId(), QStringLiteral("app|c"));
        QCOMPARE(feedback.count(), 3);
        QCOMPARE(feedback.last().at(0).toBool(), true);
        QCOMPARE(feedback.last().at(1).toString(), QStringLiteral("scroll"));

        // And the other direction walks back by the same step, so the sign
        // of delta * step is pinned from both sides.
        m_adaptor->scrollView(QStringLiteral("DP-1"), 1);
        QCOMPARE(state->strip().viewAnchor(), anchorBefore + step);
        QCOMPARE(feedback.count(), 4);
    }

    // The absolute setters: focusColumn's ownership gate, silent range
    // refusal against the ConfigDefaults bounds, and the intent each form
    // actually writes (width proportion exact, width/height px Fixed, height
    // proportion a Preset fraction anchor).
    void testAbsoluteSetters_gateValidateAndApply()
    {
        using PhosphorScrollEngine::ColumnWidth;
        using PhosphorScrollEngine::WindowHeight;
        m_engine->windowOpened(QStringLiteral("app|a"), QStringLiteral("DP-1"), 0, 0);
        auto* state = static_cast<PhosphorScrollEngine::ScrollState*>(m_engine->stateForScreen(QStringLiteral("DP-1")));
        QVERIFY(state);
        const auto activeColumn = [state]() -> const PhosphorScrollEngine::Column& {
            return state->strip().columns().at(state->strip().activeColumnIndex());
        };

        // Refusals: foreign screen, empty screen id (the file's boundary
        // convention), below the proportion floor, above the ceiling, just
        // outside each inclusive bound, and NaN (D-Bus type 'd' carries it,
        // and an exclusion-form range test would wave it through) — none of
        // them may disturb the default width intent. Full-value compare, not
        // kind-only: a refusal that clamped instead of refusing would keep
        // the kind while corrupting the value.
        const ColumnWidth before = activeColumn().width;
        m_adaptor->setColumnWidthProportion(QStringLiteral("HDMI-2"), 0.25);
        m_adaptor->setColumnWidthProportion(QString(), 0.25);
        m_adaptor->setColumnWidthProportion(QStringLiteral("DP-1"), 0.01);
        m_adaptor->setColumnWidthProportion(QStringLiteral("DP-1"), 1.5);
        m_adaptor->setColumnWidthProportion(QStringLiteral("DP-1"), 0.049);
        m_adaptor->setColumnWidthProportion(QStringLiteral("DP-1"), 1.001);
        m_adaptor->setColumnWidthProportion(QStringLiteral("DP-1"), std::numeric_limits<double>::quiet_NaN());
        QCOMPARE(activeColumn().width, before);

        // The bounds themselves are ACCEPTED (inclusive range): a `<` flipped
        // to `<=` at either edge fails here.
        m_adaptor->setColumnWidthProportion(QStringLiteral("DP-1"), 0.05);
        QCOMPARE(activeColumn().width.kind, ColumnWidth::Kind::Proportion);
        QCOMPARE(activeColumn().width.proportion, 0.05);
        m_adaptor->setColumnWidthProportion(QStringLiteral("DP-1"), 1.0);
        QCOMPARE(activeColumn().width.proportion, 1.0);

        m_adaptor->setColumnWidthProportion(QStringLiteral("DP-1"), 0.25);
        QCOMPARE(activeColumn().width.kind, ColumnWidth::Kind::Proportion);
        QCOMPARE(activeColumn().width.proportion, 0.25);

        // Pixel form: out-of-range (including just-outside) refused with the
        // full intent untouched; the bounds accepted; in-range writes Fixed.
        // The foreign-screen arm is exercised on EVERY setter, not just the
        // proportion one: the ownership gate is per-method code, and deleting
        // it from one leaves the suite green if only a sibling pins it.
        const ColumnWidth beforePx = activeColumn().width;
        m_adaptor->setColumnWidthPixels(QStringLiteral("HDMI-2"), 640);
        m_adaptor->setColumnWidthPixels(QStringLiteral("DP-1"), 50);
        m_adaptor->setColumnWidthPixels(QStringLiteral("DP-1"), 99);
        m_adaptor->setColumnWidthPixels(QStringLiteral("DP-1"), 10001);
        m_adaptor->setColumnWidthPixels(QStringLiteral("DP-1"), 20000);
        m_adaptor->setColumnWidthPixels(QString(), 640);
        QCOMPARE(activeColumn().width, beforePx);
        m_adaptor->setColumnWidthPixels(QStringLiteral("DP-1"), 100);
        QCOMPARE(activeColumn().width.kind, ColumnWidth::Kind::Fixed);
        QCOMPARE(activeColumn().width.fixedPx, 100);
        m_adaptor->setColumnWidthPixels(QStringLiteral("DP-1"), 10000);
        QCOMPARE(activeColumn().width.fixedPx, 10000);
        m_adaptor->setColumnWidthPixels(QStringLiteral("DP-1"), 640);
        QCOMPARE(activeColumn().width.kind, ColumnWidth::Kind::Fixed);
        QCOMPARE(activeColumn().width.fixedPx, 640);

        // Height twins: px is Fixed, proportion is the Preset fraction
        // anchor (heights have no exact-proportion kind).
        const auto activeHeight = [&activeColumn]() -> const WindowHeight& {
            const PhosphorScrollEngine::Column& col = activeColumn();
            return col.tiles.at(col.activeTileIdx).height;
        };
        // The per-context gate, on EVERY setter with an in-range value: the
        // gate is per-method code (like the ownership gate below), so
        // deleting it from one setter must not be hidden by a sibling. The
        // fixture's open gate is restored afterwards.
        {
            const ColumnWidth widthBeforeGate = activeColumn().width;
            const WindowHeight heightBeforeGate = activeHeight();
            m_adaptor->setContextGateProvider([](const QString& screenId) {
                return screenId == QStringLiteral("DP-1");
            });
            m_adaptor->setColumnWidthProportion(QStringLiteral("DP-1"), 0.3);
            m_adaptor->setColumnWidthPixels(QStringLiteral("DP-1"), 700);
            m_adaptor->setWindowHeightProportion(QStringLiteral("DP-1"), 0.42);
            m_adaptor->setWindowHeightPixels(QStringLiteral("DP-1"), 300);
            QCOMPARE(activeColumn().width, widthBeforeGate);
            QCOMPARE(activeHeight(), heightBeforeGate);
            // toggleMaximizeColumn takes the same per-context gate. It is the
            // one verb in this adaptor with a real in-tree caller (the KWin
            // effect's maximize interception), so a gate regression here is
            // user-reachable in a way the four setters' is not.
            m_adaptor->toggleMaximizeColumn(QStringLiteral("DP-1"), QString());
            QCOMPARE(activeColumn().width, widthBeforeGate);
            // And its maximize-to-edges twin, which the interception now
            // dispatches: same gate, pinned on the flag it toggles.
            // The returned bool is the wire answer the KWin effect's
            // interception reads to decide whether to fall through to a stock
            // maximize, so a refusal that answered true would be acted on even
            // though nothing moved. Asserted here, not just the state.
            QVERIFY(!m_adaptor->toggleMaximizeToEdges(QStringLiteral("DP-1"), QString()));
            QVERIFY(!activeColumn().maximizedToEdges);

            // The NO-PROVIDER arm, which this block's own "per-method code"
            // reasoning demands and which only focusColumn and scrollView had.
            //
            // An absent gate FAILS CLOSED: refusesForContext is
            // `!m_contextGated || m_contextGated(screenId)`, so a null
            // std::function refuses outright. That is the deliberate stance —
            // the daemon installs the gate during bring-up, and a verb
            // arriving before it must not act on a context whose disabled
            // state is not yet knowable — and it is a DIFFERENT code path
            // from the refusing gate above, which exercises the second
            // operand. A mis-written `m_contextGated && ...` would pass every
            // assertion above and fail here.
            const ColumnWidth widthBeforeNoGate = activeColumn().width;
            const WindowHeight heightBeforeNoGate = activeHeight();
            m_adaptor->setContextGateProvider({});
            m_adaptor->setColumnWidthProportion(QStringLiteral("DP-1"), 0.31);
            m_adaptor->setColumnWidthPixels(QStringLiteral("DP-1"), 712);
            m_adaptor->setWindowHeightProportion(QStringLiteral("DP-1"), 0.43);
            m_adaptor->setWindowHeightPixels(QStringLiteral("DP-1"), 301);
            m_adaptor->toggleMaximizeColumn(QStringLiteral("DP-1"), QString());
            QVERIFY(!m_adaptor->toggleMaximizeToEdges(QStringLiteral("DP-1"), QString()));
            QCOMPARE(activeColumn().width, widthBeforeNoGate);
            QVERIFY(!activeColumn().maximizedToEdges);
            QCOMPARE(activeHeight(), heightBeforeNoGate);

            m_adaptor->setContextGateProvider([](const QString&) {
                return false;
            });
        }

        // toggleMaximizeColumn's ownership and boundary gates, on the same
        // terms as the setters above: a foreign screen and an empty screen id
        // must leave the width intent untouched. The windowId argument is
        // deliberately NOT range-checked — empty means "the active column",
        // which is what the keyboard shortcut sends — so the unknown-window
        // refusal belongs to the strip and is pinned in the engine's own
        // suite, not here.
        {
            const ColumnWidth beforeToggle = activeColumn().width;
            // The boundary refusals answer false as well as doing nothing.
            // These two arms returned false before ApiVersion 7 tightened the
            // boolean too, so on their own they do not discriminate the new
            // contract from the old — they complete the pair with the accepted
            // no-op case asserted in the named-window block below.
            QVERIFY2(!m_adaptor->toggleMaximizeColumn(QStringLiteral("HDMI-2"), QString()),
                     "a screen the engine does not own must answer false");
            QVERIFY2(!m_adaptor->toggleMaximizeColumn(QString(), QString()), "an empty screen id must answer false");
            QCOMPARE(activeColumn().width, beforeToggle);
            // And the accepted call does act, so the refusals above are
            // genuine gates rather than a verb that never does anything.
            QVERIFY2(m_adaptor->toggleMaximizeColumn(QStringLiteral("DP-1"), QString()),
                     "an accepted call that changes the strip must answer true");
            QVERIFY(activeColumn().width != beforeToggle);
            m_adaptor->toggleMaximizeColumn(QStringLiteral("DP-1"), QString());
            // The round trip lands back where it started, which is the whole
            // promise of a toggle and was previously unasserted here.
            QCOMPARE(activeColumn().width, beforeToggle);
        }

        // toggleMaximizeToEdges: the same ownership and boundary gates,
        // pinned on the FLAG it toggles. The width intent must never move —
        // that separation (flag verb vs width verb) is the whole design.
        {
            const ColumnWidth widthBeforeEdges = activeColumn().width;
            QVERIFY(!activeColumn().maximizedToEdges);
            QVERIFY(!m_adaptor->toggleMaximizeToEdges(QStringLiteral("HDMI-2"), QString()));
            QVERIFY(!m_adaptor->toggleMaximizeToEdges(QString(), QString()));
            QVERIFY(!activeColumn().maximizedToEdges);
            QVERIFY(m_adaptor->toggleMaximizeToEdges(QStringLiteral("DP-1"), QString()));
            QVERIFY(activeColumn().maximizedToEdges);
            QCOMPARE(activeColumn().width, widthBeforeEdges);
            QVERIFY(m_adaptor->toggleMaximizeToEdges(QStringLiteral("DP-1"), QString()));
            QVERIFY(!activeColumn().maximizedToEdges);
            QCOMPARE(activeColumn().width, widthBeforeEdges);
        }

        // The windowId is FORWARDED, not swallowed.
        //
        // ScrollEngine::toggleMaximizeColumn defaults that argument, so an
        // adaptor that dropped it would compile and silently revert to the
        // screen-scoped v5 behaviour — acting on whichever column happens to
        // be active rather than the one holding the named window, which is
        // exactly the defect the wire argument was added to fix. Every other
        // call in this file passes an empty id, so nothing else here can tell
        // the two spellings apart. The engine-side targeting test does not
        // cross this boundary.
        {
            using PhosphorScrollEngine::ColumnWidth;
            m_engine->windowOpened(QStringLiteral("app|b"), QStringLiteral("DP-1"), 0, 0);
            auto* st =
                static_cast<PhosphorScrollEngine::ScrollState*>(m_engine->stateForScreen(QStringLiteral("DP-1")));
            QVERIFY(st);
            const auto widthOfWindow = [st](const QString& id) -> ColumnWidth {
                const int idx = st->strip().columnOfWindow(id);
                return idx < 0 ? ColumnWidth::makeFixed(-1) : st->strip().columns().at(idx).width;
            };
            // Focus the OTHER column, so "the named window's column" and "the
            // active column" are different answers.
            m_engine->windowFocused(QStringLiteral("app|a"), QStringLiteral("DP-1"));
            QVERIFY2(st->strip().columnOfWindow(QStringLiteral("app|a"))
                         != st->strip().columnOfWindow(QStringLiteral("app|b")),
                     "the two windows must be in different columns, or this proves nothing");
            const ColumnWidth aBefore = widthOfWindow(QStringLiteral("app|a"));
            const ColumnWidth bBefore = widthOfWindow(QStringLiteral("app|b"));
            QVERIFY2(m_adaptor->toggleMaximizeColumn(QStringLiteral("DP-1"), QStringLiteral("app|b")),
                     "a toggle the strip acts on must answer true");
            QVERIFY2(widthOfWindow(QStringLiteral("app|b")) != bBefore,
                     "the NAMED window's column must be the one that changed");
            QCOMPARE(widthOfWindow(QStringLiteral("app|a")), aBefore);

            // The maximize-to-edges twin forwards the windowId on the same
            // terms — it is the verb the interception actually dispatches, so
            // a swallowed id here is user-reachable through every titlebar
            // click on an unfocused window.
            const auto edgesOfWindow = [st](const QString& id) {
                const int idx = st->strip().columnOfWindow(id);
                return idx >= 0 && st->strip().columns().at(idx).maximizedToEdges;
            };
            QVERIFY(m_adaptor->toggleMaximizeToEdges(QStringLiteral("DP-1"), QStringLiteral("app|b")));
            QVERIFY2(edgesOfWindow(QStringLiteral("app|b")), "the NAMED window's column must take the flag");
            QVERIFY2(!edgesOfWindow(QStringLiteral("app|a")), "the ACTIVE column must be left alone");
            QVERIFY(m_adaptor->toggleMaximizeToEdges(QStringLiteral("DP-1"), QStringLiteral("app|b")));
            QVERIFY(!edgesOfWindow(QStringLiteral("app|b")));

            // THE BOUNDARY CONTRACT, and the only assertions in the suite that
            // discriminate it. ApiVersion 7 tightened this boolean from "the
            // daemon accepted the request" to "the strip changed", and the
            // KWin effect steers on the maximize-to-edges verb's answer: a
            // false answer is its only cue to put KWin's maximize bit back,
            // because a call that changes nothing emits no tile batch to
            // correct the window later.
            //
            // The engine-layer suite cannot see this. It calls the engine
            // directly, so reverting either adaptor's `return m_engine->...`
            // to an unconditional `return true` passes every other test in the
            // tree. These are the ones that go red.
            QVERIFY2(!m_adaptor->toggleMaximizeColumn(QStringLiteral("DP-1"), QStringLiteral("app|nosuchwindow")),
                     "an ACCEPTED call the strip does nothing with must answer false, not true");
            QVERIFY2(!m_adaptor->toggleMaximizeToEdges(QStringLiteral("DP-1"), QStringLiteral("app|nosuchwindow")),
                     "the verb the effect steers on must answer false when the strip does nothing");
        }

        // Foreign-screen refusal + the same bound pins as the width twin: a
        // `<` flipped to `<=` at either height bound was invisible before.
        // Full-value compare, like the width arm and the proportion arm
        // below: the starting kind happens to differ from the kind a clamp
        // would write, so a kind-only check passes here today, but it stops
        // discriminating the moment this block is reordered after one of the
        // legs that leaves the height Fixed.
        const WindowHeight beforePxRefusals = activeHeight();
        m_adaptor->setWindowHeightPixels(QStringLiteral("HDMI-2"), 300);
        m_adaptor->setWindowHeightPixels(QStringLiteral("DP-1"), 50);
        m_adaptor->setWindowHeightPixels(QStringLiteral("DP-1"), 99);
        m_adaptor->setWindowHeightPixels(QStringLiteral("DP-1"), 10001);
        m_adaptor->setWindowHeightPixels(QString(), 300);
        QCOMPARE(activeHeight(), beforePxRefusals);
        QCOMPARE(activeHeight().kind, WindowHeight::Kind::Auto);
        m_adaptor->setWindowHeightPixels(QStringLiteral("DP-1"), 100);
        QCOMPARE(activeHeight().kind, WindowHeight::Kind::Fixed);
        QCOMPARE(activeHeight().fixedPx, 100);
        m_adaptor->setWindowHeightPixels(QStringLiteral("DP-1"), 10000);
        QCOMPARE(activeHeight().fixedPx, 10000);
        m_adaptor->setWindowHeightPixels(QStringLiteral("DP-1"), 300);
        QCOMPARE(activeHeight().kind, WindowHeight::Kind::Fixed);
        QCOMPARE(activeHeight().fixedPx, 300);
        // Height-proportion refusals, full-value compare like the width arm
        // (kind-only would miss a clamp that rewrote fixedPx), with the
        // foreign screen, both out-of-range sides and NaN covered.
        const WindowHeight beforeHeightRefusals = activeHeight();
        m_adaptor->setWindowHeightProportion(QStringLiteral("HDMI-2"), 0.5);
        m_adaptor->setWindowHeightProportion(QStringLiteral("DP-1"), 0.01);
        m_adaptor->setWindowHeightProportion(QStringLiteral("DP-1"), 1.5);
        m_adaptor->setWindowHeightProportion(QStringLiteral("DP-1"), std::numeric_limits<double>::quiet_NaN());
        m_adaptor->setWindowHeightProportion(QString(), 0.5);
        QCOMPARE(activeHeight(), beforeHeightRefusals);
        QCOMPARE(activeHeight().kind, WindowHeight::Kind::Fixed);
        // An OFF-VOCABULARY fraction is accepted and stored VERBATIM as the
        // anchor — the setter is not exact and does not snap at store time;
        // relayout snaps the anchor to the nearest effective height preset,
        // per the XML DocString. 0.42 sits between the default vocabulary's
        // 1/3 and 1/2 entries, so a setter that stored the nearest entry
        // instead of the anchor would fail the exact compare below.
        m_adaptor->setWindowHeightProportion(QStringLiteral("DP-1"), 0.42);
        QCOMPARE(activeHeight().kind, WindowHeight::Kind::Preset);
        QCOMPARE(activeHeight().presetFraction, 0.42);
        m_adaptor->setWindowHeightProportion(QStringLiteral("DP-1"), 0.5);
        QCOMPARE(activeHeight().kind, WindowHeight::Kind::Preset);
        QCOMPARE(activeHeight().presetFraction, 0.5);
    }
};

QTEST_GUILESS_MAIN(TestScrollingAdaptorVerbs)
#include "test_scrolling_adaptor_verbs.moc"
