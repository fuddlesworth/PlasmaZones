// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

// Per-screen BEHAVIOUR overrides: the rule-written keys that layer over the
// config-seeded behaviour globals rather than over the width/height defaults
// (those are test_scrollengine_perscreen's subject).
//
// One case per key, and every one of them drives TWO screens off the same
// engine: the screen carrying the override and a screen carrying none. That
// pairing is the point. An assertion on the overridden screen alone passes
// just as happily when the engine reads the global everywhere, which is the
// exact regression these keys keep having — an effective* call site quietly
// reverting to the member read, with the suite still green because nothing
// ever compared two screens under one global.
//
// Everything asserted here is an OBSERVABLE (a resolved rect, the strip's
// active column, the column order, whether the window took a column at all),
// never the resolver's own return value: a test that called the resolver
// would still pass if the engine stopped consulting it.

#include <PhosphorScrollEngine/ScrollEngine.h>
#include <PhosphorScrollEngine/ScrollState.h>
#include <PhosphorScrollEngine/ScrollStrip.h>
#include <PhosphorScrollEngine/ScrollTypes.h>

#include "scrollstriptestutils.h"
#include "scrollstubsettings.h"
#include "scrollstubtracking.h"

#include <QVariantMap>
#include <QtTest>

using namespace PhosphorScrollEngine;

using ScrollTestUtils::kScreenHeight;
using ScrollTestUtils::kScreenWidth;
using ScrollTestUtils::makeProviderEngine;
using ScrollTestUtils::StubScrollSettings;
using ScrollTestUtils::StubWindowTracking;

namespace {

const QString kS1 = QStringLiteral("S1");
const QString kS2 = QStringLiteral("S2");

/// The one override key @p key set to @p value on @p screenId, and nothing
/// else — the shape a single rule slot arrives in.
QVariantMap onlyKey(const QString& key, const QVariant& value)
{
    QVariantMap map;
    map.insert(key, value);
    return map;
}

} // namespace

class TestScrollEngineBehaviour : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void focusNewWindowsOverrideIsPerScreen();
    void alwaysCenterSingleColumnOverrideIsPerScreen();
    void centerFocusedColumnOverrideIsPerScreen();
    void respectMinimumSizeOverrideIsPerScreen();
    void smartGapsOverrideIsPerScreen();
    void insertPositionOverrideIsPerScreen();
    void stickyWindowHandlingOverrideIsPerScreen();
    void widthClientDecidesOverrideIsPerScreen();
    void outOfRangeWidthRuleDoesNotSuppressClientDecides();
    void wrongTypedBehaviourOverridesAreRejected();

private:
    /// An engine on S1 and S2 with @p settings installed and its cached
    /// globals refreshed. @p tracker is wired only where the path under test
    /// needs one.
    static ScrollEngine* makeEngine(QObject* parent, StubScrollSettings* settings,
                                    StubWindowTracking* tracker = nullptr)
    {
        ScrollEngine* engine = makeProviderEngine(parent, {kS1, kS2}, {}, {}, tracker);
        engine->setEngineSettings(settings);
        engine->refreshConfigFromSettings();
        return engine;
    }

    /// The strip's active window on @p screenId, or empty when the screen has
    /// no state — which fails every comparison here rather than asserting.
    static QString activeWindowOn(ScrollEngine* engine, const QString& screenId)
    {
        auto* state = static_cast<ScrollState*>(engine->stateForScreen(screenId));
        return state ? state->strip().activeWindowId() : QString();
    }

    /// Strip order on @p screenId, empty on a screen with no state.
    static QStringList orderOn(ScrollEngine* engine, const QString& screenId)
    {
        auto* state = static_cast<ScrollState*>(engine->stateForScreen(screenId));
        return state ? state->strip().windowsInOrder() : QStringList();
    }

    static bool tiled(ScrollEngine* engine, const QString& screenId, const QString& windowId)
    {
        auto* state = static_cast<ScrollState*>(engine->stateForScreen(screenId));
        return state && state->strip().containsWindow(windowId);
    }
};

void TestScrollEngineBehaviour::focusNewWindowsOverrideIsPerScreen()
{
    // Global ON. S1 is scoped OFF by rule, so its strip must keep the prior
    // active column while S2's adopts the arrival.
    QObject owner;
    auto* settings = new StubScrollSettings(&owner);
    settings->focusNewWindows = true;
    ScrollEngine* engine = makeEngine(&owner, settings);
    engine->applyPerScreenConfig(kS1, onlyKey(ScrollPerScreenKeys::focusNewWindows(), false));

    engine->windowOpened(QStringLiteral("app|a1"), kS1, 0, 0);
    engine->windowOpened(QStringLiteral("app|a2"), kS1, 0, 0);
    QCOMPARE(activeWindowOn(engine, kS1), QStringLiteral("app|a1"));

    engine->windowOpened(QStringLiteral("app|b1"), kS2, 0, 0);
    engine->windowOpened(QStringLiteral("app|b2"), kS2, 0, 0);
    QCOMPARE(activeWindowOn(engine, kS2), QStringLiteral("app|b2"));
}

void TestScrollEngineBehaviour::alwaysCenterSingleColumnOverrideIsPerScreen()
{
    // A lone column narrower than the work area: centered on the overridden
    // screen, left-anchored on the other.
    QObject owner;
    auto* settings = new StubScrollSettings(&owner);
    settings->alwaysCenterSingleColumn = false;
    settings->widthKind = static_cast<int>(DefaultWidthKind::Proportion);
    settings->widthValue = 0.5;
    ScrollEngine* engine = makeEngine(&owner, settings);
    engine->applyPerScreenConfig(kS1, onlyKey(ScrollPerScreenKeys::alwaysCenterSingleColumn(), true));

    engine->windowOpened(QStringLiteral("app|a"), kS1, 0, 0);
    engine->windowOpened(QStringLiteral("app|b"), kS2, 0, 0);

    const QVector<QRect> centered = engine->visibleTileRects(kS1);
    QCOMPARE(centered.size(), 1);
    QCOMPARE(centered.first().width(), kScreenWidth / 2);
    QCOMPARE(centered.first().x(), kScreenWidth / 4);

    const QVector<QRect> anchored = engine->visibleTileRects(kS2);
    QCOMPARE(anchored.size(), 1);
    QCOMPARE(anchored.first().x(), 0);
}

void TestScrollEngineBehaviour::centerFocusedColumnOverrideIsPerScreen()
{
    // Global Never. S1 is scoped to Always, so focusing its first column
    // centers it while S2's stays pinned at the left edge. Two columns per
    // screen, so this is the focus rule and not the lone-column one above.
    QObject owner;
    auto* settings = new StubScrollSettings(&owner);
    settings->centerFocused = static_cast<int>(CenterFocusedColumn::Never);
    settings->widthKind = static_cast<int>(DefaultWidthKind::Proportion);
    settings->widthValue = 0.5;
    ScrollEngine* engine = makeEngine(&owner, settings);
    engine->applyPerScreenConfig(
        kS1, onlyKey(ScrollPerScreenKeys::centerFocusedColumn(), static_cast<int>(CenterFocusedColumn::Always)));

    for (const QString& id : {QStringLiteral("app|a1"), QStringLiteral("app|a2")}) {
        engine->windowOpened(id, kS1, 0, 0);
    }
    for (const QString& id : {QStringLiteral("app|b1"), QStringLiteral("app|b2")}) {
        engine->windowOpened(id, kS2, 0, 0);
    }
    engine->windowFocused(QStringLiteral("app|a1"), kS1);
    engine->windowFocused(QStringLiteral("app|b1"), kS2);

    // Centered: a 600px column on a 1200px work area sits at x=300. The
    // found flag keeps each assertion honest — a tile missing from the walk
    // would otherwise satisfy the loop vacuously.
    bool sawCentered = false;
    for (const ScrollEngine::VisibleTile& tile : engine->visibleTiles(kS1)) {
        if (tile.windowId == QStringLiteral("app|a1")) {
            sawCentered = true;
            QCOMPARE(tile.rect.x(), kScreenWidth / 4);
        }
    }
    QVERIFY(sawCentered);

    bool sawAnchored = false;
    for (const ScrollEngine::VisibleTile& tile : engine->visibleTiles(kS2)) {
        if (tile.windowId == QStringLiteral("app|b1")) {
            sawAnchored = true;
            QCOMPARE(tile.rect.x(), 0);
        }
    }
    QVERIFY(sawAnchored);
}

void TestScrollEngineBehaviour::respectMinimumSizeOverrideIsPerScreen()
{
    // A window whose declared minimum is wider than the default column: the
    // overridden screen widens the column to the minimum, the other commits
    // the default width and lets the client fight it.
    QObject owner;
    auto* settings = new StubScrollSettings(&owner);
    settings->respectMinimumSize = false;
    settings->widthKind = static_cast<int>(DefaultWidthKind::Proportion);
    settings->widthValue = 0.25; // 300px
    ScrollEngine* engine = makeEngine(&owner, settings);
    engine->applyPerScreenConfig(kS1, onlyKey(ScrollPerScreenKeys::respectMinimumSize(), true));

    engine->windowOpened(QStringLiteral("app|a"), kS1, 800, 0);
    engine->windowOpened(QStringLiteral("app|b"), kS2, 800, 0);

    const QVector<QRect> clamped = engine->visibleTileRects(kS1);
    QCOMPARE(clamped.size(), 1);
    QCOMPARE(clamped.first().width(), 800);

    const QVector<QRect> unclamped = engine->visibleTileRects(kS2);
    QCOMPARE(unclamped.size(), 1);
    QCOMPARE(unclamped.first().width(), kScreenWidth / 4);
}

void TestScrollEngineBehaviour::smartGapsOverrideIsPerScreen()
{
    // Smart gaps zero the OUTER gaps for a single-column strip. With a
    // full-width column the effect is the whole outer gap on each side, so
    // the overridden screen fills the output and the other keeps its inset.
    QObject owner;
    auto* settings = new StubScrollSettings(&owner);
    settings->smartGaps = false;
    settings->outerGap = 20;
    settings->widthKind = static_cast<int>(DefaultWidthKind::Proportion);
    settings->widthValue = 1.0;
    ScrollEngine* engine = makeEngine(&owner, settings);
    engine->applyPerScreenConfig(kS1, onlyKey(ScrollPerScreenKeys::smartGaps(), true));

    engine->windowOpened(QStringLiteral("app|a"), kS1, 0, 0);
    engine->windowOpened(QStringLiteral("app|b"), kS2, 0, 0);

    const QVector<QRect> ungapped = engine->visibleTileRects(kS1);
    QCOMPARE(ungapped.size(), 1);
    QCOMPARE(ungapped.first().x(), 0);
    QCOMPARE(ungapped.first().width(), kScreenWidth);

    const QVector<QRect> gapped = engine->visibleTileRects(kS2);
    QCOMPARE(gapped.size(), 1);
    QCOMPARE(gapped.first().x(), 20);
    QCOMPARE(gapped.first().width(), kScreenWidth - 40);
}

void TestScrollEngineBehaviour::insertPositionOverrideIsPerScreen()
{
    // Global right-of-active. S1 is scoped to First, so its second window
    // opens LEFTMOST while S2's opens to the right of the active column.
    QObject owner;
    auto* settings = new StubScrollSettings(&owner);
    settings->insertPosition = static_cast<int>(ScrollInsertPosition::RightOfActive);
    ScrollEngine* engine = makeEngine(&owner, settings);
    engine->applyPerScreenConfig(
        kS1, onlyKey(ScrollPerScreenKeys::insertPosition(), static_cast<int>(ScrollInsertPosition::First)));

    engine->windowOpened(QStringLiteral("app|a1"), kS1, 0, 0);
    engine->windowOpened(QStringLiteral("app|a2"), kS1, 0, 0);
    QCOMPARE(orderOn(engine, kS1), QStringList({QStringLiteral("app|a2"), QStringLiteral("app|a1")}));

    engine->windowOpened(QStringLiteral("app|b1"), kS2, 0, 0);
    engine->windowOpened(QStringLiteral("app|b2"), kS2, 0, 0);
    QCOMPARE(orderOn(engine, kS2), QStringList({QStringLiteral("app|b1"), QStringLiteral("app|b2")}));
}

void TestScrollEngineBehaviour::stickyWindowHandlingOverrideIsPerScreen()
{
    // Global TreatAsNormal, so a sticky window takes a column everywhere.
    // S1 is scoped to IgnoreAll: its sticky arrival is floated out of the
    // strip instead, while the same window class on S2 still tiles.
    QObject owner;
    auto* settings = new StubScrollSettings(&owner);
    settings->stickyHandling = static_cast<int>(PhosphorEngine::StickyWindowHandling::TreatAsNormal);
    auto* tracker = new StubWindowTracking(&owner);
    tracker->stickyWindows = {QStringLiteral("app|a"), QStringLiteral("app|b")};
    ScrollEngine* engine = makeEngine(&owner, settings, tracker);
    engine->applyPerScreenConfig(kS1,
                                 onlyKey(ScrollPerScreenKeys::stickyWindowHandling(),
                                         static_cast<int>(PhosphorEngine::StickyWindowHandling::IgnoreAll)));

    engine->windowOpened(QStringLiteral("app|a"), kS1, 0, 0);
    engine->windowOpened(QStringLiteral("app|b"), kS2, 0, 0);

    QVERIFY(!tiled(engine, kS1, QStringLiteral("app|a")));
    QVERIFY(engine->isWindowFloatingInScroll(QStringLiteral("app|a")));
    QVERIFY(tiled(engine, kS2, QStringLiteral("app|b")));
    QVERIFY(!engine->isWindowFloatingInScroll(QStringLiteral("app|b")));
}

void TestScrollEngineBehaviour::widthClientDecidesOverrideIsPerScreen()
{
    // The kind trio's ClientDecides value is a WIDTH verdict resolved per
    // screen: S1 opens at the client's own reported width, S2 at the
    // configured proportion. The gate reads the kind's VALUE — a version
    // that tested the key's presence gated the client-sized branch OFF on
    // exactly the screen scoped to it.
    QObject owner;
    auto* settings = new StubScrollSettings(&owner);
    settings->widthKind = static_cast<int>(DefaultWidthKind::Proportion);
    settings->widthValue = 0.25;
    auto* tracker = new StubWindowTracking(&owner);
    tracker->unmanagedGeometry = QRect(0, 0, 640, 400);
    ScrollEngine* engine = makeEngine(&owner, settings, tracker);
    engine->applyPerScreenConfig(
        kS1, onlyKey(ScrollPerScreenKeys::defaultColumnWidthKind(), static_cast<int>(DefaultWidthKind::ClientDecides)));

    engine->windowOpened(QStringLiteral("app|a"), kS1, 0, 0);
    engine->windowOpened(QStringLiteral("app|b"), kS2, 0, 0);

    const QVector<QRect> client = engine->visibleTileRects(kS1);
    QCOMPARE(client.size(), 1);
    QCOMPARE(client.first().width(), 640);

    const QVector<QRect> configured = engine->visibleTileRects(kS2);
    QCOMPARE(configured.size(), 1);
    QCOMPARE(configured.first().width(), kScreenWidth / 4);
}

void TestScrollEngineBehaviour::outOfRangeWidthRuleDoesNotSuppressClientDecides()
{
    // A rule fraction the width resolver REJECTS contributes no width, so it
    // must not suppress the client-sized open either: the two are one
    // decision. S1 carries ClientDecides plus a 0.001 rule fraction (below
    // the floor); S2 carries ClientDecides plus a legal 0.75 rule fraction,
    // which does pin a width and correctly wins over the client's size.
    QObject owner;
    auto* settings = new StubScrollSettings(&owner);
    auto* tracker = new StubWindowTracking(&owner);
    tracker->unmanagedGeometry = QRect(0, 0, 640, 400);
    ScrollEngine* engine = makeEngine(&owner, settings, tracker);

    QVariantMap rejected;
    rejected.insert(ScrollPerScreenKeys::defaultColumnWidthKind(), static_cast<int>(DefaultWidthKind::ClientDecides));
    rejected.insert(ScrollPerScreenKeys::defaultColumnWidth(), 0.001);
    engine->applyPerScreenConfig(kS1, rejected);

    QVariantMap accepted;
    accepted.insert(ScrollPerScreenKeys::defaultColumnWidthKind(), static_cast<int>(DefaultWidthKind::ClientDecides));
    accepted.insert(ScrollPerScreenKeys::defaultColumnWidth(), 0.75);
    engine->applyPerScreenConfig(kS2, accepted);

    engine->windowOpened(QStringLiteral("app|a"), kS1, 0, 0);
    engine->windowOpened(QStringLiteral("app|b"), kS2, 0, 0);

    const QVector<QRect> clientSized = engine->visibleTileRects(kS1);
    QCOMPARE(clientSized.size(), 1);
    QCOMPARE(clientSized.first().width(), 640);

    const QVector<QRect> rulePinned = engine->visibleTileRects(kS2);
    QCOMPARE(rulePinned.size(), 1);
    QCOMPARE(rulePinned.first().width(), qRound(0.75 * kScreenWidth));
}

void TestScrollEngineBehaviour::wrongTypedBehaviourOverridesAreRejected()
{
    // A value of the wrong TYPE is rejected, not coerced. This is the whole
    // reason the readers type-check: a string reads as false through
    // QVariant::toBool and as 0 through toInt, and 0 is a legal value of
    // every enum these keys carry — so an unchecked read would silently
    // replace the user's configured behaviour with the first enumerator.
    QObject owner;
    auto* settings = new StubScrollSettings(&owner);
    settings->focusNewWindows = true;
    settings->centerFocused = static_cast<int>(CenterFocusedColumn::Always);
    // First, NOT Last: the coerced value of a rejected string is 0 =
    // RightOfActive, and with two columns RightOfActive and Last both produce
    // [a1, a2] — so a Last baseline would pass whether or not the type check
    // exists. First produces [a2, a1], which the coerced value cannot.
    settings->insertPosition = static_cast<int>(ScrollInsertPosition::First);
    settings->tabIndicatorEnabled = true;
    settings->tabIndicatorPosition = static_cast<int>(TabIndicatorPosition::Bottom);
    settings->widthKind = static_cast<int>(DefaultWidthKind::Proportion);
    settings->widthValue = 0.5;
    ScrollEngine* engine = makeEngine(&owner, settings);

    QVariantMap garbage;
    garbage.insert(ScrollPerScreenKeys::focusNewWindows(), QStringLiteral("false"));
    garbage.insert(ScrollPerScreenKeys::centerFocusedColumn(), QStringLiteral("never"));
    garbage.insert(ScrollPerScreenKeys::insertPosition(), QStringLiteral("first"));
    // "false", not "off": QVariant::toBool maps only empty / "0" / "false" to
    // false, so "off" would coerce to TRUE and match the configured value,
    // making the leg pass with or without the type guard.
    garbage.insert(ScrollPerScreenKeys::tabIndicatorEnabled(), QStringLiteral("false"));
    garbage.insert(ScrollPerScreenKeys::tabIndicatorPosition(), QStringLiteral("left"));
    engine->applyPerScreenConfig(kS1, garbage);

    // The indicator keeps every configured value.
    const TabIndicatorParams indicator = engine->tabIndicatorParamsForScreen(kS1);
    QCOMPARE(indicator.enabled, true);
    QCOMPARE(indicator.position, TabIndicatorPosition::Bottom);

    // Focus-new-windows stays ON: the arrival still takes the strip.
    engine->windowOpened(QStringLiteral("app|a1"), kS1, 0, 0);
    engine->windowOpened(QStringLiteral("app|a2"), kS1, 0, 0);
    QCOMPARE(activeWindowOn(engine, kS1), QStringLiteral("app|a2"));

    // Insert position stays First, so the arrival lands LEFT of the existing
    // column. A coerced garbage value would be RightOfActive and put it on the
    // right, so this ordering is what discriminates. Centering stays Always,
    // so the focused column sits centered rather than at the left edge.
    QCOMPARE(orderOn(engine, kS1), QStringList({QStringLiteral("app|a2"), QStringLiteral("app|a1")}));
    bool sawActive = false;
    for (const ScrollEngine::VisibleTile& tile : engine->visibleTiles(kS1)) {
        if (tile.windowId == QStringLiteral("app|a2")) {
            sawActive = true;
            QCOMPARE(tile.rect.x(), kScreenWidth / 4);
        }
    }
    QVERIFY(sawActive);
}

QTEST_GUILESS_MAIN(TestScrollEngineBehaviour)
#include "test_scrollengine_behaviour.moc"
