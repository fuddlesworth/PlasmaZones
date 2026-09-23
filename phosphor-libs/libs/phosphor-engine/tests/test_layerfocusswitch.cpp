// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorEngine/LayerFocusSwitch.h>

#include <QtTest>

using PhosphorEngine::LayerSwitchSide;
using PhosphorEngine::resolveLayerFocusSwitch;

class TestLayerFocusSwitch : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void floatSideFocusTargetsTiledCandidate();
    void tiledSideFocusTargetsRememberedFloat();
    void tiledCandidateBeatsTiledScanPool();
    void ineligibleCandidateFallsThroughToScan();
    void scanSkipsIneligibleEntries();
    void scanSkipsBlankPoolEntries();
    void emptyTargetSideFailsWithNoTarget();
    void allIneligibleRefusesWithNoTarget();
    void nullEligibilityAcceptsEverything();
};

void TestLayerFocusSwitch::floatSideFocusTargetsTiledCandidate()
{
    LayerSwitchSide tiled;
    tiled.candidate = QStringLiteral("tile-a");
    tiled.focusForFeedback = QStringLiteral("tile-a");
    LayerSwitchSide floating;
    floating.candidate = QStringLiteral("float-a");
    floating.focusForFeedback = QStringLiteral("float-a");

    const auto result = resolveLayerFocusSwitch(true, tiled, floating);
    QVERIFY(result.success);
    QVERIFY(result.toTiled);
    QCOMPARE(result.reason, QStringLiteral("tiled"));
    QCOMPARE(result.target, QStringLiteral("tile-a"));
    // The source is the side focus DEPARTS from — the float layer.
    QCOMPARE(result.source, QStringLiteral("float-a"));
}

void TestLayerFocusSwitch::tiledSideFocusTargetsRememberedFloat()
{
    LayerSwitchSide tiled;
    tiled.candidate = QStringLiteral("tile-a");
    tiled.focusForFeedback = QStringLiteral("tile-a");
    LayerSwitchSide floating;
    floating.candidate = QStringLiteral("float-b");
    floating.fallbacks = {QStringLiteral("float-a"), QStringLiteral("float-b")};
    floating.focusForFeedback = QStringLiteral("float-b");

    const auto result = resolveLayerFocusSwitch(false, tiled, floating);
    QVERIFY(result.success);
    QVERIFY(!result.toTiled);
    QCOMPARE(result.reason, QStringLiteral("floating"));
    // The remembered candidate wins over the scan pool's first entry.
    QCOMPARE(result.target, QStringLiteral("float-b"));
    QCOMPARE(result.source, QStringLiteral("tile-a"));
}

void TestLayerFocusSwitch::tiledCandidateBeatsTiledScanPool()
{
    // Candidate precedence on the TILED leg, mirroring the floating-leg
    // case above: the remembered tile wins over the pool's first entry.
    LayerSwitchSide tiled;
    tiled.candidate = QStringLiteral("tile-b");
    tiled.fallbacks = {QStringLiteral("tile-a"), QStringLiteral("tile-b")};
    LayerSwitchSide floating;
    floating.focusForFeedback = QStringLiteral("float-a");

    const auto result = resolveLayerFocusSwitch(true, tiled, floating);
    QVERIFY(result.success);
    QVERIFY(result.toTiled);
    QCOMPARE(result.target, QStringLiteral("tile-b"));
}

void TestLayerFocusSwitch::ineligibleCandidateFallsThroughToScan()
{
    LayerSwitchSide tiled;
    tiled.focusForFeedback = QStringLiteral("tile-a");
    LayerSwitchSide floating;
    floating.candidate = QStringLiteral("float-hidden");
    floating.fallbacks = {QStringLiteral("float-hidden"), QStringLiteral("float-ok")};
    floating.isEligible = [](const QString& id) {
        return id != QStringLiteral("float-hidden");
    };

    const auto result = resolveLayerFocusSwitch(false, tiled, floating);
    QVERIFY(result.success);
    QCOMPARE(result.target, QStringLiteral("float-ok"));
}

void TestLayerFocusSwitch::scanSkipsIneligibleEntries()
{
    LayerSwitchSide tiled;
    tiled.candidate = QStringLiteral("tile-stale");
    tiled.fallbacks = {QStringLiteral("tile-stale"), QStringLiteral("tile-min"), QStringLiteral("tile-ok")};
    tiled.isEligible = [](const QString& id) {
        return id == QStringLiteral("tile-ok");
    };
    LayerSwitchSide floating;
    floating.focusForFeedback = QStringLiteral("float-a");

    const auto result = resolveLayerFocusSwitch(true, tiled, floating);
    QVERIFY(result.success);
    QVERIFY(result.toTiled);
    QCOMPARE(result.target, QStringLiteral("tile-ok"));
    QCOMPARE(result.source, QStringLiteral("float-a"));
}

void TestLayerFocusSwitch::scanSkipsBlankPoolEntries()
{
    // A blank id in the pool (which sorts FIRST in the engines' sorted
    // pools) must not short-circuit the scan into a false no_target — the
    // emptiness guard mirrors the candidate check.
    LayerSwitchSide tiled;
    tiled.fallbacks = {QString(), QStringLiteral("tile-ok")};
    LayerSwitchSide floating;
    floating.focusForFeedback = QStringLiteral("float-a");

    const auto result = resolveLayerFocusSwitch(true, tiled, floating);
    QVERIFY(result.success);
    QCOMPARE(result.target, QStringLiteral("tile-ok"));
}

void TestLayerFocusSwitch::emptyTargetSideFailsWithNoTarget()
{
    LayerSwitchSide tiled;
    tiled.candidate = QStringLiteral("tile-a");
    tiled.focusForFeedback = QStringLiteral("tile-a");
    LayerSwitchSide floating;

    // Tiling → float with no float anywhere: no_target, leg recorded, source
    // names the tiled focus the press departed from.
    const auto result = resolveLayerFocusSwitch(false, tiled, floating);
    QVERIFY(!result.success);
    QVERIFY(!result.toTiled);
    QCOMPARE(result.reason, QStringLiteral("no_target"));
    QVERIFY(result.target.isEmpty());
    QCOMPARE(result.source, QStringLiteral("tile-a"));

    // Float → tiling with an empty tiled side (empty strip): same refusal
    // shape, source names the float focus.
    LayerSwitchSide emptyTiled;
    LayerSwitchSide floatSide;
    floatSide.focusForFeedback = QStringLiteral("float-a");
    const auto reverse = resolveLayerFocusSwitch(true, emptyTiled, floatSide);
    QVERIFY(!reverse.success);
    QVERIFY(reverse.toTiled);
    QCOMPARE(reverse.reason, QStringLiteral("no_target"));
    QCOMPARE(reverse.source, QStringLiteral("float-a"));
}

void TestLayerFocusSwitch::allIneligibleRefusesWithNoTarget()
{
    // The refusal shape the minimize filter actually produces in
    // production: candidate AND every fallback present but ineligible
    // (every float on the target side minimized).
    LayerSwitchSide tiled;
    tiled.focusForFeedback = QStringLiteral("tile-a");
    LayerSwitchSide floating;
    floating.candidate = QStringLiteral("float-min-1");
    floating.fallbacks = {QStringLiteral("float-min-1"), QStringLiteral("float-min-2")};
    floating.isEligible = [](const QString&) {
        return false;
    };

    const auto result = resolveLayerFocusSwitch(false, tiled, floating);
    QVERIFY(!result.success);
    QVERIFY(!result.toTiled);
    QCOMPARE(result.reason, QStringLiteral("no_target"));
    QVERIFY(result.target.isEmpty());
    QCOMPARE(result.source, QStringLiteral("tile-a"));
}

void TestLayerFocusSwitch::nullEligibilityAcceptsEverything()
{
    LayerSwitchSide tiled;
    tiled.candidate = QStringLiteral("tile-a");
    LayerSwitchSide floating;
    floating.fallbacks = {QStringLiteral("float-a")};

    // Assert the PICKED TARGET on both legs, not just success — a wrong-side
    // pick under a null filter must fail here.
    const auto toTiled = resolveLayerFocusSwitch(true, tiled, floating);
    QVERIFY(toTiled.success);
    QCOMPARE(toTiled.target, QStringLiteral("tile-a"));
    const auto toFloat = resolveLayerFocusSwitch(false, tiled, floating);
    QVERIFY(toFloat.success);
    QCOMPARE(toFloat.target, QStringLiteral("float-a"));
}

QTEST_APPLESS_MAIN(TestLayerFocusSwitch)
#include "test_layerfocusswitch.moc"
