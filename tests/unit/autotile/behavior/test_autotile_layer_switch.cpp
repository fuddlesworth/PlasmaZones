// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
//
// switchFocusBetweenFloatingAndTiling on the autotile engine: the layer
// focus memories on TilingState feed the shared resolver, activations go
// out through activateWindowRequested, and the compositor's answering
// focus report (windowFocused) is what re-arms the memories — autotile has
// no self-activation echo filter, so both legs heal from genuine reports.

#include <QCoreApplication>
#include <QSignalSpy>
#include <QTest>

#include <PhosphorEngine/IWindowRegistry.h>
#include <PhosphorTileEngine/AutotileEngine.h>
#include <PhosphorTiles/TilingState.h>

#include "helpers/AutotileTestHelpers.h"

using PhosphorTileEngine::AutotileEngine;

namespace {

// Minimal IWindowRegistry: identity canonicalization, engaged minimize
// verdicts for every window (the verb's eligibility filter reads
// minimizedState().value_or(false), so engaged-false behaves like the
// live registry's answer for a visible window).
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

class TestAutotileLayerSwitch : public QObject
{
    Q_OBJECT

    static const inline QString kScreen = QStringLiteral("DP-1");

private Q_SLOTS:
    void noFloatAnswersNoTarget()
    {
        std::unique_ptr<AutotileEngine> engine(
            PlasmaZones::TestHelpers::createEngineWithWindows(kScreen, 2, QStringLiteral("win1")));
        QSignalSpy activateSpy(engine.get(), &AutotileEngine::activateWindowRequested);
        QSignalSpy feedbackSpy(engine.get(), &AutotileEngine::navigationFeedback);

        engine->switchFocusBetweenFloatingAndTiling(kScreen);
        QCOMPARE(activateSpy.count(), 0);
        QCOMPARE(feedbackSpy.count(), 1);
        const auto refusal = feedbackSpy.takeFirst();
        QCOMPARE(refusal.at(0).toBool(), false);
        QCOMPARE(refusal.at(1).toString(), QStringLiteral("float"));
        QCOMPARE(refusal.at(2).toString(), QStringLiteral("no_target"));
    }

    void roundTripsBetweenLayers()
    {
        std::unique_ptr<AutotileEngine> engine(
            PlasmaZones::TestHelpers::createEngineWithWindows(kScreen, 3, QStringLiteral("win1")));
        engine->toggleWindowFloat(QStringLiteral("win2"), kScreen);
        QCoreApplication::processEvents();

        QSignalSpy activateSpy(engine.get(), &AutotileEngine::activateWindowRequested);
        QSignalSpy feedbackSpy(engine.get(), &AutotileEngine::navigationFeedback);

        // Tiling → float: no float was ever focused, so the target comes
        // from the fallback scan over the floating set.
        engine->switchFocusBetweenFloatingAndTiling(kScreen);
        QCOMPARE(activateSpy.count(), 1);
        QCOMPARE(activateSpy.takeFirst().at(0).toString(), QStringLiteral("win2"));
        auto feedback = feedbackSpy.takeFirst();
        QCOMPARE(feedback.at(0).toBool(), true);
        QCOMPARE(feedback.at(1).toString(), QStringLiteral("float"));
        QCOMPARE(feedback.at(2).toString(), QStringLiteral("floating"));
        QCOMPARE(feedback.at(3).toString(), QStringLiteral("win1"));
        QCOMPARE(feedback.at(4).toString(), QStringLiteral("win2"));

        // The compositor honours the activation; the genuine report arms
        // the float-side memory.
        engine->windowFocused(QStringLiteral("win2"), kScreen);
        QCoreApplication::processEvents();
        PhosphorTiles::TilingState* state = engine->tilingStateForScreen(kScreen);
        QVERIFY(state);
        QVERIFY(state->floatingHasFocus());

        // Float → tiling: back to the remembered tile, not an arbitrary one.
        engine->switchFocusBetweenFloatingAndTiling(kScreen);
        QCOMPARE(activateSpy.count(), 1);
        QCOMPARE(activateSpy.takeFirst().at(0).toString(), QStringLiteral("win1"));
        feedback = feedbackSpy.takeFirst();
        QCOMPARE(feedback.at(0).toBool(), true);
        QCOMPARE(feedback.at(2).toString(), QStringLiteral("tiled"));
        QCOMPARE(feedback.at(3).toString(), QStringLiteral("win2"));
        QCOMPARE(feedback.at(4).toString(), QStringLiteral("win1"));

        engine->windowFocused(QStringLiteral("win1"), kScreen);
        QCoreApplication::processEvents();
        QVERIFY(!state->floatingHasFocus());

        // Third press, with a SECOND float present whose id sorts before
        // the remembered one: float win3, make it the remembered float,
        // refocus the tile, and assert the memory answers — the sorted
        // fallback scan would answer win2, so this fails if the candidate
        // read is deleted.
        engine->toggleWindowFloat(QStringLiteral("win3"), kScreen);
        QCoreApplication::processEvents();
        engine->windowFocused(QStringLiteral("win3"), kScreen);
        engine->windowFocused(QStringLiteral("win1"), kScreen);
        QCoreApplication::processEvents();
        engine->switchFocusBetweenFloatingAndTiling(kScreen);
        QCOMPARE(activateSpy.count(), 1);
        QCOMPARE(activateSpy.takeFirst().at(0).toString(), QStringLiteral("win3"));
    }

    void minimizedFloatsAreSkipped()
    {
        // Registry before the engine: the engine stores a raw pointer to
        // it, so reverse destruction order must tear the engine down first.
        FakeMinimizeRegistry registry;
        std::unique_ptr<AutotileEngine> engine(
            PlasmaZones::TestHelpers::createEngineWithWindows(kScreen, 3, QStringLiteral("win1")));
        engine->setWindowRegistry(&registry);
        engine->toggleWindowFloat(QStringLiteral("win2"), kScreen);
        engine->toggleWindowFloat(QStringLiteral("win3"), kScreen);
        QCoreApplication::processEvents();

        // win2 is the remembered float focus, then minimizes: the switch
        // must fall through to win3 rather than "activating" a hidden
        // window and reporting success.
        engine->windowFocused(QStringLiteral("win2"), kScreen);
        engine->windowFocused(QStringLiteral("win1"), kScreen);
        QCoreApplication::processEvents();
        registry.m_minimized.insert(QStringLiteral("win2"));

        QSignalSpy activateSpy(engine.get(), &AutotileEngine::activateWindowRequested);
        QSignalSpy feedbackSpy(engine.get(), &AutotileEngine::navigationFeedback);
        engine->switchFocusBetweenFloatingAndTiling(kScreen);
        QCOMPARE(activateSpy.count(), 1);
        QCOMPARE(activateSpy.takeFirst().at(0).toString(), QStringLiteral("win3"));

        // Every float hidden: the press is refused, not silently "won".
        registry.m_minimized.insert(QStringLiteral("win3"));
        feedbackSpy.clear();
        engine->switchFocusBetweenFloatingAndTiling(kScreen);
        QCOMPARE(activateSpy.count(), 0);
        QCOMPARE(feedbackSpy.count(), 1);
        const auto refusal = feedbackSpy.takeFirst();
        QCOMPARE(refusal.at(0).toBool(), false);
        QCOMPARE(refusal.at(2).toString(), QStringLiteral("no_target"));
    }
};

QTEST_MAIN(TestAutotileLayerSwitch)
#include "test_autotile_layer_switch.moc"
