// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

/**
 * @file test_scrollengine_boundary.cpp
 *
 * The screen-boundary contract: a straddling edge column is committed CLAMPED
 * at both screen edges in the default mode, keeps its TRUE rect in crop mode
 * (globally and as a per-screen rule), parks below its peek floor, and every
 * park lands below the union of all outputs. These are the applyLayout
 * branches the smoke fixtures cannot reach (their columns are either fully on
 * screen or fully off), driven here by an always-center settings stub so the
 * neighbours of the focused column genuinely straddle.
 *
 * Also the boundary's other edges: a rotation re-resolving the strip axis, a
 * stack whose minimums overflow the work area parking its tail across the
 * strip (in crop mode too), the hidden tabs of an on-screen tabbed column and
 * the tab bar of a fully parked one, and a user resize that overshoots the
 * work area across the strip being committed back. And a user resize of one
 * edge of a column the relayout centres, along the strip (the focused column
 * under Always or a lone one under alwaysCenterSingleColumn) or across it (a
 * short column that shows one rect under centerShortColumns): the dragged
 * edge lands at the release and the width or height it keeps stops at the
 * work area, while an unfocused column, a stack and a drag of both edges take
 * the frame as it is. The centred doubling has its limits: a shrink it would
 * take past the centre or below the window's minimum, and any drag under a
 * view the user panned away, are taken one-sided. Under a view that centres
 * nothing the focused column keeps its trailing edge, so a drag of its
 * leading edge lands at the release too. A resize the strip refuses within a
 * rounding pixel of the rect it offered schedules no retile, and the next
 * resize still lands. A client that commits a frame other than its column is
 * test_scrollengine_committedfloor's.
 */

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QObject>
#include <QSignalSpy>
#include <QStringList>
#include <QTest>

#include <PhosphorScrollEngine/IScrollSettings.h>
#include <PhosphorScrollEngine/ScrollEngine.h>
#include <PhosphorScrollEngine/ScrollState.h>
#include <PhosphorScrollEngine/ScrollStrip.h>
#include <PhosphorScrollEngine/ScrollTypes.h>

#include "scrollstriptestutils.h"
#include "scrollstubsettings.h"

#include <functional>

using PhosphorScrollEngine::ScrollEngine;
namespace Ax = ScrollTestUtils::Ax;

using ScrollTestUtils::defaultScreenRect;
using ScrollTestUtils::makeProviderEngine;
using ScrollTestUtils::tileRectOn;

namespace {

const QString kS1 = QStringLiteral("S1");
/// The comparison screen for the per-screen crop case: same geometry, no
/// override map, so the only difference between the two is the rule slot.
const QString kS2 = QStringLiteral("S2");

/// The SHARED stub, re-seeded for this suite instead of a local subclass: a
/// second IScrollSettings copy here drifted from the shared one on the very
/// values the two suites' cases compare against (column width 0.5 vs 0.25,
/// the tab-indicator enable inverted), which is exactly the hazard the
/// shared header's comment warns about. Everything a case here mutates
/// (cropStraddlers, centerFocused, respectMinimumSize, insertPosition,
/// tabIndicatorEnabled) is a public field on the shared stub already.
ScrollTestUtils::StubScrollSettings* makeBoundarySettings(QObject* owner)
{
    auto* settings = new ScrollTestUtils::StubScrollSettings(owner);
    // Half-work-area columns: 600px on the plain 1200px screen, and 500px on
    // the 1000px inset work area of the clamp case.
    settings->widthValue = 0.5;
    // Always-center is the straddler generator — it is what makes BOTH
    // neighbours of the focused column cross a screen edge.
    settings->centerFocused = static_cast<int>(PhosphorScrollEngine::CenterFocusedColumn::Always);
    // Off by default so the geometry tests stay indicator-free; the orphan
    // tab-bar test flips it on, since a null tabIndicatorRect would gate the
    // strip emitter shut before the skip under test could ever matter. The
    // remaining indicator values keep this suite's historical seeds.
    settings->tabIndicatorEnabled = false;
    settings->tabIndicatorHideWhenSingleTab = true;
    settings->tabIndicatorGap = 0;
    settings->tabIndicatorWidth = 2;
    return settings;
}

/// The latest windowsTiled batch entry for @p windowId, or an empty object.
QJsonObject lastEntryFor(QSignalSpy& tiled, const QString& windowId)
{
    for (int sig = tiled.count() - 1; sig >= 0; --sig) {
        const QJsonArray batch = QJsonDocument::fromJson(tiled.at(sig).at(0).toString().toUtf8()).array();
        for (const QJsonValue& v : batch) {
            if (v.toObject().value(QLatin1String("windowId")).toString() == windowId) {
                return v.toObject();
            }
        }
    }
    return {};
}

/// The committed rect a windowsTiled batch entry describes.
QRect rectOfEntry(const QJsonObject& o)
{
    return QRect(o.value(QLatin1String("x")).toInt(), o.value(QLatin1String("y")).toInt(),
                 o.value(QLatin1String("width")).toInt(), o.value(QLatin1String("height")).toInt());
}

/// How far a user resize moves each edge of a window, in role space: the low
/// and high edges along the strip, and the low and high edges across it.
struct EdgeDrag
{
    int mainLow = 0;
    int mainHigh = 0;
    int crossLow = 0;
    int crossHigh = 0;
};

/// What a user resize of one window came to.
struct ResizeOutcome
{
    QRect applied; ///< the rect committed before the resize; empty when none was
    QRect settled; ///< the window's tile rect once the relayout ran
    bool held = false; ///< the strip still holds the window, so the intents below are its own
    PhosphorScrollEngine::ColumnWidth width; ///< the resized column's stored width intent
    PhosphorScrollEngine::WindowHeight height; ///< the resized tile's stored height intent
};

/// Opens @p windows on kS1 under the boundary settings @p configure adjusts,
/// focuses @p focused (and makes its column tabbed when @p tabbed), runs
/// @p prepare on the laid-out strip when given, then has the user resize
/// @p target from its committed rect by @p drag. The engine and its settings
/// die with the call, so everything a case asserts on is read out here.
ResizeOutcome resizeOneWindow(const std::function<void(ScrollTestUtils::StubScrollSettings*)>& configure,
                              const QStringList& windows, const QString& focused, const QString& target,
                              const EdgeDrag& drag, bool tabbed = false,
                              const std::function<void(ScrollEngine*)>& prepare = {})
{
    QObject owner;
    auto* settings = makeBoundarySettings(&owner);
    configure(settings);
    ScrollEngine* engine = makeProviderEngine(&owner, {kS1});
    engine->setEngineSettings(settings);
    engine->refreshConfigFromSettings();
    QSignalSpy tiled(engine, &ScrollEngine::windowsTiled);
    for (const QString& windowId : windows) {
        engine->windowOpened(windowId, kS1, 0, 0);
    }
    engine->windowFocused(focused, kS1);
    if (tabbed) {
        engine->toggleColumnTabbed(kS1);
    }
    engine->retile(kS1);
    QCoreApplication::processEvents();
    if (prepare) {
        prepare(engine);
        QCoreApplication::processEvents();
    }

    ResizeOutcome outcome;
    outcome.applied = rectOfEntry(lastEntryFor(tiled, target));
    if (outcome.applied.isEmpty()) {
        return outcome;
    }
    // Role space, transposed with the arm: x and width run along the strip.
    QRect settled = Ax::t(outcome.applied);
    settled.setLeft(settled.left() + drag.mainLow);
    settled.setRight(settled.right() + drag.mainHigh);
    settled.setTop(settled.top() + drag.crossLow);
    settled.setBottom(settled.bottom() + drag.crossHigh);
    engine->onWindowResized(target, outcome.applied, Ax::t(settled), kS1);
    QCoreApplication::processEvents();

    outcome.settled = tileRectOn(engine, kS1, target);
    const auto* state = static_cast<const PhosphorScrollEngine::ScrollState*>(
        static_cast<const ScrollEngine*>(engine)->stateForScreen(kS1));
    const int columnIdx = state ? state->strip().columnOfWindow(target) : -1;
    if (columnIdx >= 0) {
        const PhosphorScrollEngine::Column& column = state->strip().columns().at(columnIdx);
        const int tileIdx = column.indexOfWindow(target);
        if (tileIdx >= 0) {
            outcome.held = true;
            outcome.width = column.width;
            outcome.height = column.tiles.at(tileIdx).height;
        }
    }
    return outcome;
}

/// A stored Fixed intent of @p px, as a resize writes one.
bool isFixedWidth(const PhosphorScrollEngine::ColumnWidth& width, int px)
{
    return width.kind == PhosphorScrollEngine::ColumnWidth::Fixed && width.fixedPx == px;
}
bool isFixedHeight(const PhosphorScrollEngine::WindowHeight& height, int px)
{
    return height.kind == PhosphorScrollEngine::WindowHeight::Fixed && height.fixedPx == px;
}

} // namespace

class TestScrollEngineBoundary : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    /// Proves the vertical arm really is transposed, so a lost ENVIRONMENT
    /// property cannot leave it silently re-running the horizontal suite.
    void initTestCase()
    {
        AX_GUARD_SUITE();
    }

    /// A monitor that ROTATES while the strip is populated. The geometry
    /// provider hands back a portrait rect on the second pass, so Auto
    /// resolves the other axis. What is ASSERTED is exactly the re-resolve
    /// (the committed aspect inverts); the axis-flip sweep's state drop
    /// (anchor re-derivation, the parked-edge eviction) has no clean
    /// observable at this level and is NOT claimed here — the name used to
    /// say "AndDropsOldAxisState", which nothing below ever checked.
    ///
    /// Both geometries are hardcoded PHYSICAL rects and neither is transposed
    /// with the fixture, so the two arms run this identically: it always
    /// starts landscape and always rotates to portrait. That is on purpose —
    /// what is under test is the ENGINE re-resolving Auto from the new
    /// geometry, which has nothing to do with the harness's axis.
    void aRotationFlipsTheResolvedAxis()
    {
        QObject owner;
        // Mutable geometry: the provider answers landscape until the test
        // rotates it, which is what a real output change looks like to the
        // engine (it subscribes to no screen signal and re-reads at relayout).
        auto geometry = std::make_shared<QRect>(QRect(0, 0, 1200, 800));
        ScrollEngine* engine = makeProviderEngine(&owner, {kS1}, [geometry](const QString&) {
            return *geometry;
        });

        QSignalSpy tiled(engine, &ScrollEngine::windowsTiled);
        engine->windowOpened(QStringLiteral("app|a"), kS1, 0, 0);
        engine->windowOpened(QStringLiteral("app|b"), kS1, 0, 0);
        engine->windowOpened(QStringLiteral("app|c"), kS1, 0, 0);
        QVERIFY(tiled.count() > 0);

        const QRect landscapeA = rectOfEntry(lastEntryFor(tiled, QStringLiteral("app|a")));
        QVERIFY2(!landscapeA.isEmpty(), "precondition: a is committed before the rotation");
        QVERIFY2(landscapeA.height() >= landscapeA.width(),
                 "precondition: a full-height column on a landscape strip is taller than it is wide");

        // Rotate. Same screen, transposed geometry.
        *geometry = QRect(0, 0, 800, 1200);
        engine->retile(kS1);

        const QRect portraitA = rectOfEntry(lastEntryFor(tiled, QStringLiteral("app|a")));
        QVERIFY2(!portraitA.isEmpty(), "a must still be committed after the rotation");
        // A column on a VERTICAL strip spans the full width and takes a share
        // of the height, so the aspect relationship inverts.
        QVERIFY2(portraitA.width() >= portraitA.height(),
                 qPrintable(QStringLiteral("expected a full-width row after the flip, got %1x%2")
                                .arg(portraitA.width())
                                .arg(portraitA.height())));
    }

    // Default mode: BOTH neighbours of a centered column straddle, and both
    // are committed clamped at the SCREEN edge — deleting either clamp
    // branch, or substituting the work area for the screen rect, fails this.
    void clampModeClampsBothEdgesAtTheScreenRect()
    {
        QObject owner;
        auto* settings = makeBoundarySettings(&owner);
        // Panel inset: the work area is 100px narrower on each side than the
        // screen. The clamp bound is the SCREEN edge; a work-area clamp
        // would stop 100px short and this fixture is what tells them apart.
        const QRect screen = defaultScreenRect(); // 1200 along the strip, 800 across it (transposed with the arm)
        const QRect inset = Ax::t(QRect(100, 0, 1000, 800));
        ScrollEngine* engine = makeProviderEngine(
            &owner, {kS1},
            [screen](const QString&) {
                return screen;
            },
            [inset](const QString&) {
                return inset;
            });
        engine->setEngineSettings(settings);
        engine->refreshConfigFromSettings();

        QSignalSpy tiled(engine, &ScrollEngine::windowsTiled);
        engine->windowOpened(QStringLiteral("app|a"), kS1, 0, 0);
        engine->windowOpened(QStringLiteral("app|b"), kS1, 0, 0);
        engine->windowOpened(QStringLiteral("app|c"), kS1, 0, 0);
        // Center b: a straddles the LEAD screen edge, c the TRAIL one
        // (columns are 500px on the 1000px work area).
        engine->windowFocused(QStringLiteral("app|b"), kS1);
        engine->retile(kS1);
        QCoreApplication::processEvents();

        const QJsonObject a = lastEntryFor(tiled, QStringLiteral("app|a"));
        const QJsonObject c = lastEntryFor(tiled, QStringLiteral("app|c"));
        QVERIFY(!a.isEmpty());
        QVERIFY(!c.isEmpty());
        // Clamped exactly at the screen edges — not the work area's.
        QCOMPARE(Ax::entryMainPos(a), Ax::mainPos(screen));
        QCOMPARE(Ax::entryMainEnd(c), Ax::mainEnd(screen));
        // And genuinely clamped, i.e. narrower than a full column.
        QVERIFY(Ax::entryMainLen(a) < 500);
        QVERIFY(Ax::entryMainLen(c) < 500);
    }

    // Crop mode: the same fixture commits the TRUE rects — the straddlers
    // keep their full width and their overhang crosses the screen edge.
    void cropModeCommitsTrueRectsForStraddlers()
    {
        QObject owner;
        auto* settings = makeBoundarySettings(&owner);
        settings->cropStraddlers = true;
        ScrollEngine* engine = makeProviderEngine(&owner, {kS1});
        engine->setEngineSettings(settings);
        engine->refreshConfigFromSettings();

        QSignalSpy tiled(engine, &ScrollEngine::windowsTiled);
        engine->windowOpened(QStringLiteral("app|a"), kS1, 0, 0);
        engine->windowOpened(QStringLiteral("app|b"), kS1, 0, 0);
        engine->windowOpened(QStringLiteral("app|c"), kS1, 0, 0);
        engine->windowFocused(QStringLiteral("app|b"), kS1);
        engine->retile(kS1);
        QCoreApplication::processEvents();

        const QRect screen = defaultScreenRect();
        const QJsonObject a = lastEntryFor(tiled, QStringLiteral("app|a"));
        const QJsonObject c = lastEntryFor(tiled, QStringLiteral("app|c"));
        QVERIFY(!a.isEmpty());
        QVERIFY(!c.isEmpty());
        // True 600px columns, overhanging both edges.
        QCOMPARE(Ax::entryMainLen(a), 600);
        QCOMPARE(Ax::entryMainLen(c), 600);
        QVERIFY(Ax::entryMainPos(a) < Ax::mainPos(screen));
        QVERIFY(Ax::entryMainEnd(c) > Ax::mainEnd(screen));
    }

    // A remainder below the peek floor parks (with its departure edge)
    // instead of committing a sliver.
    void peekFloorParksAThinRemainder()
    {
        QObject owner;
        auto* settings = makeBoundarySettings(&owner);
        ScrollEngine* engine = makeProviderEngine(&owner, {kS1});
        engine->setEngineSettings(settings);
        engine->refreshConfigFromSettings();

        // Every new column opens at 0.98 of the work area (the per-screen
        // rule channel): centering a leaves b a 12px sliver at the TRAIL
        // strip edge, under the 48px floor.
        QVariantMap wide;
        wide.insert(PhosphorScrollEngine::ScrollPerScreenKeys::defaultColumnWidth(), 0.98);
        engine->applyPerScreenConfig(kS1, wide);

        QSignalSpy tiled(engine, &ScrollEngine::windowsTiled);
        engine->windowOpened(QStringLiteral("app|a"), kS1, 0, 0);
        engine->windowOpened(QStringLiteral("app|b"), kS1, 0, 0);
        engine->windowFocused(QStringLiteral("app|a"), kS1);
        engine->retile(kS1);
        QCoreApplication::processEvents();

        const QRect screen = defaultScreenRect();
        const QJsonObject b = lastEntryFor(tiled, QStringLiteral("app|b"));
        QVERIFY(!b.isEmpty());
        // Parked below the screen, not committed as a sliver on it.
        QVERIFY2(
            b.value(QLatin1String("y")).toInt() > screen.bottom(),
            qPrintable(
                QStringLiteral("expected a park below the screen, got y=%1").arg(b.value(QLatin1String("y")).toInt())));
        QCOMPARE(b.value(QLatin1String("scrollEdge")).toString(), Ax::edgeTrail());
    }

    // Crop mode is a per-SCREEN rule slot, and the emit loop must read the
    // override rather than the config-seeded member. Global crop OFF, S1
    // scoped ON: S1's straddlers keep their true rects while the same
    // fixture on S2 commits clamped ones. Without the second screen this
    // reads identically to a member-only implementation, which is exactly
    // the regression the per-screen keys keep having.
    void cropStraddlersOverrideIsPerScreen()
    {
        QObject owner;
        auto* settings = makeBoundarySettings(&owner);
        settings->cropStraddlers = false;
        ScrollEngine* engine = makeProviderEngine(&owner, {kS1, kS2});
        engine->setEngineSettings(settings);
        engine->refreshConfigFromSettings();

        QVariantMap crop;
        crop.insert(PhosphorScrollEngine::ScrollPerScreenKeys::cropStraddlers(), true);
        engine->applyPerScreenConfig(kS1, crop);

        QSignalSpy tiled(engine, &ScrollEngine::windowsTiled);
        for (const QString& id : {QStringLiteral("app|a1"), QStringLiteral("app|a2"), QStringLiteral("app|a3")}) {
            engine->windowOpened(id, kS1, 0, 0);
        }
        for (const QString& id : {QStringLiteral("app|b1"), QStringLiteral("app|b2"), QStringLiteral("app|b3")}) {
            engine->windowOpened(id, kS2, 0, 0);
        }
        // Center the middle column on each screen, so its two neighbours
        // straddle (the always-center stub is the straddler generator).
        engine->windowFocused(QStringLiteral("app|a2"), kS1);
        engine->windowFocused(QStringLiteral("app|b2"), kS2);
        engine->retile(kS1);
        engine->retile(kS2);
        QCoreApplication::processEvents();

        const QRect screen = defaultScreenRect();
        const QJsonObject cropped = lastEntryFor(tiled, QStringLiteral("app|a1"));
        const QJsonObject clamped = lastEntryFor(tiled, QStringLiteral("app|b1"));
        QVERIFY(!cropped.isEmpty());
        QVERIFY(!clamped.isEmpty());
        // S1 keeps the true 600px rect, overhanging the LEAD screen edge.
        QCOMPARE(Ax::entryMainLen(cropped), 600);
        QVERIFY(Ax::entryMainPos(cropped) < Ax::mainPos(screen));
        // S2 is clamped at the edge and narrower for it.
        QCOMPARE(Ax::entryMainPos(clamped), Ax::mainPos(screen));
        QVERIFY(Ax::entryMainLen(clamped) < 600);
    }

    // The park lands below the union of ALL outputs — the headline rule of
    // the boundary redesign. With a second output stacked underneath, a
    // park below only the strip's own screen would land INSIDE it.
    void parkLandsBelowTheUnionOfAllOutputs()
    {
        QObject owner;
        auto* settings = makeBoundarySettings(&owner);
        // Plain edge-aligned view.
        settings->centerFocused = static_cast<int>(PhosphorScrollEngine::CenterFocusedColumn::Never);
        const QRect top = defaultScreenRect();
        // Stacked directly BENEATH the primary, keyed off it rather than
        // hardcoded. Ax::t() would be wrong here: transposing puts the second
        // output to the SIDE instead, the union's lower edge collapses back
        // onto the primary's, and the test stops discriminating a
        // below-the-union park from a below-this-screen one. The claim under
        // test is physical ("below every output"), so the fixture is too.
        const QRect bottom(top.left(), top.bottom() + 1, top.width(), top.height());
        ScrollEngine* engine = makeProviderEngine(&owner, {kS1}, [top](const QString&) {
            return top;
        });
        engine->setAllScreenGeometriesProvider([top, bottom]() {
            return QList<QRect>{top, bottom};
        });
        engine->setEngineSettings(settings);
        engine->refreshConfigFromSettings();

        QSignalSpy tiled(engine, &ScrollEngine::windowsTiled);
        engine->windowOpened(QStringLiteral("app|a"), kS1, 0, 0);
        engine->windowOpened(QStringLiteral("app|b"), kS1, 0, 0);
        engine->windowOpened(QStringLiteral("app|c"), kS1, 0, 0);
        // Focus the last column: the first scrolls fully off and parks.
        engine->focusColumnLast(kS1);
        QCoreApplication::processEvents();

        const QJsonObject a = lastEntryFor(tiled, QStringLiteral("app|a"));
        QVERIFY(!a.isEmpty());
        const int y = a.value(QLatin1String("y")).toInt();
        QVERIFY2(y > bottom.bottom(),
                 qPrintable(
                     QStringLiteral("park y=%1 must clear the lower output (bottom=%2)").arg(y).arg(bottom.bottom())));
    }

    // Cross-axis enforcement runs in BOTH modes: crop mode opts out of the
    // MAIN-axis straddler clamp only, and a stacked column whose minimum
    // cross extents overflow the work area still has its below-floor tail
    // parked (not committed onto whatever sits beneath the screen). The park
    // is edge-less — a stack overflow is stack layout, not strip motion.
    void crossOverflowParksEvenInCropMode()
    {
        QObject owner;
        auto* settings = makeBoundarySettings(&owner);
        settings->cropStraddlers = true;
        settings->respectMinimumSize = true;
        settings->centerFocused = static_cast<int>(PhosphorScrollEngine::CenterFocusedColumn::Never);
        settings->insertPosition = static_cast<int>(PhosphorScrollEngine::ScrollInsertPosition::IntoActiveColumn);
        ScrollEngine* engine = makeProviderEngine(&owner, {kS1});
        engine->setEngineSettings(settings);
        engine->refreshConfigFromSettings();

        QSignalSpy tiled(engine, &ScrollEngine::windowsTiled);
        // Two tiles stacked into one column, each with a 600px minimum
        // ACROSS the strip on an 800px cross extent: the min-size clamp lays
        // the second tile out past the work area (relayout documents the
        // overflow as standing). The minimum is transposed with the fixture,
        // because what overflows a stack is the extent the stack divides.
        const QSize minSize = Ax::t(QSize(0, 600));
        engine->windowOpened(QStringLiteral("app|a"), kS1, minSize.width(), minSize.height());
        engine->windowOpened(QStringLiteral("app|b"), kS1, minSize.width(), minSize.height());
        engine->retile(kS1);
        QCoreApplication::processEvents();

        const QRect screen = defaultScreenRect();
        const QJsonObject b = lastEntryFor(tiled, QStringLiteral("app|b"));
        QVERIFY(!b.isEmpty());
        QVERIFY2(b.value(QLatin1String("y")).toInt() > screen.bottom(),
                 qPrintable(QStringLiteral("expected a cross-axis park below the screen even in crop mode, got y=%1")
                                .arg(b.value(QLatin1String("y")).toInt())));
        QVERIFY2(!b.contains(QLatin1String("scrollEdge")),
                 "a cross-axis park carries no scrollEdge (there is no side to animate from)");
    }

    // A tabbed column's hidden tiles are parked so they cannot take input from
    // the visible tab, NOT because the strip scrolled them away. While the
    // column is ON screen they have made no departure, so they report no
    // scrollEdge — and the next tab switch must not slide the newly active tab
    // in from a side the column never left.
    void hiddenTabOfAnOnScreenColumnCarriesNoScrollEdge()
    {
        QObject owner;
        auto* settings = makeBoundarySettings(&owner);
        settings->centerFocused = static_cast<int>(PhosphorScrollEngine::CenterFocusedColumn::Never);
        ScrollEngine* engine = makeProviderEngine(&owner, {kS1});
        engine->setEngineSettings(settings);
        engine->refreshConfigFromSettings();

        QSignalSpy tiled(engine, &ScrollEngine::windowsTiled);
        engine->windowOpened(QStringLiteral("app|a"), kS1, 0, 0);
        engine->windowOpened(QStringLiteral("app|b"), kS1, 0, 0);
        engine->consumeOrExpelWindow(-1, kS1); // b joins a's column
        engine->windowFocused(QStringLiteral("app|a"), kS1);
        engine->toggleColumnTabbed(kS1);
        QCoreApplication::processEvents();

        const QRect screen = defaultScreenRect();
        const QJsonObject hidden = lastEntryFor(tiled, QStringLiteral("app|b"));
        QVERIFY2(!hidden.isEmpty(), "expected the hidden tab in the tile batch");
        QVERIFY2(hidden.value(QLatin1String("y")).toInt() > screen.bottom(),
                 "a hidden tab is parked off-canvas so it cannot take input");
        QVERIFY2(!hidden.contains(QLatin1String("scrollEdge")),
                 "a hidden tab of an on-screen column must not report a departure edge");

        // The discriminating leg: with a fallback edge recorded above, this
        // switch consumed it and slid the newly shown tab in from off the
        // TRAIL strip edge, a move the column never made.
        tiled.clear();
        engine->windowFocused(QStringLiteral("app|b"), kS1);
        QCoreApplication::processEvents();
        const QJsonObject shown = lastEntryFor(tiled, QStringLiteral("app|b"));
        QVERIFY2(!shown.isEmpty(), "expected the newly active tab in the tile batch");
        QVERIFY2(shown.value(QLatin1String("y")).toInt() <= screen.bottom(),
                 "the newly active tab must be placed on screen, not parked");
        QVERIFY2(!shown.contains(QLatin1String("scrollEdge")),
                 "a tab switch on an on-screen column must not slide the new tab in from an edge");
    }

    // A tabbed column whose peek falls below the floor parks every one of its
    // tiles, so nothing it labels is on screen — yet its TRUE rect still
    // intersects the work area, which is exactly what left an orphan tab bar
    // drawn at the edge. The strip emitter skips a column once every tile it
    // emitted was parked.
    void fullyParkedTabbedColumnEmitsNoTabStrip()
    {
        QObject owner;
        auto* settings = makeBoundarySettings(&owner);
        settings->tabIndicatorEnabled = true;
        ScrollEngine* engine = makeProviderEngine(&owner, {kS1});
        engine->setEngineSettings(settings);
        engine->refreshConfigFromSettings();

        // Same generator as peekFloorParksAThinRemainder: every column opens
        // at 0.98 of the work area, so centering the first leaves the second a
        // 12px sliver at the TRAIL strip edge, under the 48px floor.
        QVariantMap wide;
        wide.insert(PhosphorScrollEngine::ScrollPerScreenKeys::defaultColumnWidth(), 0.98);
        engine->applyPerScreenConfig(kS1, wide);

        QSignalSpy strips(engine, &ScrollEngine::tabStripsChanged);
        QSignalSpy tiled(engine, &ScrollEngine::windowsTiled);
        engine->windowOpened(QStringLiteral("app|a"), kS1, 0, 0);
        engine->windowOpened(QStringLiteral("app|b"), kS1, 0, 0);
        engine->windowOpened(QStringLiteral("app|c"), kS1, 0, 0);
        engine->consumeOrExpelWindow(-1, kS1); // c joins b's column
        engine->windowFocused(QStringLiteral("app|b"), kS1);
        engine->toggleColumnTabbed(kS1);
        QCoreApplication::processEvents();

        // The tabbed column is on screen at this point, so it DOES own a bar.
        // Asserting that first is what makes the skip below discriminating
        // rather than vacuous.
        const auto namesB = [](const QJsonArray& payload) {
            for (const QJsonValue& v : payload) {
                if (v.toObject().value(QLatin1String("tabs")).toArray().contains(QJsonValue(QStringLiteral("app|b")))) {
                    return true;
                }
            }
            return false;
        };
        QVERIFY(!strips.isEmpty());
        QVERIFY2(namesB(QJsonDocument::fromJson(strips.last().at(1).toString().toUtf8()).array()),
                 "the fixture must first produce a tab bar for the on-screen tabbed column");

        // Centering the first column pushes the tabbed one to a below-floor
        // remainder, so every one of its tiles parks.
        engine->windowFocused(QStringLiteral("app|a"), kS1);
        engine->retile(kS1);
        QCoreApplication::processEvents();

        const QRect screen = defaultScreenRect();
        // BOTH of the column's tiles, so the message's "every tile" claim is
        // what the assertions actually check — reading only the shown tab
        // left a hidden tab that failed to park invisible to the case.
        for (const char* id : {"app|b", "app|c"}) {
            const QJsonObject entry = lastEntryFor(tiled, QString::fromLatin1(id));
            QVERIFY2(
                !entry.isEmpty(),
                qPrintable(
                    QStringLiteral("expected %1 of the tabbed column in the tile batch").arg(QString::fromLatin1(id))));
            QVERIFY2(entry.value(QLatin1String("y")).toInt() > screen.bottom(),
                     qPrintable(QStringLiteral("the fixture must park the tabbed column's every tile, including %1")
                                    .arg(QString::fromLatin1(id))));
        }

        QVERIFY2(!namesB(QJsonDocument::fromJson(strips.last().at(1).toString().toUtf8()).array()),
                 "a fully parked tabbed column must not leave an orphan tab bar at the edge");
    }

    // A user resize that overshoots the work area ACROSS the strip (the
    // bottom edge dragged into the outer gap) is clamped straight back by
    // the relayout, which resolves to the very rect the window held before.
    // That corrective rect MUST still reach the compositor: the window is
    // sitting at the overshoot now, so "same rect as last time" is not
    // "nothing moved". The emit-on-change gate used to swallow exactly this
    // batch and leave the window stranded over the gap.
    void crossAxisOvershootResizeIsCommittedBackToTheWorkArea()
    {
        QObject owner;
        auto* settings = makeBoundarySettings(&owner);
        const QRect screen = defaultScreenRect();
        // A work area inset ACROSS the strip only, standing in for the
        // bottom gap the report's user reserves for a panel.
        const QRect inset = Ax::t(QRect(0, 0, 1200, 700));
        ScrollEngine* engine = makeProviderEngine(
            &owner, {kS1},
            [screen](const QString&) {
                return screen;
            },
            [inset](const QString&) {
                return inset;
            });
        engine->setEngineSettings(settings);
        engine->refreshConfigFromSettings();

        QSignalSpy tiled(engine, &ScrollEngine::windowsTiled);
        engine->windowOpened(QStringLiteral("app|a"), kS1, 0, 0);
        engine->retile(kS1);
        QCoreApplication::processEvents();

        const QRect applied = rectOfEntry(lastEntryFor(tiled, QStringLiteral("app|a")));
        QVERIFY2(!applied.isEmpty(), "precondition: a is committed before the resize");
        QCOMPARE(Ax::crossEnd(applied), Ax::crossEnd(inset));

        // The settled frame: the same rect grown 60px past the work area's
        // cross edge, in role space and transposed with the arm.
        QRect overshoot = Ax::t(applied);
        overshoot.setHeight(overshoot.height() + 60);
        overshoot = Ax::t(overshoot);
        const int before = tiled.count();
        engine->onWindowResized(QStringLiteral("app|a"), applied, overshoot, kS1);
        QCoreApplication::processEvents();

        QVERIFY2(tiled.count() > before, "the clamped-back rect must be re-emitted, not gated as unchanged");
        const QRect corrected = rectOfEntry(lastEntryFor(tiled, QStringLiteral("app|a")));
        QCOMPARE(Ax::crossEnd(corrected), Ax::crossEnd(inset));
        QCOMPARE(corrected, applied);
    }

    // A user resize of ONE edge along the strip, on a column the relayout
    // centres, lands the dragged edge where it was released: the column takes
    // twice the drag and the far edge moves by the same amount. Taken
    // one-sided, the re-centre moved each edge by half the drag and the
    // dragged edge sprang back from the cursor. Only the focused column
    // centres, under Always or under alwaysCenterSingleColumn on a lone
    // column, and a drag of both edges is taken as it is. Under Never nothing
    // re-centres, so the column takes the frame as the user left it.
    void oneEdgeResizeOfACentredColumnLandsTheDraggedEdgeAtTheRelease()
    {
        const QString lone = QStringLiteral("app|a");
        // The lone window's column under @p centerFocused (and
        // alwaysCenterSingleColumn when @p centerSingle), its edges moved
        // along the strip by @p lowDrag and @p highDrag.
        const auto resized = [&lone](int centerFocused, bool centerSingle, int lowDrag, int highDrag) {
            return resizeOneWindow(
                [centerFocused, centerSingle](ScrollTestUtils::StubScrollSettings* settings) {
                    settings->centerFocused = centerFocused;
                    settings->alwaysCenterSingleColumn = centerSingle;
                },
                {lone}, lone, lone, EdgeDrag{lowDrag, highDrag, 0, 0});
        };
        const int always = static_cast<int>(PhosphorScrollEngine::CenterFocusedColumn::Always);
        const int never = static_cast<int>(PhosphorScrollEngine::CenterFocusedColumn::Never);

        // Precondition the arithmetic below leans on: a centred 600px column
        // on the 1200px screen.
        const ResizeOutcome grown = resized(always, false, -200, 0);
        QVERIFY2(!grown.applied.isEmpty() && grown.held, "precondition: the column is committed before the resize");
        QCOMPARE(Ax::mainPos(grown.applied), 300);
        QCOMPARE(Ax::mainLen(grown.applied), 600);

        // The low edge dragged out from 300 to 100: the column takes 1000 and,
        // centred, starts at the release point.
        QCOMPARE(Ax::mainLen(grown.settled), 1000);
        QCOMPARE(Ax::mainPos(grown.settled), 100);
        QVERIFY(isFixedWidth(grown.width, 1000));

        // The high edge dragged in by 100: the column gives up 200 and its
        // high edge ends where the drag did.
        const ResizeOutcome shrunk = resized(always, false, 0, -100);
        QVERIFY(!shrunk.applied.isEmpty());
        QCOMPARE(Ax::mainLen(shrunk.settled), 400);
        QCOMPARE(Ax::mainEnd(shrunk.settled), Ax::mainEnd(shrunk.applied) - 100);

        // A drag past what the screen can hold stops at the work area, and so
        // does the width the column keeps: the doubled 1400 would come back
        // on every later relayout the clamp does not reach.
        const ResizeOutcome wide = resized(always, false, -400, 0);
        QVERIFY(!wide.applied.isEmpty() && wide.held);
        QCOMPARE(Ax::mainLen(wide.settled), 1200);
        QVERIFY2(isFixedWidth(wide.width, 1200),
                 qPrintable(QStringLiteral("the stored width must be the work area's 1200, got kind %1 at %2px")
                                .arg(static_cast<int>(wide.width.kind))
                                .arg(wide.width.fixedPx)));

        // Both edges dragged out by 100: taken as it is, 800 wide, and centred
        // both edges are where the user let go.
        const ResizeOutcome both = resized(always, false, -100, 100);
        QVERIFY(!both.applied.isEmpty() && both.held);
        QCOMPARE(Ax::mainLen(both.settled), 800);
        QCOMPARE(Ax::mainPos(both.settled), Ax::mainPos(both.applied) - 100);
        QVERIFY(isFixedWidth(both.width, 800));

        // alwaysCenterSingleColumn centres the lone column under Never, so the
        // drag lands the same way it does under Always.
        const ResizeOutcome single = resized(never, true, -200, 0);
        QVERIFY(!single.applied.isEmpty());
        QCOMPARE(Ax::mainPos(single.applied), 300);
        QCOMPARE(Ax::mainLen(single.settled), 1000);
        QCOMPARE(Ax::mainPos(single.settled), 100);

        // Under Never the frame is the answer: the high edge dragged out by
        // 200 makes the column 200 wider and leaves its low edge in place.
        const ResizeOutcome plain = resized(never, false, 0, 200);
        QVERIFY(!plain.applied.isEmpty() && plain.held);
        QCOMPARE(Ax::mainLen(plain.settled), Ax::mainLen(plain.applied) + 200);
        QCOMPARE(Ax::mainPos(plain.settled), Ax::mainPos(plain.applied));
        QVERIFY(isFixedWidth(plain.width, Ax::mainLen(plain.applied) + 200));
    }

    // The doubling's edges. A shrink the doubling would take past the centre,
    // or below the window's own declared minimum, is taken one-sided: the
    // frame the drag ended at already honours both, and a doubled answer
    // under them would persist an intent the window can never take. An edge
    // the user did not touch may read a pixel off on a fractional scale, and
    // still counts as untouched. A view the user panned away (detached) is
    // left where it is by the relayout, so nothing re-centres and the drag is
    // taken as it is.
    void theCentredResizeDoublingHasItsLimits()
    {
        const QString lone = QStringLiteral("app|a");
        const int always = static_cast<int>(PhosphorScrollEngine::CenterFocusedColumn::Always);
        const auto alwaysCentred = [always](ScrollTestUtils::StubScrollSettings* settings) {
            settings->centerFocused = always;
        };

        // Past the centre: 600 less twice 350 is nothing, so the frame's 250.
        const ResizeOutcome pastCentre = resizeOneWindow(alwaysCentred, {lone}, lone, lone, EdgeDrag{0, -350, 0, 0});
        QVERIFY(!pastCentre.applied.isEmpty() && pastCentre.held);
        QCOMPARE(Ax::mainLen(pastCentre.applied), 600);
        QCOMPARE(Ax::mainLen(pastCentre.settled), 250);
        QVERIFY(isFixedWidth(pastCentre.width, 250));

        // Below a declared 500 minimum: the doubled 400 would not do, the
        // frame's 500 does.
        const ResizeOutcome belowMin = resizeOneWindow(
            alwaysCentred, {lone}, lone, lone, EdgeDrag{0, -100, 0, 0}, false, [&lone](ScrollEngine* engine) {
                const QSize min = Ax::t(QSize(500, 0));
                engine->windowMinSizeUpdated(lone, min.width(), min.height());
            });
        QVERIFY(!belowMin.applied.isEmpty() && belowMin.held);
        QCOMPARE(Ax::mainLen(belowMin.applied), 600);
        QCOMPARE(Ax::mainLen(belowMin.settled), 500);
        QVERIFY(isFixedWidth(belowMin.width, 500));

        // The far edge a pixel off still reads as untouched: doubled to 1000.
        const ResizeOutcome rounded = resizeOneWindow(alwaysCentred, {lone}, lone, lone, EdgeDrag{-200, 1, 0, 0});
        QVERIFY(!rounded.applied.isEmpty() && rounded.held);
        QCOMPARE(Ax::mainLen(rounded.settled), 1000);
        QCOMPARE(Ax::mainPos(rounded.settled), Ax::mainPos(rounded.applied) - 200);

        // A panned (detached) view: the drag is taken as it is.
        const QString b = QStringLiteral("app|b");
        const ResizeOutcome detached = resizeOneWindow(
            [always](ScrollTestUtils::StubScrollSettings* settings) {
                settings->centerFocused = always;
                settings->widthValue = 0.5;
            },
            {QStringLiteral("app|a"), b, QStringLiteral("app|c")}, b, b, EdgeDrag{-200, 0, 0, 0}, false,
            [](ScrollEngine* engine) {
                engine->scrollViewByPx(100, kS1);
            });
        QVERIFY(!detached.applied.isEmpty() && detached.held);
        QVERIFY2(Ax::mainPos(detached.applied) != 300, "precondition: the pan moved the centred column");
        QCOMPARE(Ax::mainLen(detached.settled), 800);
        QVERIFY(isFixedWidth(detached.width, 800));
    }

    // A user resize the strip refuses, with a frame within a pixel of the
    // rect it was offered, is explained by rounding: the memory of the
    // applied rect is dropped and no retile is scheduled, since one would
    // offer the size the client already refused. The next resize still
    // reconciles with the memory gone, measured from the gesture's own start
    // frame, instead of being discarded until a retile laid a new baseline.
    void aRefusedResizeWithinARoundingPixelSkipsTheRetileAndTheNextOneStillLands()
    {
        QObject owner;
        auto* settings = makeBoundarySettings(&owner);
        settings->centerFocused = static_cast<int>(PhosphorScrollEngine::CenterFocusedColumn::Never);
        ScrollEngine* engine = makeProviderEngine(&owner, {kS1});
        engine->setEngineSettings(settings);
        engine->refreshConfigFromSettings();
        const QString a = QStringLiteral("app|a");
        QSignalSpy tiled(engine, &ScrollEngine::windowsTiled);
        engine->windowOpened(a, kS1, 0, 0);
        engine->setColumnWidth(PhosphorScrollEngine::ColumnWidth::makeFixed(300), kS1);
        engine->retile(kS1);
        QCoreApplication::processEvents();
        const QRect applied = rectOfEntry(lastEntryFor(tiled, a));
        QVERIFY2(!applied.isEmpty(), "precondition: a is committed");
        QCOMPARE(Ax::mainLen(applied), 300);

        // The gesture started from a frame 10px narrower and ended at the
        // column's own 300, a pixel along: the Fixed(300) intent already says
        // so, the reconcile refuses, and the pixel is rounding.
        QRect start = Ax::t(applied);
        start.setWidth(290);
        QRect settled = Ax::t(applied);
        settled.translate(1, 0);
        tiled.clear();
        engine->onWindowResized(a, Ax::t(start), Ax::t(settled), kS1);
        QCoreApplication::processEvents();
        QCOMPARE(tiled.count(), 0);

        // The next drag, from where the window sits, to 400.
        QRect wider = settled;
        wider.setWidth(400);
        engine->onWindowResized(a, Ax::t(settled), Ax::t(wider), kS1);
        QCoreApplication::processEvents();
        QCOMPARE(Ax::mainLen(tileRectOn(engine, kS1, a)), 400);
    }

    // Under a view that does not centre, the focused column's leading edge
    // sits at the view anchor and the relayout grows the column from there,
    // so the view moves by what the column grew: the dragged leading edge
    // lands at the release and the trailing edge stays where it was.
    void oneLeadingEdgeResizeUnderNeverLandsTheDraggedEdgeAtTheRelease()
    {
        const ResizeOutcome outcome = resizeOneWindow(
            [](ScrollTestUtils::StubScrollSettings* settings) {
                settings->centerFocused = static_cast<int>(PhosphorScrollEngine::CenterFocusedColumn::Never);
            },
            {QStringLiteral("app|a"), QStringLiteral("app|b")}, QStringLiteral("app|b"), QStringLiteral("app|b"),
            EdgeDrag{-100, 0, 0, 0});
        QVERIFY2(!outcome.applied.isEmpty() && outcome.held, "precondition: b is committed before the resize");
        QCOMPARE(Ax::mainPos(outcome.applied), 600);
        QCOMPARE(Ax::mainLen(outcome.applied), 600);

        QCOMPARE(Ax::mainLen(outcome.settled), 700);
        QCOMPARE(Ax::mainPos(outcome.settled), Ax::mainPos(outcome.applied) - 100);
        QCOMPARE(Ax::mainEnd(outcome.settled), Ax::mainEnd(outcome.applied));
        QVERIFY(isFixedWidth(outcome.width, 700));

        // The focused column in the middle of three quarter-width columns:
        // the same, with c beyond it untouched.
        const auto quarterColumns = [](ScrollTestUtils::StubScrollSettings* settings) {
            settings->centerFocused = static_cast<int>(PhosphorScrollEngine::CenterFocusedColumn::Never);
            settings->widthValue = 0.25;
        };
        const QStringList three{QStringLiteral("app|a"), QStringLiteral("app|b"), QStringLiteral("app|c")};
        const ResizeOutcome middle = resizeOneWindow(quarterColumns, three, QStringLiteral("app|b"),
                                                     QStringLiteral("app|b"), EdgeDrag{-100, 0, 0, 0});
        QVERIFY(!middle.applied.isEmpty() && middle.held);
        QCOMPARE(Ax::mainPos(middle.applied), 300);
        QCOMPARE(Ax::mainLen(middle.settled), 400);
        QCOMPARE(Ax::mainPos(middle.settled), Ax::mainPos(middle.applied) - 100);
        QCOMPARE(Ax::mainEnd(middle.settled), Ax::mainEnd(middle.applied));

        // The shift is the focused column's only: b dragged while a is
        // focused grows from its leading edge, which stays put.
        const ResizeOutcome unfocused = resizeOneWindow(quarterColumns, three, QStringLiteral("app|a"),
                                                        QStringLiteral("app|b"), EdgeDrag{-100, 0, 0, 0});
        QVERIFY(!unfocused.applied.isEmpty() && unfocused.held);
        QCOMPARE(Ax::mainPos(unfocused.applied), 300);
        QCOMPARE(Ax::mainLen(unfocused.settled), 400);
        QCOMPARE(Ax::mainPos(unfocused.settled), Ax::mainPos(unfocused.applied));
    }

    // Only the FOCUSED column centres, so a column beside it takes a drag of
    // one edge as it is. Its low edge still lands at the release, since it
    // packs against the centred column, which did not move.
    void oneEdgeResizeOfAnUnfocusedColumnIsTakenAsItIs()
    {
        const ResizeOutcome outcome = resizeOneWindow(
            [](ScrollTestUtils::StubScrollSettings* settings) {
                // 300px columns, so the lead neighbour of the centred one is
                // fully on screen and is committed at its true rect.
                settings->widthValue = 0.25;
            },
            {QStringLiteral("app|a"), QStringLiteral("app|b"), QStringLiteral("app|c")}, QStringLiteral("app|b"),
            QStringLiteral("app|a"), EdgeDrag{-100, 0, 0, 0});
        QVERIFY2(!outcome.applied.isEmpty() && outcome.held, "precondition: a is committed before the resize");
        QCOMPARE(Ax::mainLen(outcome.applied), 300);
        QVERIFY2(Ax::mainPos(outcome.applied) >= 100, "precondition: a sits fully on screen beside the centred b");

        QCOMPARE(Ax::mainLen(outcome.settled), 400);
        QCOMPARE(Ax::mainPos(outcome.settled), Ax::mainPos(outcome.applied) - 100);
        QCOMPARE(Ax::mainEnd(outcome.settled), Ax::mainEnd(outcome.applied));
        QVERIFY(isFixedWidth(outcome.width, 400));
    }

    // The twin across the strip: with centerShortColumns on, a column that
    // shows one rect and comes out short is centred across, so a drag of one
    // of its edges lands that edge at the release the same way. A stack of
    // several tiles takes the drag as it is, since the drag also moves the
    // other tiles' shares and no single doubling lands the edge.
    void oneEdgeResizeOfAShortColumnLandsTheDraggedEdgeAtTheRelease()
    {
        const QString first = QStringLiteral("app|t0");
        // Opens @p tiles windows into one column (tabbed when @p tabbed),
        // focuses the first and moves its top (cross low) and bottom (cross
        // high) edges by @p lowDrag and @p highDrag.
        const auto resized = [&first](bool centerShort, int tiles, int lowDrag, int highDrag, bool tabbed = false) {
            QStringList windows;
            for (int i = 0; i < tiles; ++i) {
                windows.append(QStringLiteral("app|t%1").arg(i));
            }
            return resizeOneWindow(
                [centerShort](ScrollTestUtils::StubScrollSettings* settings) {
                    settings->centerShortColumns = centerShort;
                    settings->insertPosition =
                        static_cast<int>(PhosphorScrollEngine::ScrollInsertPosition::IntoActiveColumn);
                },
                windows, first, first, EdgeDrag{0, 0, lowDrag, highDrag}, tabbed);
        };

        // A lone tile filling the 800px cross extent, its bottom edge dragged
        // up 100px: it takes 600, and centred its bottom edge is at the
        // release.
        const ResizeOutcome shrunk = resized(true, 1, 0, -100);
        QVERIFY2(!shrunk.applied.isEmpty() && shrunk.held, "precondition: the tile is committed before the resize");
        QCOMPARE(Ax::crossLen(shrunk.applied), 800);
        QCOMPARE(Ax::crossLen(shrunk.settled), 600);
        QCOMPARE(Ax::crossEnd(shrunk.settled), Ax::crossEnd(shrunk.applied) - 100);
        QVERIFY(isFixedHeight(shrunk.height, 600));

        // The top edge the same way: dragged down 100px, the tile starts
        // where it was let go.
        const ResizeOutcome fromTop = resized(true, 1, 100, 0);
        QVERIFY(!fromTop.applied.isEmpty());
        QCOMPARE(Ax::crossLen(fromTop.settled), 600);
        QCOMPARE(Ax::crossPos(fromTop.settled), Ax::crossPos(fromTop.applied) + 100);

        // Dragged past the work area, the doubled extent stops at it in the
        // height the tile keeps, not only in the rect this relayout clamps.
        const ResizeOutcome tall = resized(true, 1, 0, 100);
        QVERIFY(!tall.applied.isEmpty() && tall.held);
        QCOMPARE(Ax::crossLen(tall.settled), 800);
        QVERIFY2(isFixedHeight(tall.height, 800),
                 qPrintable(QStringLiteral("the stored height must be the work area's 800, got kind %1 at %2px")
                                .arg(static_cast<int>(tall.height.kind))
                                .arg(tall.height.fixedPx)));

        // Not centred, the frame is the answer.
        const ResizeOutcome plain = resized(false, 1, 0, -100);
        QVERIFY(!plain.applied.isEmpty());
        QCOMPARE(Ax::crossLen(plain.settled), 700);

        // A tabbed column shows one rect, its tabs sharing it, so a drag of the
        // shown tab's edge lands like the lone tile's.
        const ResizeOutcome tabbed = resized(true, 2, 0, -100, /*tabbed=*/true);
        QVERIFY2(!tabbed.applied.isEmpty(), "precondition: the shown tab is committed before the resize");
        QCOMPARE(Ax::crossLen(tabbed.applied), 800);
        QCOMPARE(Ax::crossLen(tabbed.settled), 600);
        QCOMPARE(Ax::crossEnd(tabbed.settled), Ax::crossEnd(tabbed.applied) - 100);

        // A stack of two takes the drag as it is.
        const ResizeOutcome stacked = resized(true, 2, 0, -100);
        QVERIFY(!stacked.applied.isEmpty());
        QCOMPARE(Ax::crossLen(stacked.settled), Ax::crossLen(stacked.applied) - 100);
    }
};

QTEST_GUILESS_MAIN(TestScrollEngineBoundary)
#include "test_scrollengine_boundary.moc"
