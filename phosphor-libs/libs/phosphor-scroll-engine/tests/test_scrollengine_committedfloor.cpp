// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

/**
 * @file test_scrollengine_committedfloor.cpp
 *
 * A client that commits a frame other than the tile it was offered
 * (ScrollEngine::reportCommittedSize): the floor a wider answer holds its
 * column to, the give-back a narrower one makes and the pin that settles a
 * client that will not answer one offer the same way twice, the peek-only
 * memory that keeps protecting the screen-edge peek after the floor is
 * satisfied, the oversized verdict a floor or a cross answer past the work
 * area triggers, every path that drops the floor (satisfaction in every
 * desktop, a user resize, a lowered minimum, a work-area shrink, close,
 * handoff, a cross-output or cross-desktop move, the last membership going,
 * the aliveness prune) and every one that keeps it (float round trips, the
 * fullscreen hold, a cancelled drag, a drag reorder, consume, Retile, the
 * evacuee park). Plus the strip-level verbs whose honesty the floor tests:
 * the preset cycles and minimize-width.
 */

#include <QCoreApplication>
#include <QObject>
#include <QSignalSpy>
#include <QTest>

#include <PhosphorScrollEngine/IScrollSettings.h>
#include <PhosphorScrollEngine/ScrollEngine.h>
#include <PhosphorScrollEngine/ScrollState.h>
#include <PhosphorScrollEngine/ScrollStrip.h>
#include <PhosphorScrollEngine/ScrollTypes.h>

#include "scrollstriptestutils.h"
#include "scrollstubsettings.h"

#include <memory>

using namespace PhosphorScrollEngine;
namespace Ax = ScrollTestUtils::Ax;
using ScrollTestUtils::defaultScreenRect;
using ScrollTestUtils::GeometryFn;
using ScrollTestUtils::makeProviderEngine;
using ScrollTestUtils::StubScrollSettings;

namespace {

const QString kS1 = QStringLiteral("S1");
const QString kS2 = QStringLiteral("S2");
const QString kA = QStringLiteral("app|a");
const QString kB = QStringLiteral("app|b");
const QString kC = QStringLiteral("app|c");
const QString kD1 = QStringLiteral("app|d1");
const QString kSticky = QStringLiteral("app|sticky");

/// A size in ROLE terms, transposed with the arm into the physical size the
/// compositor reports.
QSize roleSize(int main, int cross)
{
    return Ax::t(QSize(main, cross));
}

/// Quarter-width (300px) columns that never centre, so a column's extent is
/// a clean function of its width and its floor. Minimum sizes are respected
/// unless @p respectMinimumSize says otherwise, since that is the setting the
/// committed floor rides; the tab indicator is off so nothing reserves.
StubScrollSettings* makeFloorSettings(QObject* owner, bool respectMinimumSize = true)
{
    auto* settings = new StubScrollSettings(owner);
    settings->widthValue = 0.25;
    settings->centerFocused = static_cast<int>(CenterFocusedColumn::Never);
    settings->respectMinimumSize = respectMinimumSize;
    settings->tabIndicatorEnabled = false;
    return settings;
}

ScrollEngine* engineWith(QObject* owner, StubScrollSettings* settings, const QSet<QString>& screens,
                         GeometryFn screenGeometry = {}, GeometryFn availableGeometry = {})
{
    ScrollEngine* engine = makeProviderEngine(owner, screens, screenGeometry, availableGeometry);
    engine->setEngineSettings(settings);
    engine->refreshConfigFromSettings();
    return engine;
}

/// The floor fixture: a and b side by side at 300px each, a focused, so the
/// width verbs act on a.
ScrollEngine* makeFloorEngine(QObject* owner, StubScrollSettings** outSettings = nullptr,
                              bool respectMinimumSize = true, GeometryFn screenGeometry = {},
                              GeometryFn availableGeometry = {})
{
    StubScrollSettings* settings = makeFloorSettings(owner, respectMinimumSize);
    ScrollEngine* engine = engineWith(owner, settings, {kS1}, screenGeometry, availableGeometry);
    engine->windowOpened(kA, kS1, 0, 0);
    engine->windowOpened(kB, kS1, 0, 0);
    engine->windowFocused(kA, kS1);
    engine->retile(kS1);
    QCoreApplication::processEvents();
    if (outSettings) {
        *outSettings = settings;
    }
    return engine;
}

/// The extent along the strip of the column holding @p windowId on
/// @p screenId's current strip, as the engine resolves it (floors included),
/// or -1 when that strip does not hold the window.
int extentOf(ScrollEngine* engine, const QString& screenId, const QString& windowId)
{
    const ScrollStripModel model = engine->stripModelForScreen(screenId);
    for (const ScrollStripModelColumn& column : model.columns) {
        for (const ScrollStripModelTile& tile : column.tiles) {
            if (tile.windowId == windowId) {
                return column.extentPx;
            }
        }
    }
    return -1;
}

int extentOf(ScrollEngine* engine, const QString& windowId)
{
    return extentOf(engine, kS1, windowId);
}

/// @p windowId answers the tile it was last applied with @p main along the
/// strip and its cross extent moved by @p crossDelta, and the retile that
/// answer schedules runs.
void answer(ScrollEngine* engine, const QString& windowId, int main, int crossDelta = 0)
{
    const QRect offered = engine->lastManagedRect(windowId);
    QVERIFY2(offered.isValid(), "precondition: the window holds an applied tile to answer");
    engine->reportCommittedSize(windowId, offered.size(), roleSize(main, Ax::crossLen(offered) + crossDelta));
    QCoreApplication::processEvents();
}

ScrollState* stateOf(ScrollEngine* engine, const QString& screenId = kS1)
{
    return static_cast<ScrollState*>(engine->stateForScreen(screenId));
}

/// Desktop fixture: d1 and the sticky window on desktop 1 of S1, the sticky
/// one focused.
ScrollEngine* makeDesktopFloorEngine(QObject* owner)
{
    ScrollEngine* engine = engineWith(owner, makeFloorSettings(owner), {kS1});
    engine->setCurrentDesktopForScreen(kS1, 1);
    engine->windowOpened(kD1, kS1, 0, 0);
    engine->windowOpened(kSticky, kS1, 0, 0);
    engine->windowFocused(kSticky, kS1);
    engine->retile(kS1);
    QCoreApplication::processEvents();
    return engine;
}

void switchTo(ScrollEngine* engine, int desktop)
{
    engine->setCurrentDesktopForScreen(kS1, desktop);
    engine->setActiveScreens({kS1});
    QCoreApplication::processEvents();
}

/// kSticky on every desktop, everything else on desktop 1 only.
PhosphorEngine::DesktopSpanQuery stickyOnly()
{
    return [](const QString& windowId) {
        PhosphorEngine::DesktopSpan span;
        span.known = true;
        span.sticky = (windowId == kSticky);
        if (!span.sticky) {
            span.desktops = {1};
        }
        return span;
    };
}

/// kSticky on @p desktop alone, everything else on desktop 1.
PhosphorEngine::DesktopSpanQuery stickyMovedTo(int desktop)
{
    return [desktop](const QString& windowId) {
        PhosphorEngine::DesktopSpan span;
        span.known = true;
        span.desktops = {windowId == kSticky ? desktop : 1};
        return span;
    };
}

void adopt(ScrollEngine* engine, const PhosphorEngine::DesktopSpanQuery& span)
{
    engine->reconcileDesktopMemberships(kS1, span);
    QCoreApplication::processEvents();
}

/// Half-width (600px) columns under Always centering: centring b leaves a
/// peeking 300px of its column past the lead screen edge, which is what the
/// peek-floor cases need.
ScrollEngine* makeStraddleEngine(QObject* owner)
{
    auto* settings = makeFloorSettings(owner);
    settings->widthValue = 0.5;
    settings->centerFocused = static_cast<int>(CenterFocusedColumn::Always);
    ScrollEngine* engine = engineWith(owner, settings, {kS1});
    engine->windowOpened(kA, kS1, 0, 0);
    engine->windowOpened(kB, kS1, 0, 0);
    engine->windowOpened(kC, kS1, 0, 0);
    engine->windowFocused(kA, kS1);
    engine->retile(kS1);
    QCoreApplication::processEvents();
    return engine;
}

bool isParked(ScrollEngine* engine, const QString& windowId)
{
    return engine->lastManagedRect(windowId).y() > defaultScreenRect().bottom();
}

} // namespace

class TestScrollEngineCommittedFloor : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase()
    {
        AX_GUARD_SUITE();
    }

    // ── Learning a floor ────────────────────────────────────────────────

    // Some clients answer their column with a frame WIDER along the strip (a
    // game holding a whole-number scale of its native resolution picks the
    // scale from the cross extent alone). Laid out at its intent, that column
    // puts the next one underneath the window.
    void aWiderCommitWidensItsColumnAndMovesTheNeighbour()
    {
        QObject owner;
        ScrollEngine* engine = makeFloorEngine(&owner);
        QCOMPARE(extentOf(engine, kA), 300);

        // Wider along the strip and a little short across it.
        answer(engine, kA, 450, -20);

        QCOMPARE(extentOf(engine, kA), 450);
        const QRect a = engine->lastManagedRect(kA);
        QCOMPARE(Ax::mainLen(a), 450);
        QCOMPARE(Ax::mainPos(engine->lastManagedRect(kB)), Ax::mainEnd(a) + 1);
    }

    // The floor change republishes the strip: placementChanged is what the
    // placement map, the Monitors view and the scroll-cap list wake on, and
    // the report is a layout change with no verb behind it.
    void aFloorChangeAnnouncesThePlacement()
    {
        QObject owner;
        ScrollEngine* engine = makeFloorEngine(&owner);
        const QRect offered = engine->lastManagedRect(kA);
        QSignalSpy placement(engine, &PhosphorEngine::PlacementEngineBase::placementChanged);
        engine->reportCommittedSize(kA, offered.size(), roleSize(450, Ax::crossLen(offered)));
        QCOMPARE(placement.count(), 1);
        QCOMPARE(placement.first().at(0).toString(), kS1);
    }

    // Only an answer to the tile's whole rect says anything about the column.
    // The stored state is read after a retile, so a refusal that recorded a
    // floor and merely skipped scheduling the relayout cannot pass.
    void aCommitAgainstAnythingButTheWholeTileTeachesNothing()
    {
        QObject owner;
        ScrollEngine* engine = makeFloorEngine(&owner);
        const int cross = Ax::crossLen(engine->lastManagedRect(kA));

        // Narrower than the tile along the strip: a peek clamped at the edge.
        engine->reportCommittedSize(kA, roleSize(200, cross), roleSize(450, cross));
        // The tile's main extent without its cross one: a stack tile clamped
        // across the strip, or an offer a newer batch has since replaced.
        engine->reportCommittedSize(kA, roleSize(300, cross - 100), roleSize(450, cross - 100));
        engine->retile(kS1);
        QCoreApplication::processEvents();

        QCOMPARE(extentOf(engine, kA), 300);
        QCOMPARE(extentOf(engine, kB), 300);
    }

    // Fractional scaling rounds a logical size to device pixels and back, so a
    // client that took its tile can commit a pixel past it. That is the offer,
    // not an answer, and plants no floor.
    void anAnswerWithinTheSlackIsTheOffer()
    {
        QObject owner;
        ScrollEngine* engine = makeFloorEngine(&owner);
        answer(engine, kA, 301);
        QCOMPARE(extentOf(engine, kA), 300);
        engine->setColumnWidth(ColumnWidth::makeFixed(200), kS1);
        QCoreApplication::processEvents();
        QCOMPARE(extentOf(engine, kA), 200);
    }

    // The minimum sizes setting gates the floor twice, and each gate is
    // observable on its own. Learning: nothing is recorded while it is off,
    // so turning it on finds no floor. Reading: a floor learned while it is on
    // stops applying when it goes off and applies again when it comes back.
    void theFloorRidesRespectMinimumSizeOnBothEnds()
    {
        {
            QObject owner;
            StubScrollSettings* settings = nullptr;
            ScrollEngine* engine = makeFloorEngine(&owner, &settings, /*respectMinimumSize=*/false);
            answer(engine, kA, 450);
            QCOMPARE(extentOf(engine, kA), 300);
            settings->respectMinimumSize = true;
            engine->refreshConfigFromSettings();
            QCoreApplication::processEvents();
            QCOMPARE(extentOf(engine, kA), 300);
        }
        {
            QObject owner;
            StubScrollSettings* settings = nullptr;
            ScrollEngine* engine = makeFloorEngine(&owner, &settings);
            answer(engine, kA, 450);
            QCOMPARE(extentOf(engine, kA), 450);
            settings->respectMinimumSize = false;
            engine->refreshConfigFromSettings();
            QCoreApplication::processEvents();
            QCOMPARE(extentOf(engine, kA), 300);
            settings->respectMinimumSize = true;
            engine->refreshConfigFromSettings();
            QCoreApplication::processEvents();
            QCOMPARE(extentOf(engine, kA), 450);
        }
    }

    // Nothing is learned while minimum sizes are off, and the compositor
    // reports an answer once per offer, so turning them on has to make a
    // fresh offer for the answer to be learned at all. A strip whose rects
    // did not move would otherwise emit nothing.
    void turningRespectMinimumSizeOnForcesAFreshOffer()
    {
        QObject owner;
        StubScrollSettings* settings = nullptr;
        ScrollEngine* engine = makeFloorEngine(&owner, &settings, /*respectMinimumSize=*/false);
        QSignalSpy tiled(engine, &ScrollEngine::windowsTiled);
        settings->respectMinimumSize = true;
        engine->refreshConfigFromSettings();
        QCoreApplication::processEvents();
        QVERIFY2(tiled.count() > 0, "the batch must be sent although no rect moved");
    }

    // ── Giving a floor back ─────────────────────────────────────────────

    void aNarrowerAnswerGivesTheWideningBack()
    {
        QObject owner;
        ScrollEngine* engine = makeFloorEngine(&owner);
        answer(engine, kA, 450);
        QCOMPARE(extentOf(engine, kA), 450);

        // Narrower than the widened offer, still wider than the column asks
        // for: the floor follows the client down.
        answer(engine, kA, 350);
        QCOMPARE(extentOf(engine, kA), 350);
        QCOMPARE(Ax::mainPos(engine->lastManagedRect(kB)), Ax::mainEnd(engine->lastManagedRect(kA)) + 1);

        // Under the column's own width: the widening goes entirely.
        answer(engine, kA, 250);
        QCOMPARE(extentOf(engine, kA), 300);
    }

    // A client that rounds its size down answers narrower than its column.
    // That is not a floor, so the column still narrows when asked.
    void aNarrowerAnswerNeverSetsAFloor()
    {
        QObject owner;
        ScrollEngine* engine = makeFloorEngine(&owner);
        answer(engine, kA, 290);
        engine->setColumnWidth(ColumnWidth::makeFixed(200), kS1);
        QCoreApplication::processEvents();
        QCOMPARE(extentOf(engine, kA), 200);
    }

    // A client that does not answer one offer the same way twice: the floor
    // it taught is given back, it teaches it again, and following it down a
    // second time would offer it the size that taught the floor and resize
    // its column on every round trip. The re-taught floor is pinned.
    void aFloorReTaughtAfterAGiveBackIsPinned()
    {
        QObject owner;
        ScrollEngine* engine = makeFloorEngine(&owner);
        answer(engine, kA, 450); // offered 300
        QCOMPARE(extentOf(engine, kA), 450);
        answer(engine, kA, 300); // offered 450: given back entirely
        QCOMPARE(extentOf(engine, kA), 300);
        answer(engine, kA, 450); // offered 300 again: re-taught, pinned
        QCOMPARE(extentOf(engine, kA), 450);
        answer(engine, kA, 300); // offered 450: the pin holds
        QCOMPARE(extentOf(engine, kA), 450);
    }

    // ── Satisfaction ────────────────────────────────────────────────────

    void aFloorHoldsUntilTheColumnSatisfiesIt()
    {
        QObject owner;
        ScrollEngine* engine = makeFloorEngine(&owner);
        answer(engine, kA, 450);

        // Narrowing the column cannot narrow a client that never took less.
        engine->setColumnWidth(ColumnWidth::makeFixed(200), kS1);
        QCoreApplication::processEvents();
        QCOMPARE(extentOf(engine, kA), 450);

        // EXACTLY the floor satisfies it: the column offers the client what
        // it last committed without the floor's help, so it is dropped...
        engine->setColumnWidth(ColumnWidth::makeFixed(450), kS1);
        QCoreApplication::processEvents();
        QCOMPARE(extentOf(engine, kA), 450);

        // ...and narrowing again offers the column as asked, instead of
        // reapplying an answer the client gave to a different offer.
        engine->setColumnWidth(ColumnWidth::makeFixed(300), kS1);
        QCoreApplication::processEvents();
        QCOMPARE(extentOf(engine, kA), 300);

        // A client that still will not take the narrower column teaches the
        // floor again with its next answer.
        answer(engine, kA, 450);
        QCOMPARE(extentOf(engine, kA), 450);
    }

    // The floor is the window's, read by every desktop's strip it holds a
    // column in, so it goes only once EVERY one of those columns satisfies
    // it: a column widened on one desktop says nothing about the column the
    // window holds on another.
    void aFloorDropsOnlyWhenEveryDesktopSatisfiesIt()
    {
        QObject owner;
        ScrollEngine* engine = makeDesktopFloorEngine(&owner);
        answer(engine, kSticky, 450);
        QCOMPARE(extentOf(engine, kSticky), 450);

        switchTo(engine, 2);
        adopt(engine, stickyOnly());
        QVERIFY2(engine->heldKeyForWindow(kSticky).has_value(), "precondition: adopted onto desktop 2");
        // The floor learned on desktop 1 holds the desktop-2 column too.
        QCOMPARE(extentOf(engine, kSticky), 450);

        // Satisfied here, not on desktop 1: kept.
        engine->setColumnWidth(ColumnWidth::makeFixed(600), kS1);
        QCoreApplication::processEvents();
        QCOMPARE(extentOf(engine, kSticky), 600);
        switchTo(engine, 1);
        QCOMPARE(extentOf(engine, kSticky), 450);

        // Satisfied on both now: dropped.
        engine->windowFocused(kSticky, kS1);
        engine->setColumnWidth(ColumnWidth::makeFixed(600), kS1);
        QCoreApplication::processEvents();
        engine->setColumnWidth(ColumnWidth::makeFixed(300), kS1);
        QCoreApplication::processEvents();
        QCOMPARE(extentOf(engine, kSticky), 300);
    }

    // Retile writes intents, and a floor is not one: a floored column stays
    // at its floor.
    void retileKeepsTheFloor()
    {
        QObject owner;
        ScrollEngine* engine = makeFloorEngine(&owner);
        answer(engine, kA, 450);
        engine->resetStripToDefaults(kS1);
        QCoreApplication::processEvents();
        QCOMPARE(extentOf(engine, kA), 450);
    }

    // ── The peek floor ──────────────────────────────────────────────────

    // A floored column clamped at the screen edge would hand the client a
    // sliver it answers at full width, so it parks instead of peeking. The
    // floor is learned while the column is WHOLE: a client answering the
    // clamped peek was never offered its column and teaches nothing.
    void aFlooredStraddlerParksInsteadOfPeeking()
    {
        QObject owner;
        ScrollEngine* engine = makeStraddleEngine(&owner);
        QCOMPARE(Ax::mainLen(engine->lastManagedRect(kA)), 600);
        answer(engine, kA, 700);
        QCOMPARE(extentOf(engine, kA), 700);

        engine->windowFocused(kB, kS1);
        QCoreApplication::processEvents();
        QVERIFY2(isParked(engine, kA), "a 300px remainder of a column the client holds 700px wide must park");
    }

    // Without any committed extent the same remainder peeks, clamped. A
    // narrower answer teaches neither a floor nor a peek memory.
    void aNarrowerAnswerLeavesThePeekAlone()
    {
        QObject owner;
        ScrollEngine* engine = makeStraddleEngine(&owner);
        answer(engine, kA, 550);
        engine->windowFocused(kB, kS1);
        QCoreApplication::processEvents();
        QVERIFY(!isParked(engine, kA));
        QVERIFY(Ax::mainLen(engine->lastManagedRect(kA)) < 600);
    }

    // The peek memory outlives the floor: once the column satisfies the floor
    // it is dropped, but the client still answers at that width, and a clamp
    // would still hand it a sliver.
    void aSatisfiedFloorStillProtectsThePeek()
    {
        QObject owner;
        ScrollEngine* engine = makeStraddleEngine(&owner);
        answer(engine, kA, 700);
        engine->setColumnWidth(ColumnWidth::makeFixed(800), kS1); // satisfied: the floor goes
        QCoreApplication::processEvents();
        engine->setColumnWidth(ColumnWidth::makeFixed(600), kS1);
        QCoreApplication::processEvents();
        QCOMPARE(extentOf(engine, kA), 600);

        engine->windowFocused(kB, kS1);
        QCoreApplication::processEvents();
        QVERIFY2(isParked(engine, kA), "the peek memory must survive the floor's satisfaction");
    }

    // A narrower whole-tile answer REPLACES the memory rather than ending it,
    // in exactly the widened-past state: the column outgrew the floor, the
    // client answered narrower, and it still answers wider than a clamp.
    void aNarrowerAnswerReplacesThePeekMemory()
    {
        {
            QObject owner;
            ScrollEngine* engine = makeStraddleEngine(&owner);
            answer(engine, kA, 700);
            engine->setColumnWidth(ColumnWidth::makeFixed(800), kS1);
            QCoreApplication::processEvents();
            answer(engine, kA, 650); // offered 800
            engine->setColumnWidth(ColumnWidth::makeFixed(600), kS1);
            QCoreApplication::processEvents();
            engine->windowFocused(kB, kS1);
            QCoreApplication::processEvents();
            QVERIFY2(isParked(engine, kA), "a 650px memory still protects a 300px remainder");
        }
        {
            QObject owner;
            ScrollEngine* engine = makeStraddleEngine(&owner);
            answer(engine, kA, 700);
            engine->setColumnWidth(ColumnWidth::makeFixed(800), kS1);
            QCoreApplication::processEvents();
            answer(engine, kA, 250); // offered 800: the memory follows it down
            engine->setColumnWidth(ColumnWidth::makeFixed(600), kS1);
            QCoreApplication::processEvents();
            engine->windowFocused(kB, kS1);
            QCoreApplication::processEvents();
            QVERIFY2(!isParked(engine, kA), "a 250px memory does not outgrow a 300px remainder");
        }
    }

    // ── The oversized verdict ───────────────────────────────────────────

    // A floor no column can offer floats the window, the policy a declared
    // minimum past the work area already gets.
    void aFloorPastTheWorkAreaFloatsTheWindow()
    {
        QObject owner;
        ScrollEngine* engine = makeFloorEngine(&owner);
        answer(engine, kA, 1300);
        QVERIFY(engine->isWindowFloatingInScroll(kA));
        QCOMPARE(extentOf(engine, kB), 300);
    }

    // The client bound: an absurd extent is cut down before any sum can
    // overflow, and still floats the window.
    void anAbsurdCommitIsBoundedAndFloats()
    {
        QObject owner;
        ScrollEngine* engine = makeFloorEngine(&owner);
        answer(engine, kA, 1000000000);
        QVERIFY(engine->isWindowFloatingInScroll(kA));
    }

    // An answer longer than the work area ACROSS the strip can never fit any
    // tile, and floats too. A shorter cross overshoot teaches nothing: the
    // stack tile here is offered 400 and answers 500, inside the 800 work
    // area, and stays tiled at its column's width.
    void aCrossAnswerPastTheWorkAreaFloatsTheWindow()
    {
        QObject owner;
        ScrollEngine* engine = makeFloorEngine(&owner);
        engine->consumeWindowIntoColumn(kS1); // b joins a's column
        engine->retile(kS1);
        QCoreApplication::processEvents();
        const QRect offered = engine->lastManagedRect(kA);
        QCOMPARE(Ax::crossLen(offered), 400);

        engine->reportCommittedSize(kA, offered.size(), roleSize(300, 500));
        QCoreApplication::processEvents();
        QVERIFY(!engine->isWindowFloatingInScroll(kA));
        QCOMPARE(extentOf(engine, kA), 300);

        engine->reportCommittedSize(kA, offered.size(), roleSize(300, 900));
        QCoreApplication::processEvents();
        QVERIFY(engine->isWindowFloatingInScroll(kA));
    }

    // A tab indicator placed within the column takes its thickness out of the
    // tile's extent along the strip. The floor holds the TILE, so the column
    // grows by the reservation too; and a floor that fits the work area alone
    // but not with the reservation is one the column can never offer.
    void aTabbedColumnHoldsItsTileToTheFloor()
    {
        const auto makeTabbed = [](QObject* owner) {
            auto* settings = makeFloorSettings(owner);
            settings->tabIndicatorEnabled = true;
            settings->tabIndicatorPlaceWithinColumn = true;
            settings->tabIndicatorHideWhenSingleTab = false;
            settings->tabIndicatorWidth = 4;
            settings->tabIndicatorGap = 2;
            // The edge whose thickness eats the MAIN axis on this arm.
            settings->tabIndicatorPosition =
                static_cast<int>(Ax::vertical() ? TabIndicatorPosition::Top : TabIndicatorPosition::Left);
            ScrollEngine* engine = engineWith(owner, settings, {kS1});
            engine->windowOpened(kA, kS1, 0, 0);
            engine->windowOpened(kB, kS1, 0, 0);
            engine->windowFocused(kA, kS1);
            engine->consumeWindowIntoColumn(kS1);
            engine->toggleColumnTabbed(kS1);
            engine->retile(kS1);
            QCoreApplication::processEvents();
            return engine;
        };
        {
            QObject owner;
            ScrollEngine* engine = makeTabbed(&owner);
            QCOMPARE(Ax::mainLen(engine->lastManagedRect(kA)), 294); // 300 less the 6px reservation
            answer(engine, kA, 450);
            QCOMPARE(extentOf(engine, kA), 456);
        }
        {
            QObject owner;
            ScrollEngine* engine = makeTabbed(&owner);
            answer(engine, kA, 1197);
            QVERIFY2(engine->isWindowFloatingInScroll(kA), "1197 plus the 6px reservation cannot fit 1200");
        }
    }

    // A work area that shrinks under a held floor leaves it past the work
    // area with no answer able to revise it. It is dropped, so the column does
    // not jump back to the stale floor when the area grows again. The shrunk
    // area keeps the strip's orientation (still longer along the strip than
    // across it), since an area that turned the other way would re-resolve
    // the axis and read the other orientation's floor instead.
    void aWorkAreaShrinkDropsAFloorItCanNoLongerHold()
    {
        QObject owner;
        auto available = std::make_shared<QRect>(defaultScreenRect());
        ScrollEngine* engine = makeFloorEngine(
            &owner, nullptr, true,
            [](const QString&) {
                return defaultScreenRect();
            },
            [available](const QString&) {
                return *available;
            });
        answer(engine, kA, 450);
        QCOMPARE(extentOf(engine, kA), 450);

        *available = Ax::t(QRect(0, 0, 400, 300));
        engine->retile(kS1);
        QCoreApplication::processEvents();
        QCOMPARE(extentOf(engine, kA), 100);

        *available = defaultScreenRect();
        engine->retile(kS1);
        QCoreApplication::processEvents();
        QCOMPARE(extentOf(engine, kA), 300);
    }

    // ── Other drops ─────────────────────────────────────────────────────

    // The user's own resize along the strip settles the extent there, so the
    // floor is superseded: without the drop it kept the column wider than the
    // size the user released it at.
    void aUserResizeAlongTheStripDropsTheFloor()
    {
        QObject owner;
        ScrollEngine* engine = makeFloorEngine(&owner);
        answer(engine, kA, 450);
        const QRect applied = engine->lastManagedRect(kA);
        QRect settled = Ax::t(applied);
        settled.setWidth(350);
        settled = Ax::t(settled);
        engine->onWindowResized(kA, applied, settled, kS1);
        QCoreApplication::processEvents();
        QCOMPARE(extentOf(engine, kA), 350);

        engine->setColumnWidth(ColumnWidth::makeFixed(200), kS1);
        QCoreApplication::processEvents();
        QCOMPARE(extentOf(engine, kA), 200);
    }

    // The resize's axes are read off the gesture's own start frame. A client
    // that committed wider than its offer sits at a frame the applied rect
    // does not describe, and a drag of its cross edge alone must not read as
    // a main-axis resize that pins a proportional width to Fixed pixels.
    void aCrossEdgeDragOfAWiderClientKeepsTheWidthIntent()
    {
        QObject owner;
        ScrollEngine* engine = makeFloorEngine(&owner);
        const QRect applied = engine->lastManagedRect(kA);
        QRect frame = Ax::t(applied);
        frame.setWidth(320); // the client's own answer, not reported
        const QRect start = Ax::t(frame);
        frame.setHeight(600);
        const QRect settled = Ax::t(frame);
        engine->onWindowResized(kA, start, settled, kS1);
        QCoreApplication::processEvents();

        ScrollState* state = stateOf(engine);
        QVERIFY(state);
        const int col = state->strip().columnOfWindow(kA);
        QVERIFY(col >= 0);
        QCOMPARE(state->strip().columns().at(col).width.kind, ColumnWidth::Proportion);
    }

    // A LOWERED declared minimum along the strip says the client can now be
    // narrower there, so the floor it taught at the old minimum goes. A raised
    // one takes nothing.
    void aLoweredMinimumDropsTheFloor()
    {
        QObject owner;
        ScrollEngine* engine = makeFloorEngine(&owner);
        const QSize min100 = roleSize(100, 0);
        engine->windowMinSizeUpdated(kA, min100.width(), min100.height());
        QCoreApplication::processEvents();
        answer(engine, kA, 450);

        const QSize min150 = roleSize(150, 0);
        engine->windowMinSizeUpdated(kA, min150.width(), min150.height());
        QCoreApplication::processEvents();
        QCOMPARE(extentOf(engine, kA), 450);

        const QSize min50 = roleSize(50, 0);
        engine->windowMinSizeUpdated(kA, min50.width(), min50.height());
        QCoreApplication::processEvents();
        QCOMPARE(extentOf(engine, kA), 300);
    }

    void closingTheWindowDropsTheFloor()
    {
        QObject owner;
        ScrollEngine* engine = makeFloorEngine(&owner);
        answer(engine, kA, 450);
        engine->windowClosed(kA);
        engine->windowOpened(kA, kS1, 0, 0);
        QCoreApplication::processEvents();
        QCOMPARE(extentOf(engine, kA), 300);
    }

    void aHandoffDropsTheFloor()
    {
        QObject owner;
        ScrollEngine* engine = makeFloorEngine(&owner);
        answer(engine, kA, 450);
        engine->handoffRelease(kA);
        engine->windowOpened(kA, kS1, 0, 0);
        QCoreApplication::processEvents();
        QCOMPARE(extentOf(engine, kA), 300);
    }

    // The aliveness prune is the backstop for a window that died with no
    // close reaching the engine.
    void thePruneDropsADeadWindowsFloor()
    {
        QObject owner;
        ScrollEngine* engine = makeFloorEngine(&owner);
        answer(engine, kA, 450);
        engine->pruneStaleWindows({kB});
        QCoreApplication::processEvents();
        engine->windowOpened(kA, kS1, 0, 0);
        QCoreApplication::processEvents();
        QCOMPARE(extentOf(engine, kA), 300);
    }

    // What the client taught answered the source output's tiles; on the
    // target it starts unfloored.
    void aCrossOutputMoveDropsTheFloor()
    {
        ScrollTestUtils::TrailNeighbourResolver resolver;
        QObject owner;
        ScrollEngine* engine = engineWith(&owner, makeFloorSettings(&owner), {kS1, kS2});
        engine->setCrossSurfaceResolver(&resolver);
        engine->windowOpened(kA, kS1, 0, 0);
        engine->windowOpened(kB, kS2, 0, 0);
        engine->retile(kS1);
        QCoreApplication::processEvents();
        answer(engine, kA, 450);
        QCOMPARE(extentOf(engine, kS1, kA), 450);

        PhosphorEngine::NavigationContext ctx;
        ctx.screenId = kS1;
        engine->moveFocusedInDirection(Ax::navTrail(), ctx);
        QCoreApplication::processEvents();
        QCOMPARE(extentOf(engine, kS2, kA), 300);
        engine->setCrossSurfaceResolver(nullptr);
    }

    // A window that moved desktop re-enters as a migration, which starts the
    // new context unfloored.
    void aCrossDesktopMigrationDropsTheFloor()
    {
        QObject owner;
        ScrollEngine* engine = makeDesktopFloorEngine(&owner);
        answer(engine, kSticky, 450);
        engine->setCurrentDesktopForScreen(kS1, 2);
        engine->windowOpened(kSticky, kS1, 0, 0);
        QCoreApplication::processEvents();
        QCOMPARE(engine->heldKeyForWindow(kSticky)->desktop, 2);
        QCOMPARE(extentOf(engine, kSticky), 300);
    }

    // Releasing one desktop's column leaves the floor for the desktops that
    // still hold one; releasing the last takes it.
    void onlyTheLastMembershipTakesTheFloor()
    {
        QObject owner;
        ScrollEngine* engine = makeDesktopFloorEngine(&owner);
        answer(engine, kSticky, 450);
        switchTo(engine, 2);
        adopt(engine, stickyOnly());

        adopt(engine, stickyMovedTo(2)); // desktop 1's column goes
        QVERIFY(!engine->managedWindowOrder(kS1).isEmpty());
        QCOMPARE(extentOf(engine, kSticky), 450);

        adopt(engine, stickyMovedTo(3)); // desktop 2's column goes too, the last one
        QVERIFY(!engine->isWindowTracked(kSticky));
        engine->windowOpened(kSticky, kS1, 0, 0);
        QCoreApplication::processEvents();
        QCOMPARE(extentOf(engine, kSticky), 300);
    }

    // ── What keeps a floor ──────────────────────────────────────────────

    // A float round trip (a user float, or minimize, which the effect reports
    // as one) is a return, not a migration: the column comes back held to
    // what the client already said, like it comes back at its declared
    // minimum.
    void aFloorSurvivesAFloatRoundTrip()
    {
        QObject owner;
        ScrollEngine* engine = makeFloorEngine(&owner);
        answer(engine, kA, 450);
        engine->setWindowFloat(kA, true, kS1);
        engine->setWindowFloat(kA, false, kS1);
        engine->retile(kS1);
        QCoreApplication::processEvents();
        QCOMPARE(extentOf(engine, kA), 450);
    }

    void aFloorSurvivesTheFullscreenHold()
    {
        QObject owner;
        ScrollEngine* engine = makeFloorEngine(&owner);
        answer(engine, kA, 450);
        QVERIFY(engine->setWindowFullscreenFloat(kA, true, kS1));
        QVERIFY(engine->setWindowFullscreenFloat(kA, false, kS1));
        QCoreApplication::processEvents();
        QVERIFY(!engine->isWindowFloatingInScroll(kA));
        QCOMPARE(extentOf(engine, kA), 450);
    }

    // The compositor's return re-runs the oversized verdict: a client that
    // pinned a minimum past the work area during the hold floats again rather
    // than sitting tiled at a size no column can honour.
    void theHoldReturnReRunsTheOversizedVerdict()
    {
        QObject owner;
        ScrollEngine* engine = makeFloorEngine(&owner);
        QVERIFY(engine->setWindowFullscreenFloat(kA, true, kS1));
        const QSize oversized = roleSize(1300, 0);
        engine->windowMinSizeUpdated(kA, oversized.width(), oversized.height());
        QVERIFY(engine->setWindowFullscreenFloat(kA, false, kS1));
        QCoreApplication::processEvents();
        QVERIFY(engine->isWindowFloatingInScroll(kA));
    }

    // A user's own unfloat stays manual: the window comes back tiled whatever
    // its minimum says, and only a later report can float it again.
    void aUserUnfloatStaysManual()
    {
        QObject owner;
        ScrollEngine* engine = makeFloorEngine(&owner);
        engine->setWindowFloat(kA, true, kS1);
        const QSize oversized = roleSize(1300, 0);
        engine->windowMinSizeUpdated(kA, oversized.width(), oversized.height());
        engine->setWindowFloat(kA, false, kS1);
        QCoreApplication::processEvents();
        QVERIFY(!engine->isWindowFloatingInScroll(kA));
    }

    // A cancelled drag is a round trip too, and the drop indicator draws the
    // slot at the floor the drop will land at.
    void aFloorSurvivesACancelledDragAndShapesTheIndicator()
    {
        QObject owner;
        ScrollEngine* engine = makeFloorEngine(&owner);
        answer(engine, kA, 450);
        QVERIFY(engine->beginDragInsertPreview(kA, kS1));
        const auto target = engine->computeDragInsertTargetAtPoint(kS1, Ax::point(1150, 400));
        QVERIFY(target.isValid());
        engine->updateDragInsertPreview(target);
        QCOMPARE(Ax::mainLen(engine->dragInsertIndicatorRect(kS1)), 450);
        engine->cancelDragInsertPreview();
        engine->retile(kS1);
        QCoreApplication::processEvents();
        QCOMPARE(extentOf(engine, kA), 450);
    }

    // A drag reorder on the same output keeps the floor: the window is in the
    // same strip, answering the same column.
    void aFloorSurvivesADragReorder()
    {
        QObject owner;
        ScrollEngine* engine = makeFloorEngine(&owner);
        answer(engine, kA, 450);
        QVERIFY(engine->beginDragInsertPreview(kA, kS1));
        const auto target = engine->computeDragInsertTargetAtPoint(kS1, Ax::point(1150, 400));
        QVERIFY(target.isValid());
        engine->updateDragInsertPreview(target);
        engine->commitDragInsertPreview();
        QCoreApplication::processEvents();
        QCOMPARE(engine->managedWindowOrder(kS1), (QStringList{kB, kA}));
        QCOMPARE(extentOf(engine, kA), 450);
    }

    // Consume carries the floor with the window into its new column, which
    // the floor then holds for every tile there.
    void aFloorTravelsWithAConsumedWindow()
    {
        QObject owner;
        ScrollEngine* engine = makeFloorEngine(&owner);
        answer(engine, kA, 450);
        engine->consumeOrExpelWindow(+1, kS1); // a, alone in its column, joins b's
        QCoreApplication::processEvents();
        ScrollState* state = stateOf(engine);
        QVERIFY(state);
        QCOMPARE(state->strip().columnCount(), 1);
        QCOMPARE(extentOf(engine, kB), 450);
    }

    // The KWin sleep/unplug park is a round trip like a float: the window
    // comes back held to its floor.
    void theEvacueeParkCarriesTheFloor()
    {
        QObject owner;
        ScrollEngine* engine = engineWith(&owner, makeFloorSettings(&owner), {kS1});
        engine->setCurrentDesktopForScreen(kS1, 1);
        engine->windowOpened(kA, kS1, 0, 0);
        engine->windowOpened(kB, kS1, 0, 0);
        engine->retile(kS1);
        QCoreApplication::processEvents();
        answer(engine, kA, 450);
        QCOMPARE(extentOf(engine, kA), 450);

        engine->parkOutput(kS1);
        engine->pruneStatesForRemovedScreen(kS1);
        QVERIFY(!engine->isWindowTracked(kA));
        engine->setActiveScreens({kS1});
        engine->setCurrentDesktopForScreen(kS1, 1);
        QCoreApplication::processEvents();
        QVERIFY(engine->readoptParked(kA, kS1, kS1));
        QVERIFY(engine->readoptParked(kB, kS1, kS1));
        QCoreApplication::processEvents();
        QCOMPARE(extentOf(engine, kA), 450);
    }

    // ── Orientation ─────────────────────────────────────────────────────

    // A floor learned along one axis is not a floor along the other. A
    // rotation flips the strip's axis, and the width the client answered says
    // nothing about its height; the rotation back finds the floor again.
    void aRotationNeverReadsAWidthFloorAsAHeightFloor()
    {
        QObject owner;
        auto geometry = std::make_shared<QRect>(defaultScreenRect());
        ScrollEngine* engine = makeFloorEngine(&owner, nullptr, true, [geometry](const QString&) {
            return *geometry;
        });
        answer(engine, kA, 450);
        QCOMPARE(extentOf(engine, kA), 450);

        *geometry = Ax::t(QRect(0, 0, 800, 1200)); // the other aspect
        engine->retile(kS1);
        QCoreApplication::processEvents();
        QCOMPARE(extentOf(engine, kA), extentOf(engine, kB));

        *geometry = defaultScreenRect();
        engine->retile(kS1);
        QCoreApplication::processEvents();
        QCOMPARE(extentOf(engine, kA), 450);
    }

    // A sticky window's desktops can run different axes. The floor it taught
    // on a desktop running one way never holds its column on a desktop
    // running the other.
    void aDesktopRunningTheOtherWayReadsNoFloor()
    {
        QObject owner;
        ScrollEngine* engine = makeDesktopFloorEngine(&owner);
        answer(engine, kSticky, 450);

        switchTo(engine, 2);
        QVariantMap otherAxis;
        otherAxis.insert(ScrollPerScreenKeys::stripAxis(), Ax::vertical() ? 1 : 2);
        engine->applyPerScreenConfig(kS1, otherAxis);
        adopt(engine, stickyOnly());
        QCOMPARE(engine->stripAxisForScreen(kS1) == Ax::stripAxis(), false);
        // A lone column at the 0.25 default along the 800px axis.
        QCOMPARE(extentOf(engine, kSticky), 200);

        switchTo(engine, 1);
        QCOMPARE(extentOf(engine, kSticky), 450);
    }

    // ── Context ─────────────────────────────────────────────────────────

    // A report for a window whose strip is in the BACKGROUND is judged
    // against that strip under its own context: learned, laid out when the
    // desktop returns.
    void aBackgroundWindowsAnswerIsLearnedForItsOwnStrip()
    {
        QObject owner;
        ScrollEngine* engine = engineWith(&owner, makeFloorSettings(&owner), {kS1});
        engine->setCurrentDesktopForScreen(kS1, 1);
        engine->windowOpened(kD1, kS1, 0, 0);
        engine->windowOpened(kA, kS1, 0, 0);
        engine->retile(kS1);
        QCoreApplication::processEvents();
        switchTo(engine, 2);
        answer(engine, kA, 450);
        switchTo(engine, 1);
        QCOMPARE(extentOf(engine, kA), 450);
    }

    // With smart gaps the outer gaps depend on a strip's column count. A
    // background strip's params count ITS columns, not the strip in view, or
    // the exact whole-tile compare refuses a genuine answer from it.
    void aBackgroundStripsSmartGapsCountItsOwnColumns()
    {
        QObject owner;
        auto* settings = makeFloorSettings(&owner);
        settings->smartGaps = true;
        settings->outerGap = 20;
        ScrollEngine* engine = engineWith(&owner, settings, {kS1});
        engine->setCurrentDesktopForScreen(kS1, 1);
        engine->windowOpened(kD1, kS1, 0, 0);
        engine->windowOpened(kA, kS1, 0, 0);
        engine->retile(kS1);
        QCoreApplication::processEvents();
        QCOMPARE(Ax::mainLen(engine->lastManagedRect(kA)), 290); // 0.25 of the 1160px inset area

        switchTo(engine, 2);
        engine->windowOpened(kC, kS1, 0, 0); // one column: smart gaps drop desktop 2's outer gaps
        QCoreApplication::processEvents();
        answer(engine, kA, 450);
        switchTo(engine, 1);
        QCOMPARE(extentOf(engine, kA), 450);
    }

    // ── Refusals ────────────────────────────────────────────────────────

    void refusedForADraggedFloatingOrEdgeMaximizedWindow()
    {
        {
            // A frame under an interactive move is motion, not an answer.
            QObject owner;
            ScrollEngine* engine = makeFloorEngine(&owner);
            engine->setInteractiveDragWindow(kA);
            answer(engine, kA, 450);
            engine->setInteractiveDragWindow(QString());
            engine->retile(kS1);
            QCoreApplication::processEvents();
            QCOMPARE(extentOf(engine, kA), 300);
        }
        {
            // A float has no column to answer.
            QObject owner;
            ScrollEngine* engine = makeFloorEngine(&owner);
            const QRect offered = engine->lastManagedRect(kA);
            engine->setWindowFloat(kA, true, kS1);
            engine->reportCommittedSize(kA, offered.size(), roleSize(450, Ax::crossLen(offered)));
            engine->setWindowFloat(kA, false, kS1);
            QCoreApplication::processEvents();
            QCOMPARE(extentOf(engine, kA), 300);
        }
        {
            // A maximized-to-edges column's extent is declared and reads no
            // floor, so nothing its client says about it is learned.
            QObject owner;
            ScrollEngine* engine = makeFloorEngine(&owner);
            QVERIFY(engine->toggleMaximizeToEdges(kS1, kA));
            QCoreApplication::processEvents();
            answer(engine, kA, Ax::mainLen(engine->lastManagedRect(kA)) + 150);
            QVERIFY(!engine->isWindowFloatingInScroll(kA));
            QVERIFY(engine->toggleMaximizeToEdges(kS1, kA));
            QCoreApplication::processEvents();
            QCOMPARE(extentOf(engine, kA), 300);
        }
    }

    // ── The rect a close keeps ──────────────────────────────────────────

    // The close keeps the last-applied rect for the daemon's close capture,
    // and the post-capture hook ends it once no strip holds the window. A
    // live window's rect is the emit gate's memory and stays.
    void theClosedRectEndsAfterTheCapture()
    {
        QObject owner;
        ScrollEngine* engine = makeFloorEngine(&owner);
        engine->windowClosed(kA);
        QVERIFY(!engine->lastManagedRect(kA).isNull());
        engine->forgetClosedFullscreenHold(kA);
        QVERIFY(engine->lastManagedRect(kA).isNull());

        const QRect live = engine->lastManagedRect(kB);
        QVERIFY(!live.isNull());
        engine->forgetClosedFullscreenHold(kB);
        QCOMPARE(engine->lastManagedRect(kB), live);
    }

    // ── Strip-level verbs under a floor ─────────────────────────────────

    // A backward cycle from a floored column would write a preset the floor
    // still outranks and report a resize nobody sees. It refuses and leaves
    // the intent; a forward press past the floor still moves.
    void aCycleUnderAFloorRefusesAnEntryThatRendersTheSame()
    {
        ScrollLayoutParams params = ScrollTestUtils::defaultParams();
        params.committedFloor = [](const QString& windowId) {
            return windowId == QLatin1String("a") ? Ax::t(QSize(700, 0)) : QSize(0, 0);
        };
        ScrollStrip strip;
        const ColumnWidth third = ColumnWidth::makePreset(1.0 / 3.0);
        QVERIFY(strip.insertWindow(QStringLiteral("a"), third, ColumnDisplay::Normal, params));
        QCOMPARE(Ax::mainLen(ScrollTestUtils::rectOf(strip.relayout(params), QStringLiteral("a"))), 700);

        QVERIFY(!strip.cycleActiveColumnPresetWidth(-1, params));
        QVERIFY(strip.activeColumn()->width == third);

        QVERIFY(strip.cycleActiveColumnPresetWidth(+1, params));
        QVERIFY(Ax::mainLen(ScrollTestUtils::rectOf(strip.relayout(params), QStringLiteral("a"))) > 700);
    }

    // A strip laid out outside an engine sets no lookup and reads no floor.
    void aStripWithoutALookupReadsNoFloor()
    {
        const ScrollLayoutParams params = ScrollTestUtils::defaultParams();
        QVERIFY(!params.committedFloor);
        ScrollStrip strip;
        QVERIFY(strip.insertWindow(QStringLiteral("a"), ColumnWidth::makeFixed(300), ColumnDisplay::Normal, params));
        QCOMPARE(Ax::mainLen(ScrollTestUtils::rectOf(strip.relayout(params), QStringLiteral("a"))), 300);
    }

    // A floor past the work area renders the column at the work area. The
    // minimize compare caps its target the same way, or every press would
    // report a resize with nothing moving.
    void minimizeUnderAFloorPastTheWorkAreaRefuses()
    {
        ScrollLayoutParams params = ScrollTestUtils::defaultParams();
        params.committedFloor = [](const QString& windowId) {
            return windowId == QLatin1String("a") ? Ax::t(QSize(1500, 0)) : QSize(0, 0);
        };
        ScrollStrip strip;
        const ColumnWidth half = ColumnWidth::makeProportion(0.5);
        QVERIFY(strip.insertWindow(QStringLiteral("a"), half, ColumnDisplay::Normal, params));
        QVERIFY(!strip.minimizeActiveColumnWidth(params));
        QVERIFY(strip.activeColumn()->width == half);
    }

    // The height twin of the cycle refusal: a tile held at its cross minimum
    // renders a shorter preset at the very extent it already draws.
    void aHeightCycleAtTheCrossMinimumRefusesAnEntryThatRendersTheSame()
    {
        ScrollLayoutParams params = ScrollTestUtils::defaultParams();
        params.respectMinimumSize = true;
        const QSize min = Ax::t(QSize(0, 700));
        ScrollStrip strip;
        QVERIFY(strip.insertWindow(QStringLiteral("a"), ScrollTestUtils::kHalf, ColumnDisplay::Normal, params,
                                   min.width(), min.height()));
        const WindowHeight third = WindowHeight::makePreset(1.0 / 3.0);
        QVERIFY(strip.setActiveWindowHeight(third));
        QCOMPARE(Ax::crossLen(ScrollTestUtils::rectOf(strip.relayout(params), QStringLiteral("a"))), 700);

        QVERIFY(!strip.cycleActiveWindowPresetHeight(-1, params));
        QVERIFY(strip.columns().at(0).tiles.at(0).height == third);

        QVERIFY(strip.cycleActiveWindowPresetHeight(+1, params));
        QCOMPARE(Ax::crossLen(ScrollTestUtils::rectOf(strip.relayout(params), QStringLiteral("a"))), 800);
    }
};

QTEST_GUILESS_MAIN(TestScrollEngineCommittedFloor)
#include "test_scrollengine_committedfloor.moc"
