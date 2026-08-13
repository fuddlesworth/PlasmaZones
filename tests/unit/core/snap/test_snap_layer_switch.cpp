// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
//
// switchFocusBetweenFloatingAndTiling on the snap engine: the layer focus
// memories on SnapState are armed by windowFocused reports, the focus side
// is derived live from the navigation state provider, and activations go
// out through activateWindowRequested with "snapped"/"floating" feedback.

#include "helpers/SnapEngineTestFixture.h"

#include <PhosphorEngine/IWindowRegistry.h>
#include <PhosphorSnapEngine/INavigationStateProvider.h>

namespace {

class FakeNavState : public PhosphorSnapEngine::INavigationStateProvider
{
public:
    QString lastCursorScreenName() const override
    {
        return m_screen;
    }
    QString lastActiveScreenName() const override
    {
        return m_screen;
    }
    QString lastActiveWindowId() const override
    {
        return m_activeWindow;
    }
    QRect frameGeometry(const QString& windowId) const override
    {
        Q_UNUSED(windowId)
        return {};
    }

    QString m_screen = QStringLiteral("DP-1");
    QString m_activeWindow;
};

// Engaged minimize verdicts, identity canonicalization — same shape as the
// autotile suite's fake.
class FakeMinimizeRegistry : public QObject, public PhosphorEngine::IWindowRegistry
{
public:
    QString canonicalizeWindowId(const QString& rawWindowId) override
    {
        return rawWindowId;
    }
    QString canonicalizeForLookup(const QString& rawWindowId) const override
    {
        return rawWindowId;
    }
    QString appIdFor(const QString& instanceId) const override
    {
        Q_UNUSED(instanceId)
        return {};
    }
    std::optional<bool> minimizedState(const QString& windowId) const override
    {
        return m_minimized.contains(windowId);
    }

    QSet<QString> m_minimized;
};

} // namespace

class TestSnapLayerSwitch : public SnapEngineTestFixture
{
    Q_OBJECT

    static const inline QString kScreen = QStringLiteral("DP-1");
    static const inline QString kScreen2 = QStringLiteral("DP-2");
    static const inline QString kSnapped = QStringLiteral("app|snapped");
    static const inline QString kFloat = QStringLiteral("app|float");
    // The "a"/"z" prefixes are load-bearing: the fallback pool is sorted by
    // id, so kFloat0 sorts before kFloat and kFloat2 sorts after it, which
    // is what lets the assertions below separate "used the remembered
    // candidate" from "scanned the sorted pool".
    static const inline QString kFloat0 = QStringLiteral("app|afloat0");
    static const inline QString kFloat2 = QStringLiteral("app|zfloat2");

private Q_SLOTS:
    void noFloatAnswersNoTarget()
    {
        // Fakes before the engine (raw-pointer teardown order, see
        // roundTripsBetweenLayers).
        FakeNavState nav;
        SnapEngine engine(nullptr, m_wts, nullptr, nullptr, nullptr);
        engine.setNavigationStateProvider(&nav);
        SnapState* state = engine.stateForWindowOnScreen(kSnapped, kScreen);
        state->assignWindowToZone(kSnapped, QStringLiteral("zone-1"), kScreen, 1);
        engine.windowFocused(kSnapped, kScreen);
        nav.m_activeWindow = kSnapped;

        QSignalSpy activateSpy(&engine, &SnapEngine::activateWindowRequested);
        QSignalSpy feedbackSpy(&engine, &SnapEngine::navigationFeedback);
        engine.switchFocusBetweenFloatingAndTiling(kScreen);
        QCOMPARE(activateSpy.count(), 0);
        QCOMPARE(feedbackSpy.count(), 1);
        const auto refusal = feedbackSpy.takeFirst();
        QCOMPARE(refusal.at(0).toBool(), false);
        QCOMPARE(refusal.at(1).toString(), QStringLiteral("float"));
        QCOMPARE(refusal.at(2).toString(), QStringLiteral("no_target"));
    }

    void roundTripsBetweenLayers()
    {
        // Fakes before the engine: the engine stores raw pointers to them,
        // so reverse destruction order must tear the engine down first.
        FakeNavState nav;
        SnapEngine engine(nullptr, m_wts, nullptr, nullptr, nullptr);
        engine.setNavigationStateProvider(&nav);
        SnapState* state = engine.stateForWindowOnScreen(kSnapped, kScreen);
        state->assignWindowToZone(kSnapped, QStringLiteral("zone-1"), kScreen, 1);
        engine.stateForWindowOnScreen(kFloat, kScreen)->setFloatingOnScreen(kFloat, kScreen, 1);

        // Arm the snapped-side memory through a genuine focus report.
        engine.windowFocused(kSnapped, kScreen);
        nav.m_activeWindow = kSnapped;

        QSignalSpy activateSpy(&engine, &SnapEngine::activateWindowRequested);
        QSignalSpy feedbackSpy(&engine, &SnapEngine::navigationFeedback);

        // Snapped → float: no float was ever focused, so the fallback scan
        // over the floating set answers.
        engine.switchFocusBetweenFloatingAndTiling(kScreen);
        QCOMPARE(activateSpy.count(), 1);
        QCOMPARE(activateSpy.takeFirst().at(0).toString(), kFloat);
        auto feedback = feedbackSpy.takeFirst();
        QCOMPARE(feedback.at(0).toBool(), true);
        QCOMPARE(feedback.at(1).toString(), QStringLiteral("float"));
        QCOMPARE(feedback.at(2).toString(), QStringLiteral("floating"));
        QCOMPARE(feedback.at(3).toString(), kSnapped);
        QCOMPARE(feedback.at(4).toString(), kFloat);

        // The compositor honours the activation.
        engine.windowFocused(kFloat, kScreen);
        nav.m_activeWindow = kFloat;

        // Float → snapped: back to the remembered zone window, reason says
        // the layer in snap's vocabulary.
        engine.switchFocusBetweenFloatingAndTiling(kScreen);
        QCOMPARE(activateSpy.count(), 1);
        QCOMPARE(activateSpy.takeFirst().at(0).toString(), kSnapped);
        feedback = feedbackSpy.takeFirst();
        QCOMPARE(feedback.at(0).toBool(), true);
        QCOMPARE(feedback.at(2).toString(), QStringLiteral("snapped"));
        QCOMPARE(feedback.at(3).toString(), kFloat);
        QCOMPARE(feedback.at(4).toString(), kSnapped);
        engine.windowFocused(kSnapped, kScreen);
        nav.m_activeWindow = kSnapped;

        // Third press, with a second float that sorts BEFORE the remembered
        // one: the REMEMBERED float must win, which fails if the candidate
        // read is deleted and the sorted scan answers instead.
        engine.stateForWindowOnScreen(kFloat0, kScreen)->setFloatingOnScreen(kFloat0, kScreen, 1);
        engine.switchFocusBetweenFloatingAndTiling(kScreen);
        QCOMPARE(activateSpy.count(), 1);
        QCOMPARE(activateSpy.takeFirst().at(0).toString(), kFloat);
    }

    void minimizedFloatsAreSkipped()
    {
        // Fakes before the engine (raw-pointer teardown order, see above).
        FakeNavState nav;
        FakeMinimizeRegistry registry;
        SnapEngine engine(nullptr, m_wts, nullptr, nullptr, nullptr);
        engine.setNavigationStateProvider(&nav);
        engine.setWindowRegistry(&registry);
        SnapState* state = engine.stateForWindowOnScreen(kSnapped, kScreen);
        state->assignWindowToZone(kSnapped, QStringLiteral("zone-1"), kScreen, 1);
        engine.stateForWindowOnScreen(kFloat0, kScreen)->setFloatingOnScreen(kFloat0, kScreen, 1);
        engine.stateForWindowOnScreen(kFloat, kScreen)->setFloatingOnScreen(kFloat, kScreen, 1);
        engine.stateForWindowOnScreen(kFloat2, kScreen)->setFloatingOnScreen(kFloat2, kScreen, 1);

        // kFloat is the remembered float focus, then minimizes: the switch
        // must reject the remembered candidate and scan the sorted pool,
        // landing on kFloat0 (sorts first) — landing on kFloat would mean a
        // hidden window was "activated".
        engine.windowFocused(kFloat, kScreen);
        engine.windowFocused(kSnapped, kScreen);
        nav.m_activeWindow = kSnapped;
        registry.m_minimized.insert(kFloat);

        QSignalSpy activateSpy(&engine, &SnapEngine::activateWindowRequested);
        QSignalSpy feedbackSpy(&engine, &SnapEngine::navigationFeedback);
        engine.switchFocusBetweenFloatingAndTiling(kScreen);
        QCOMPARE(activateSpy.count(), 1);
        QCOMPARE(activateSpy.takeFirst().at(0).toString(), kFloat0);

        // Every float hidden: refused, not silently "won".
        registry.m_minimized.insert(kFloat0);
        registry.m_minimized.insert(kFloat2);
        feedbackSpy.clear();
        engine.switchFocusBetweenFloatingAndTiling(kScreen);
        QCOMPARE(activateSpy.count(), 0);
        QCOMPARE(feedbackSpy.count(), 1);
        const auto refusal = feedbackSpy.takeFirst();
        QCOMPARE(refusal.at(0).toBool(), false);
        QCOMPARE(refusal.at(2).toString(), QStringLiteral("no_target"));
    }

    void emptyScreenRefusesWithNoWindows()
    {
        // No screen anywhere: the pre-resolver bail refuses with
        // "no_windows" before any state is consulted.
        FakeNavState nav;
        nav.m_screen.clear();
        SnapEngine engine(nullptr, m_wts, nullptr, nullptr, nullptr);
        engine.setNavigationStateProvider(&nav);

        QSignalSpy activateSpy(&engine, &SnapEngine::activateWindowRequested);
        QSignalSpy feedbackSpy(&engine, &SnapEngine::navigationFeedback);
        engine.switchFocusBetweenFloatingAndTiling(QString());
        QCOMPARE(activateSpy.count(), 0);
        QCOMPARE(feedbackSpy.count(), 1);
        const auto refusal = feedbackSpy.takeFirst();
        QCOMPARE(refusal.at(0).toBool(), false);
        QCOMPARE(refusal.at(2).toString(), QStringLiteral("no_windows"));
    }

    void residenceOnlyFocusTakesFloatLeg()
    {
        // A focused free window (recorded residence, no zone, no float bit)
        // is on neither layer, so the press deliberately takes the
        // tiling→float leg — "give me a float" is the documented answer.
        FakeNavState nav;
        SnapEngine engine(nullptr, m_wts, nullptr, nullptr, nullptr);
        engine.setNavigationStateProvider(&nav);
        const QString kFree = QStringLiteral("app|free");
        SnapState* state = engine.stateForWindowOnScreen(kFree, kScreen);
        state->recordResidence(kFree, kScreen, 1);
        engine.stateForWindowOnScreen(kFloat, kScreen)->setFloatingOnScreen(kFloat, kScreen, 1);
        engine.windowFocused(kFree, kScreen);
        nav.m_activeWindow = kFree;

        QSignalSpy activateSpy(&engine, &SnapEngine::activateWindowRequested);
        engine.switchFocusBetweenFloatingAndTiling(kScreen);
        QCOMPARE(activateSpy.count(), 1);
        QCOMPARE(activateSpy.takeFirst().at(0).toString(), kFloat);
    }

    void layerMemoriesArePerScreen()
    {
        // The memories live on the per-(screen, desktop, activity) store: a
        // press on DP-2 must not see DP-1's remembered float, and with no
        // window on DP-2 at all it refuses rather than crossing screens.
        FakeNavState nav;
        SnapEngine engine(nullptr, m_wts, nullptr, nullptr, nullptr);
        engine.setNavigationStateProvider(&nav);
        SnapState* state = engine.stateForWindowOnScreen(kSnapped, kScreen);
        state->assignWindowToZone(kSnapped, QStringLiteral("zone-1"), kScreen, 1);
        engine.stateForWindowOnScreen(kFloat, kScreen)->setFloatingOnScreen(kFloat, kScreen, 1);
        engine.windowFocused(kFloat, kScreen);
        engine.windowFocused(kSnapped, kScreen);
        nav.m_activeWindow = kSnapped;

        QSignalSpy activateSpy(&engine, &SnapEngine::activateWindowRequested);
        QSignalSpy feedbackSpy(&engine, &SnapEngine::navigationFeedback);
        engine.switchFocusBetweenFloatingAndTiling(kScreen2);
        QCOMPARE(activateSpy.count(), 0);
        QCOMPARE(feedbackSpy.count(), 1);
        const auto refusal = feedbackSpy.takeFirst();
        QCOMPARE(refusal.at(0).toBool(), false);
        QCOMPARE(refusal.at(2).toString(), QStringLiteral("no_target"));
    }
};

QTEST_MAIN(TestSnapLayerSwitch)
#include "test_snap_layer_switch.moc"
