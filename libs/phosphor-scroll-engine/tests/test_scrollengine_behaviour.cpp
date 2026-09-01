// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

// Per-screen BEHAVIOUR overrides: the rule-written keys that layer over the
// config-seeded behaviour globals rather than over the width/height defaults
// (those are test_scrollengine_perscreen's subject).
//
// The two ClientDecides kinds are the deliberate exception and live here, not
// there. What they need is the window TRACKER (a client-decided size is read
// off it), and this is the only suite wired with one — see the stub's own
// note. Their sibling's concern is the kind trio's per-screen resolution;
// theirs is a verdict that changes where the size comes from at all.
//
// One case per key, and each *OverrideIsPerScreen case drives TWO screens
// off the same engine: the screen carrying the override and a screen
// carrying none. That pairing is the point. (Two rejection cases — the
// out-of-range width rule and the wrong-typed overrides — are single-screen
// by nature: they assert a value is REFUSED, so there is no override side
// for a pairing to discriminate. The retile case between them is a third
// shape: both screens carry an override and the discriminator is a rule
// width against none.) An assertion on the overridden screen alone passes
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

namespace Ax = ScrollTestUtils::Ax;

using ScrollTestUtils::kMainExtent;
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
    /// Proves the vertical arm really is transposed, so a lost ENVIRONMENT
    /// property cannot leave it silently re-running the horizontal suite.
    void initTestCase()
    {
        AX_GUARD_SUITE();
    }

    void focusNewWindowsOverrideIsPerScreen();
    void alwaysCenterSingleColumnOverrideIsPerScreen();
    void centerShortColumnsOverrideIsPerScreen();
    void centerFocusedColumnOverrideIsPerScreen();
    void respectMinimumSizeOverrideIsPerScreen();
    void smartGapsOverrideIsPerScreen();
    void insertPositionOverrideIsPerScreen();
    void stickyWindowHandlingOverrideIsPerScreen();
    void widthClientDecidesOverrideIsPerScreen();
    void outOfRangeWidthRuleDoesNotSuppressClientDecides();
    void heightClientDecidesOverrideIsPerScreen();
    void outOfRangeHeightRuleDoesNotSuppressClientDecides();
    void retileKeepsClientDecidedHeightsUnlessARulePinsOne();
    void clientDecidedHeightYieldsToARuleAndToAJoin();
    void clientDecidedHeightBoundsAndDeclinesUnusableClientSizes();
    void clientDecidedHeightSurvivesAMigrationAndReachesAnUnfloat();
    void retileKeepsClientDecidedWidthsUnlessARulePinsOne();
    void wrongTypedBehaviourOverridesAreRejected();
    void focusScrollLimitNamesTheWindowsPastTheCap();
    void focusScrollLimitNamesEveryTileOfABlockedColumn();
    void focusScrollLimitFailsOpen();

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
    // A lone column narrower than the work area's main extent: centered on
    // the overridden screen, LEAD-anchored on the other.
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
    QCOMPARE(Ax::mainLen(centered.first()), kMainExtent / 2);
    QCOMPARE(Ax::mainPos(centered.first()), kMainExtent / 4);

    const QVector<QRect> anchored = engine->visibleTileRects(kS2);
    QCOMPARE(anchored.size(), 1);
    QCOMPARE(Ax::mainPos(anchored.first()), 0);
}

void TestScrollEngineBehaviour::centerShortColumnsOverrideIsPerScreen()
{
    // The CROSS-axis twin of the lone-column slot above, and the rule seam's
    // only engine-visible effect. Both screens open a window at a fixed
    // height shorter than the work area; only the overridden one centres it
    // across the strip, and neither column changes extent.
    QObject owner;
    auto* settings = new StubScrollSettings(&owner);
    settings->centerShortColumns = false;
    settings->heightKind = static_cast<int>(DefaultHeightKind::Fixed);
    settings->heightValue = 250.0;
    ScrollEngine* engine = makeEngine(&owner, settings);
    engine->applyPerScreenConfig(kS1, onlyKey(ScrollPerScreenKeys::centerShortColumns(), true));

    engine->windowOpened(QStringLiteral("app|a"), kS1, 0, 0);
    engine->windowOpened(QStringLiteral("app|b"), kS2, 0, 0);

    const QVector<QRect> centered = engine->visibleTileRects(kS1);
    QCOMPARE(centered.size(), 1);
    QCOMPARE(Ax::crossLen(centered.first()), 250);
    QCOMPARE(Ax::crossPos(centered.first()), (ScrollTestUtils::kCrossExtent - 250) / 2);

    const QVector<QRect> anchored = engine->visibleTileRects(kS2);
    QCOMPARE(anchored.size(), 1);
    QCOMPARE(Ax::crossLen(anchored.first()), 250);
    QCOMPARE(Ax::crossPos(anchored.first()), 0);
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

    // Centered: a 600px column on a 1200px main extent sits at main 300. The
    // found flag keeps each assertion honest — a tile missing from the walk
    // would otherwise satisfy the loop vacuously.
    bool sawCentered = false;
    for (const ScrollEngine::VisibleTile& tile : engine->visibleTiles(kS1)) {
        if (tile.windowId == QStringLiteral("app|a1")) {
            sawCentered = true;
            QCOMPARE(Ax::mainPos(tile.rect), kMainExtent / 4);
        }
    }
    QVERIFY(sawCentered);

    bool sawAnchored = false;
    for (const ScrollEngine::VisibleTile& tile : engine->visibleTiles(kS2)) {
        if (tile.windowId == QStringLiteral("app|b1")) {
            sawAnchored = true;
            QCOMPARE(Ax::mainPos(tile.rect), 0);
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

    // A minimum ALONG the strip, so it transposes with the fixture: what
    // widens a column on a horizontal strip lengthens it on a vertical one.
    const QSize minMain = Ax::t(QSize(800, 0));
    engine->windowOpened(QStringLiteral("app|a"), kS1, minMain.width(), minMain.height());
    engine->windowOpened(QStringLiteral("app|b"), kS2, minMain.width(), minMain.height());

    const QVector<QRect> clamped = engine->visibleTileRects(kS1);
    QCOMPARE(clamped.size(), 1);
    QCOMPARE(Ax::mainLen(clamped.first()), 800);

    const QVector<QRect> unclamped = engine->visibleTileRects(kS2);
    QCOMPARE(unclamped.size(), 1);
    QCOMPARE(Ax::mainLen(unclamped.first()), kMainExtent / 4);
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
    QCOMPARE(Ax::mainPos(ungapped.first()), 0);
    QCOMPARE(Ax::mainLen(ungapped.first()), kMainExtent);

    const QVector<QRect> gapped = engine->visibleTileRects(kS2);
    QCOMPARE(gapped.size(), 1);
    QCOMPARE(Ax::mainPos(gapped.first()), 20);
    QCOMPARE(Ax::mainLen(gapped.first()), kMainExtent - 40);
}

void TestScrollEngineBehaviour::insertPositionOverrideIsPerScreen()
{
    // Global right-of-active. S1 is scoped to First, so its second window
    // opens LEADMOST while S2's opens trailward of the active column.
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
    // Transposed with the arm: the client's MAIN extent must be 640 on both
    // axes, so the 640 the assertions expect is the column's length along the
    // strip rather than a number that happens to match a physical width.
    tracker->unmanagedGeometry = Ax::t(QRect(0, 0, 640, 400));
    ScrollEngine* engine = makeEngine(&owner, settings, tracker);
    engine->applyPerScreenConfig(
        kS1, onlyKey(ScrollPerScreenKeys::defaultColumnWidthKind(), static_cast<int>(DefaultWidthKind::ClientDecides)));

    engine->windowOpened(QStringLiteral("app|a"), kS1, 0, 0);
    engine->windowOpened(QStringLiteral("app|b"), kS2, 0, 0);

    const QVector<QRect> client = engine->visibleTileRects(kS1);
    QCOMPARE(client.size(), 1);
    QCOMPARE(Ax::mainLen(client.first()), 640);

    const QVector<QRect> configured = engine->visibleTileRects(kS2);
    QCOMPARE(configured.size(), 1);
    QCOMPARE(Ax::mainLen(configured.first()), kMainExtent / 4);
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
    // Transposed with the arm: the client's MAIN extent must be 640 on both
    // axes, so the 640 the assertions expect is the column's length along the
    // strip rather than a number that happens to match a physical width.
    tracker->unmanagedGeometry = Ax::t(QRect(0, 0, 640, 400));
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
    QCOMPARE(Ax::mainLen(clientSized.first()), 640);

    const QVector<QRect> rulePinned = engine->visibleTileRects(kS2);
    QCOMPARE(rulePinned.size(), 1);
    QCOMPARE(Ax::mainLen(rulePinned.first()), qRound(0.75 * kMainExtent));
}

void TestScrollEngineBehaviour::heightClientDecidesOverrideIsPerScreen()
{
    // The height twin of widthClientDecidesOverrideIsPerScreen: S1 opens
    // windows at the client's own CROSS extent, S2 at the configured fixed
    // height. Cross, not physical height — the transposed rect makes the 400
    // the assertion expects the tile's extent ACROSS the strip on both axes,
    // so an arm that read the client's main extent fails on one of them.
    QObject owner;
    auto* settings = new StubScrollSettings(&owner);
    settings->heightKind = static_cast<int>(DefaultHeightKind::Fixed);
    settings->heightValue = 250.0;
    auto* tracker = new StubWindowTracking(&owner);
    tracker->unmanagedGeometry = Ax::t(QRect(0, 0, 640, 400));
    ScrollEngine* engine = makeEngine(&owner, settings, tracker);
    engine->applyPerScreenConfig(
        kS1,
        onlyKey(ScrollPerScreenKeys::defaultWindowHeightKind(), static_cast<int>(DefaultHeightKind::ClientDecides)));

    engine->windowOpened(QStringLiteral("app|a"), kS1, 0, 0);
    engine->windowOpened(QStringLiteral("app|b"), kS2, 0, 0);

    const QVector<QRect> client = engine->visibleTileRects(kS1);
    QCOMPARE(client.size(), 1);
    QCOMPARE(Ax::crossLen(client.first()), 400);

    const QVector<QRect> configured = engine->visibleTileRects(kS2);
    QCOMPARE(configured.size(), 1);
    QCOMPARE(Ax::crossLen(configured.first()), 250);
}

void TestScrollEngineBehaviour::outOfRangeHeightRuleDoesNotSuppressClientDecides()
{
    // The height twin of outOfRangeWidthRuleDoesNotSuppressClientDecides: a
    // rule fraction the height resolver REJECTS contributes no height, so it
    // must not suppress the client-sized open either. S1 carries
    // ClientDecides plus an out-of-range 1.5 fraction; S2 carries
    // ClientDecides plus a legal 0.25, which does pin a height and wins.
    QObject owner;
    auto* settings = new StubScrollSettings(&owner);
    settings->heightKind = static_cast<int>(DefaultHeightKind::ClientDecides);
    auto* tracker = new StubWindowTracking(&owner);
    tracker->unmanagedGeometry = Ax::t(QRect(0, 0, 640, 400));
    ScrollEngine* engine = makeEngine(&owner, settings, tracker);

    // Kind AND value per screen, the shape the width twin uses: with the kind
    // only on the global, a regression that let a co-present per-screen VALUE
    // beat the per-screen KIND read would go unseen here.
    QVariantMap s1;
    s1.insert(ScrollPerScreenKeys::defaultWindowHeightKind(), static_cast<int>(DefaultHeightKind::ClientDecides));
    s1.insert(ScrollPerScreenKeys::defaultWindowHeight(), 1.5);
    QVariantMap s2;
    s2.insert(ScrollPerScreenKeys::defaultWindowHeightKind(), static_cast<int>(DefaultHeightKind::ClientDecides));
    s2.insert(ScrollPerScreenKeys::defaultWindowHeight(), 0.25);
    engine->applyPerScreenConfig(kS1, s1);
    engine->applyPerScreenConfig(kS2, s2);

    engine->windowOpened(QStringLiteral("app|a"), kS1, 0, 0);
    engine->windowOpened(QStringLiteral("app|b"), kS2, 0, 0);

    const QVector<QRect> clientSized = engine->visibleTileRects(kS1);
    QCOMPARE(clientSized.size(), 1);
    QCOMPARE(Ax::crossLen(clientSized.first()), 400);

    const QVector<QRect> rulePinned = engine->visibleTileRects(kS2);
    QCOMPARE(rulePinned.size(), 1);
    QCOMPARE(Ax::crossLen(rulePinned.first()), qRound(0.25 * ScrollTestUtils::kCrossExtent));

    // A fraction BELOW MinWindowHeightFraction is rejected on the same terms
    // as one above 1.0, which is what makes the floor worth having: it would
    // otherwise both suppress the client-sized open and pin a sliver of a
    // tile, where the identical width value is refused outright.
    QVariantMap tiny;
    tiny.insert(ScrollPerScreenKeys::defaultWindowHeightKind(), static_cast<int>(DefaultHeightKind::ClientDecides));
    tiny.insert(ScrollPerScreenKeys::defaultWindowHeight(), 0.01);
    engine->applyPerScreenConfig(kS1, tiny);
    engine->windowClosed(QStringLiteral("app|a"));
    engine->windowOpened(QStringLiteral("app|tiny"), kS1, 0, 0);
    QCOMPARE(Ax::crossLen(engine->visibleTileRects(kS1).first()), 400);
}

void TestScrollEngineBehaviour::retileKeepsClientDecidedHeightsUnlessARulePinsOne()
{
    // The height twin of retileKeepsClientDecidedWidthsUnlessARulePinsOne: on
    // a ClientDecides HEIGHT screen there is no default height to go back to,
    // so retile leaves the tile at the height it has instead of forcing the
    // even split. A rule fraction outranks the kind, so S2 does reset — to
    // the EVEN SPLIT, which is what retile has always reset heights to and
    // what its copy promises; the rule's fraction is an OPEN-time default,
    // not a reset target (see resetDefaultWindowHeightFor).
    QObject owner;
    auto* settings = new StubScrollSettings(&owner);
    settings->heightKind = static_cast<int>(DefaultHeightKind::ClientDecides);
    auto* tracker = new StubWindowTracking(&owner);
    tracker->unmanagedGeometry = Ax::t(QRect(0, 0, 640, 400));
    ScrollEngine* engine = makeEngine(&owner, settings, tracker);
    engine->applyPerScreenConfig(kS2, onlyKey(ScrollPerScreenKeys::defaultWindowHeight(), 0.25));

    engine->windowOpened(QStringLiteral("app|a"), kS1, 0, 0);
    engine->windowOpened(QStringLiteral("app|b"), kS2, 0, 0);
    // Push both off their opening height so the reset has something to do.
    engine->windowFocused(QStringLiteral("app|a"), kS1);
    engine->setWindowHeight(WindowHeight::makeFixed(300), kS1);
    engine->windowFocused(QStringLiteral("app|b"), kS2);
    engine->setWindowHeight(WindowHeight::makeFixed(300), kS2);
    QCOMPARE(Ax::crossLen(engine->visibleTileRects(kS1).first()), 300);
    QCOMPARE(Ax::crossLen(engine->visibleTileRects(kS2).first()), 300);

    QSignalSpy feedback(engine, &PhosphorEngine::PlacementEngineBase::navigationFeedback);

    // ClientDecides with no rule: the height the user set stays, and with
    // nothing else off its default the verb reports no_target rather than a
    // success that changed nothing — the width twin pins the same pair, and
    // without it a resetToDefaults that returned true off the untouched-height
    // path would pass every geometry assertion here.
    engine->resetStripToDefaults(kS1);
    QCOMPARE(Ax::crossLen(engine->visibleTileRects(kS1).first()), 300);
    QCOMPARE(feedback.last().at(1).toString(), QStringLiteral("retile"));
    QCOMPARE(feedback.last().at(0).toBool(), false);
    QCOMPARE(feedback.last().at(2).toString(), QStringLiteral("no_target"));

    // ClientDecides with a rule height: there IS a default to go back to, and
    // it is the even split — a lone tile then fills its column's cross extent.
    engine->resetStripToDefaults(kS2);
    QCOMPARE(Ax::crossLen(engine->visibleTileRects(kS2).first()), ScrollTestUtils::kCrossExtent);
    QCOMPARE(feedback.last().at(0).toBool(), true);
}

void TestScrollEngineBehaviour::clientDecidedHeightYieldsToARuleAndToAJoin()
{
    // The gate's remaining terms, none of which the geometry cases above
    // reach. A per-window openWindowHeight rule outranks the kind, and a
    // window that JOINS an existing column takes the column's share rather
    // than its own client height — the width twin never reaches a join
    // either (insertWindowIntoActiveColumn honours a width only on its
    // empty-strip fallback), and committing there would squeeze the sibling
    // it landed beside.
    QObject owner;
    auto* settings = new StubScrollSettings(&owner);
    settings->heightKind = static_cast<int>(DefaultHeightKind::ClientDecides);
    settings->focusNewWindows = true;
    auto* tracker = new StubWindowTracking(&owner);
    tracker->unmanagedGeometry = Ax::t(QRect(0, 0, 640, 400));
    ScrollEngine* engine = makeEngine(&owner, settings, tracker);

    // A per-window rule fraction wins over the client's own size.
    engine->setOpenParamsResolver([](const QString& windowId, const QString&) {
        ScrollOpenParams params;
        if (windowId == QStringLiteral("app|ruled")) {
            params.heightFraction = 0.25;
        }
        return params;
    });
    engine->windowOpened(QStringLiteral("app|ruled"), kS1, 0, 0);
    QCOMPARE(Ax::crossLen(engine->visibleTileRects(kS1).first()), qRound(0.25 * ScrollTestUtils::kCrossExtent));

    // A joining window: the client-decided commit must not fire, or the
    // Fixed intent would renormalize its sibling down to fit.
    engine->setOpenParamsResolver({});
    engine->windowOpened(QStringLiteral("app|host"), kS2, 0, 0);
    settings->insertPosition = static_cast<int>(ScrollInsertPosition::IntoActiveColumn);
    engine->refreshConfigFromSettings();
    engine->windowOpened(QStringLiteral("app|joiner"), kS2, 0, 0);

    auto* state = static_cast<ScrollState*>(engine->stateForScreen(kS2));
    QVERIFY(state);
    QCOMPARE(state->strip().columnCount(), 1);
    // The host opened a fresh column, so the commit DID pin it at its own
    // cross extent. The joiner is the one that must carry no intent: a Fixed
    // intent there claims tabbed ownership or renormalizes the host down.
    QCOMPARE(state->strip().windowHeightIntent(QStringLiteral("app|host")), WindowHeight::makeFixed(400));
    QCOMPARE(state->strip().windowHeightIntent(QStringLiteral("app|joiner")).kind, WindowHeight::Auto);
    QCOMPARE(engine->visibleTileRects(kS2).size(), 2);

    // The consume-rule arm reaches the same join through a different route
    // (an openColumnPlacement rule rather than the insert-position config),
    // and it too must leave the arrival unpinned.
    settings->insertPosition = static_cast<int>(ScrollInsertPosition::RightOfActive);
    engine->refreshConfigFromSettings();
    engine->setOpenParamsResolver([](const QString& windowId, const QString&) {
        ScrollOpenParams params;
        if (windowId == QStringLiteral("app|consumed")) {
            params.consume = true;
        }
        return params;
    });
    engine->windowOpened(QStringLiteral("app|consumed"), kS2, 0, 0);
    QCOMPARE(state->strip().columnCount(), 1);
    QCOMPARE(state->strip().windowHeightIntent(QStringLiteral("app|consumed")).kind, WindowHeight::Auto);
}

void TestScrollEngineBehaviour::clientDecidedHeightBoundsAndDeclinesUnusableClientSizes()
{
    // The commit's bounds. An absurd client extent is clamped to the work
    // area's cross extent rather than persisted as an intent no column can
    // hold, and a degenerate one is DECLINED so the tile keeps the context
    // default instead of becoming a standing 1px sliver. Also pins the
    // per-window contract: a sizing intent reads the window's OWN record. The
    // resolver now has no appId fallback at all, so this is structural, but the
    // sizing arm must still consult it, or it can
    // be minted from a same-app sibling's remembered rect.
    QObject owner;
    auto* settings = new StubScrollSettings(&owner);
    settings->heightKind = static_cast<int>(DefaultHeightKind::ClientDecides);
    auto* tracker = new StubWindowTracking(&owner);
    tracker->unmanagedGeometry = Ax::t(QRect(0, 0, 640, 100000));
    ScrollEngine* engine = makeEngine(&owner, settings, tracker);

    engine->windowOpened(QStringLiteral("app|huge"), kS1, 0, 0);
    auto* s1 = static_cast<ScrollState*>(engine->stateForScreen(kS1));
    QVERIFY(s1);
    // The INTENT, not the rendered extent: relayout clamps a Fixed height to
    // the column at paint time, so a rendered-extent assertion reads
    // kCrossExtent whether the commit bounded the value or persisted 100000.
    // The whole point of the bound is that the STORED intent is sane.
    QCOMPARE(s1->strip().windowHeightIntent(QStringLiteral("app|huge")),
             WindowHeight::makeFixed(ScrollTestUtils::kCrossExtent));
    QVERIFY2(tracker->unmanagedGeometryCalls > 0, "a sizing intent must consult the placement record");

    // No record at all: the tile falls back to the context default, which
    // under ClientDecides is the even split, and nothing is pinned.
    tracker->unmanagedGeometry = QRect();
    engine->windowOpened(QStringLiteral("app|nogeo"), kS2, 0, 0);
    auto* sNo = static_cast<ScrollState*>(engine->stateForScreen(kS2));
    QVERIFY(sNo);
    QCOMPARE(sNo->strip().windowHeightIntent(QStringLiteral("app|nogeo")).kind, WindowHeight::Auto);
}

void TestScrollEngineBehaviour::clientDecidedHeightSurvivesAMigrationAndReachesAnUnfloat()
{
    // Two legs that re-run the open path, or skip it, and so get the client
    // size wrong in opposite directions.
    QObject owner;
    auto* settings = new StubScrollSettings(&owner);
    settings->heightKind = static_cast<int>(DefaultHeightKind::ClientDecides);
    settings->focusNewWindows = true;
    auto* tracker = new StubWindowTracking(&owner);
    tracker->unmanagedGeometry = Ax::t(QRect(0, 0, 640, 400));
    ScrollEngine* engine = makeEngine(&owner, settings, tracker);

    // MIGRATION: a window that moves screen re-enters through the open path,
    // so the client-decides commit would fire a second time and overwrite a
    // height the user had set. A migration is a move, not an open — the
    // height the window already carries outranks the open-time default.
    engine->windowOpened(QStringLiteral("app|mover"), kS1, 0, 0);
    QCOMPARE(Ax::crossLen(engine->visibleTileRects(kS1).first()), 400);
    engine->windowFocused(QStringLiteral("app|mover"), kS1);
    engine->setWindowHeight(WindowHeight::makeFixed(300), kS1);
    QCOMPARE(Ax::crossLen(engine->visibleTileRects(kS1).first()), 300);
    engine->windowOpened(QStringLiteral("app|mover"), kS2, 0, 0); // same id, new screen
    auto* s2 = static_cast<ScrollState*>(engine->stateForScreen(kS2));
    QVERIFY(s2);
    QVERIFY(s2->strip().containsWindow(QStringLiteral("app|mover")));
    QCOMPARE(s2->strip().windowHeightIntent(QStringLiteral("app|mover")), WindowHeight::makeFixed(300));

    // UNFLOAT of a window floated AT OPEN. It must be floated by the engine
    // at open time, not by the user afterwards: a window that was ever a tile
    // carries a real remembered height and comes back through the ordinary
    // restore, so it would pass this assertion with no client-decides arm at
    // all. A rule-floated open writes a SEEDED entry with no height, and the
    // fresh insert then seeds only the concrete Auto fallback — the open
    // path's commit never runs for it, because an unfloat is not an open.
    engine->setFloatPredicate([](const QString& windowId, const QString&) {
        return windowId == QStringLiteral("app|floater");
    });
    engine->windowOpened(QStringLiteral("app|floater"), kS1, 0, 0);
    auto* s1 = static_cast<ScrollState*>(engine->stateForScreen(kS1));
    QVERIFY(s1);
    QVERIFY(s1->isFloating(QStringLiteral("app|floater")));
    QVERIFY(!s1->strip().containsWindow(QStringLiteral("app|floater")));
    // The predicate is a rule that can stop matching; the unfloat is the user
    // re-tiling the window afterwards.
    engine->setFloatPredicate({});
    engine->windowFocused(QStringLiteral("app|floater"), kS1);
    engine->moveFocusedToTiling(kS1);
    QVERIFY(s1->strip().containsWindow(QStringLiteral("app|floater")));
    QCOMPARE(s1->strip().windowHeightIntent(QStringLiteral("app|floater")), WindowHeight::makeFixed(400));
}

void TestScrollEngineBehaviour::retileKeepsClientDecidedWidthsUnlessARulePinsOne()
{
    // Retile's scrolling arm resets every column to "the default width", and
    // on a ClientDecides screen there is no such width: the column opened at
    // the client's own size, and the reset must leave it there rather than
    // hand it the global proportion the user turned off. A rule fraction
    // outranks the kind (the open path's rule), so a ClientDecides screen
    // WITH a rule width does reset to the rule's width.
    QObject owner;
    auto* settings = new StubScrollSettings(&owner);
    settings->widthKind = static_cast<int>(DefaultWidthKind::Proportion);
    settings->widthValue = 0.25;
    auto* tracker = new StubWindowTracking(&owner);
    tracker->unmanagedGeometry = Ax::t(QRect(0, 0, 640, 400));
    ScrollEngine* engine = makeEngine(&owner, settings, tracker);
    engine->applyPerScreenConfig(
        kS1, onlyKey(ScrollPerScreenKeys::defaultColumnWidthKind(), static_cast<int>(DefaultWidthKind::ClientDecides)));
    QVariantMap pinned;
    pinned.insert(ScrollPerScreenKeys::defaultColumnWidthKind(), static_cast<int>(DefaultWidthKind::ClientDecides));
    pinned.insert(ScrollPerScreenKeys::defaultColumnWidth(), 0.75);
    engine->applyPerScreenConfig(kS2, pinned);

    engine->windowOpened(QStringLiteral("app|a"), kS1, 0, 0);
    engine->windowOpened(QStringLiteral("app|b"), kS2, 0, 0);
    // Push both off their opening width so the reset has something to do.
    engine->windowFocused(QStringLiteral("app|a"), kS1);
    engine->setColumnWidth(ColumnWidth::makeProportion(0.5), kS1);
    engine->windowFocused(QStringLiteral("app|b"), kS2);
    engine->setColumnWidth(ColumnWidth::makeProportion(0.5), kS2);
    QCOMPARE(engine->visibleTileRects(kS1).size(), 1);
    QCOMPARE(engine->visibleTileRects(kS2).size(), 1);
    QCOMPARE(Ax::mainLen(engine->visibleTileRects(kS1).first()), kMainExtent / 2);
    QCOMPARE(Ax::mainLen(engine->visibleTileRects(kS2).first()), kMainExtent / 2);

    QSignalSpy feedback(engine, &PhosphorEngine::PlacementEngineBase::navigationFeedback);

    // ClientDecides with no rule: the width the user set stays, and with
    // nothing else off its default the verb reports no_target rather than a
    // success that changed nothing.
    engine->resetStripToDefaults(kS1);
    QCOMPARE(engine->visibleTileRects(kS1).size(), 1);
    QCOMPARE(Ax::mainLen(engine->visibleTileRects(kS1).first()), kMainExtent / 2);
    QCOMPARE(feedback.last().at(1).toString(), QStringLiteral("retile"));
    QCOMPARE(feedback.last().at(0).toBool(), false);
    QCOMPARE(feedback.last().at(2).toString(), QStringLiteral("no_target"));

    // ClientDecides with a rule width: the rule's width is the default.
    engine->resetStripToDefaults(kS2);
    QCOMPARE(engine->visibleTileRects(kS2).size(), 1);
    QCOMPARE(Ax::mainLen(engine->visibleTileRects(kS2).first()), qRound(0.75 * kMainExtent));
    QCOMPARE(feedback.last().at(0).toBool(), true);
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

    // Insert position stays First, so the arrival lands on the LEAD side of
    // the existing column. A coerced garbage value would be RightOfActive and
    // put it trailward, so this ordering is what discriminates. Centering
    // stays Always, so the focused column sits centered rather than at the
    // lead edge.
    QCOMPARE(orderOn(engine, kS1), QStringList({QStringLiteral("app|a2"), QStringLiteral("app|a1")}));
    bool sawActive = false;
    for (const ScrollEngine::VisibleTile& tile : engine->visibleTiles(kS1)) {
        if (tile.windowId == QStringLiteral("app|a2")) {
            sawActive = true;
            QCOMPARE(Ax::mainPos(tile.rect), kMainExtent / 4);
        }
    }
    QVERIFY(sawActive);
}

void TestScrollEngineBehaviour::focusScrollLimitNamesTheWindowsPastTheCap()
{
    // The daemon publishes this list as membership and the compositor answers
    // a pointer with one set lookup, so what it names has to be windows. This
    // covers the sentinel, the clamp and the shape of the answer on
    // single-tile columns; the multi-tile expansion is the sibling below.
    QObject owner;
    auto* settings = new StubScrollSettings(&owner);
    settings->centerFocused = static_cast<int>(CenterFocusedColumn::Always);
    settings->widthKind = static_cast<int>(DefaultWidthKind::Proportion);
    settings->widthValue = 0.5;
    ScrollEngine* engine = makeEngine(&owner, settings);

    engine->windowOpened(QStringLiteral("app|a"), kS1, 0, 0);
    engine->windowOpened(QStringLiteral("app|b"), kS1, 0, 0);
    engine->windowOpened(QStringLiteral("app|c"), kS1, 0, 0);
    engine->windowOpened(QStringLiteral("app|d"), kS1, 0, 0);

    // The maximum is the no-cap sentinel and short-circuits before any walk.
    QVERIFY(engine->windowsBeyondFocusScrollLimit(kS1, 100).isEmpty());
    // Anything past it reads the same way rather than wrapping into a cap.
    QVERIFY(engine->windowsBeyondFocusScrollLimit(kS1, 250).isEmpty());

    // Zero means "follow the pointer only onto what needs no scrolling at
    // all", so every column that is not already the focused one is refused.
    const QStringList none = engine->windowsBeyondFocusScrollLimit(kS1, 0);
    QVERIFY(!none.isEmpty());
    QVERIFY(!none.contains(activeWindowOn(engine, kS1)));

    // A negative percent is the same question as zero, not a wider cap.
    QCOMPARE(engine->windowsBeyondFocusScrollLimit(kS1, -20), none);

    // Every id named is a real strip window, and no id is named twice.
    const QStringList order = orderOn(engine, kS1);
    for (const QString& windowId : none) {
        QVERIFY2(order.contains(windowId), qPrintable(windowId));
        QCOMPARE(none.count(windowId), 1);
    }

    // The cap is monotone: relaxing it can only ever refuse fewer windows.
    // The non-empty guard is load-bearing — a subset assertion alone is
    // satisfied by an empty list, so an arm that answered {} for every
    // positive percent would pass the loop while refusing nothing at all.
    // With four half-width columns and the focus centred, the far ones still
    // cost a scroll at 50%.
    const QStringList half = engine->windowsBeyondFocusScrollLimit(kS1, 50);
    QVERIFY(!half.isEmpty());
    for (const QString& windowId : half) {
        QVERIFY2(none.contains(windowId), qPrintable(windowId));
    }
}

void TestScrollEngineBehaviour::focusScrollLimitNamesEveryTileOfABlockedColumn()
{
    // The query asks its question per COLUMN, because the cost of focusing a
    // window is a property of the column it sits in. What that must not do is
    // answer with the column: the compositor matches the window under the
    // pointer, so a stacked or tabbed column past the cap has to contribute
    // EVERY one of its tiles. A per-column walk that forgot to expand would
    // pass every other assertion in the suite while silently letting the
    // pointer focus the second tile of a refused column.
    QObject owner;
    auto* settings = new StubScrollSettings(&owner);
    settings->centerFocused = static_cast<int>(CenterFocusedColumn::Always);
    settings->widthKind = static_cast<int>(DefaultWidthKind::Proportion);
    settings->widthValue = 0.5;
    ScrollEngine* engine = makeEngine(&owner, settings);

    // Column 0 is a lone tile, column 1 stacks two: the first two arrivals
    // take a column each under the default insert, and only then does the
    // third go INTO the column the second opened.
    settings->focusNewWindows = true;
    engine->refreshConfigFromSettings();
    engine->windowOpened(QStringLiteral("app|solo"), kS1, 0, 0);
    engine->windowOpened(QStringLiteral("app|stackA"), kS1, 0, 0);
    settings->insertPosition = static_cast<int>(ScrollInsertPosition::IntoActiveColumn);
    engine->refreshConfigFromSettings();
    engine->windowOpened(QStringLiteral("app|stackB"), kS1, 0, 0);

    auto* state = static_cast<ScrollState*>(engine->stateForScreen(kS1));
    QVERIFY(state);
    QCOMPARE(state->strip().columnCount(), 2);

    // Focus the lone column, so the stacked one is the one that costs a scroll.
    engine->focusColumnFirst(kS1);
    QCOMPARE(state->strip().activeColumnIndex(), 0);

    const QStringList blocked = engine->windowsBeyondFocusScrollLimit(kS1, 0);
    QVERIFY2(blocked.contains(QStringLiteral("app|stackA")), qPrintable(blocked.join(QLatin1Char(','))));
    QVERIFY2(blocked.contains(QStringLiteral("app|stackB")), qPrintable(blocked.join(QLatin1Char(','))));
    // The focused column is never refused, however tight the cap.
    QVERIFY(!blocked.contains(QStringLiteral("app|solo")));
}

void TestScrollEngineBehaviour::focusScrollLimitFailsOpen()
{
    // A question the engine cannot answer must come back as "refuse nothing".
    // The caller turns this list into a REFUSAL, so an unknown screen or an
    // empty strip answering anything else would silently break focus.
    QObject owner;
    auto* settings = new StubScrollSettings(&owner);
    ScrollEngine* engine = makeEngine(&owner, settings);

    QVERIFY(engine->windowsBeyondFocusScrollLimit(QStringLiteral("DP-nonexistent"), 0).isEmpty());
    QVERIFY(engine->windowsBeyondFocusScrollLimit(kS1, 0).isEmpty()); // no windows yet

    engine->windowOpened(QStringLiteral("app|a"), kS1, 0, 0);
    // A lone column is the one the pointer is already on, so nothing is past
    // the cap however tight it is.
    QVERIFY(engine->windowsBeyondFocusScrollLimit(kS1, 0).isEmpty());
}

QTEST_GUILESS_MAIN(TestScrollEngineBehaviour)
#include "test_scrollengine_behaviour.moc"
