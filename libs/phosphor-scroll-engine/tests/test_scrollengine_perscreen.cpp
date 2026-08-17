// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

// FILE-SIZE EXCEPTION (sanctioned): past the 1150 hard ceiling. No exact
// figure is quoted here on purpose — it goes stale on the next slot added.
//
// The case for it: this file's concern is a single resolution cascade —
// rule > per-screen settings trio > cached global, plus the template channel
// that replaces the preset LISTS those kinds index into. The channels are not
// independent of each other; every test here is a PRECEDENCE claim between two
// or more of them, and each is written as a paired two-screen fixture because
// a one-screen assertion passes just as happily when the engine reads the
// global everywhere. Splitting by channel would put the two halves of a
// precedence pair in different files and leave neither able to state the
// ordering it exists to pin. Splitting by default (width here, height there)
// would duplicate the whole fixture layer and split the trio's per-slot
// fall-back rule, which is one rule applying identically to both.
//
// If a channel is ever added that resolves INDEPENDENTLY of these three, it
// takes a sibling rather than growing this file.

// Per-screen default-width / default-height resolution.
//
// The engine reads three channels for each of the two defaults, in order:
// the RULE channel (a bare work-area fraction the rule cascade writes), the
// SETTINGS channel (a kind/value/preset-index trio the settings app writes
// per monitor), and the cached GLOBAL from refreshConfigFromSettings. Every
// layer writes the trio's keys independently, so a per-screen kind beside an
// absent value is the ordinary case and each slot falls back on its own to
// the global's matching slot. The TEMPLATE channel is a fourth of a
// different shape. It is not a default at all: it replaces the preset LISTS
// the Preset kinds index into (presetColumnWidths / presetWindowHeights)
// wholesale, and it also carries the seed blueprint for the first columns.
// Its tests come in two groups, the preset-list ones straight after the
// default-channel tests and the blueprint ones closing the file.
//
// None of that is directly observable: the two resolvers are private. What IS
// observable is the width a freshly-opened column takes and the height intent
// its tile is seeded with, which is where every one of these values lands, so
// the tests drive windowOpened and read the strip back.

#include <PhosphorScrollEngine/IScrollSettings.h>
#include <PhosphorScrollEngine/ScrollEngine.h>
#include <PhosphorScrollEngine/ScrollState.h>
#include <PhosphorScrollEngine/ScrollStrip.h>
#include <PhosphorScrollEngine/ScrollTypes.h>

#include "scrollstriptestutils.h"
#include "scrollstubsettings.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QVariantMap>
#include <QtTest>

using namespace PhosphorScrollEngine;

namespace Ax = ScrollTestUtils::Ax;

using ScrollTestUtils::kCrossExtent;
using ScrollTestUtils::kMainExtent;
using ScrollTestUtils::makeProviderEngine;

namespace {

// The configured baseline every case here layers a per-screen override over.
// Shared with the behaviour suite (scrollstubsettings.h) rather than copied:
// both suites assert against "the configured default", and two stubs would
// let that value drift apart between them.
using ScrollTestUtils::StubScrollSettings;

const QString kS1 = QStringLiteral("S1");
const QString kS2 = QStringLiteral("S2");
const QString kS3 = QStringLiteral("S3");

} // namespace

class TestScrollEnginePerScreen : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    /// Proves the vertical arm really is transposed, so a lost ENVIRONMENT
    /// property cannot leave it silently re-running the horizontal suite.
    void initTestCase()
    {
        AX_GUARD_SUITE();
    }

    void widthChannelsRankRuleOverSettingsOverGlobal();
    void heightChannelsRankRuleOverSettingsOverGlobal();
    void perScreenStripAxisOverridesTheResolvedAxis();
    void globalStripAxisChannelReachesTheEngine();
    void absentTrioSlotsFallBackPerSlotToTheGlobal();
    void presetIndexIsClampedToTheLivePresetList();
    void fixedKindWithAProportionValueFallsThroughToTheGlobal();
    void templatePresetListReplacesSettingsListWholesale();
    void templatePresetHeightsReplaceSettingsHeights();
    void templateListShrinkClampsResolvedPresetWidth();
    void invalidTemplateEntriesFallBackToSettingsList();
    void invalidTemplateHeightEntriesFallBackToSettingsList();
    void templateListsAreCappedAtTheKeepAndScanBounds();
    void wrongTypedKindAndSpinValuesFallThrough();
    void autoHeightKindOverridesAFixedGlobal();
    void tabIndicatorOverridesArePerProperty();
    void tabIndicatorRejectsGarbageNumericOverrides();
    void tabIndicatorRejectsAGarbagePositionOverride();
    void templateBlueprintSeedsFirstColumns();
    void openRuleOutranksTemplateBlueprint();
    void openMaximizedRuleOutranksWidthRuleAndBlueprint();
    void openFocusedRuleOverridesFocusNewWindows();
    void openFocusedFalseOnAnEmptyStripStillAdoptsTheArrival();
    void openFocusedFalseSurvivesTheCompositorsOwnFocusReport();
    void openMaximizedFalseLeavesTheDefaultWidth();
    void openMaximizedIsDroppedByAConsumeOpen();
    void templateBlueprintNeverResizesExistingColumns();
    void templateBlueprintEntryWithoutDisplayKeepsTheDefault();
    void closingAColumnDoesNotHandItsBlueprintEntryBack();
    void emptyingTheStripRestartsTheBlueprintSeed();
    void anewBlueprintRestartsTheSeedInsteadOfResumingTheOldCount();

private:
    /// A headless engine active on the three screens, with @p settings
    /// installed and its cached globals refreshed.
    static ScrollEngine* makeEngine(QObject* parent, StubScrollSettings* settings)
    {
        ScrollEngine* engine = makeProviderEngine(parent, {kS1, kS2, kS3});
        engine->setEngineSettings(settings);
        engine->refreshConfigFromSettings();
        return engine;
    }

    /// The width intent of the column @p windowId opened on @p screenId, or a
    /// default-constructed ColumnWidth when the strip has no such column.
    /// Callers pair every read with columnExists — a missing column would
    /// otherwise satisfy a Proportion assertion vacuously.
    static ColumnWidth openedWidth(ScrollEngine* engine, const QString& screenId, const QString& windowId)
    {
        auto* state = static_cast<ScrollState*>(engine->stateForScreen(screenId));
        if (!state) {
            return {};
        }
        for (const Column& col : state->strip().columns()) {
            if (col.indexOfWindow(windowId) >= 0) {
                return col.width;
            }
        }
        return {};
    }

    static WindowHeight openedHeight(ScrollEngine* engine, const QString& screenId, const QString& windowId)
    {
        auto* state = static_cast<ScrollState*>(engine->stateForScreen(screenId));
        if (!state) {
            return {};
        }
        for (const Column& col : state->strip().columns()) {
            const int idx = col.indexOfWindow(windowId);
            if (idx >= 0) {
                return col.tiles.at(idx).height;
            }
        }
        return {};
    }

    static bool columnExists(ScrollEngine* engine, const QString& screenId, const QString& windowId)
    {
        auto* state = static_cast<ScrollState*>(engine->stateForScreen(screenId));
        if (!state) {
            return false;
        }
        for (const Column& col : state->strip().columns()) {
            if (col.indexOfWindow(windowId) >= 0) {
                return true;
            }
        }
        return false;
    }
};

void TestScrollEnginePerScreen::widthChannelsRankRuleOverSettingsOverGlobal()
{
    QObject owner;
    auto* settings = new StubScrollSettings(&owner);
    settings->widthKind = static_cast<int>(DefaultWidthKind::Proportion);
    settings->widthValue = 0.25; // the global
    ScrollEngine* engine = makeEngine(&owner, settings);

    // S1 carries BOTH channels: the rule fraction must win outright, not be
    // merged with the settings trio sitting beside it.
    QVariantMap both;
    both.insert(ScrollPerScreenKeys::defaultColumnWidth(), 0.75); // rule channel
    both.insert(ScrollPerScreenKeys::defaultColumnWidthKind(), static_cast<int>(DefaultWidthKind::Proportion));
    both.insert(ScrollPerScreenKeys::defaultColumnWidthValue(), 0.5); // settings channel
    engine->applyPerScreenConfig(kS1, both);

    // S2 carries the settings channel only.
    QVariantMap settingsOnly;
    settingsOnly.insert(ScrollPerScreenKeys::defaultColumnWidthKind(), static_cast<int>(DefaultWidthKind::Proportion));
    settingsOnly.insert(ScrollPerScreenKeys::defaultColumnWidthValue(), 0.5);
    engine->applyPerScreenConfig(kS2, settingsOnly);

    // S3 carries neither — the cached global is the whole answer.
    engine->windowOpened(QStringLiteral("app|a"), kS1, 0, 0);
    engine->windowOpened(QStringLiteral("app|b"), kS2, 0, 0);
    engine->windowOpened(QStringLiteral("app|c"), kS3, 0, 0);

    QVERIFY(columnExists(engine, kS1, QStringLiteral("app|a")));
    QVERIFY(columnExists(engine, kS2, QStringLiteral("app|b")));
    QVERIFY(columnExists(engine, kS3, QStringLiteral("app|c")));

    const ColumnWidth ruled = openedWidth(engine, kS1, QStringLiteral("app|a"));
    QCOMPARE(ruled.kind, ColumnWidth::Proportion);
    QCOMPARE(ruled.proportion, 0.75);

    const ColumnWidth perScreen = openedWidth(engine, kS2, QStringLiteral("app|b"));
    QCOMPARE(perScreen.kind, ColumnWidth::Proportion);
    QCOMPARE(perScreen.proportion, 0.5);

    const ColumnWidth global = openedWidth(engine, kS3, QStringLiteral("app|c"));
    QCOMPARE(global.kind, ColumnWidth::Proportion);
    QCOMPARE(global.proportion, 0.25);
}

// The per-screen StripAxis key is the PR's headline knob and had ZERO coverage
// anywhere in the repo: nothing wrote ScrollPerScreenKeys::stripAxis(), so the
// whole intent switch in effectiveStripAxis was dead as far as the suite was
// concerned and could have been deleted with every test still green.
//
// This pins the FORCED intents, which the harness cannot otherwise reach: it
// transposes the work area and lets the axis fall out of the Auto branch, so
// main is always the longer edge and a width-versus-mainSize confusion
// coincides numerically. Forcing an axis AGAINST the fixture's aspect is what
// separates the two, so the assertions below are on the CROSS extent, which is
// the one the forced axis actually changes.
void TestScrollEnginePerScreen::perScreenStripAxisOverridesTheResolvedAxis()
{
    QObject owner;
    auto* settings = new StubScrollSettings(&owner);
    ScrollEngine* engine = makeEngine(&owner, settings);

    // S1 forced to the axis the fixture would NOT resolve on its own, S2 left
    // to resolve from its work-area aspect. Auto is 0, Horizontal 1, Vertical 2
    // in the config tri-state, which is deliberately NOT the protocol's
    // numbering — the engine switches rather than casting.
    const bool fixtureIsVertical = ScrollTestUtils::Ax::vertical();
    QVariantMap forced;
    forced.insert(ScrollPerScreenKeys::stripAxis(), fixtureIsVertical ? 1 : 2);
    engine->applyPerScreenConfig(kS1, forced);

    engine->windowOpened(QStringLiteral("app|a"), kS1, 0, 0);
    engine->windowOpened(QStringLiteral("app|b"), kS2, 0, 0);

    QVERIFY(columnExists(engine, kS1, QStringLiteral("app|a")));
    QVERIFY(columnExists(engine, kS2, QStringLiteral("app|b")));

    // A lone column always spans the full CROSS extent of ITS OWN axis and
    // takes only a share along its main axis. The harness's Ax:: readers
    // measure against the ARM's axis, so on the forced screen the two roles
    // are swapped relative to them: the column runs full length along the
    // harness's MAIN direction, because that is the engine's cross there.
    //
    // That is the discriminator. If the per-screen key were ignored, S1 would
    // resolve exactly like S2 and its harness-main extent would be the default
    // column share rather than the full extent.
    const QVector<QRect> forcedRects = engine->visibleTileRects(kS1);
    const QVector<QRect> autoRects = engine->visibleTileRects(kS2);
    QCOMPARE(forcedRects.size(), 1);
    QCOMPARE(autoRects.size(), 1);

    QCOMPARE(ScrollTestUtils::Ax::crossLen(autoRects.first()), ScrollTestUtils::kCrossExtent);
    QVERIFY(ScrollTestUtils::Ax::mainLen(autoRects.first()) < ScrollTestUtils::kMainExtent);
    QCOMPARE(ScrollTestUtils::Ax::mainLen(forcedRects.first()), ScrollTestUtils::kMainExtent);
    // Both roles, not just one: a regression that hands back the full screen
    // rect satisfies the mainLen compare above, so the forced leg also has to
    // bound the harness-cross extent (the engine's MAIN there — a lone column
    // takes only the default share of it).
    QVERIFY(ScrollTestUtils::Ax::crossLen(forcedRects.first()) < ScrollTestUtils::kCrossExtent);

    // Clearing the override drops the screen back onto the resolved axis, so
    // it looks like the untouched one again. applyPerScreenConfig and its
    // clear schedule the retile QUEUED — drain the loop rather than calling
    // retile(): the assertions below read the resolve-on-demand path either
    // way, and draining is what the committed-rect spy leg at the end needs.
    engine->clearPerScreenConfig(kS1);
    QCoreApplication::processEvents();
    const QVector<QRect> cleared = engine->visibleTileRects(kS1);
    QCOMPARE(cleared.size(), 1);
    QCOMPARE(ScrollTestUtils::Ax::crossLen(cleared.first()), ScrollTestUtils::kCrossExtent);
    QVERIFY(ScrollTestUtils::Ax::mainLen(cleared.first()) < ScrollTestUtils::kMainExtent);

    // An out-of-range intent degrades to Auto rather than to a fixed axis.
    QVariantMap garbage;
    garbage.insert(ScrollPerScreenKeys::stripAxis(), 99);
    engine->applyPerScreenConfig(kS1, garbage);
    QCoreApplication::processEvents();
    const QVector<QRect> degraded = engine->visibleTileRects(kS1);
    QCOMPARE(degraded.size(), 1);
    QCOMPARE(ScrollTestUtils::Ax::crossLen(degraded.first()), ScrollTestUtils::kCrossExtent);
    QVERIFY(ScrollTestUtils::Ax::mainLen(degraded.first()) < ScrollTestUtils::kMainExtent);

    // The COMMIT path, not only the on-demand resolve the reads above take:
    // re-force the axis and assert a committed windowsTiled entry flips its
    // main extent once the queued retile drains. Without this leg an axis
    // flip that never reaches applyLayout would pass everything above.
    QSignalSpy tiledSpy(engine, &ScrollEngine::windowsTiled);
    engine->applyPerScreenConfig(kS1, forced);
    QCoreApplication::processEvents();
    QVERIFY(tiledSpy.count() > 0);
    bool sawCommitted = false;
    for (const auto& emission : std::as_const(tiledSpy)) {
        const QJsonArray batch = QJsonDocument::fromJson(emission.at(0).toString().toUtf8()).array();
        for (const QJsonValue& v : batch) {
            const QJsonObject o = v.toObject();
            if (o.value(QLatin1String("windowId")).toString() != QStringLiteral("app|a")) {
                continue;
            }
            const QRect committed(o.value(QLatin1String("x")).toInt(), o.value(QLatin1String("y")).toInt(),
                                  o.value(QLatin1String("width")).toInt(), o.value(QLatin1String("height")).toInt());
            QCOMPARE(ScrollTestUtils::Ax::mainLen(committed), ScrollTestUtils::kMainExtent);
            sawCommitted = true;
        }
    }
    QVERIFY2(sawCommitted, "the queued retile after the axis re-force must commit app|a on the forced axis");
}

// The GLOBAL settings channel, which the per-screen test above cannot reach:
// with every stub answering Auto, effectiveStripAxis's m_stripAxis fallback
// always lands on the resolve-from-geometry default, so replacing the
// refreshConfigFromSettings read with a literal 0 kept every suite green.
// Three legs pin the channel as a PRECEDENCE claim like every other key in
// this file: a forced global flips BOTH screens, an out-of-range global
// degrades to Auto (the range clamp, not a fixed axis), and a per-screen key
// beats a forced global.
void TestScrollEnginePerScreen::globalStripAxisChannelReachesTheEngine()
{
    QObject owner;
    auto* settings = new StubScrollSettings(&owner);
    // Forced AGAINST the fixture aspect before the engine's construction-time
    // refresh, so the very first resolve already runs on the global.
    const bool fixtureIsVertical = ScrollTestUtils::Ax::vertical();
    const int forcedGlobal = fixtureIsVertical ? 1 : 2;
    settings->stripAxis = forcedGlobal;
    ScrollEngine* engine = makeEngine(&owner, settings);

    engine->windowOpened(QStringLiteral("app|a"), kS1, 0, 0);
    engine->windowOpened(QStringLiteral("app|b"), kS2, 0, 0);

    // Both screens flip: the global is not a per-screen value, so the lone
    // column on EACH runs full length along the harness's main direction
    // (the engine's cross there) and takes only a share of the harness cross.
    for (const QString& screen : {kS1, kS2}) {
        const QVector<QRect> rects = engine->visibleTileRects(screen);
        QCOMPARE(rects.size(), 1);
        QCOMPARE(Ax::mainLen(rects.first()), kMainExtent);
        QVERIFY(Ax::crossLen(rects.first()) < kCrossExtent);
    }

    // Out of range degrades to Auto, not to either fixed axis: the refresh
    // clamp (0..2, else 0) is the branch under test.
    settings->stripAxis = 7;
    engine->refreshConfigFromSettings();
    QCoreApplication::processEvents();
    const QVector<QRect> degraded = engine->visibleTileRects(kS1);
    QCOMPARE(degraded.size(), 1);
    QCOMPARE(Ax::crossLen(degraded.first()), kCrossExtent);
    QVERIFY(Ax::mainLen(degraded.first()) < kMainExtent);

    // A per-screen key OPPOSING a forced global wins — the precedence claim
    // the sibling test cannot make with an Auto global. kS1 names the
    // fixture's own aspect (so it reads like an untouched screen), kS2 stays
    // on the forced global.
    settings->stripAxis = forcedGlobal;
    engine->refreshConfigFromSettings();
    QVariantMap opposing;
    opposing.insert(ScrollPerScreenKeys::stripAxis(), fixtureIsVertical ? 2 : 1);
    engine->applyPerScreenConfig(kS1, opposing);
    QCoreApplication::processEvents();

    const QVector<QRect> perScreenWins = engine->visibleTileRects(kS1);
    QCOMPARE(perScreenWins.size(), 1);
    QCOMPARE(Ax::crossLen(perScreenWins.first()), kCrossExtent);
    QVERIFY(Ax::mainLen(perScreenWins.first()) < kMainExtent);

    const QVector<QRect> globalStill = engine->visibleTileRects(kS2);
    QCOMPARE(globalStill.size(), 1);
    QCOMPARE(Ax::mainLen(globalStill.first()), kMainExtent);
    QVERIFY(Ax::crossLen(globalStill.first()) < kCrossExtent);
}

void TestScrollEnginePerScreen::heightChannelsRankRuleOverSettingsOverGlobal()
{
    QObject owner;
    auto* settings = new StubScrollSettings(&owner);
    settings->heightKind = static_cast<int>(DefaultHeightKind::Fixed);
    settings->heightValue = 200.0; // the global, in pixels
    ScrollEngine* engine = makeEngine(&owner, settings);

    // Rule channel: a work-area FRACTION, committed as pixels against the
    // live work area (the gaps are zero here, so the work area is the screen).
    QVariantMap both;
    both.insert(ScrollPerScreenKeys::defaultWindowHeight(), 0.5);
    both.insert(ScrollPerScreenKeys::defaultWindowHeightKind(), static_cast<int>(DefaultHeightKind::Fixed));
    both.insert(ScrollPerScreenKeys::defaultWindowHeightValue(), 300.0);
    engine->applyPerScreenConfig(kS1, both);

    QVariantMap settingsOnly;
    settingsOnly.insert(ScrollPerScreenKeys::defaultWindowHeightKind(), static_cast<int>(DefaultHeightKind::Fixed));
    settingsOnly.insert(ScrollPerScreenKeys::defaultWindowHeightValue(), 300.0);
    engine->applyPerScreenConfig(kS2, settingsOnly);

    engine->windowOpened(QStringLiteral("app|a"), kS1, 0, 0);
    engine->windowOpened(QStringLiteral("app|b"), kS2, 0, 0);
    engine->windowOpened(QStringLiteral("app|c"), kS3, 0, 0);

    QVERIFY(columnExists(engine, kS1, QStringLiteral("app|a")));
    QVERIFY(columnExists(engine, kS2, QStringLiteral("app|b")));
    QVERIFY(columnExists(engine, kS3, QStringLiteral("app|c")));

    const WindowHeight ruled = openedHeight(engine, kS1, QStringLiteral("app|a"));
    QCOMPARE(ruled.kind, WindowHeight::Fixed);
    QCOMPARE(ruled.fixedPx, kCrossExtent / 2);

    const WindowHeight perScreen = openedHeight(engine, kS2, QStringLiteral("app|b"));
    QCOMPARE(perScreen.kind, WindowHeight::Fixed);
    QCOMPARE(perScreen.fixedPx, 300);

    const WindowHeight global = openedHeight(engine, kS3, QStringLiteral("app|c"));
    QCOMPARE(global.kind, WindowHeight::Fixed);
    QCOMPARE(global.fixedPx, 200);
}

void TestScrollEnginePerScreen::absentTrioSlotsFallBackPerSlotToTheGlobal()
{
    QObject owner;
    auto* settings = new StubScrollSettings(&owner);
    // Global is Preset index 2. A monitor that only ever touched the KIND
    // combo writes the kind key alone, and the preset spin must INHERIT the
    // global's index — reading an absent spin as 0 pinned the monitor to the
    // first preset while the settings UI showed the inherited index.
    settings->widthKind = static_cast<int>(DefaultWidthKind::Preset);
    settings->widthPresetIndex = 2;
    settings->heightKind = static_cast<int>(DefaultHeightKind::Preset);
    settings->heightPresetIndex = 2;
    ScrollEngine* engine = makeEngine(&owner, settings);

    QVariantMap kindOnly;
    kindOnly.insert(ScrollPerScreenKeys::defaultColumnWidthKind(), static_cast<int>(DefaultWidthKind::Preset));
    kindOnly.insert(ScrollPerScreenKeys::defaultWindowHeightKind(), static_cast<int>(DefaultHeightKind::Preset));
    engine->applyPerScreenConfig(kS1, kindOnly);

    // The other direction of the same rule: the per-screen KIND is honoured
    // even when the global's kind differs, PROVIDED the layer also wrote the
    // value slot that kind needs. Proportion here, against a Preset global.
    // The value-ABSENT twin of this case is exercised at the tail of this
    // test, where the proportion slot has no global twin to inherit from.
    QVariantMap differingKind;
    differingKind.insert(ScrollPerScreenKeys::defaultColumnWidthKind(), static_cast<int>(DefaultWidthKind::Proportion));
    differingKind.insert(ScrollPerScreenKeys::defaultColumnWidthValue(), 0.6);
    engine->applyPerScreenConfig(kS2, differingKind);

    engine->windowOpened(QStringLiteral("app|a"), kS1, 0, 0);
    engine->windowOpened(QStringLiteral("app|b"), kS2, 0, 0);
    QVERIFY(columnExists(engine, kS1, QStringLiteral("app|a")));
    QVERIFY(columnExists(engine, kS2, QStringLiteral("app|b")));

    // Index 2 of the stub's 0.25/0.5/0.75 lists resolves to the VALUE
    // anchor 0.75 at construction (value-anchored preset intent).
    const ColumnWidth inherited = openedWidth(engine, kS1, QStringLiteral("app|a"));
    QCOMPARE(inherited.kind, ColumnWidth::Preset);
    QCOMPARE(inherited.presetFraction, 0.75);

    const WindowHeight inheritedHeight = openedHeight(engine, kS1, QStringLiteral("app|a"));
    QCOMPARE(inheritedHeight.kind, WindowHeight::Preset);
    QCOMPARE(inheritedHeight.presetFraction, 0.75);

    const ColumnWidth kindHonoured = openedWidth(engine, kS2, QStringLiteral("app|b"));
    QCOMPARE(kindHonoured.kind, ColumnWidth::Proportion);
    QCOMPARE(kindHonoured.proportion, 0.6);

    // The value-ABSENT arm, which had no coverage anywhere: a per-screen
    // Proportion kind with NO value slot, against a PRESET global. The
    // per-slot inheritance has nothing to inherit — the global is not a
    // proportion, so its value slot is not a proportion either — and the
    // resolver falls all the way through to the whole global rather than
    // committing a zero proportion. That whole-global fall-through is the one
    // documented case where the per-screen kind does NOT survive, and pinning
    // it is what stops a "helpful" zero or an arbitrary clamp being added.
    QVariantMap kindWithoutValue;
    kindWithoutValue.insert(ScrollPerScreenKeys::defaultColumnWidthKind(),
                            static_cast<int>(DefaultWidthKind::Proportion));
    engine->applyPerScreenConfig(kS2, kindWithoutValue);
    engine->windowOpened(QStringLiteral("app|b2"), kS2, 0, 0);
    QVERIFY(columnExists(engine, kS2, QStringLiteral("app|b2")));
    const ColumnWidth fellThrough = openedWidth(engine, kS2, QStringLiteral("app|b2"));
    QCOMPARE(fellThrough.kind, ColumnWidth::Preset);
    QCOMPARE(fellThrough.presetFraction, 0.75);
}

void TestScrollEnginePerScreen::presetIndexIsClampedToTheLivePresetList()
{
    QObject owner;
    auto* settings = new StubScrollSettings(&owner);
    // Two presets, so a stored index of 9 (written when the list was longer)
    // must RESOLVE to the last live entry's value rather than reading past
    // it — the spin index turns into a value anchor at construction.
    settings->widthPresets = QStringList{QStringLiteral("0.25"), QStringLiteral("0.5")};
    ScrollEngine* engine = makeEngine(&owner, settings);

    QVariantMap tooHigh;
    tooHigh.insert(ScrollPerScreenKeys::defaultColumnWidthKind(), static_cast<int>(DefaultWidthKind::Preset));
    tooHigh.insert(ScrollPerScreenKeys::defaultColumnWidthPresetIndex(), 9);
    engine->applyPerScreenConfig(kS1, tooHigh);

    QVariantMap negative;
    negative.insert(ScrollPerScreenKeys::defaultColumnWidthKind(), static_cast<int>(DefaultWidthKind::Preset));
    negative.insert(ScrollPerScreenKeys::defaultColumnWidthPresetIndex(), -3);
    engine->applyPerScreenConfig(kS2, negative);

    engine->windowOpened(QStringLiteral("app|a"), kS1, 0, 0);
    engine->windowOpened(QStringLiteral("app|b"), kS2, 0, 0);
    QVERIFY(columnExists(engine, kS1, QStringLiteral("app|a")));
    QVERIFY(columnExists(engine, kS2, QStringLiteral("app|b")));

    const ColumnWidth clampedHigh = openedWidth(engine, kS1, QStringLiteral("app|a"));
    QCOMPARE(clampedHigh.kind, ColumnWidth::Preset);
    QCOMPARE(clampedHigh.presetFraction, 0.5);

    const ColumnWidth clampedLow = openedWidth(engine, kS2, QStringLiteral("app|b"));
    QCOMPARE(clampedLow.kind, ColumnWidth::Preset);
    QCOMPARE(clampedLow.presetFraction, 0.25);
}

void TestScrollEnginePerScreen::fixedKindWithAProportionValueFallsThroughToTheGlobal()
{
    QObject owner;
    auto* settings = new StubScrollSettings(&owner);
    settings->widthKind = static_cast<int>(DefaultWidthKind::Proportion);
    settings->widthValue = 0.25;
    ScrollEngine* engine = makeEngine(&owner, settings);

    // Fixed pixels below 1.0 is the shape a proportion written into the pixel
    // slot takes. Committing it would open a sub-pixel column; the resolver
    // rejects it and falls through to the global instead. This is the sole
    // guard between a malformed pair and a 1px column, and nothing else pins
    // it.
    QVariantMap subPixel;
    subPixel.insert(ScrollPerScreenKeys::defaultColumnWidthKind(), static_cast<int>(DefaultWidthKind::Fixed));
    subPixel.insert(ScrollPerScreenKeys::defaultColumnWidthValue(), 0.5);
    engine->applyPerScreenConfig(kS1, subPixel);

    // A legitimate pixel width on the same kind still commits, so the test
    // above is a rejection and not a dead branch.
    QVariantMap pixels;
    pixels.insert(ScrollPerScreenKeys::defaultColumnWidthKind(), static_cast<int>(DefaultWidthKind::Fixed));
    pixels.insert(ScrollPerScreenKeys::defaultColumnWidthValue(), 640.0);
    engine->applyPerScreenConfig(kS2, pixels);

    // The rule channel applies the same floor to its bare fraction.
    QVariantMap tinyFraction;
    tinyFraction.insert(ScrollPerScreenKeys::defaultColumnWidth(), 0.001);
    engine->applyPerScreenConfig(kS3, tinyFraction);

    engine->windowOpened(QStringLiteral("app|a"), kS1, 0, 0);
    engine->windowOpened(QStringLiteral("app|b"), kS2, 0, 0);
    engine->windowOpened(QStringLiteral("app|c"), kS3, 0, 0);
    QVERIFY(columnExists(engine, kS1, QStringLiteral("app|a")));
    QVERIFY(columnExists(engine, kS2, QStringLiteral("app|b")));
    QVERIFY(columnExists(engine, kS3, QStringLiteral("app|c")));

    const ColumnWidth rejected = openedWidth(engine, kS1, QStringLiteral("app|a"));
    QCOMPARE(rejected.kind, ColumnWidth::Proportion);
    QCOMPARE(rejected.proportion, 0.25);

    const ColumnWidth committed = openedWidth(engine, kS2, QStringLiteral("app|b"));
    QCOMPARE(committed.kind, ColumnWidth::Fixed);
    QCOMPARE(committed.fixedPx, 640);
    // Promoted from a compile-time `640 < 1200` (which caught no mutation) to
    // the thing that actually matters: the committed INTENT reaches the
    // RESOLVED rect's main extent. A Fixed width that never made it into the
    // layout would satisfy the intent assertion above on its own.
    const QVector<QRect> committedRects = engine->visibleTileRects(kS2);
    QCOMPARE(committedRects.size(), 1);
    QCOMPARE(ScrollTestUtils::Ax::mainLen(committedRects.first()), 640);

    const ColumnWidth rejectedFraction = openedWidth(engine, kS3, QStringLiteral("app|c"));
    QCOMPARE(rejectedFraction.kind, ColumnWidth::Proportion);
    QCOMPARE(rejectedFraction.proportion, 0.25);
}

void TestScrollEnginePerScreen::templatePresetListReplacesSettingsListWholesale()
{
    // TEMPLATE channel: a pushed preset list replaces the settings list for
    // that screen only. Observable through a Preset-kind default width: the
    // stored spin resolves to a value anchor out of the template vocabulary
    // on S1 and out of the settings vocabulary on S2.
    QObject owner;
    auto* settings = new StubScrollSettings(&owner);
    settings->widthKind = static_cast<int>(DefaultWidthKind::Preset);
    settings->widthPresetIndex = 1; // settings presets: 0.25 / 0.5 / 0.75
    ScrollEngine* engine = makeEngine(&owner, settings);

    QVariantMap templ;
    templ.insert(ScrollPerScreenKeys::presetColumnWidths(), QVariantList{0.2, 0.4});
    engine->applyPerScreenConfig(kS1, templ);

    engine->windowOpened(QStringLiteral("app|a"), kS1, 0, 0);
    engine->windowOpened(QStringLiteral("app|b"), kS2, 0, 0);

    const QVector<QRect> onTemplate = engine->visibleTileRects(kS1);
    QCOMPARE(onTemplate.size(), 1);
    // 0.5 anchor snaps to 0.4, the nearer template entry.
    QCOMPARE(Ax::mainLen(onTemplate.first()), qRound(0.4 * kMainExtent));

    const QVector<QRect> onSettings = engine->visibleTileRects(kS2);
    QCOMPARE(onSettings.size(), 1);
    QCOMPARE(Ax::mainLen(onSettings.first()), qRound(0.5 * kMainExtent)); // settings idx 1
}

void TestScrollEnginePerScreen::templatePresetHeightsReplaceSettingsHeights()
{
    // Height twin of the width test above: a pushed presetWindowHeights list
    // replaces the settings height list for that screen only. Observed both
    // as the seeded INTENT (the preset index survives) and as the RESOLVED
    // rect height (the same index lands on 0.6 of the work area under the
    // template versus 0.5 under the settings list).
    QObject owner;
    auto* settings = new StubScrollSettings(&owner);
    settings->heightKind = static_cast<int>(DefaultHeightKind::Preset);
    settings->heightPresetIndex = 1; // settings heights: 0.25 / 0.5 / 0.75
    ScrollEngine* engine = makeEngine(&owner, settings);

    QVariantMap templ;
    templ.insert(ScrollPerScreenKeys::presetWindowHeights(), QVariantList{0.3, 0.6});
    engine->applyPerScreenConfig(kS1, templ);

    engine->windowOpened(QStringLiteral("app|a"), kS1, 0, 0);
    engine->windowOpened(QStringLiteral("app|b"), kS2, 0, 0);

    // The seeded INTENT is a value anchor now: no per-screen spin override
    // exists here, so BOTH screens inherit the global anchor (settings list
    // index 1 → 0.5). What differs is resolution — the template screen
    // SNAPS the anchor into its {0.3, 0.6} vocabulary (→ 0.6, asserted on
    // the rects below) while the settings screen resolves it verbatim.
    const WindowHeight onTemplate = openedHeight(engine, kS1, QStringLiteral("app|a"));
    QCOMPARE(onTemplate.kind, WindowHeight::Preset);
    QCOMPARE(onTemplate.presetFraction, 0.5);
    const WindowHeight onSettings = openedHeight(engine, kS2, QStringLiteral("app|b"));
    QCOMPARE(onSettings.kind, WindowHeight::Preset);
    QCOMPARE(onSettings.presetFraction, 0.5);

    const QVector<QRect> templateRects = engine->visibleTileRects(kS1);
    QCOMPARE(templateRects.size(), 1);
    QCOMPARE(Ax::crossLen(templateRects.first()), qRound(0.6 * kCrossExtent));
    const QVector<QRect> settingsRects = engine->visibleTileRects(kS2);
    QCOMPARE(settingsRects.size(), 1);
    QCOMPARE(Ax::crossLen(settingsRects.first()), qRound(0.5 * kCrossExtent));
}

void TestScrollEnginePerScreen::templateListShrinkClampsResolvedPresetWidth()
{
    // A template swap that shrinks the vocabulary must reflow the column's
    // value anchor onto the nearest surviving entry, never crash or zero out.
    QObject owner;
    auto* settings = new StubScrollSettings(&owner);
    settings->widthKind = static_cast<int>(DefaultWidthKind::Preset);
    settings->widthPresetIndex = 2; // resolves to 0.75 pre-template
    ScrollEngine* engine = makeEngine(&owner, settings);

    engine->windowOpened(QStringLiteral("app|a"), kS1, 0, 0);
    QVector<QRect> rects = engine->visibleTileRects(kS1);
    QCOMPARE(rects.size(), 1);
    QCOMPARE(Ax::mainLen(rects.first()), qRound(0.75 * kMainExtent));

    // Now the template arrives with a single entry: the column's anchor
    // snaps to the lone 0.6 preset at the next resolve.
    QVariantMap templ;
    templ.insert(ScrollPerScreenKeys::presetColumnWidths(), QVariantList{0.6});
    engine->applyPerScreenConfig(kS1, templ);
    rects = engine->visibleTileRects(kS1);
    QCOMPARE(rects.size(), 1);
    QCOMPARE(Ax::mainLen(rects.first()), qRound(0.6 * kMainExtent));

    // Clearing the override restores the settings vocabulary.
    engine->clearPerScreenConfig(kS1);
    rects = engine->visibleTileRects(kS1);
    QCOMPARE(rects.size(), 1);
    QCOMPARE(Ax::mainLen(rects.first()), qRound(0.75 * kMainExtent));
}

void TestScrollEnginePerScreen::invalidTemplateEntriesFallBackToSettingsList()
{
    // Entries below the width floor or above 1.0 are dropped; a list with
    // nothing left is "no template" and the settings list stays in force.
    QObject owner;
    auto* settings = new StubScrollSettings(&owner);
    settings->widthKind = static_cast<int>(DefaultWidthKind::Preset);
    settings->widthPresetIndex = 1;
    ScrollEngine* engine = makeEngine(&owner, settings);

    QVariantMap garbage;
    garbage.insert(ScrollPerScreenKeys::presetColumnWidths(), QVariantList{0.01, 1.5, QStringLiteral("junk")});
    engine->applyPerScreenConfig(kS1, garbage);

    engine->windowOpened(QStringLiteral("app|a"), kS1, 0, 0);
    QVector<QRect> rects = engine->visibleTileRects(kS1);
    QCOMPARE(rects.size(), 1);
    QCOMPARE(Ax::mainLen(rects.first()), qRound(0.5 * kMainExtent));

    // A mixed list keeps its valid entries: 0.01 drops, 0.3 survives, and
    // the anchor snaps to the lone remaining entry.
    QVariantMap mixed;
    mixed.insert(ScrollPerScreenKeys::presetColumnWidths(), QVariantList{0.01, 0.3});
    engine->applyPerScreenConfig(kS1, mixed);
    rects = engine->visibleTileRects(kS1);
    QCOMPARE(rects.size(), 1);
    QCOMPARE(Ax::mainLen(rects.first()), qRound(0.3 * kMainExtent));
}

void TestScrollEnginePerScreen::invalidTemplateHeightEntriesFallBackToSettingsList()
{
    // The height twin of the width case above. The two vocabularies are
    // parsed by one shared helper but with DIFFERENT floors, so a case that
    // only ever drove widths could not tell the height floor from the width
    // one — and an entirely-invalid height list must be "no template" rather
    // than an empty vocabulary, which would break height-preset cycling.
    QObject owner;
    auto* settings = new StubScrollSettings(&owner);
    settings->heightKind = static_cast<int>(DefaultHeightKind::Preset);
    settings->heightPresetIndex = 1; // settings heights: 0.25 / 0.5 / 0.75
    ScrollEngine* engine = makeEngine(&owner, settings);

    QVariantMap garbage;
    garbage.insert(ScrollPerScreenKeys::presetWindowHeights(), QVariantList{0.01, 1.5, QStringLiteral("junk"), -0.5});
    engine->applyPerScreenConfig(kS1, garbage);
    QCOMPARE(engine->effectivePresetWindowHeights(kS1), QList<qreal>({0.25, 0.5, 0.75}));

    engine->windowOpened(QStringLiteral("app|a"), kS1, 0, 0);
    const QVector<QRect> rects = engine->visibleTileRects(kS1);
    QCOMPARE(rects.size(), 1);
    QCOMPARE(Ax::crossLen(rects.first()), qRound(0.5 * kCrossExtent));

    // A mixed list keeps its valid entries, so the rejection above is a
    // rejection and not a dead branch.
    QVariantMap mixed;
    mixed.insert(ScrollPerScreenKeys::presetWindowHeights(), QVariantList{0.01, 0.4});
    engine->applyPerScreenConfig(kS1, mixed);
    QCOMPARE(engine->effectivePresetWindowHeights(kS1), QList<qreal>({0.4}));
    const QVector<QRect> snapped = engine->visibleTileRects(kS1);
    QCOMPARE(snapped.size(), 1);
    QCOMPARE(Ax::crossLen(snapped.first()), qRound(0.4 * kCrossExtent));
}

void TestScrollEnginePerScreen::templateListsAreCappedAtTheKeepAndScanBounds()
{
    // applyPerScreenConfig is exported library surface and stores the map
    // verbatim, so both bounds in enginelimits.h are public-API guards. The
    // numbers are hand-mirrored here (the header is private to the library),
    // which is the same kept-in-sync-by-hand rule the constants themselves
    // carry against the settings validator.
    constexpr int kKeepCap = 16;
    constexpr int kScanCap = 256;

    QObject owner;
    auto* settings = new StubScrollSettings(&owner);
    ScrollEngine* engine = makeEngine(&owner, settings);

    // KEEP cap: twenty valid entries, sixteen survive — and they are the
    // FIRST sixteen, so the cap truncates rather than sampling.
    QVariantList overlong;
    for (int i = 0; i < 20; ++i) {
        overlong.append(0.05 + 0.01 * i);
    }
    QVariantMap keep;
    keep.insert(ScrollPerScreenKeys::presetColumnWidths(), overlong);
    engine->applyPerScreenConfig(kS1, keep);
    const QList<qreal> kept = engine->effectivePresetColumnWidths(kS1);
    QCOMPARE(kept.size(), kKeepCap);
    QCOMPARE(kept.first(), 0.05);
    QCOMPARE(kept.last(), 0.05 + 0.01 * (kKeepCap - 1));

    // SCAN cap: the keep cap alone would let a long list of REJECTS be
    // converted in full on every relayout, since nothing ever fills the
    // output. Here every entry inside the scan window is invalid and the
    // only valid ones sit past it, so a parse that respected the scan bound
    // finds nothing and falls back to the settings vocabulary.
    QVariantList mostlyRejects;
    for (int i = 0; i < kScanCap; ++i) {
        mostlyRejects.append(0.001); // below the width floor
    }
    mostlyRejects.append(0.6);
    mostlyRejects.append(0.7);
    QVariantMap scan;
    scan.insert(ScrollPerScreenKeys::presetColumnWidths(), mostlyRejects);
    engine->applyPerScreenConfig(kS2, scan);
    QCOMPARE(engine->effectivePresetColumnWidths(kS2), QList<qreal>({0.25, 0.5, 0.75}));
}

void TestScrollEnginePerScreen::wrongTypedKindAndSpinValuesFallThrough()
{
    // Wrong TYPES, not out-of-range numbers. QVariant answers 0 for a value
    // it cannot convert, and 0 is a legal kind (Proportion for width, Auto
    // for height) and a legal spin — so an unchecked read would commit the
    // first enumerator or the first preset while reporting nothing wrong.
    // Every leg here must land on the configured GLOBAL instead.
    QObject owner;
    auto* settings = new StubScrollSettings(&owner);
    settings->widthKind = static_cast<int>(DefaultWidthKind::Preset);
    settings->widthPresetIndex = 2; // → 0.75
    settings->heightKind = static_cast<int>(DefaultHeightKind::Fixed);
    settings->heightValue = 200.0;
    ScrollEngine* engine = makeEngine(&owner, settings);

    // A non-numeric KIND on both axes: the whole settings channel falls
    // through, so both defaults stay the global ones.
    QVariantMap badKind;
    badKind.insert(ScrollPerScreenKeys::defaultColumnWidthKind(), QStringLiteral("preset"));
    badKind.insert(ScrollPerScreenKeys::defaultWindowHeightKind(), QStringLiteral("fixed"));
    engine->applyPerScreenConfig(kS1, badKind);

    // A legitimate Preset kind beside a non-numeric SPIN: the kind is
    // honoured and only the spin falls back, which for a matching global
    // kind means the global's own anchor (0.75) — never index 0.
    QVariantMap badSpin;
    badSpin.insert(ScrollPerScreenKeys::defaultColumnWidthKind(), static_cast<int>(DefaultWidthKind::Preset));
    badSpin.insert(ScrollPerScreenKeys::defaultColumnWidthPresetIndex(), QStringLiteral("first"));
    engine->applyPerScreenConfig(kS2, badSpin);

    // A non-numeric rule FRACTION is rejected by the rule channel too.
    QVariantMap badFraction;
    badFraction.insert(ScrollPerScreenKeys::defaultColumnWidth(), QStringLiteral("half"));
    engine->applyPerScreenConfig(kS3, badFraction);

    engine->windowOpened(QStringLiteral("app|a"), kS1, 0, 0);
    engine->windowOpened(QStringLiteral("app|b"), kS2, 0, 0);
    engine->windowOpened(QStringLiteral("app|c"), kS3, 0, 0);
    QVERIFY(columnExists(engine, kS1, QStringLiteral("app|a")));
    QVERIFY(columnExists(engine, kS2, QStringLiteral("app|b")));
    QVERIFY(columnExists(engine, kS3, QStringLiteral("app|c")));

    const ColumnWidth kindRejected = openedWidth(engine, kS1, QStringLiteral("app|a"));
    QCOMPARE(kindRejected.kind, ColumnWidth::Preset);
    QCOMPARE(kindRejected.presetFraction, 0.75);
    const WindowHeight heightKindRejected = openedHeight(engine, kS1, QStringLiteral("app|a"));
    QCOMPARE(heightKindRejected.kind, WindowHeight::Fixed);
    QCOMPARE(heightKindRejected.fixedPx, 200);

    const ColumnWidth spinRejected = openedWidth(engine, kS2, QStringLiteral("app|b"));
    QCOMPARE(spinRejected.kind, ColumnWidth::Preset);
    QCOMPARE(spinRejected.presetFraction, 0.75);

    const ColumnWidth fractionRejected = openedWidth(engine, kS3, QStringLiteral("app|c"));
    QCOMPARE(fractionRejected.kind, ColumnWidth::Preset);
    QCOMPARE(fractionRejected.presetFraction, 0.75);
}

void TestScrollEnginePerScreen::autoHeightKindOverridesAFixedGlobal()
{
    // The Auto arm of the height resolver, which no other case reaches: it
    // is the one kind that returns a fresh WindowHeight rather than reading
    // a slot, so a screen scoped to Auto must drop the global's Fixed
    // pixels and take the even split instead.
    QObject owner;
    auto* settings = new StubScrollSettings(&owner);
    settings->heightKind = static_cast<int>(DefaultHeightKind::Fixed);
    settings->heightValue = 200.0;
    ScrollEngine* engine = makeEngine(&owner, settings);

    QVariantMap autoKind;
    autoKind.insert(ScrollPerScreenKeys::defaultWindowHeightKind(), static_cast<int>(DefaultHeightKind::Auto));
    engine->applyPerScreenConfig(kS1, autoKind);

    engine->windowOpened(QStringLiteral("app|a"), kS1, 0, 0);
    engine->windowOpened(QStringLiteral("app|b"), kS2, 0, 0);
    QVERIFY(columnExists(engine, kS1, QStringLiteral("app|a")));
    QVERIFY(columnExists(engine, kS2, QStringLiteral("app|b")));

    const WindowHeight autoHeight = openedHeight(engine, kS1, QStringLiteral("app|a"));
    QCOMPARE(autoHeight.kind, WindowHeight::Auto);
    // Auto fills the column, unlike the global's 200px.
    const QVector<QRect> filled = engine->visibleTileRects(kS1);
    QCOMPARE(filled.size(), 1);
    QCOMPARE(Ax::crossLen(filled.first()), kCrossExtent);

    const WindowHeight globalHeight = openedHeight(engine, kS2, QStringLiteral("app|b"));
    QCOMPARE(globalHeight.kind, WindowHeight::Fixed);
    QCOMPARE(globalHeight.fixedPx, 200);
}

void TestScrollEnginePerScreen::tabIndicatorOverridesArePerProperty()
{
    // effectiveTabIndicator resolves SEVEN independent properties out of one
    // override map. A rule that sets only one must leave the other six on
    // their configured values — the same per-property contract the width trio
    // above has, and the reason each read is guarded by constFind rather than
    // value(): a default-constructed QVariant would read as false/0 and
    // silently override a configured value with a zero.
    QObject owner;
    auto* settings = new StubScrollSettings(&owner);
    settings->tabIndicatorPlaceWithinColumn = false;
    settings->tabIndicatorGap = 5;
    settings->tabIndicatorWidth = 4;
    settings->tabIndicatorPosition = static_cast<int>(TabIndicatorPosition::Left);
    ScrollEngine* engine = makeEngine(&owner, settings);

    // One key only.
    QVariantMap onlyGap;
    onlyGap.insert(ScrollPerScreenKeys::tabIndicatorGap(), -12);
    engine->applyPerScreenConfig(kS1, onlyGap);

    const TabIndicatorParams ruled = engine->tabIndicatorParamsForScreen(kS1);
    QCOMPARE(ruled.gap, -12); // the override landed, negative and all
    // ...and nothing else moved.
    QCOMPARE(ruled.width, 4);
    QCOMPARE(ruled.placeWithinColumn, false);
    QCOMPARE(ruled.position, TabIndicatorPosition::Left);

    // A screen with no override map at all is the pure configured answer.
    const TabIndicatorParams global = engine->tabIndicatorParamsForScreen(kS3);
    QCOMPARE(global.gap, 5);
    QCOMPARE(global.width, 4);
}

void TestScrollEnginePerScreen::tabIndicatorRejectsGarbageNumericOverrides()
{
    // Each of the three numeric fields has a hand-written guard rather than a
    // shared macro, so each needs its own leg; without these the guards can be
    // deleted with the suite still green. Gap and width fall back to the
    // configured value on anything out of range; length falls back below its
    // floor but CLAMPS above 1.0, which the legs below pin separately.
    QObject owner;
    auto* settings = new StubScrollSettings(&owner);
    settings->tabIndicatorEnabled = true;
    settings->tabIndicatorHideWhenSingleTab = false;
    settings->tabIndicatorGap = 5;
    settings->tabIndicatorWidth = 4;
    settings->tabIndicatorLengthProportion = 0.5;
    ScrollEngine* engine = makeEngine(&owner, settings);

    // Length: a legal fraction lands; zero and negative leave the configured
    // value alone (they would resolve the indicator to a sliver while every
    // setting still reported it on), and above 1.0 clamps rather than falls back.
    QVariantMap m;
    m.insert(ScrollPerScreenKeys::tabIndicatorLengthProportion(), 0.25);
    engine->applyPerScreenConfig(kS1, m);
    QCOMPARE(engine->tabIndicatorParamsForScreen(kS1).lengthProportion, 0.25);

    m.insert(ScrollPerScreenKeys::tabIndicatorLengthProportion(), 0.0);
    engine->applyPerScreenConfig(kS1, m);
    QCOMPARE(engine->tabIndicatorParamsForScreen(kS1).lengthProportion, 0.5);

    m.insert(ScrollPerScreenKeys::tabIndicatorLengthProportion(), -1.0);
    engine->applyPerScreenConfig(kS1, m);
    QCOMPARE(engine->tabIndicatorParamsForScreen(kS1).lengthProportion, 0.5);

    m.insert(ScrollPerScreenKeys::tabIndicatorLengthProportion(), 4.0);
    engine->applyPerScreenConfig(kS1, m);
    QCOMPARE(engine->tabIndicatorParamsForScreen(kS1).lengthProportion, 1.0);

    // Gap and width are bounded at this boundary too: an out-of-range override
    // leaves the configured value rather than feeding the reservation
    // arithmetic a number the config layer would never have produced.
    QVariantMap wild;
    wild.insert(ScrollPerScreenKeys::tabIndicatorGap(), 100000);
    wild.insert(ScrollPerScreenKeys::tabIndicatorWidth(), -5);
    engine->applyPerScreenConfig(kS2, wild);
    QCOMPARE(engine->tabIndicatorParamsForScreen(kS2).gap, 5);
    QCOMPARE(engine->tabIndicatorParamsForScreen(kS2).width, 4);

    // The two bools that no other case exercises still round-trip.
    QVariantMap bools;
    bools.insert(ScrollPerScreenKeys::tabIndicatorHideWhenSingleTab(), true);
    engine->applyPerScreenConfig(kS3, bools);
    QCOMPARE(engine->tabIndicatorParamsForScreen(kS3).hideWhenSingleTab, true);
    QCOMPARE(engine->tabIndicatorParamsForScreen(kS3).enabled, true);
}

void TestScrollEnginePerScreen::tabIndicatorRejectsAGarbagePositionOverride()
{
    // Position is the one field cast into an enum, so it is validate-then-
    // fall-back like its siblings: an out-of-range value must leave the
    // CONFIGURED position alone rather than snapping the indicator to Left
    // (which a raw static_cast or a value()-with-default would do).
    QObject owner;
    auto* settings = new StubScrollSettings(&owner);
    settings->tabIndicatorPosition = static_cast<int>(TabIndicatorPosition::Bottom);
    ScrollEngine* engine = makeEngine(&owner, settings);

    QVariantMap garbage;
    garbage.insert(ScrollPerScreenKeys::tabIndicatorPosition(), 99);
    engine->applyPerScreenConfig(kS1, garbage);
    QCOMPARE(engine->tabIndicatorParamsForScreen(kS1).position, TabIndicatorPosition::Bottom);

    // A legitimate override still applies, so the guard is not simply inert.
    QVariantMap valid;
    valid.insert(ScrollPerScreenKeys::tabIndicatorPosition(), static_cast<int>(TabIndicatorPosition::Right));
    engine->applyPerScreenConfig(kS2, valid);
    QCOMPARE(engine->tabIndicatorParamsForScreen(kS2).position, TabIndicatorPosition::Right);
}

void TestScrollEnginePerScreen::templateBlueprintSeedsFirstColumns()
{
    // The template's blueprint shapes the first N materializing columns
    // (width AND display), and the column beyond it takes the pushed
    // beyond-blueprint default.
    QObject owner;
    auto* settings = new StubScrollSettings(&owner);
    ScrollEngine* engine = makeEngine(&owner, settings);

    QVariantMap templ;
    QVariantList blueprint;
    QVariantMap first;
    first.insert(ScrollPerScreenKeys::templateColumnWidth(), 0.6);
    blueprint.append(first);
    QVariantMap second;
    second.insert(ScrollPerScreenKeys::templateColumnWidth(), 0.4);
    second.insert(ScrollPerScreenKeys::templateColumnDisplay(), 1);
    blueprint.append(second);
    templ.insert(ScrollPerScreenKeys::templateColumns(), blueprint);
    templ.insert(ScrollPerScreenKeys::defaultColumnWidthKind(), static_cast<int>(DefaultWidthKind::Proportion));
    templ.insert(ScrollPerScreenKeys::defaultColumnWidthValue(), 0.3);
    engine->applyPerScreenConfig(kS1, templ);

    engine->windowOpened(QStringLiteral("app|a"), kS1, 0, 0);
    engine->windowOpened(QStringLiteral("app|b"), kS1, 0, 0);
    engine->windowOpened(QStringLiteral("app|c"), kS1, 0, 0);

    auto* state = static_cast<ScrollState*>(engine->stateForScreen(kS1));
    QVERIFY(state);
    QCOMPARE(state->strip().columns().size(), 3);
    QCOMPARE(openedWidth(engine, kS1, QStringLiteral("app|a")).kind, ColumnWidth::Proportion);
    QCOMPARE(openedWidth(engine, kS1, QStringLiteral("app|a")).proportion, 0.6);
    QCOMPARE(openedWidth(engine, kS1, QStringLiteral("app|b")).proportion, 0.4);
    // The second blueprint column opened tabbed; the strip stores display
    // per column, so find app|b's column. The found flag keeps the assertion
    // honest: without it a missing app|b column would satisfy the loop
    // vacuously, which is the same discipline columnExists enforces for the
    // width reads above.
    bool foundB = false;
    for (const Column& col : state->strip().columns()) {
        if (col.indexOfWindow(QStringLiteral("app|b")) >= 0) {
            foundB = true;
            QCOMPARE(col.display, ColumnDisplay::Tabbed);
        }
    }
    QVERIFY(foundB);
    // Beyond the blueprint: the template's declared default.
    QCOMPARE(openedWidth(engine, kS1, QStringLiteral("app|c")).proportion, 0.3);
}

void TestScrollEnginePerScreen::openRuleOutranksTemplateBlueprint()
{
    // A per-window open rule pins the width; the blueprint entry the column
    // would have taken must not override it.
    QObject owner;
    auto* settings = new StubScrollSettings(&owner);
    ScrollEngine* engine = makeEngine(&owner, settings);

    QVariantMap templ;
    QVariantList blueprint;
    QVariantMap first;
    first.insert(ScrollPerScreenKeys::templateColumnWidth(), 0.6);
    blueprint.append(first);
    templ.insert(ScrollPerScreenKeys::templateColumns(), blueprint);
    engine->applyPerScreenConfig(kS1, templ);

    engine->setOpenParamsResolver([](const QString&, const QString&) {
        ScrollOpenParams params;
        params.widthFraction = 0.25;
        return params;
    });
    engine->windowOpened(QStringLiteral("app|a"), kS1, 0, 0);
    QCOMPARE(openedWidth(engine, kS1, QStringLiteral("app|a")).kind, ColumnWidth::Proportion);
    QCOMPARE(openedWidth(engine, kS1, QStringLiteral("app|a")).proportion, 0.25);
}

void TestScrollEnginePerScreen::openMaximizedRuleOutranksWidthRuleAndBlueprint()
{
    // openMaximized is the stronger width verdict: it wins over a width rule
    // on the same window AND over the blueprint entry the column would have
    // taken.
    QObject owner;
    auto* settings = new StubScrollSettings(&owner);
    ScrollEngine* engine = makeEngine(&owner, settings);

    QVariantMap templ;
    QVariantList blueprint;
    QVariantMap first;
    first.insert(ScrollPerScreenKeys::templateColumnWidth(), 0.6);
    blueprint.append(first);
    templ.insert(ScrollPerScreenKeys::templateColumns(), blueprint);
    engine->applyPerScreenConfig(kS1, templ);

    engine->setOpenParamsResolver([](const QString&, const QString&) {
        ScrollOpenParams params;
        params.widthFraction = 0.25;
        params.maximized = true;
        return params;
    });
    engine->windowOpened(QStringLiteral("app|a"), kS1, 0, 0);
    QCOMPARE(openedWidth(engine, kS1, QStringLiteral("app|a")).kind, ColumnWidth::Proportion);
    QCOMPARE(openedWidth(engine, kS1, QStringLiteral("app|a")).proportion, 1.0);
}

void TestScrollEnginePerScreen::openFocusedRuleOverridesFocusNewWindows()
{
    // Both polarities layer over the global setting: focused=false withholds
    // strip adoption under a focus-new-windows ON global, focused=true forces
    // it under an OFF one.
    QObject owner;
    auto* settings = new StubScrollSettings(&owner);
    ScrollEngine* engine = makeEngine(&owner, settings);

    engine->windowOpened(QStringLiteral("app|a"), kS1, 0, 0);
    auto* state = static_cast<ScrollState*>(engine->stateForScreen(kS1));
    QVERIFY(state);
    QCOMPARE(state->strip().activeWindowId(), QStringLiteral("app|a"));

    // Global ON, rule false: the strip keeps the prior active column.
    engine->setOpenParamsResolver([](const QString&, const QString&) {
        ScrollOpenParams params;
        params.focused = false;
        return params;
    });
    engine->windowOpened(QStringLiteral("app|b"), kS1, 0, 0);
    QCOMPARE(state->strip().activeWindowId(), QStringLiteral("app|a"));

    // Global OFF, rule true: the arrival is adopted as the active column.
    settings->focusNewWindows = false;
    engine->setOpenParamsResolver([](const QString&, const QString&) {
        ScrollOpenParams params;
        params.focused = true;
        return params;
    });
    engine->windowOpened(QStringLiteral("app|c"), kS1, 0, 0);
    QCOMPARE(state->strip().activeWindowId(), QStringLiteral("app|c"));

    // Global OFF, no rule: the setting stays authoritative.
    engine->setOpenParamsResolver({});
    engine->windowOpened(QStringLiteral("app|d"), kS1, 0, 0);
    QCOMPARE(state->strip().activeWindowId(), QStringLiteral("app|c"));
}

void TestScrollEnginePerScreen::openFocusedFalseSurvivesTheCompositorsOwnFocusReport()
{
    // The regression this pins was observed live, not theorised: declining
    // focus rewound the STRIP, but the compositor had already focused the
    // arriving window on its own and reported that focus back independently.
    // The report adopted the arrival and undid the rewind, so the rule read as
    // a no-op to the user. Driving windowOpened alone cannot catch that — the
    // report has to be delivered, which is what this test adds over its
    // sibling above.
    QObject owner;
    auto* settings = new StubScrollSettings(&owner);
    ScrollEngine* engine = makeEngine(&owner, settings);

    engine->windowOpened(QStringLiteral("app|a"), kS1, 0, 0);
    auto* state = static_cast<ScrollState*>(engine->stateForScreen(kS1));
    QVERIFY(state);
    QCOMPARE(state->strip().activeWindowId(), QStringLiteral("app|a"));

    engine->setOpenParamsResolver([](const QString&, const QString&) {
        ScrollOpenParams params;
        params.focused = false;
        return params;
    });
    engine->windowOpened(QStringLiteral("app|b"), kS1, 0, 0);
    QCOMPARE(state->strip().activeWindowId(), QStringLiteral("app|a"));

    // The compositor's own report for the declined arrival. Consumed once, so
    // the rewind stands.
    engine->windowFocused(QStringLiteral("app|b"), kS1);
    QCOMPARE(state->strip().activeWindowId(), QStringLiteral("app|a"));

    // Consumed ONCE and no more: a later report for the same window is a real
    // user click and must adopt normally. A standing veto would make the
    // window unfocusable, which is why the mark is one-shot.
    engine->windowFocused(QStringLiteral("app|b"), kS1);
    QCOMPARE(state->strip().activeWindowId(), QStringLiteral("app|b"));
}

void TestScrollEnginePerScreen::openFocusedFalseOnAnEmptyStripStillAdoptsTheArrival()
{
    // The rewind arm needs a prior active column to rewind TO. On an empty
    // strip there is none, so the first window becomes the active column
    // whatever the rule says — a strip whose only column were not active
    // would leave every later direction verb navigating from nowhere. The
    // rule still governs the SECOND arrival, which is what makes this a
    // guard on the empty case rather than the rule being inert.
    QObject owner;
    auto* settings = new StubScrollSettings(&owner);
    ScrollEngine* engine = makeEngine(&owner, settings);
    engine->setOpenParamsResolver([](const QString&, const QString&) {
        ScrollOpenParams params;
        params.focused = false;
        return params;
    });

    engine->windowOpened(QStringLiteral("app|a"), kS1, 0, 0);
    auto* state = static_cast<ScrollState*>(engine->stateForScreen(kS1));
    QVERIFY(state);
    QCOMPARE(state->strip().activeWindowId(), QStringLiteral("app|a"));

    engine->windowOpened(QStringLiteral("app|b"), kS1, 0, 0);
    QCOMPARE(state->strip().activeWindowId(), QStringLiteral("app|a"));
}

void TestScrollEnginePerScreen::openMaximizedFalseLeavesTheDefaultWidth()
{
    // An EXPLICIT false must read exactly like an unset optional: the rule
    // says "do not maximize", not "maximize to the default". Without this
    // leg a resolver that treated the field's mere presence as a verdict
    // would pass the whole suite.
    QObject owner;
    auto* settings = new StubScrollSettings(&owner);
    settings->widthKind = static_cast<int>(DefaultWidthKind::Proportion);
    settings->widthValue = 0.25;
    ScrollEngine* engine = makeEngine(&owner, settings);
    engine->setOpenParamsResolver([](const QString&, const QString&) {
        ScrollOpenParams params;
        params.maximized = false;
        return params;
    });

    engine->windowOpened(QStringLiteral("app|a"), kS1, 0, 0);
    QVERIFY(columnExists(engine, kS1, QStringLiteral("app|a")));
    const ColumnWidth width = openedWidth(engine, kS1, QStringLiteral("app|a"));
    QCOMPARE(width.kind, ColumnWidth::Proportion);
    QCOMPARE(width.proportion, 0.25);
}

void TestScrollEnginePerScreen::openMaximizedIsDroppedByAConsumeOpen()
{
    // A consume open joins an existing column, and a joining tile carries NO
    // width verdict — resizing the host would resize every sibling in the
    // stack. So openMaximized (like openColumnWidth) reaches only a column
    // the open CREATES. The header documents that on ScrollOpenParams; this
    // pins it, because the drop is silent.
    QObject owner;
    auto* settings = new StubScrollSettings(&owner);
    settings->widthKind = static_cast<int>(DefaultWidthKind::Proportion);
    settings->widthValue = 0.25;
    ScrollEngine* engine = makeEngine(&owner, settings);

    engine->windowOpened(QStringLiteral("app|a"), kS1, 0, 0);
    QCOMPARE(openedWidth(engine, kS1, QStringLiteral("app|a")).proportion, 0.25);

    engine->setOpenParamsResolver([](const QString&, const QString&) {
        ScrollOpenParams params;
        params.consume = true;
        params.maximized = true;
        return params;
    });
    engine->windowOpened(QStringLiteral("app|b"), kS1, 0, 0);

    auto* state = static_cast<ScrollState*>(engine->stateForScreen(kS1));
    QVERIFY(state);
    // One column holding both windows, still at the host's width.
    QCOMPARE(state->strip().columns().size(), 1);
    QVERIFY(state->strip().columns().first().indexOfWindow(QStringLiteral("app|b")) >= 0);
    QCOMPARE(state->strip().columns().first().width.kind, ColumnWidth::Proportion);
    QCOMPARE(state->strip().columns().first().width.proportion, 0.25);
}

void TestScrollEnginePerScreen::templateBlueprintNeverResizesExistingColumns()
{
    // Applying a template reshapes nothing that already exists: existing
    // columns keep their widths, and only columns created AFTER the apply
    // consume blueprint entries (from the current column count onward).
    QObject owner;
    auto* settings = new StubScrollSettings(&owner);
    settings->widthKind = static_cast<int>(DefaultWidthKind::Proportion);
    settings->widthValue = 0.5;
    ScrollEngine* engine = makeEngine(&owner, settings);

    engine->windowOpened(QStringLiteral("app|a"), kS1, 0, 0);
    QCOMPARE(openedWidth(engine, kS1, QStringLiteral("app|a")).proportion, 0.5);

    QVariantMap templ;
    QVariantList blueprint;
    for (qreal width : {0.7, 0.2}) {
        QVariantMap entry;
        entry.insert(ScrollPerScreenKeys::templateColumnWidth(), width);
        blueprint.append(entry);
    }
    templ.insert(ScrollPerScreenKeys::templateColumns(), blueprint);
    engine->applyPerScreenConfig(kS1, templ);
    // Drain the retile the apply scheduled: without it this leg would assert
    // against a strip the relayout has not touched yet, so a regression that
    // reshaped existing columns AT RELAYOUT would still read green here.
    QCoreApplication::processEvents();

    // The existing column is untouched.
    QCOMPARE(openedWidth(engine, kS1, QStringLiteral("app|a")).proportion, 0.5);
    // The next column materializes at index 1 and takes blueprint[1].
    engine->windowOpened(QStringLiteral("app|b"), kS1, 0, 0);
    QCOMPARE(openedWidth(engine, kS1, QStringLiteral("app|b")).proportion, 0.2);
}

void TestScrollEnginePerScreen::templateBlueprintEntryWithoutDisplayKeepsTheDefault()
{
    // A blueprint entry may carry a width only. Its column must then keep the
    // EFFECTIVE default display rather than falling to Normal: reading the
    // absent key as 0 silently overrode a Tabbed default for exactly the
    // first N columns, which is the stretch a template is most likely to
    // shape. The in-tree daemon always writes both keys on every entry, so
    // this covers the public-API belt for embedder-supplied maps rather than
    // a shipped bug.
    QObject owner;
    auto* settings = new StubScrollSettings(&owner);
    ScrollEngine* engine = makeEngine(&owner, settings);

    QVariantMap templ;
    templ.insert(ScrollPerScreenKeys::defaultColumnDisplay(), static_cast<int>(ColumnDisplay::Tabbed));
    QVariantList blueprint;
    QVariantMap widthOnly;
    widthOnly.insert(ScrollPerScreenKeys::templateColumnWidth(), 0.6);
    blueprint.append(widthOnly);
    templ.insert(ScrollPerScreenKeys::templateColumns(), blueprint);
    engine->applyPerScreenConfig(kS1, templ);

    engine->windowOpened(QStringLiteral("app|a"), kS1, 0, 0);

    auto* state = static_cast<ScrollState*>(engine->stateForScreen(kS1));
    QVERIFY(state);
    QCOMPARE(state->strip().columns().size(), 1);
    // The entry's width still lands, so the blueprint really was consumed.
    QCOMPARE(openedWidth(engine, kS1, QStringLiteral("app|a")).proportion, 0.6);
    QCOMPARE(state->strip().columns().first().display, ColumnDisplay::Tabbed);

    // An entry that DOES carry a display still wins over the same default.
    QVariantMap explicitNormal;
    explicitNormal.insert(ScrollPerScreenKeys::defaultColumnDisplay(), static_cast<int>(ColumnDisplay::Tabbed));
    QVariantList twoEntries;
    twoEntries.append(widthOnly);
    QVariantMap normalEntry;
    normalEntry.insert(ScrollPerScreenKeys::templateColumnWidth(), 0.4);
    normalEntry.insert(ScrollPerScreenKeys::templateColumnDisplay(), static_cast<int>(ColumnDisplay::Normal));
    twoEntries.append(normalEntry);
    explicitNormal.insert(ScrollPerScreenKeys::templateColumns(), twoEntries);
    engine->applyPerScreenConfig(kS2, explicitNormal);

    engine->windowOpened(QStringLiteral("app|b"), kS2, 0, 0);
    engine->windowOpened(QStringLiteral("app|c"), kS2, 0, 0);

    auto* other = static_cast<ScrollState*>(engine->stateForScreen(kS2));
    QVERIFY(other);
    QCOMPARE(other->strip().columns().size(), 2);
    QCOMPARE(other->strip().columns().at(0).display, ColumnDisplay::Tabbed);
    QCOMPARE(other->strip().columns().at(1).display, ColumnDisplay::Normal);
}

void TestScrollEnginePerScreen::closingAColumnDoesNotHandItsBlueprintEntryBack()
{
    // The blueprint is a SEED, not a standing rule: an entry a column already
    // took is spent, so closing that column must not prescribe the next open.
    // Deriving the entry from the live column count did exactly that — it
    // refilled any gap, so a column the user had toggled to Normal came back
    // Tabbed and the toggle read as broken.
    QObject owner;
    auto* settings = new StubScrollSettings(&owner);
    ScrollEngine* engine = makeEngine(&owner, settings);

    QVariantMap templ;
    QVariantList blueprint;
    for (qreal width : {0.6, 0.4}) {
        QVariantMap entry;
        entry.insert(ScrollPerScreenKeys::templateColumnWidth(), width);
        entry.insert(ScrollPerScreenKeys::templateColumnDisplay(), static_cast<int>(ColumnDisplay::Tabbed));
        blueprint.append(entry);
    }
    templ.insert(ScrollPerScreenKeys::templateColumns(), blueprint);
    templ.insert(ScrollPerScreenKeys::defaultColumnWidthKind(), static_cast<int>(DefaultWidthKind::Proportion));
    templ.insert(ScrollPerScreenKeys::defaultColumnWidthValue(), 0.3);
    templ.insert(ScrollPerScreenKeys::defaultColumnDisplay(), static_cast<int>(ColumnDisplay::Normal));
    engine->applyPerScreenConfig(kS1, templ);

    engine->windowOpened(QStringLiteral("app|a"), kS1, 0, 0);
    engine->windowOpened(QStringLiteral("app|b"), kS1, 0, 0);
    QCOMPARE(openedWidth(engine, kS1, QStringLiteral("app|b")).proportion, 0.4);

    // Close the second column. The strip is back to one column, but both
    // entries are spent.
    engine->windowClosed(QStringLiteral("app|b"));
    QCoreApplication::processEvents();

    auto* state = static_cast<ScrollState*>(engine->stateForScreen(kS1));
    QVERIFY(state);
    QCOMPARE(state->strip().columns().size(), 1);

    // The replacement takes the BEYOND-blueprint defaults, not entry 1 again.
    engine->windowOpened(QStringLiteral("app|c"), kS1, 0, 0);
    QCOMPARE(openedWidth(engine, kS1, QStringLiteral("app|c")).proportion, 0.3);
    bool foundC = false;
    for (const Column& col : state->strip().columns()) {
        if (col.indexOfWindow(QStringLiteral("app|c")) >= 0) {
            foundC = true;
            QCOMPARE(col.display, ColumnDisplay::Normal);
        }
    }
    QVERIFY(foundC);
}

void TestScrollEnginePerScreen::emptyingTheStripRestartsTheBlueprintSeed()
{
    // The other half of the spent-entry contract: a screen cleared out has no
    // column standing for any entry, so the next window opens from the top of
    // the blueprint again. This is what makes the template describe the
    // STARTING shape of a screen rather than a one-time event in its history.
    QObject owner;
    auto* settings = new StubScrollSettings(&owner);
    ScrollEngine* engine = makeEngine(&owner, settings);

    QVariantMap templ;
    QVariantList blueprint;
    QVariantMap first;
    first.insert(ScrollPerScreenKeys::templateColumnWidth(), 0.6);
    blueprint.append(first);
    templ.insert(ScrollPerScreenKeys::templateColumns(), blueprint);
    templ.insert(ScrollPerScreenKeys::defaultColumnWidthKind(), static_cast<int>(DefaultWidthKind::Proportion));
    templ.insert(ScrollPerScreenKeys::defaultColumnWidthValue(), 0.3);
    engine->applyPerScreenConfig(kS1, templ);

    engine->windowOpened(QStringLiteral("app|a"), kS1, 0, 0);
    QCOMPARE(openedWidth(engine, kS1, QStringLiteral("app|a")).proportion, 0.6);

    engine->windowClosed(QStringLiteral("app|a"));
    // Drained: the reset rides applyLayout's empty branch, which the close
    // only SCHEDULES. Without this the next open would still see the spent
    // cursor and the assertion below would pass for the wrong reason.
    QCoreApplication::processEvents();

    engine->windowOpened(QStringLiteral("app|b"), kS1, 0, 0);
    QCOMPARE(openedWidth(engine, kS1, QStringLiteral("app|b")).proportion, 0.6);
}

void TestScrollEnginePerScreen::anewBlueprintRestartsTheSeedInsteadOfResumingTheOldCount()
{
    // Assigning a different template is an explicit act, so its blueprint
    // seeds from its own first entry rather than resuming a cursor that
    // counted the previous one's. The live column count still floors the
    // result — the new blueprint shapes what comes NEXT and never reaches
    // back to reshape the column already on screen.
    QObject owner;
    auto* settings = new StubScrollSettings(&owner);
    ScrollEngine* engine = makeEngine(&owner, settings);

    QVariantMap oldTempl;
    QVariantList oldBlueprint;
    for (qreal width : {0.6, 0.4}) {
        QVariantMap entry;
        entry.insert(ScrollPerScreenKeys::templateColumnWidth(), width);
        oldBlueprint.append(entry);
    }
    oldTempl.insert(ScrollPerScreenKeys::templateColumns(), oldBlueprint);
    engine->applyPerScreenConfig(kS1, oldTempl);

    engine->windowOpened(QStringLiteral("app|a"), kS1, 0, 0);
    engine->windowOpened(QStringLiteral("app|b"), kS1, 0, 0);
    engine->windowClosed(QStringLiteral("app|b"));
    QCoreApplication::processEvents();
    // One column left, both of the old blueprint's entries spent.

    QVariantMap newTempl;
    QVariantList newBlueprint;
    for (qreal width : {0.9, 0.8, 0.7}) {
        QVariantMap entry;
        entry.insert(ScrollPerScreenKeys::templateColumnWidth(), width);
        newBlueprint.append(entry);
    }
    newTempl.insert(ScrollPerScreenKeys::templateColumns(), newBlueprint);
    engine->applyPerScreenConfig(kS1, newTempl);
    QCoreApplication::processEvents();

    // Entry 1 of the NEW blueprint: the seed restarted (a resumed cursor
    // would have reached entry 2) and the one existing column floors it.
    engine->windowOpened(QStringLiteral("app|c"), kS1, 0, 0);
    QCOMPARE(openedWidth(engine, kS1, QStringLiteral("app|c")).proportion, 0.8);
}

QTEST_GUILESS_MAIN(TestScrollEnginePerScreen)
#include "test_scrollengine_perscreen.moc"
