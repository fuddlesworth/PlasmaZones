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
    static const inline QString kSnapped = QStringLiteral("app|snapped");
    static const inline QString kFloat = QStringLiteral("app|float");
    static const inline QString kFloat2 = QStringLiteral("app|zfloat2");

private Q_SLOTS:
    void noFloatAnswersNoTarget()
    {
        SnapEngine engine(nullptr, m_wts, nullptr, nullptr, nullptr);
        FakeNavState nav;
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
        SnapEngine engine(nullptr, m_wts, nullptr, nullptr, nullptr);
        FakeNavState nav;
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
    }

    void minimizedFloatsAreSkipped()
    {
        SnapEngine engine(nullptr, m_wts, nullptr, nullptr, nullptr);
        FakeNavState nav;
        FakeMinimizeRegistry registry;
        engine.setNavigationStateProvider(&nav);
        engine.setWindowRegistry(&registry);
        SnapState* state = engine.stateForWindowOnScreen(kSnapped, kScreen);
        state->assignWindowToZone(kSnapped, QStringLiteral("zone-1"), kScreen, 1);
        engine.stateForWindowOnScreen(kFloat, kScreen)->setFloatingOnScreen(kFloat, kScreen, 1);
        engine.stateForWindowOnScreen(kFloat2, kScreen)->setFloatingOnScreen(kFloat2, kScreen, 1);

        // kFloat is the remembered float focus, then minimizes: the switch
        // falls through to the other float rather than activating a hidden
        // window.
        engine.windowFocused(kFloat, kScreen);
        engine.windowFocused(kSnapped, kScreen);
        nav.m_activeWindow = kSnapped;
        registry.m_minimized.insert(kFloat);

        QSignalSpy activateSpy(&engine, &SnapEngine::activateWindowRequested);
        QSignalSpy feedbackSpy(&engine, &SnapEngine::navigationFeedback);
        engine.switchFocusBetweenFloatingAndTiling(kScreen);
        QCOMPARE(activateSpy.count(), 1);
        QCOMPARE(activateSpy.takeFirst().at(0).toString(), kFloat2);

        // Every float hidden: refused, not silently "won".
        registry.m_minimized.insert(kFloat2);
        feedbackSpy.clear();
        engine.switchFocusBetweenFloatingAndTiling(kScreen);
        QCOMPARE(activateSpy.count(), 0);
        const auto refusal = feedbackSpy.takeFirst();
        QCOMPARE(refusal.at(0).toBool(), false);
        QCOMPARE(refusal.at(2).toString(), QStringLiteral("no_target"));
    }
};

QTEST_MAIN(TestSnapLayerSwitch)
#include "test_snap_layer_switch.moc"
