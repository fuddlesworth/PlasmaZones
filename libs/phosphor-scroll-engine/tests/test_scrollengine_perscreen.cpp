// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

// Per-screen default-width / default-height resolution.
//
// The engine reads three channels for each of the two defaults, in order:
// the RULE channel (a bare work-area fraction the rule cascade writes), the
// SETTINGS channel (a kind/value/preset-index trio the settings app writes
// per monitor), and the cached GLOBAL from refreshConfigFromSettings. Every
// layer writes the trio's keys independently, so a per-screen kind beside an
// absent value is the ordinary case and each slot falls back on its own to
// the global's matching slot. The TEMPLATE channel is a fourth of a
// different shape — not a default at all but a wholesale replacement of the
// preset LISTS the Preset kinds index into (presetColumnWidths /
// presetWindowHeights); its tests live at the bottom of this file.
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

#include <QVariantMap>
#include <QtTest>

using namespace PhosphorScrollEngine;

using ScrollTestUtils::kScreenHeight;
using ScrollTestUtils::kScreenWidth;
using ScrollTestUtils::makeProviderEngine;

namespace {

/// Minimal IScrollSettings the engine can qobject_cast to. Only the width /
/// height / preset accessors carry test-settable state; everything else
/// answers the inert value that keeps the layout math out of the way (zero
/// gaps, no smart-gaps, no minimum-size clamping), so a resolved rect is a
/// clean function of the value under test.
class StubScrollSettings : public QObject, public PhosphorEngine::IScrollSettings
{
    Q_OBJECT
    Q_INTERFACES(PhosphorEngine::IScrollSettings)

public:
    using QObject::QObject;

    int widthKind = static_cast<int>(DefaultWidthKind::Proportion);
    qreal widthValue = 0.25;
    int widthPresetIndex = 0;
    int heightKind = static_cast<int>(DefaultHeightKind::Auto);
    qreal heightValue = 0.0;
    int heightPresetIndex = 0;
    QStringList widthPresets{QStringLiteral("0.25"), QStringLiteral("0.5"), QStringLiteral("0.75")};
    QStringList heightPresets{QStringLiteral("0.25"), QStringLiteral("0.5"), QStringLiteral("0.75")};

    int scrollingInnerGap() const override
    {
        return 0;
    }
    bool scrollingUsePerSideOuterGap() const override
    {
        return false;
    }
    int scrollingOuterGap() const override
    {
        return 0;
    }
    int scrollingOuterGapTop() const override
    {
        return 0;
    }
    int scrollingOuterGapBottom() const override
    {
        return 0;
    }
    int scrollingOuterGapLeft() const override
    {
        return 0;
    }
    int scrollingOuterGapRight() const override
    {
        return 0;
    }
    bool scrollingFocusNewWindows() const override
    {
        return true;
    }
    int scrollingStickyWindowHandling() const override
    {
        return 0;
    }
    bool scrollingRespectMinimumSize() const override
    {
        return false;
    }
    bool scrollingSmartGaps() const override
    {
        return false;
    }
    int scrollingCenterFocusedColumn() const override
    {
        return 0;
    }
    bool scrollingAlwaysCenterSingleColumn() const override
    {
        return false;
    }
    int scrollingDefaultColumnWidthKind() const override
    {
        return widthKind;
    }
    qreal scrollingDefaultColumnWidthValue() const override
    {
        return widthValue;
    }
    int scrollingDefaultColumnWidthPresetIndex() const override
    {
        return widthPresetIndex;
    }
    int scrollingDefaultWindowHeightKind() const override
    {
        return heightKind;
    }
    qreal scrollingDefaultWindowHeightValue() const override
    {
        return heightValue;
    }
    int scrollingDefaultWindowHeightPresetIndex() const override
    {
        return heightPresetIndex;
    }
    int scrollingInsertPosition() const override
    {
        return 0;
    }
    int scrollingDefaultColumnDisplay() const override
    {
        return 0;
    }
    QStringList scrollingPresetColumnWidths() const override
    {
        return widthPresets;
    }
    QStringList scrollingPresetWindowHeights() const override
    {
        return heightPresets;
    }

    // Tab-indicator geometry. Public fields so a case can drive the indicator
    // the way it drives the width/height trios above; the seeds are the
    // shipped defaults, so an untouched stub behaves like a fresh config.
    bool tabIndicatorEnabled = true;
    bool tabIndicatorHideWhenSingleTab = false;
    bool tabIndicatorPlaceWithinColumn = false;
    int tabIndicatorGap = 5;
    int tabIndicatorWidth = 4;
    qreal tabIndicatorLengthProportion = 0.5;
    int tabIndicatorPosition = static_cast<int>(TabIndicatorPosition::Left);

    bool scrollingTabIndicatorEnabled() const override
    {
        return tabIndicatorEnabled;
    }
    bool scrollingTabIndicatorHideWhenSingleTab() const override
    {
        return tabIndicatorHideWhenSingleTab;
    }
    bool scrollingTabIndicatorPlaceWithinColumn() const override
    {
        return tabIndicatorPlaceWithinColumn;
    }
    int scrollingTabIndicatorGap() const override
    {
        return tabIndicatorGap;
    }
    int scrollingTabIndicatorWidth() const override
    {
        return tabIndicatorWidth;
    }
    qreal scrollingTabIndicatorLengthProportion() const override
    {
        return tabIndicatorLengthProportion;
    }
    int scrollingTabIndicatorPosition() const override
    {
        return tabIndicatorPosition;
    }
};

const QString kS1 = QStringLiteral("S1");
const QString kS2 = QStringLiteral("S2");
const QString kS3 = QStringLiteral("S3");

} // namespace

class TestScrollEnginePerScreen : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void widthChannelsRankRuleOverSettingsOverGlobal();
    void heightChannelsRankRuleOverSettingsOverGlobal();
    void absentTrioSlotsFallBackPerSlotToTheGlobal();
    void presetIndexIsClampedToTheLivePresetList();
    void fixedKindWithAProportionValueFallsThroughToTheGlobal();
    void templatePresetListReplacesSettingsListWholesale();
    void templatePresetHeightsReplaceSettingsHeights();
    void templateListShrinkClampsResolvedPresetWidth();
    void invalidTemplateEntriesFallBackToSettingsList();
    void tabIndicatorOverridesArePerProperty();
    void tabIndicatorRejectsGarbageNumericOverrides();
    void tabIndicatorRejectsAGarbagePositionOverride();

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
    QCOMPARE(ruled.fixedPx, kScreenHeight / 2);

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
    // even when the global's kind differs, and the value slot it needs is the
    // one that falls back. Proportion here, against a Preset global, so the
    // proportion slot has no global twin to inherit and resolves to the
    // documented fall-through instead.
    QVariantMap differingKind;
    differingKind.insert(ScrollPerScreenKeys::defaultColumnWidthKind(), static_cast<int>(DefaultWidthKind::Proportion));
    differingKind.insert(ScrollPerScreenKeys::defaultColumnWidthValue(), 0.6);
    engine->applyPerScreenConfig(kS2, differingKind);

    engine->windowOpened(QStringLiteral("app|a"), kS1, 0, 0);
    engine->windowOpened(QStringLiteral("app|b"), kS2, 0, 0);
    QVERIFY(columnExists(engine, kS1, QStringLiteral("app|a")));
    QVERIFY(columnExists(engine, kS2, QStringLiteral("app|b")));

    const ColumnWidth inherited = openedWidth(engine, kS1, QStringLiteral("app|a"));
    QCOMPARE(inherited.kind, ColumnWidth::Preset);
    QCOMPARE(inherited.presetIdx, 2);

    const WindowHeight inheritedHeight = openedHeight(engine, kS1, QStringLiteral("app|a"));
    QCOMPARE(inheritedHeight.kind, WindowHeight::Preset);
    QCOMPARE(inheritedHeight.presetIdx, 2);

    const ColumnWidth kindHonoured = openedWidth(engine, kS2, QStringLiteral("app|b"));
    QCOMPARE(kindHonoured.kind, ColumnWidth::Proportion);
    QCOMPARE(kindHonoured.proportion, 0.6);
}

void TestScrollEnginePerScreen::presetIndexIsClampedToTheLivePresetList()
{
    QObject owner;
    auto* settings = new StubScrollSettings(&owner);
    // Two presets, so a stored index of 9 (written when the list was longer)
    // must land on the last live entry rather than reading past it.
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
    QCOMPARE(clampedHigh.presetIdx, 1);

    const ColumnWidth clampedLow = openedWidth(engine, kS2, QStringLiteral("app|b"));
    QCOMPARE(clampedLow.kind, ColumnWidth::Preset);
    QCOMPARE(clampedLow.presetIdx, 0);
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
    QVERIFY(committed.fixedPx < kScreenWidth); // the fixture's own sanity check

    const ColumnWidth rejectedFraction = openedWidth(engine, kS3, QStringLiteral("app|c"));
    QCOMPARE(rejectedFraction.kind, ColumnWidth::Proportion);
    QCOMPARE(rejectedFraction.proportion, 0.25);
}

void TestScrollEnginePerScreen::templatePresetListReplacesSettingsListWholesale()
{
    // TEMPLATE channel: a pushed preset list replaces the settings list for
    // that screen only. Observable through a Preset-kind default width: the
    // same stored index resolves against the template list on S1 and the
    // settings list on S2.
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
    QCOMPARE(onTemplate.first().width(), qRound(0.4 * kScreenWidth)); // template idx 1

    const QVector<QRect> onSettings = engine->visibleTileRects(kS2);
    QCOMPARE(onSettings.size(), 1);
    QCOMPARE(onSettings.first().width(), qRound(0.5 * kScreenWidth)); // settings idx 1
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

    const WindowHeight onTemplate = openedHeight(engine, kS1, QStringLiteral("app|a"));
    QCOMPARE(onTemplate.kind, WindowHeight::Preset);
    QCOMPARE(onTemplate.presetIdx, 1);
    const WindowHeight onSettings = openedHeight(engine, kS2, QStringLiteral("app|b"));
    QCOMPARE(onSettings.kind, WindowHeight::Preset);
    QCOMPARE(onSettings.presetIdx, 1);

    const QVector<QRect> templateRects = engine->visibleTileRects(kS1);
    QCOMPARE(templateRects.size(), 1);
    QCOMPARE(templateRects.first().height(), qRound(0.6 * kScreenHeight));
    const QVector<QRect> settingsRects = engine->visibleTileRects(kS2);
    QCOMPARE(settingsRects.size(), 1);
    QCOMPARE(settingsRects.first().height(), qRound(0.5 * kScreenHeight));
}

void TestScrollEnginePerScreen::templateListShrinkClampsResolvedPresetWidth()
{
    // A template swap that shrinks the list must reflow an out-of-range
    // stored Preset index to the last entry (presetAt's clamp), never crash
    // or zero out.
    QObject owner;
    auto* settings = new StubScrollSettings(&owner);
    settings->widthKind = static_cast<int>(DefaultWidthKind::Preset);
    settings->widthPresetIndex = 2; // resolves to 0.75 pre-template
    ScrollEngine* engine = makeEngine(&owner, settings);

    engine->windowOpened(QStringLiteral("app|a"), kS1, 0, 0);
    QVector<QRect> rects = engine->visibleTileRects(kS1);
    QCOMPARE(rects.size(), 1);
    QCOMPARE(rects.first().width(), qRound(0.75 * kScreenWidth));

    // Now the template arrives with a single entry: the stored index 2
    // clamps to the lone 0.6 preset at the next resolve.
    QVariantMap templ;
    templ.insert(ScrollPerScreenKeys::presetColumnWidths(), QVariantList{0.6});
    engine->applyPerScreenConfig(kS1, templ);
    rects = engine->visibleTileRects(kS1);
    QCOMPARE(rects.size(), 1);
    QCOMPARE(rects.first().width(), qRound(0.6 * kScreenWidth));

    // Clearing the override restores the settings vocabulary.
    engine->clearPerScreenConfig(kS1);
    rects = engine->visibleTileRects(kS1);
    QCOMPARE(rects.size(), 1);
    QCOMPARE(rects.first().width(), qRound(0.75 * kScreenWidth));
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
    QCOMPARE(rects.first().width(), qRound(0.5 * kScreenWidth));

    // A mixed list keeps its valid entries: 0.01 drops, 0.3 survives, and
    // index 1 clamps to the lone remaining entry.
    QVariantMap mixed;
    mixed.insert(ScrollPerScreenKeys::presetColumnWidths(), QVariantList{0.01, 0.3});
    engine->applyPerScreenConfig(kS1, mixed);
    rects = engine->visibleTileRects(kS1);
    QCOMPARE(rects.size(), 1);
    QCOMPARE(rects.first().width(), qRound(0.3 * kScreenWidth));
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

QTEST_GUILESS_MAIN(TestScrollEnginePerScreen)
#include "test_scrollengine_perscreen.moc"
