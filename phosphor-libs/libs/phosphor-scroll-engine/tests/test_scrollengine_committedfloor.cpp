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
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
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
const QString kD2 = QStringLiteral("app|d2");
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
ScrollEngine* makeStraddleEngine(QObject* owner, GeometryFn screenGeometry = {}, GeometryFn availableGeometry = {})
{
    auto* settings = makeFloorSettings(owner);
    settings->widthValue = 0.5;
    settings->centerFocused = static_cast<int>(CenterFocusedColumn::Always);
    ScrollEngine* engine = engineWith(owner, settings, {kS1}, screenGeometry, availableGeometry);
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

/// Whether any windowsTiled batch @p tiled caught carries an entry for
/// @p windowId.
bool anyBatchCarries(const QSignalSpy& tiled, const QString& windowId)
{
    for (const QList<QVariant>& emission : tiled) {
        const QJsonArray batch = QJsonDocument::fromJson(emission.at(0).toString().toUtf8()).array();
        for (const QJsonValue& entry : batch) {
            if (entry.toObject().value(QLatin1String("windowId")).toString() == windowId) {
                return true;
            }
        }
    }
    return false;
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
    // not an answer, and plants no floor. The slack is exactly one pixel on
    // each side: two past the offer is an answer, and a pixel short of a
    // floored offer gives nothing back.
    void anAnswerWithinTheSlackIsTheOffer()
    {
        {
            QObject owner;
            ScrollEngine* engine = makeFloorEngine(&owner);
            answer(engine, kA, 301);
            QCOMPARE(extentOf(engine, kA), 300);
            engine->setColumnWidth(ColumnWidth::makeFixed(200), kS1);
            QCoreApplication::processEvents();
            QCOMPARE(extentOf(engine, kA), 200);
        }
        {
            QObject owner;
            ScrollEngine* engine = makeFloorEngine(&owner);
            answer(engine, kA, 302);
            QCOMPARE(extentOf(engine, kA), 302);
        }
        {
            // Offered the 450 its floor holds the column at, the client
            // commits 449: rounding, not a narrower answer, so the floor
            // stays where it is rather than following it down a pixel.
            QObject owner;
            ScrollEngine* engine = makeFloorEngine(&owner);
            answer(engine, kA, 450);
            QCOMPARE(Ax::mainLen(engine->lastManagedRect(kA)), 450);
            answer(engine, kA, 449);
            QCOMPARE(extentOf(engine, kA), 450);
        }
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
        {
            // The global setting.
            QObject owner;
            StubScrollSettings* settings = nullptr;
            ScrollEngine* engine = makeFloorEngine(&owner, &settings, /*respectMinimumSize=*/false);
            QSignalSpy tiled(engine, &ScrollEngine::windowsTiled);
            settings->respectMinimumSize = true;
            engine->refreshConfigFromSettings();
            QCoreApplication::processEvents();
            QVERIFY2(tiled.count() > 0, "the batch must be sent although no rect moved");
        }
        {
            // A per-screen rule turning it on over a global that is off
            // (applyPerScreenConfig's arm).
            QObject owner;
            ScrollEngine* engine = makeFloorEngine(&owner, nullptr, /*respectMinimumSize=*/false);
            QSignalSpy tiled(engine, &ScrollEngine::windowsTiled);
            QVariantMap rule;
            rule.insert(ScrollPerScreenKeys::respectMinimumSize(), true);
            engine->applyPerScreenConfig(kS1, rule);
            QCoreApplication::processEvents();
            QVERIFY2(tiled.count() > 0, "the rule's batch must be sent although no rect moved");
        }
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
        {
            // From a window nothing has been learned about, the answer is
            // refused at the axis-free early return, before anything is
            // resolved: the cheap path every rounding client takes.
            QObject owner;
            ScrollEngine* engine = makeFloorEngine(&owner);
            answer(engine, kA, 290);
            engine->setColumnWidth(ColumnWidth::makeFixed(200), kS1);
            QCoreApplication::processEvents();
            QCOMPARE(extentOf(engine, kA), 200);
        }
        {
            // With a peek memory held and no floor (a floor taught at 450,
            // then satisfied by a 600 column), the narrower answer gets past
            // every early return to the narrower branch, which replaces the
            // memory and still plants no floor.
            QObject owner;
            ScrollEngine* engine = makeFloorEngine(&owner);
            answer(engine, kA, 450);
            engine->setColumnWidth(ColumnWidth::makeFixed(600), kS1);
            QCoreApplication::processEvents();
            QCOMPARE(Ax::mainLen(engine->lastManagedRect(kA)), 600);
            answer(engine, kA, 500); // offered 600
            engine->setColumnWidth(ColumnWidth::makeFixed(200), kS1);
            QCoreApplication::processEvents();
            QCOMPARE(extentOf(engine, kA), 200);

            // Nor does it leave give-back history behind: the next floor
            // taught is an ordinary one, so a narrower answer gives it back
            // rather than meeting a pin.
            answer(engine, kA, 450); // offered 200
            QCOMPARE(extentOf(engine, kA), 450);
            answer(engine, kA, 200); // offered 450
            QCOMPARE(extentOf(engine, kA), 200);
        }
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

        // A pinned floor still drops on satisfaction: the pin stops narrower
        // ANSWERS from giving it back, not the column outgrowing it.
        engine->setColumnWidth(ColumnWidth::makeFixed(450), kS1);
        QCoreApplication::processEvents();
        engine->setColumnWidth(ColumnWidth::makeFixed(300), kS1);
        QCoreApplication::processEvents();
        QCOMPARE(extentOf(engine, kA), 300);

        // And the drop ended the pin and the give-back history with it. A
        // floor taught afresh is an ordinary one again: a wider answer that
        // follows no give-back does not pin, so the next narrower answer
        // gives it back.
        answer(engine, kA, 450); // offered 300: taught afresh
        QCOMPARE(extentOf(engine, kA), 450);
        answer(engine, kA, 300); // offered 450: given back
        QCOMPARE(extentOf(engine, kA), 300);
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

    // A column maximized to the edges on another desktop is not measured
    // there: its extent is declared, the in-view sweep skips such a column
    // for the same reason, and the floor waits for the column it will hold
    // once that one is un-maximized. Measured, its whole-work-area extent
    // would read as satisfying any floor.
    void aMaximizedColumnElsewhereDoesNotSatisfyTheFloor()
    {
        QObject owner;
        ScrollEngine* engine = makeDesktopFloorEngine(&owner);
        answer(engine, kSticky, 450);
        switchTo(engine, 2);
        adopt(engine, stickyOnly());
        QVERIFY2(engine->heldKeyForWindow(kSticky).has_value(), "precondition: adopted onto desktop 2");
        engine->windowFocused(kSticky, kS1);
        QVERIFY(engine->toggleMaximizeToEdges(kS1, kSticky));
        QCoreApplication::processEvents();

        switchTo(engine, 1);
        engine->windowFocused(kSticky, kS1);
        engine->setColumnWidth(ColumnWidth::makeFixed(600), kS1); // satisfied here only
        QCoreApplication::processEvents();
        engine->setColumnWidth(ColumnWidth::makeFixed(300), kS1);
        QCoreApplication::processEvents();
        QCOMPARE(extentOf(engine, kSticky), 450);
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
    // narrower answer from a window nothing has been learned about teaches
    // neither a floor nor a peek memory: it is refused at the axis-free early
    // return, before anything resolves (the narrower branch's own handling of
    // the memory is aNarrowerAnswerReplacesThePeekMemory's).
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
    // would still hand it a sliver. The column here stays at the 800 that
    // satisfied the floor, so it still offers more than the 700 remembered.
    void aSatisfiedFloorStillProtectsThePeek()
    {
        QObject owner;
        ScrollEngine* engine = makeStraddleEngine(&owner);
        answer(engine, kA, 700);
        engine->setColumnWidth(ColumnWidth::makeFixed(800), kS1); // satisfied: the floor goes
        QCoreApplication::processEvents();
        QCOMPARE(extentOf(engine, kA), 800);

        engine->windowFocused(kB, kS1);
        QCoreApplication::processEvents();
        QVERIFY2(isParked(engine, kA), "the peek memory must survive the floor's satisfaction");
    }

    // With no floor held, the memory ends once the column offers the tile
    // less than it. A client that takes the narrower offer reports nothing
    // (an answer equal to its offer is silent), so a memory kept past that
    // point would park the column on every straddle for good. A client that
    // still draws wider answers the narrower offer and teaches the floor and
    // the memory again.
    void aStalePeekEndsOnceTheColumnOffersLess()
    {
        {
            QObject owner;
            ScrollEngine* engine = makeStraddleEngine(&owner);
            answer(engine, kA, 700);
            engine->setColumnWidth(ColumnWidth::makeFixed(800), kS1); // satisfied: the floor goes
            QCoreApplication::processEvents();
            engine->setColumnWidth(ColumnWidth::makeFixed(600), kS1); // offers less than the 700 remembered
            QCoreApplication::processEvents();
            QCOMPARE(extentOf(engine, kA), 600);

            // The client took 600 and said nothing.
            engine->windowFocused(kB, kS1);
            QCoreApplication::processEvents();
            QVERIFY2(!isParked(engine, kA), "a memory the column no longer offers must not park it");
            QVERIFY(Ax::mainLen(engine->lastManagedRect(kA)) < 600);
        }
        {
            QObject owner;
            ScrollEngine* engine = makeStraddleEngine(&owner);
            answer(engine, kA, 700);
            engine->setColumnWidth(ColumnWidth::makeFixed(800), kS1);
            QCoreApplication::processEvents();
            engine->setColumnWidth(ColumnWidth::makeFixed(600), kS1);
            QCoreApplication::processEvents();
            QCOMPARE(Ax::mainLen(engine->lastManagedRect(kA)), 600);

            // The client still draws 700: the narrower offer is answered, and
            // both the floor and the memory come back.
            answer(engine, kA, 700); // offered 600
            QCOMPARE(extentOf(engine, kA), 700);
            engine->windowFocused(kB, kS1);
            QCoreApplication::processEvents();
            QVERIFY2(isParked(engine, kA), "the re-taught memory parks the straddler again");
        }
        {
            // A column offering EXACTLY the memory still offers it: the
            // floor taught at 700 is satisfied by a 700 column and goes, and
            // the memory stays.
            QObject owner;
            ScrollEngine* engine = makeStraddleEngine(&owner);
            answer(engine, kA, 700);
            engine->setColumnWidth(ColumnWidth::makeFixed(700), kS1);
            QCoreApplication::processEvents();
            QCOMPARE(extentOf(engine, kA), 700);
            engine->windowFocused(kB, kS1);
            QCoreApplication::processEvents();
            QVERIFY2(isParked(engine, kA), "a column offering exactly the memory keeps it");
        }
        {
            // Ending the memory ends the whole extent, the give-back history
            // with it. A floor taught at 450 and given back entirely by a 300
            // answer leaves a 300 memory and that history; the column then
            // narrows to 200 and the client takes it. The next floor taught
            // is an ordinary one, so a narrower answer gives it back rather
            // than meeting a pin.
            QObject owner;
            ScrollEngine* engine = makeFloorEngine(&owner);
            answer(engine, kA, 450); // offered 300
            answer(engine, kA, 300); // offered 450: given back entirely
            QCOMPARE(extentOf(engine, kA), 300);
            engine->setColumnWidth(ColumnWidth::makeFixed(200), kS1); // offers less than the 300 remembered
            QCoreApplication::processEvents();
            QCOMPARE(extentOf(engine, kA), 200);
            answer(engine, kA, 450); // offered 200: taught afresh
            QCOMPARE(extentOf(engine, kA), 450);
            answer(engine, kA, 200); // offered 450: given back
            QCOMPARE(extentOf(engine, kA), 200);
        }
    }

    // The memory ends only on a pass that offers the tile its whole rect. A
    // column that narrows below it while straddling the screen edge would
    // otherwise be clamped to a sliver, which a client still drawing wider
    // answers across the edge, and an answer to a sliver teaches nothing.
    // Here the column narrows with no verb behind it: a proportional width
    // under a work area that shrinks while the column straddles.
    void aStraddlerKeepsThePeekUntilItIsOfferedWhole()
    {
        // 700 learned and satisfied at 0.75 of the 1200 area, then the area
        // shrinks to 900 with b centred: the column is 675, less than the 700
        // remembered, and 225 of it straddles the leading screen edge.
        const auto straddleAtNarrowedColumn = [](QObject* owner, const std::shared_ptr<QRect>& available) {
            ScrollEngine* engine = makeStraddleEngine(
                owner,
                [](const QString&) {
                    return defaultScreenRect();
                },
                [available](const QString&) {
                    return *available;
                });
            answer(engine, kA, 700);
            engine->setColumnWidth(ColumnWidth::makeProportion(0.75), kS1); // satisfied: the floor goes
            QCoreApplication::processEvents();
            engine->windowFocused(kB, kS1);
            QCoreApplication::processEvents();
            *available = Ax::t(QRect(0, 0, 900, 800));
            engine->retile(kS1);
            QCoreApplication::processEvents();
            return engine;
        };
        {
            QObject owner;
            auto available = std::make_shared<QRect>(defaultScreenRect());
            ScrollEngine* engine = straddleAtNarrowedColumn(&owner, available);
            QCOMPARE(extentOf(engine, kA), 675);
            QVERIFY2(isParked(engine, kA), "a straddler is not offered its whole tile, so the memory stays");

            // Shown whole, the column is offered less than the memory, and
            // the client says nothing: the memory ends there, and the next
            // straddle peeks.
            engine->windowFocused(kA, kS1);
            QCoreApplication::processEvents();
            QCOMPARE(Ax::mainLen(engine->lastManagedRect(kA)), 675);
            engine->windowFocused(kB, kS1);
            QCoreApplication::processEvents();
            QVERIFY2(!isParked(engine, kA), "the memory ends on the pass that offers the tile whole");
            QVERIFY(Ax::mainLen(engine->lastManagedRect(kA)) < 675);
        }
        {
            // The park is the whole tile, so a client that still draws 700
            // answers it and teaches the floor again.
            QObject owner;
            auto available = std::make_shared<QRect>(defaultScreenRect());
            ScrollEngine* engine = straddleAtNarrowedColumn(&owner, available);
            QVERIFY(isParked(engine, kA));
            answer(engine, kA, 700); // offered the parked 675
            QCOMPARE(extentOf(engine, kA), 700);
        }
        {
            // A pass that comes before the client answers the park parks it
            // again, so a late answer still teaches. The park is a whole
            // offer, but silence after one proves nothing yet: ending the
            // memory once a park at this size had gone out would make this
            // pass offer a sliver, and the client's answer would then answer
            // the sliver and be refused.
            QObject owner;
            auto available = std::make_shared<QRect>(defaultScreenRect());
            ScrollEngine* engine = straddleAtNarrowedColumn(&owner, available);
            QVERIFY(isParked(engine, kA));
            engine->retile(kS1);
            QCoreApplication::processEvents();
            QVERIFY2(isParked(engine, kA), "an unanswered park is no evidence the client took it");
            answer(engine, kA, 700); // offered the parked 675
            QCOMPARE(extentOf(engine, kA), 700);
        }
        {
            // A window under an interactive move is offered nothing, so a
            // pass that shows its tile whole on the strip ends nothing: a is
            // focused mid-drag, which centres it whole, and straddles again
            // before the drop, whose relayout keeps the memory and parks it.
            QObject owner;
            auto available = std::make_shared<QRect>(defaultScreenRect());
            ScrollEngine* engine = straddleAtNarrowedColumn(&owner, available);
            engine->setInteractiveDragWindow(kA);
            engine->windowFocused(kA, kS1);
            QCoreApplication::processEvents();
            engine->windowFocused(kB, kS1);
            QCoreApplication::processEvents();
            engine->setInteractiveDragWindow(QString());
            engine->retile(kS1);
            QCoreApplication::processEvents();
            QVERIFY2(isParked(engine, kA), "a pass under the drag judges nothing about the dragged window");
        }
    }

    // The memory is judged on the view the batch commits, after the
    // re-anchor. Here the column sits whole under the view the pass starts
    // from and straddles only once the pass re-centres b in a narrower work
    // area. Judged on the old view, the memory would end on a pass that
    // clamps the tile.
    void thePeekIsJudgedAfterTheReanchor()
    {
        QObject owner;
        auto available = std::make_shared<QRect>(defaultScreenRect());
        ScrollEngine* engine = makeStraddleEngine(
            &owner,
            [](const QString&) {
                return defaultScreenRect();
            },
            [available](const QString&) {
                return *available;
            });
        answer(engine, kA, 700);
        engine->setColumnWidth(ColumnWidth::makeProportion(0.6), kS1); // 720: satisfied, the floor goes
        QCoreApplication::processEvents();
        engine->windowFocused(kB, kS1); // b centred at 300, a parked off the leading edge
        QCoreApplication::processEvents();

        // At 480 the columns are 288 and 240. Under the view the pass starts
        // from, b still sits at 300 and a whole at 12; re-centred, b sits at
        // 120 and a straddles the leading screen edge with 120 on screen.
        *available = Ax::t(QRect(0, 0, 480, 300));
        engine->retile(kS1);
        QCoreApplication::processEvents();
        QCOMPARE(extentOf(engine, kA), 288);
        QVERIFY2(isParked(engine, kA), "a tile the re-anchor makes straddle keeps the memory");
    }

    // The memory is the window's, read by every column it holds, so it ends
    // only once every one of them offers less. A column on another desktop
    // that still offers the memory is not stale, and ending the memory from
    // here would clamp that one to a sliver the next time it straddles.
    void aPeekEndsOnlyWhenEveryDesktopOffersLess()
    {
        QObject owner;
        StubScrollSettings* settings = makeFloorSettings(&owner);
        settings->widthValue = 0.5;
        settings->centerFocused = static_cast<int>(CenterFocusedColumn::Always);
        ScrollEngine* engine = engineWith(&owner, settings, {kS1});
        engine->setCurrentDesktopForScreen(kS1, 1);
        engine->windowOpened(kD1, kS1, 0, 0);
        engine->windowOpened(kSticky, kS1, 0, 0);
        engine->windowFocused(kSticky, kS1);
        engine->retile(kS1);
        QCoreApplication::processEvents();
        answer(engine, kSticky, 700);

        switchTo(engine, 2);
        engine->windowOpened(kD2, kS1, 0, 0);
        QCoreApplication::processEvents();
        adopt(engine, [](const QString& windowId) {
            PhosphorEngine::DesktopSpan span;
            span.known = true;
            span.sticky = (windowId == kSticky);
            if (!span.sticky) {
                span.desktops = {windowId == kD2 ? 2 : 1};
            }
            return span;
        });
        QVERIFY2(engine->heldKeyForWindow(kSticky).has_value(), "precondition: adopted onto desktop 2");
        engine->windowFocused(kSticky, kS1);
        engine->setColumnWidth(ColumnWidth::makeFixed(800), kS1);
        QCoreApplication::processEvents();
        switchTo(engine, 1);
        engine->windowFocused(kSticky, kS1);
        engine->setColumnWidth(ColumnWidth::makeFixed(800), kS1); // satisfied on both: the floor goes
        QCoreApplication::processEvents();
        engine->setColumnWidth(ColumnWidth::makeFixed(600), kS1); // offers less here, shown whole
        QCoreApplication::processEvents();
        QCOMPARE(extentOf(engine, kSticky), 600);

        // Desktop 2's column still offers 800, so the memory stands there.
        switchTo(engine, 2);
        engine->windowFocused(kD2, kS1);
        QCoreApplication::processEvents();
        QCOMPARE(extentOf(engine, kSticky), 800);
        QVERIFY2(isParked(engine, kSticky), "a column that still offers the memory keeps it");

        // Once it offers less too, the memory ends and the next straddle
        // peeks.
        engine->windowFocused(kSticky, kS1);
        engine->setColumnWidth(ColumnWidth::makeFixed(600), kS1);
        QCoreApplication::processEvents();
        engine->windowFocused(kD2, kS1);
        QCoreApplication::processEvents();
        QVERIFY2(!isParked(engine, kSticky), "the memory ends once every column offers less");
        QVERIFY(Ax::mainLen(engine->lastManagedRect(kSticky)) < 600);
    }

    // A narrower whole-tile answer REPLACES the memory rather than ending it,
    // in exactly the widened-past state: the column outgrew the floor, the
    // client answered narrower, and it still answers wider than a clamp.
    void aNarrowerAnswerReplacesThePeekMemory()
    {
        {
            // The column stays at 800, which still offers more than the 650
            // the memory now holds.
            QObject owner;
            ScrollEngine* engine = makeStraddleEngine(&owner);
            answer(engine, kA, 700);
            engine->setColumnWidth(ColumnWidth::makeFixed(800), kS1);
            QCoreApplication::processEvents();
            answer(engine, kA, 650); // offered 800
            engine->windowFocused(kB, kS1);
            QCoreApplication::processEvents();
            QVERIFY2(isParked(engine, kA), "a 650px memory still protects a 300px remainder");
        }
        {
            // The column stays at 800 here too, so the memory is still
            // offered and nothing but the replacement can stop the park.
            QObject owner;
            ScrollEngine* engine = makeStraddleEngine(&owner);
            answer(engine, kA, 700);
            engine->setColumnWidth(ColumnWidth::makeFixed(800), kS1);
            QCoreApplication::processEvents();
            answer(engine, kA, 250); // offered 800: the memory follows it down
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

    // An absurd extent still floats the window. What this pins is the
    // outcome, not the client bound itself (kMaxClientExtentPx): on this
    // untabbed column nothing is added to the floor, so an extent left uncut
    // would float the window the same way. The bound is what keeps the sums
    // that do add to it (the tab reservation, the relayout's floors) inside
    // int, and an overflow there is undefined behaviour no assertion here can
    // be trusted to observe.
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

        // The drop moves the column with no verb behind it, so the strip is
        // republished.
        QSignalSpy placement(engine, &PhosphorEngine::PlacementEngineBase::placementChanged);
        *available = Ax::t(QRect(0, 0, 400, 300));
        engine->retile(kS1);
        QCoreApplication::processEvents();
        QCOMPARE(extentOf(engine, kA), 100);
        QVERIFY(placement.count() > 0);
        QCOMPARE(placement.last().at(0).toString(), kS1);

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

    // The drop is a layout change of its own, announced even when the resize
    // writes no intent: here the column already asks for the 350 the user
    // settles on, and only the floor held it at 450.
    void aUserResizeFloorDropAnnouncesThePlacement()
    {
        QObject owner;
        ScrollEngine* engine = makeFloorEngine(&owner);
        answer(engine, kA, 450);
        engine->setColumnWidth(ColumnWidth::makeFixed(350), kS1);
        QCoreApplication::processEvents();
        QCOMPARE(extentOf(engine, kA), 450);
        const QRect applied = engine->lastManagedRect(kA);
        QRect settled = Ax::t(applied);
        settled.setWidth(350);
        settled = Ax::t(settled);
        QSignalSpy placement(engine, &PhosphorEngine::PlacementEngineBase::placementChanged);
        engine->onWindowResized(kA, applied, settled, kS1);
        QCOMPARE(placement.count(), 1);
        QCOMPARE(placement.first().at(0).toString(), kS1);
        QCoreApplication::processEvents();
        QCOMPARE(extentOf(engine, kA), 350);
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

        QSignalSpy placement(engine, &PhosphorEngine::PlacementEngineBase::placementChanged);
        const QSize min150 = roleSize(150, 0);
        engine->windowMinSizeUpdated(kA, min150.width(), min150.height());
        QCOMPARE(placement.count(), 0); // raised: nothing dropped, nothing to announce
        QCoreApplication::processEvents();
        QCOMPARE(extentOf(engine, kA), 450);

        const QSize min50 = roleSize(50, 0);
        engine->windowMinSizeUpdated(kA, min50.width(), min50.height());
        // The drop moves the column with no verb behind it: announced by the
        // report itself, ahead of the retile it schedules.
        QCOMPARE(placement.count(), 1);
        QCoreApplication::processEvents();
        QCOMPARE(extentOf(engine, kA), 300);
    }

    // A lowered minimum still drops a floating window's floor, but the drop
    // moves no column while it floats, so nothing is announced. The floor is
    // gone when the window comes back.
    void aLoweredMinimumOnAFloatingWindowAnnouncesNothing()
    {
        QObject owner;
        ScrollEngine* engine = makeFloorEngine(&owner);
        const QSize min100 = roleSize(100, 0);
        engine->windowMinSizeUpdated(kA, min100.width(), min100.height());
        QCoreApplication::processEvents();
        answer(engine, kA, 450);
        engine->setWindowFloat(kA, true, kS1);
        QCoreApplication::processEvents();

        QSignalSpy placement(engine, &PhosphorEngine::PlacementEngineBase::placementChanged);
        const QSize min50 = roleSize(50, 0);
        engine->windowMinSizeUpdated(kA, min50.width(), min50.height());
        QCoreApplication::processEvents();
        QCOMPARE(placement.count(), 0);

        engine->setWindowFloat(kA, false, kS1);
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

    // A boundary SWAP migrates both windows, so the partner traded back onto
    // the source output leaves its floor behind as well.
    void aBoundarySwapDropsThePartnersFloor()
    {
        ScrollTestUtils::TrailNeighbourResolver resolver;
        QObject owner;
        ScrollEngine* engine = engineWith(&owner, makeFloorSettings(&owner), {kS1, kS2});
        engine->setCrossSurfaceResolver(&resolver);
        engine->windowOpened(kA, kS1, 0, 0);
        engine->windowOpened(kB, kS2, 0, 0);
        engine->retile(kS1);
        engine->retile(kS2);
        QCoreApplication::processEvents();
        answer(engine, kB, 450);
        QCOMPARE(extentOf(engine, kS2, kB), 450);

        PhosphorEngine::NavigationContext ctx;
        ctx.screenId = kS1;
        engine->swapFocusedInDirection(Ax::navTrail(), ctx);
        QCoreApplication::processEvents();
        QCOMPARE(engine->screenForTrackedWindow(kB), kS1);
        QCOMPARE(extentOf(engine, kS1, kB), 300);
        engine->setCrossSurfaceResolver(nullptr);
    }

    // A drag dropped on another output migrates the window the same way. The
    // drop indicator promises the unfloored slot, which is where the drop
    // lands: the floor goes before the insert, so neither the slot nor the
    // view the drop anchors is shaped by it.
    void aCrossOutputDragDropsTheFloorAndTheIndicatorShowsIt()
    {
        QObject owner;
        ScrollEngine* engine = engineWith(&owner, makeFloorSettings(&owner), {kS1, kS2});
        engine->windowOpened(kA, kS1, 0, 0);
        engine->windowOpened(kC, kS1, 0, 0);
        engine->windowOpened(kB, kS2, 0, 0);
        engine->windowFocused(kA, kS1);
        engine->retile(kS1);
        engine->retile(kS2);
        QCoreApplication::processEvents();
        answer(engine, kA, 450);
        QCOMPARE(extentOf(engine, kS1, kA), 450);

        QVERIFY(engine->beginDragInsertPreview(kA, kS2));
        const auto target = engine->computeDragInsertTargetAtPoint(kS2, Ax::point(1150, 400));
        QVERIFY(target.isValid());
        engine->updateDragInsertPreview(target);
        QCOMPARE(Ax::mainLen(engine->dragInsertIndicatorRect(kS2)), 300);
        engine->commitDragInsertPreview();
        QCoreApplication::processEvents();
        QCOMPARE(engine->screenForTrackedWindow(kA), kS2);
        QCOMPARE(extentOf(engine, kS2, kA), 300);
    }

    // The receive's defence-in-depth migration (a window still tracked in
    // another context when a handoff hands it over) is a migration like the
    // rest, and leaves the floor behind.
    void aHandoffReceiveMigrationDropsTheFloor()
    {
        QObject owner;
        ScrollEngine* engine = engineWith(&owner, makeFloorSettings(&owner), {kS1, kS2});
        engine->windowOpened(kA, kS1, 0, 0);
        engine->windowOpened(kB, kS1, 0, 0);
        engine->retile(kS1);
        QCoreApplication::processEvents();
        answer(engine, kA, 450);
        QCOMPARE(extentOf(engine, kS1, kA), 450);

        PhosphorEngine::IPlacementEngine::HandoffContext ctx;
        ctx.windowId = kA;
        ctx.toScreenId = kS2;
        engine->handoffReceive(ctx);
        QCoreApplication::processEvents();
        QCOMPARE(engine->screenForTrackedWindow(kA), kS2);
        QCOMPARE(extentOf(engine, kS2, kA), 300);
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
    // The verdict runs before the strip is laid out, the open path's order,
    // so no batch ever configures the column of a window about to float
    // again.
    void theHoldReturnReRunsTheOversizedVerdict()
    {
        QObject owner;
        ScrollEngine* engine = makeFloorEngine(&owner);
        QVERIFY(engine->setWindowFullscreenFloat(kA, true, kS1));
        const QSize oversized = roleSize(1300, 0);
        engine->windowMinSizeUpdated(kA, oversized.width(), oversized.height());
        QCoreApplication::processEvents();
        QSignalSpy tiled(engine, &ScrollEngine::windowsTiled);
        QVERIFY(engine->setWindowFullscreenFloat(kA, false, kS1));
        QCoreApplication::processEvents();
        QVERIFY(engine->isWindowFloatingInScroll(kA));
        QVERIFY2(!anyBatchCarries(tiled, kA), "the returning window's column must never be configured");
    }

    // A same-key re-announce of a held window is the compositor's return too
    // (an effect that lost its record of the hold), and re-runs the verdict
    // the same way, outside an arrival burst...
    void aReannouncedHoldReturnReRunsTheOversizedVerdict()
    {
        QObject owner;
        ScrollEngine* engine = makeFloorEngine(&owner);
        QVERIFY(engine->setWindowFullscreenFloat(kA, true, kS1));
        const QSize oversized = roleSize(1300, 0);
        engine->windowMinSizeUpdated(kA, oversized.width(), oversized.height());
        QCoreApplication::processEvents();
        QSignalSpy tiled(engine, &ScrollEngine::windowsTiled);
        engine->windowOpened(kA, kS1, 0, 0);
        QCoreApplication::processEvents();
        QVERIFY(engine->isWindowFloatingInScroll(kA));
        QVERIFY(!engine->isFullscreenFloated(kA));
        QVERIFY2(!anyBatchCarries(tiled, kA), "the returning window's column must never be configured");
    }

    // ...and inside one, where the verdict floats the window without a
    // relayout of its own: the burst's deferred apply lays the strip out
    // once, at the burst's end, and never with the returning window in it.
    // Another arrival in the same burst is what a synchronous relayout
    // mid-burst would have laid out early.
    void aReannouncedHoldReturnInABurstFloatsWithoutAnEarlyApply()
    {
        QObject owner;
        ScrollEngine* engine = makeFloorEngine(&owner);
        QVERIFY(engine->setWindowFullscreenFloat(kA, true, kS1));
        const QSize oversized = roleSize(1300, 0);
        engine->windowMinSizeUpdated(kA, oversized.width(), oversized.height());
        QCoreApplication::processEvents();
        QSignalSpy tiled(engine, &ScrollEngine::windowsTiled);
        engine->beginArrivalBurst();
        engine->windowOpened(kC, kS1, 0, 0);
        engine->windowOpened(kA, kS1, 0, 0);
        QCOMPARE(tiled.count(), 0);
        engine->endArrivalBurst();
        QCoreApplication::processEvents();
        QVERIFY(engine->isWindowFloatingInScroll(kA));
        QVERIFY(!engine->isFullscreenFloated(kA));
        QVERIFY(engine->isWindowTiled(kC));
        QVERIFY(tiled.count() > 0);
        QVERIFY2(!anyBatchCarries(tiled, kA), "the returning window's column must never be configured");
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
            // A float has no column to answer. This pins the outcome rather
            // than the isFloating test itself: a floating window holds no
            // column either, so the column lookup refuses it as well, and
            // either check alone would pass this leg.
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
            // floor, so nothing its client says about it is learned, and
            // nothing it says gives back what was learned before. A floor
            // taught at 450 survives a narrower answer to the maximized
            // offer: read as an answer, it would give the floor back
            // entirely (the column offers the whole work area without it).
            QObject owner;
            ScrollEngine* engine = makeFloorEngine(&owner);
            answer(engine, kA, 450);
            QCOMPARE(extentOf(engine, kA), 450);
            QVERIFY(engine->toggleMaximizeToEdges(kS1, kA));
            QCoreApplication::processEvents();
            answer(engine, kA, Ax::mainLen(engine->lastManagedRect(kA)) - 200);
            QVERIFY(!engine->isWindowFloatingInScroll(kA));
            QVERIFY(engine->toggleMaximizeToEdges(kS1, kA));
            QCoreApplication::processEvents();
            QCOMPARE(extentOf(engine, kA), 450);
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
