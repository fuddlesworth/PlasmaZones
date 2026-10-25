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
 * a renumber displacing the window, the aliveness prune) and every one that
 * keeps it (float round trips, the fullscreen hold, a cancelled drag, a drag
 * reorder, consume, Retile, the evacuee park). Plus the strip-level verbs
 * whose honesty the floor tests:
 * the preset cycles and minimize-width.
 *
 * Across the strip: the floor a taller answer holds its stacked tile's share
 * to, the slack that tells an answer from the offer, the float when the stack
 * can never hold it, its own drops (alone in its column, satisfied in every
 * desktop, a stack or work area that can no longer hold it, a moved offer
 * along the strip, a user resize across, a lowered minimum across, a leave)
 * and what keeps it (a raised minimum, a float round trip, Retile). An answer
 * for a background strip is learned for that strip, and a dragged frame
 * teaches nothing. And the parked tile's matched answer, which ends a peek
 * memory the client no longer draws to.
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
#include "scrollstubtracking.h"

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

/// The extent across the strip of the tile last applied to @p windowId.
int crossOf(ScrollEngine* engine, const QString& windowId)
{
    return Ax::crossLen(engine->lastManagedRect(windowId));
}

/// The floor fixture with b consumed into a's column: one 300px column
/// stacking a over b, 400px each across the 800px work area.
ScrollEngine* makeStackEngine(QObject* owner, StubScrollSettings** outSettings = nullptr,
                              bool respectMinimumSize = true, GeometryFn screenGeometry = {},
                              GeometryFn availableGeometry = {})
{
    ScrollEngine* engine = makeFloorEngine(owner, outSettings, respectMinimumSize, screenGeometry, availableGeometry);
    engine->consumeWindowIntoColumn(kS1);
    engine->retile(kS1);
    QCoreApplication::processEvents();
    return engine;
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

/// kSticky on every desktop, kD2 on desktop 2, everything else on desktop 1.
PhosphorEngine::DesktopSpanQuery stickyBesideD2()
{
    return [](const QString& windowId) {
        PhosphorEngine::DesktopSpan span;
        span.known = true;
        span.sticky = (windowId == kSticky);
        if (!span.sticky) {
            span.desktops = {windowId == kD2 ? 2 : 1};
        }
        return span;
    };
}

/// The two-desktop peek fixture, under makeStraddleEngine's half-width columns
/// and Always centring: the sticky window taught a 700 floor on desktop 1,
/// was adopted onto desktop 2 beside d2, and both its columns were widened
/// to 800, which satisfies the floor everywhere and leaves the 700 memory.
/// Desktop 1's column was then narrowed to 600, so it offers less than the
/// memory while desktop 2's still offers more. Desktop 1 is in view and the
/// sticky window focused. Null when the adoption onto desktop 2 failed.
ScrollEngine* makeTwoDesktopPeekEngine(QObject* owner)
{
    StubScrollSettings* settings = makeFloorSettings(owner);
    settings->widthValue = 0.5;
    settings->centerFocused = static_cast<int>(CenterFocusedColumn::Always);
    ScrollEngine* engine = engineWith(owner, settings, {kS1});
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
    adopt(engine, stickyBesideD2());
    if (!engine->heldKeyForWindow(kSticky).has_value()) {
        return nullptr;
    }
    engine->windowFocused(kSticky, kS1);
    engine->setColumnWidth(ColumnWidth::makeFixed(800), kS1);
    QCoreApplication::processEvents();
    switchTo(engine, 1);
    engine->windowFocused(kSticky, kS1);
    engine->setColumnWidth(ColumnWidth::makeFixed(800), kS1); // satisfied on both: the floor goes
    QCoreApplication::processEvents();
    engine->setColumnWidth(ColumnWidth::makeFixed(600), kS1); // offers less here only
    QCoreApplication::processEvents();
    return engine;
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

    // A narrower answer a pixel past what the column offers without the floor
    // is that offer, within the rounding slack: the widening goes entirely.
    // Kept a pixel wide, the floor is one no sweep could ever find satisfied,
    // and it would hold the column a pixel wide for good.
    void aGiveBackWithinTheSlackDropsTheFloor()
    {
        QObject owner;
        ScrollEngine* engine = makeFloorEngine(&owner);
        answer(engine, kA, 450);
        QCOMPARE(extentOf(engine, kA), 450);
        answer(engine, kA, 301); // offered 450; the column asks for 300
        QCOMPARE(extentOf(engine, kA), 300);
    }

    // A narrower answer to a park lowers the peek memory without touching the
    // column, and the straddler parked on the old memory is laid out again on
    // the new one, which no longer protects its remainder.
    void aNarrowerAnswerToAParkLaysTheStraddlerOutAgain()
    {
        QObject owner;
        ScrollEngine* engine = makeStraddleEngine(&owner);
        answer(engine, kA, 700);
        engine->setColumnWidth(ColumnWidth::makeFixed(800), kS1); // satisfied: the floor goes, the memory stays
        QCoreApplication::processEvents();
        engine->windowFocused(kB, kS1);
        QCoreApplication::processEvents();
        QVERIFY2(isParked(engine, kA), "precondition: the 700 memory parks the straddler");

        answer(engine, kA, 250); // offered the whole parked 800
        QVERIFY2(!isParked(engine, kA), "the lowered memory no longer parks it, and the strip says so at once");
    }

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
        {
            // The client takes the park and says so: the compositor reports a
            // parked tile's settled commit even when it equals the offer
            // along the strip. That is an answer to the park itself, not
            // silence, so the memory ends and the next pass peeks.
            QObject owner;
            auto available = std::make_shared<QRect>(defaultScreenRect());
            ScrollEngine* engine = straddleAtNarrowedColumn(&owner, available);
            QVERIFY(isParked(engine, kA));
            QSignalSpy placement(engine, &PhosphorEngine::PlacementEngineBase::placementChanged);
            answer(engine, kA, Ax::mainLen(engine->lastManagedRect(kA))); // took the parked 675
            QVERIFY2(!isParked(engine, kA), "a taken park ends the memory, and the straddler peeks");
            QVERIFY(Ax::mainLen(engine->lastManagedRect(kA)) < 675);
            // Only the park/clamp choice moved, not the model.
            QCOMPARE(placement.count(), 0);
        }
        {
            // A matched answer to a park a newer batch has since replaced is
            // refused at the whole-tile gate like any other: the work area
            // narrows again, the column is parked at 660, and the client's
            // late answer to the 675 park ends nothing. Its answer to the 660
            // park does.
            QObject owner;
            auto available = std::make_shared<QRect>(defaultScreenRect());
            ScrollEngine* engine = straddleAtNarrowedColumn(&owner, available);
            QVERIFY(isParked(engine, kA));
            const QSize stale = engine->lastManagedRect(kA).size();
            *available = Ax::t(QRect(0, 0, 880, 800));
            engine->retile(kS1);
            QCoreApplication::processEvents();
            QCOMPARE(extentOf(engine, kA), 660);
            QVERIFY2(isParked(engine, kA), "precondition: the narrower column still parks on the memory");
            engine->reportCommittedSize(kA, stale, stale);
            QCoreApplication::processEvents();
            engine->retile(kS1);
            QCoreApplication::processEvents();
            QVERIFY2(isParked(engine, kA), "a matched answer to a replaced park ends nothing");
            answer(engine, kA, Ax::mainLen(engine->lastManagedRect(kA))); // took the parked 660
            QVERIFY2(!isParked(engine, kA), "the matched answer to the current park ends the memory");
        }
        {
            // A frame under an interactive move is motion, not an answer, so
            // a matched report for the dragged window ends nothing.
            QObject owner;
            auto available = std::make_shared<QRect>(defaultScreenRect());
            ScrollEngine* engine = straddleAtNarrowedColumn(&owner, available);
            QVERIFY(isParked(engine, kA));
            engine->setInteractiveDragWindow(kA);
            answer(engine, kA, Ax::mainLen(engine->lastManagedRect(kA)));
            engine->setInteractiveDragWindow(QString());
            engine->retile(kS1);
            QCoreApplication::processEvents();
            QVERIFY2(isParked(engine, kA), "a dragged window's matched answer is ignored");
        }
    }

    // The memory is the window's, and a matched answer on one desktop ends
    // it only once every other column the window holds offers less too,
    // endStalePeeks' own rule. Here desktop 2's column still offers 800, so
    // desktop 1's taken park keeps the memory and the straddler parks again.
    void aTakenParkKeepsThePeekWhileAnotherDesktopOffersIt()
    {
        QObject owner;
        ScrollEngine* engine = makeTwoDesktopPeekEngine(&owner);
        QVERIFY2(engine, "precondition: adopted onto desktop 2");
        QCOMPARE(extentOf(engine, kSticky), 600);

        engine->windowFocused(kD1, kS1);
        QCoreApplication::processEvents();
        QVERIFY2(isParked(engine, kSticky), "precondition: desktop 2 still offers the memory, so this straddle parks");
        answer(engine, kSticky, Ax::mainLen(engine->lastManagedRect(kSticky))); // took the parked 600
        engine->retile(kS1);
        QCoreApplication::processEvents();
        QVERIFY2(isParked(engine, kSticky), "a column elsewhere that still offers the memory keeps it");

        // The control: what kept the memory was desktop 2's column. Narrowed
        // there too, it offers less everywhere, the memory ends and desktop
        // 1's straddle peeks.
        switchTo(engine, 2);
        engine->windowFocused(kSticky, kS1);
        engine->setColumnWidth(ColumnWidth::makeFixed(600), kS1);
        QCoreApplication::processEvents();
        switchTo(engine, 1);
        engine->windowFocused(kD1, kS1);
        QCoreApplication::processEvents();
        QVERIFY2(!isParked(engine, kSticky), "with every column offering less, the straddle peeks");
    }

    // A held floor is never touched by a matched answer: the column is held
    // at it, so a matched answer to the park only says the client took the
    // floor it taught, and nothing is republished. Neither is the memory the
    // floor carries, which outlives the floor once a wider column satisfies
    // it, as aSatisfiedFloorStillProtectsThePeek shows without the matched
    // answer.
    //
    // A floor the column is held at never leaves the memory longer than the
    // offer, so here mayEndPeek's no-floor term is not what keeps the floor.
    // It is load-bearing where the work area caps the column below the floor,
    // which aMatchedAnswerToACappedFloorKeepsIt pins.
    void aMatchedAnswerUnderAFloorChangesNothing()
    {
        QObject owner;
        ScrollEngine* engine = makeStraddleEngine(&owner);
        answer(engine, kA, 700);
        engine->windowFocused(kB, kS1);
        QCoreApplication::processEvents();
        QVERIFY2(isParked(engine, kA), "precondition: the floored straddler parks");

        QSignalSpy placement(engine, &PhosphorEngine::PlacementEngineBase::placementChanged);
        answer(engine, kA, Ax::mainLen(engine->lastManagedRect(kA))); // took the parked 700
        engine->retile(kS1);
        QCoreApplication::processEvents();
        QCOMPARE(placement.count(), 0);
        QCOMPARE(extentOf(engine, kA), 700);
        QVERIFY2(isParked(engine, kA), "the floor still parks the straddler");

        // The floor satisfied by an 800 column goes, and the memory is still
        // there to park the next straddle.
        engine->windowFocused(kA, kS1);
        engine->setColumnWidth(ColumnWidth::makeFixed(800), kS1);
        QCoreApplication::processEvents();
        QCOMPARE(extentOf(engine, kA), 800);
        engine->windowFocused(kB, kS1);
        QCoreApplication::processEvents();
        QVERIFY2(isParked(engine, kA), "the matched answer left the memory standing");
    }

    // A floor up to the rounding slack past the work area stays tiled, and the
    // work area caps its column a pixel below it. The client's matched answer
    // to that capped offer then sits under a memory longer than the offer,
    // which is the shape that ends a peek memory. Under a held floor it must
    // not: ending it would wipe the floor with it. The 1100 work area grows
    // back to the screen afterwards, where the column reads the floor whole
    // and shows it is still the 1101 the client taught.
    void aMatchedAnswerToACappedFloorKeepsIt()
    {
        QObject owner;
        auto available = std::make_shared<QRect>(Ax::t(QRect(0, 0, 1100, ScrollTestUtils::kCrossExtent)));
        ScrollEngine* engine = makeFloorEngine(
            &owner, nullptr, true,
            [](const QString&) {
                return defaultScreenRect();
            },
            [available](const QString&) {
                return *available;
            });
        engine->setColumnWidth(ColumnWidth::makeFixed(1099), kS1);
        QCoreApplication::processEvents();
        QCOMPARE(Ax::mainLen(engine->lastManagedRect(kA)), 1099);

        answer(engine, kA, 1101);
        QVERIFY2(!engine->isWindowFloatingInScroll(kA), "a floor a pixel past the work area stays tiled");
        QCOMPARE(Ax::mainLen(engine->lastManagedRect(kA)), 1100);

        answer(engine, kA, 1100); // took the capped offer
        engine->retile(kS1);
        QCoreApplication::processEvents();
        QCOMPARE(extentOf(engine, kA), 1100);

        *available = defaultScreenRect();
        engine->retile(kS1);
        QCoreApplication::processEvents();
        QCOMPARE(extentOf(engine, kA), 1101);
    }

    // A matched answer from a window nothing has been learned about is the
    // compositor's steady state, with no republish and no batch. The
    // axis-free early return answers it before anything resolves; were that
    // return gone, the steady-state return after it would answer it the same
    // way, so what this pins is the silence, not which return gives it.
    void aMatchedAnswerWithoutMemoryIsSilent()
    {
        QObject owner;
        ScrollEngine* engine = makeFloorEngine(&owner);
        QSignalSpy placement(engine, &PhosphorEngine::PlacementEngineBase::placementChanged);
        QSignalSpy tiled(engine, &ScrollEngine::windowsTiled);
        answer(engine, kA, 300);
        QCOMPARE(placement.count(), 0);
        QCOMPARE(tiled.count(), 0);
        QCOMPARE(extentOf(engine, kA), 300);
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
        // Desktop 1's column offers less than the memory, shown whole.
        ScrollEngine* engine = makeTwoDesktopPeekEngine(&owner);
        QVERIFY2(engine, "precondition: adopted onto desktop 2");
        QCOMPARE(extentOf(engine, kSticky), 600);

        // Desktop 2's column still offers 800, so the memory stands there.
        switchTo(engine, 2);
        engine->windowFocused(kD2, kS1);
        QCoreApplication::processEvents();
        QCOMPARE(extentOf(engine, kSticky), 800);
        QVERIFY2(isParked(engine, kSticky), "a column that still offers the memory keeps it");

        // A column offering exactly the memory still offers it: desktop 2
        // narrows to 700, desktop 1 is laid out again at 600 with the tile
        // whole, and the memory stands for desktop 2's next straddle.
        engine->windowFocused(kSticky, kS1);
        engine->setColumnWidth(ColumnWidth::makeFixed(700), kS1);
        QCoreApplication::processEvents();
        switchTo(engine, 1);
        engine->windowFocused(kSticky, kS1);
        QCoreApplication::processEvents();
        QCOMPARE(extentOf(engine, kSticky), 600);
        QVERIFY2(!isParked(engine, kSticky), "precondition: desktop 1 offers the tile whole");
        switchTo(engine, 2);
        engine->windowFocused(kD2, kS1);
        QCoreApplication::processEvents();
        QCOMPARE(extentOf(engine, kSticky), 700);
        QVERIFY2(isParked(engine, kSticky), "a column offering exactly the memory keeps it");

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

    // A lone tile offered the whole cross extent answers a pixel over it on a
    // fractional scale. That is the offer, within the rounding slack, and not
    // an answer that cannot fit the work area: the window stays tiled.
    void aCrossAnswerAPixelPastTheWorkAreaStaysTiled()
    {
        QObject owner;
        ScrollEngine* engine = makeFloorEngine(&owner);
        QCOMPARE(crossOf(engine, kA), ScrollTestUtils::kCrossExtent);
        answer(engine, kA, 300, 1);
        QVERIFY(!engine->isWindowFloatingInScroll(kA));
        QCOMPARE(crossOf(engine, kA), ScrollTestUtils::kCrossExtent);
    }

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
    // tile, and floats too. A shorter cross overshoot does not: the stack
    // tile here is offered 400 and answers 500, inside the 800 work area, and
    // stays tiled at its column's width, held at that share of the column
    // (the cross floor cases below). The longer answer then answers the 500
    // the tile is offered from there on.
    void aCrossAnswerPastTheWorkAreaFloatsTheWindow()
    {
        QObject owner;
        ScrollEngine* engine = makeStackEngine(&owner);
        const QRect offered = engine->lastManagedRect(kA);
        QCOMPARE(Ax::crossLen(offered), 400);

        engine->reportCommittedSize(kA, offered.size(), roleSize(300, 500));
        QCoreApplication::processEvents();
        QVERIFY(!engine->isWindowFloatingInScroll(kA));
        QCOMPARE(extentOf(engine, kA), 300);

        const QRect held = engine->lastManagedRect(kA);
        QCOMPARE(Ax::crossLen(held), 500);
        engine->reportCommittedSize(kA, held.size(), roleSize(300, 900));
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

    // Every tab of a column shares its extent along the strip, which is held
    // to the largest floor among them, so a user resize of the shown tab
    // settles the hidden tab's floor too. Left standing it held the column,
    // and the release reverted the drag.
    void aShownTabsResizeDropsAHiddenTabsFloor()
    {
        QObject owner;
        ScrollEngine* engine = makeFloorEngine(&owner);
        engine->consumeWindowIntoColumn(kS1); // b joins a's column
        engine->toggleColumnTabbed(kS1);
        engine->windowFocused(kB, kS1);
        engine->retile(kS1);
        QCoreApplication::processEvents();
        answer(engine, kB, 450); // b, shown, teaches 450
        QCOMPARE(extentOf(engine, kA), 450);

        engine->windowFocused(kA, kS1);
        QCoreApplication::processEvents();
        const QRect applied = engine->lastManagedRect(kA);
        QCOMPARE(Ax::mainLen(applied), 450);
        QRect settled = Ax::t(applied);
        settled.setWidth(350);
        settled = Ax::t(settled);
        engine->onWindowResized(kA, applied, settled, kS1);
        QCoreApplication::processEvents();
        QCOMPARE(extentOf(engine, kA), 350);
    }

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
    // A desktop move through the membership pass, into the desktop in view,
    // starts the new context unfloored too: the window gives up every context
    // it held, so what its client taught answered strips it has left. It used
    // to keep the floor this way and lose it on a move into a background
    // desktop, on nothing but which desktop it went to.
    void aMembershipMoveIntoTheDesktopInViewDropsTheFloor()
    {
        QObject owner;
        ScrollEngine* engine = makeDesktopFloorEngine(&owner);
        answer(engine, kSticky, 450);
        QCOMPARE(extentOf(engine, kSticky), 450);

        switchTo(engine, 2);
        adopt(engine, stickyMovedTo(2));
        QVERIFY2(engine->heldKeyForWindow(kSticky).has_value(), "precondition: moved onto desktop 2");
        QCOMPARE(engine->heldKeyForWindow(kSticky)->desktop, 2);
        QCOMPARE(extentOf(engine, kSticky), 300);
    }

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

    // A desktop renumber that lands a strip on a key still holding a live one
    // displaces every window of the strip there, and a window that strip
    // shares with a surviving desktop leaves the engine too. The renumber here
    // skips the prune the daemon runs ahead of it, which is what leaves the
    // removed desktop's strip standing at the key desktop 3 moves to. The
    // displaced release skipped the window, whose primary is the surviving
    // desktop in view, so the strip it really leaves records its placement,
    // and what its client taught goes with it.
    void aWindowADisplacementTakesFromASurvivingStripIsRecordedAndLosesItsFloor()
    {
        QObject owner;
        auto* tracker = new ScrollTestUtils::StubWindowTracking(&owner);
        ScrollEngine* engine = makeProviderEngine(&owner, {kS1}, {}, {}, tracker);
        engine->setEngineSettings(makeFloorSettings(&owner));
        engine->refreshConfigFromSettings();
        engine->setCurrentDesktopForScreen(kS1, 1);
        engine->windowOpened(kD1, kS1, 0, 0);
        engine->windowOpened(kSticky, kS1, 0, 0);
        engine->windowFocused(kSticky, kS1);
        engine->retile(kS1);
        QCoreApplication::processEvents();
        answer(engine, kSticky, 450);
        switchTo(engine, 2);
        engine->windowOpened(kD2, kS1, 0, 0);
        QCoreApplication::processEvents();
        adopt(engine, stickyBesideD2());
        switchTo(engine, 3);
        engine->windowOpened(kC, kS1, 0, 0);
        QCoreApplication::processEvents();
        switchTo(engine, 1);
        QCOMPARE(extentOf(engine, kSticky), 450);
        QVERIFY2(!tracker->placementStore().peekExact(kSticky).has_value(), "precondition: nothing recorded yet");
        QSignalSpy released(engine, &ScrollEngine::windowsReleased);

        engine->renumberDesktopsAfterRemoval(2);
        QCoreApplication::processEvents();

        QCOMPARE(released.count(), 1);
        QVERIFY(released.first().at(0).toStringList().contains(kSticky));
        QVERIFY(!engine->isWindowTracked(kSticky));
        QCOMPARE(engine->managedWindowOrder(kS1), QStringList{kD1});
        const auto record = tracker->placementStore().peekExact(kSticky);
        QVERIFY2(record.has_value(), "the surviving strip records the window it lets go");
        QCOMPARE(record->virtualDesktop, 1);
        QCOMPARE(record->slotFor(engine->engineId()).state, QString(PhosphorEngine::WindowPlacement::stateTiled()));

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

    // ── Across the strip ────────────────────────────────────────────────

    // A stacked tile whose client commits longer than its share of the
    // column, inside the work area, overlapped the tile under it. The share is
    // held at the answer, the sibling shrinks, and the column's extent along
    // the strip does not move. The change is republished like a floor along
    // the strip.
    void aTallerCommitHoldsItsShareOfTheStack()
    {
        QObject owner;
        ScrollEngine* engine = makeStackEngine(&owner);
        QCOMPARE(crossOf(engine, kA), 400);
        QSignalSpy placement(engine, &PhosphorEngine::PlacementEngineBase::placementChanged);

        answer(engine, kA, 300, 100); // offered 400, answers 500

        QVERIFY(!engine->isWindowFloatingInScroll(kA));
        QCOMPARE(placement.count(), 1);
        QCOMPARE(crossOf(engine, kA), 500);
        QCOMPARE(crossOf(engine, kB), 300);
        QCOMPARE(Ax::crossPos(engine->lastManagedRect(kB)), Ax::crossEnd(engine->lastManagedRect(kA)) + 1);
        QCOMPARE(extentOf(engine, kA), 300);
    }

    // The slack across the strip, the twin of anAnswerWithinTheSlackIsTheOffer:
    // a pixel taller than the share is the offer and teaches nothing, two
    // pixels is an answer. The floor's premise, the offer along the strip it
    // was learned at, has the same slack: a column a pixel wider keeps the
    // floor, two pixels wider drops it.
    void aCrossAnswerWithinTheSlackIsTheOffer()
    {
        {
            QObject owner;
            ScrollEngine* engine = makeStackEngine(&owner);
            answer(engine, kA, 300, 1);
            engine->retile(kS1);
            QCoreApplication::processEvents();
            QCOMPARE(crossOf(engine, kA), 400);
        }
        {
            QObject owner;
            ScrollEngine* engine = makeStackEngine(&owner);
            answer(engine, kA, 300, 2);
            QCOMPARE(crossOf(engine, kA), 402);
            QCOMPARE(crossOf(engine, kB), 398);
        }
        {
            QObject owner;
            ScrollEngine* engine = makeStackEngine(&owner);
            answer(engine, kA, 300, 100);
            QCOMPARE(crossOf(engine, kA), 500);
            engine->setColumnWidth(ColumnWidth::makeFixed(301), kS1);
            QCoreApplication::processEvents();
            QCOMPARE(extentOf(engine, kA), 301);
            QCOMPARE(crossOf(engine, kA), 500);
            engine->setColumnWidth(ColumnWidth::makeFixed(302), kS1);
            QCoreApplication::processEvents();
            QCOMPARE(extentOf(engine, kA), 302);
            QCOMPARE(crossOf(engine, kA), 400);
        }
    }

    // The steady state: the client answering the share it is now held at,
    // and a late copy of the answer that taught the floor, are both silent.
    // What this pins is the silence. The late copy answers an offer the strip
    // no longer holds, so the whole-tile gate would refuse it even without
    // the steady-state return that answers it first, and nothing observable
    // tells the two apart.
    void aRepeatedTallerAnswerIsSilent()
    {
        QObject owner;
        ScrollEngine* engine = makeStackEngine(&owner);
        const QSize firstOffer = engine->lastManagedRect(kA).size();
        answer(engine, kA, 300, 100);
        QCOMPARE(crossOf(engine, kA), 500);

        QSignalSpy placement(engine, &PhosphorEngine::PlacementEngineBase::placementChanged);
        QSignalSpy tiled(engine, &ScrollEngine::windowsTiled);
        answer(engine, kA, 300); // the held 500, answered as offered
        engine->reportCommittedSize(kA, firstOffer, roleSize(300, 500)); // the teaching answer again
        QCoreApplication::processEvents();
        QCOMPARE(placement.count(), 0);
        QCOMPARE(tiled.count(), 0);
        QCOMPARE(crossOf(engine, kA), 500);
    }

    // A share the stack can never hold beside its siblings' own floors floats
    // the window, the policy any window that outgrows its slot gets. A share
    // that fits exactly takes the sibling down to its minimum instead.
    void aCrossFloorTheStackCannotHoldFloatsTheWindow()
    {
        {
            QObject owner;
            ScrollEngine* engine = makeStackEngine(&owner);
            const QSize min400 = roleSize(0, 400);
            engine->windowMinSizeUpdated(kB, min400.width(), min400.height());
            QCoreApplication::processEvents();
            answer(engine, kA, 300, 100); // 500 beside a 400 minimum in 800
            QVERIFY(engine->isWindowFloatingInScroll(kA));
            QVERIFY(!engine->isWindowFloatingInScroll(kB));
        }
        {
            QObject owner;
            ScrollEngine* engine = makeStackEngine(&owner);
            const QSize min300 = roleSize(0, 300);
            engine->windowMinSizeUpdated(kB, min300.width(), min300.height());
            QCoreApplication::processEvents();
            answer(engine, kA, 300, 100); // 500 beside a 300 minimum: fits exactly
            QVERIFY(!engine->isWindowFloatingInScroll(kA));
            QCOMPARE(crossOf(engine, kA), 500);
            QCOMPARE(crossOf(engine, kB), 300);
        }
    }

    // Alone in its column the tile, at its Auto height, is offered the whole
    // work area across the strip, which satisfies the floor, so it goes. A
    // sibling that joins later is split as asked until the client answers
    // again.
    void aCrossFloorDropsWhenItsTileIsAlone()
    {
        QObject owner;
        ScrollEngine* engine = makeStackEngine(&owner);
        answer(engine, kA, 300, 100);
        QCOMPARE(crossOf(engine, kA), 500);

        engine->windowClosed(kB);
        engine->retile(kS1);
        QCoreApplication::processEvents();
        QCOMPARE(crossOf(engine, kA), 800);

        engine->windowOpened(kC, kS1, 0, 0);
        engine->windowFocused(kA, kS1);
        engine->consumeWindowIntoColumn(kS1);
        engine->retile(kS1);
        QCoreApplication::processEvents();
        ScrollState* state = stateOf(engine);
        QVERIFY(state);
        QCOMPARE(state->strip().columnCount(), 1);
        QCOMPARE(crossOf(engine, kA), 400);
    }

    // A sibling that raises its own minimum past what the stack can give the
    // floored tile leaves a floor it can no longer hold. It is dropped rather
    // than floating the window, and the stack is split as asked. A client
    // that still answers longer then gets the float.
    void aCrossFloorDropsWhenTheStackCanNoLongerHoldIt()
    {
        QObject owner;
        ScrollEngine* engine = makeStackEngine(&owner);
        answer(engine, kA, 300, 100);
        QCOMPARE(crossOf(engine, kA), 500);

        QSignalSpy placement(engine, &PhosphorEngine::PlacementEngineBase::placementChanged);
        const QSize min400 = roleSize(0, 400);
        engine->windowMinSizeUpdated(kB, min400.width(), min400.height());
        QCoreApplication::processEvents();
        QVERIFY(!engine->isWindowFloatingInScroll(kA));
        QVERIFY(placement.count() > 0);
        QCOMPARE(crossOf(engine, kA), 400);
        QCOMPARE(crossOf(engine, kB), 400);

        answer(engine, kA, 300, 100); // offered 400, answers 500 again
        QVERIFY(engine->isWindowFloatingInScroll(kA));
    }

    // The floor's premise is the tile's offer along the strip. A client that
    // picks its height from its width answers a new width differently, and
    // never reports a shorter cross answer, so a column offered another width
    // drops the floor.
    void aWidthChangeDropsTheCrossFloor()
    {
        QObject owner;
        ScrollEngine* engine = makeStackEngine(&owner);
        answer(engine, kA, 300, 100);
        QCOMPARE(crossOf(engine, kA), 500);

        engine->setColumnWidth(ColumnWidth::makeFixed(400), kS1);
        QCoreApplication::processEvents();
        QCOMPARE(extentOf(engine, kA), 400);
        QCOMPARE(crossOf(engine, kA), 400);
    }

    // The user's own resize across the strip settles the tile's share, so
    // the floor goes and the height the user released it at stands. A resize
    // along the strip leaves the cross floor to its premise: here it lands on
    // the width the column already had, and the floor holds.
    void aUserResizeAcrossTheStripDropsTheCrossFloor()
    {
        {
            QObject owner;
            ScrollEngine* engine = makeStackEngine(&owner);
            answer(engine, kA, 300, 100);
            const QRect applied = engine->lastManagedRect(kA);
            QCOMPARE(Ax::crossLen(applied), 500);
            QRect settled = Ax::t(applied);
            settled.setHeight(450);
            settled = Ax::t(settled);
            engine->onWindowResized(kA, applied, settled, kS1);
            QCoreApplication::processEvents();
            QCOMPARE(crossOf(engine, kA), 450);
            QCOMPARE(crossOf(engine, kB), 350);
        }
        {
            QObject owner;
            ScrollEngine* engine = makeStackEngine(&owner);
            answer(engine, kA, 300, 100);
            const QRect applied = engine->lastManagedRect(kA);
            QRect start = Ax::t(applied);
            start.setWidth(320); // the gesture started from the client's own frame
            start = Ax::t(start);
            engine->onWindowResized(kA, start, applied, kS1);
            QCoreApplication::processEvents();
            QCOMPARE(extentOf(engine, kA), 300);
            QCOMPARE(crossOf(engine, kA), 500);
        }
    }

    // A LOWERED declared minimum across the strip drops the cross floor and
    // announces the change. A raised one takes nothing, and one lowered
    // along the strip leaves it.
    void aLoweredMinimumAcrossDropsTheCrossFloor()
    {
        {
            QObject owner;
            ScrollEngine* engine = makeStackEngine(&owner);
            const QSize min100 = roleSize(0, 100);
            engine->windowMinSizeUpdated(kA, min100.width(), min100.height());
            QCoreApplication::processEvents();
            answer(engine, kA, 300, 100);
            QCOMPARE(crossOf(engine, kA), 500);

            QSignalSpy placement(engine, &PhosphorEngine::PlacementEngineBase::placementChanged);
            const QSize min150 = roleSize(0, 150);
            engine->windowMinSizeUpdated(kA, min150.width(), min150.height());
            QCOMPARE(placement.count(), 0); // raised: nothing dropped, nothing to announce
            QCoreApplication::processEvents();
            QCOMPARE(crossOf(engine, kA), 500);

            const QSize min50 = roleSize(0, 50);
            engine->windowMinSizeUpdated(kA, min50.width(), min50.height());
            QCOMPARE(placement.count(), 1);
            QCoreApplication::processEvents();
            QCOMPARE(crossOf(engine, kA), 400);
        }
        {
            QObject owner;
            ScrollEngine* engine = makeStackEngine(&owner);
            const QSize min100 = roleSize(100, 0);
            engine->windowMinSizeUpdated(kA, min100.width(), min100.height());
            QCoreApplication::processEvents();
            answer(engine, kA, 300, 100);

            QSignalSpy placement(engine, &PhosphorEngine::PlacementEngineBase::placementChanged);
            const QSize min50 = roleSize(50, 0);
            engine->windowMinSizeUpdated(kA, min50.width(), min50.height());
            QCOMPARE(placement.count(), 0);
            QCoreApplication::processEvents();
            QCOMPARE(crossOf(engine, kA), 500);
        }
    }

    // A float round trip brings the tile back into its stack held to the
    // share its client taught, like the floor along the strip. Closing the
    // window, one of the leave paths, takes it. The reopened window goes
    // straight into b's stack: opened alone, its column would satisfy any
    // floor it still held and drop it, and the close would go untested.
    void aCrossFloorSurvivesAFloatRoundTripAndDropsOnALeave()
    {
        QObject owner;
        StubScrollSettings* settings = nullptr;
        ScrollEngine* engine = makeStackEngine(&owner, &settings);
        answer(engine, kA, 300, 100);
        engine->setWindowFloat(kA, true, kS1);
        engine->setWindowFloat(kA, false, kS1);
        engine->retile(kS1);
        QCoreApplication::processEvents();
        ScrollState* state = stateOf(engine);
        QVERIFY(state);
        QCOMPARE(state->strip().columnCount(), 1);
        QCOMPARE(crossOf(engine, kA), 500);

        engine->windowClosed(kA);
        settings->insertPosition = static_cast<int>(ScrollInsertPosition::IntoActiveColumn);
        engine->refreshConfigFromSettings();
        engine->windowFocused(kB, kS1);
        engine->windowOpened(kA, kS1, 0, 0);
        engine->retile(kS1);
        QCoreApplication::processEvents();
        state = stateOf(engine);
        QVERIFY(state);
        QCOMPARE(state->strip().columnCount(), 1);
        QCOMPARE(state->strip().columnOfWindow(kA), state->strip().columnOfWindow(kB));
        QCOMPARE(crossOf(engine, kA), 400);
    }

    // A height learned across a horizontal strip is not a width across a
    // vertical one. A rotation flips the strip's axis and splits the stack
    // the other way with no floor read; the rotation back finds the floor
    // again, its premise (the column's width) unchanged.
    void aRotationNeverReadsACrossFloorAcrossTheOtherWay()
    {
        QObject owner;
        auto geometry = std::make_shared<QRect>(defaultScreenRect());
        ScrollEngine* engine = makeStackEngine(&owner, nullptr, true, [geometry](const QString&) {
            return *geometry;
        });
        answer(engine, kA, 300, 300); // offered 400, answers 700
        QCOMPARE(crossOf(engine, kA), 700);

        // The rotated strip runs along what was the cross axis, so its own
        // cross extent is the suite's main one.
        *geometry = Ax::t(QRect(0, 0, 800, 1200));
        engine->retile(kS1);
        QCoreApplication::processEvents();
        // Without the flip the floor would still be read, and the split below
        // would pass for an unrelated reason.
        QVERIFY2(engine->stripAxisForScreen(kS1) != Ax::stripAxis(), "precondition: the rotation flips the axis");
        QCOMPARE(Ax::mainLen(engine->lastManagedRect(kA)), Ax::mainLen(engine->lastManagedRect(kB)));

        *geometry = defaultScreenRect();
        engine->retile(kS1);
        QCoreApplication::processEvents();
        QCOMPARE(crossOf(engine, kA), 700);
    }

    // Longer on both axes teaches both floors in one answer, and the cross
    // floor's premise is the widened offer, so it holds.
    void aWiderAndTallerAnswerTeachesBoth()
    {
        QObject owner;
        ScrollEngine* engine = makeStackEngine(&owner);
        answer(engine, kA, 450, 100);
        QCOMPARE(extentOf(engine, kA), 450);
        QCOMPARE(crossOf(engine, kA), 500);
        QCOMPARE(crossOf(engine, kB), 300);
        engine->retile(kS1);
        QCoreApplication::processEvents();
        QCOMPARE(crossOf(engine, kA), 500);
    }

    // A shorter answer gives the cross floor back, the narrower answer's twin:
    // down to the answer while that still overshoots the share the tile gets
    // without the floor, entirely once it does not. A taller answer after a
    // give-back is a client that does not answer one offer the same way
    // twice, so the re-taught floor is pinned, and a shorter answer then
    // keeps it.
    void aShorterAnswerGivesTheCrossFloorBack()
    {
        QObject owner;
        ScrollEngine* engine = makeStackEngine(&owner);
        answer(engine, kA, 300, 100); // offered 400, answers 500
        QCOMPARE(crossOf(engine, kA), 500);

        answer(engine, kA, 300, -50); // offered 500, answers 450: still past the 400 share
        QCOMPARE(crossOf(engine, kA), 450);
        QCOMPARE(crossOf(engine, kB), 350);

        answer(engine, kA, 300, -49); // offered 450, answers 401: the share, within the slack
        QCOMPARE(crossOf(engine, kA), 400);

        answer(engine, kA, 300, 100); // offered 400, answers 500 again: re-taught, pinned
        QCOMPARE(crossOf(engine, kA), 500);
        answer(engine, kA, 300, -50); // offered 500, answers 450: the pin holds
        engine->retile(kS1);
        QCoreApplication::processEvents();
        QCOMPARE(crossOf(engine, kA), 500);
    }

    // A wider answer moves the tile's offer along the strip, and the cross
    // extent in the same commit was given at that width, so a held cross
    // floor takes the new offer as its premise. Read against the old one, the
    // next pass dropped it and waited a round trip for the client to teach it
    // again.
    void aWiderAnswerKeepsTheHeldCrossFloor()
    {
        QObject owner;
        ScrollEngine* engine = makeStackEngine(&owner);
        answer(engine, kA, 300, 100);
        QCOMPARE(crossOf(engine, kA), 500);

        answer(engine, kA, 450); // offered 300 x 500, answers 450 x 500
        QCOMPARE(extentOf(engine, kA), 450);
        engine->retile(kS1);
        QCoreApplication::processEvents();
        QCOMPARE(crossOf(engine, kA), 500);
        QCOMPARE(crossOf(engine, kB), 300);
    }

    // Siblings whose own floors fill the column leave the floored tile a
    // capacity of nothing, which reads as 0 and not as the "no tile here"
    // sentinel, so the held floor is dropped like any the stack can no longer
    // hold. A negative capacity kept it, and the share came back the moment
    // the siblings let go.
    void aCrossFloorDropsWhenItsSiblingsFillTheColumn()
    {
        QObject owner;
        ScrollEngine* engine = makeStackEngine(&owner);
        engine->windowOpened(kC, kS1, 0, 0);
        engine->windowFocused(kA, kS1);
        engine->consumeWindowIntoColumn(kS1); // c joins too: a over b over c
        engine->retile(kS1);
        QCoreApplication::processEvents();
        ScrollState* state = stateOf(engine);
        QVERIFY(state);
        QCOMPARE(state->strip().columnCount(), 1);
        const int evenShare = crossOf(engine, kA);
        answer(engine, kA, 300, 100);
        QCOMPARE(crossOf(engine, kA), evenShare + 100);

        // b and c each declare 500 across: 1000 of an 800 column.
        const QSize min500 = roleSize(0, 500);
        engine->windowMinSizeUpdated(kB, min500.width(), min500.height());
        engine->windowMinSizeUpdated(kC, min500.width(), min500.height());
        QCoreApplication::processEvents();
        QVERIFY(!engine->isWindowFloatingInScroll(kA));

        // They let go, and a is split as asked: its floor went.
        engine->windowMinSizeUpdated(kB, 0, 0);
        engine->windowMinSizeUpdated(kC, 0, 0);
        engine->retile(kS1);
        QCoreApplication::processEvents();
        QCOMPARE(crossOf(engine, kA), evenShare);
    }

    // Retile resets the stack's heights to the even split, and a cross floor
    // is not a height: the floored tile keeps its share.
    void retileKeepsTheCrossFloor()
    {
        QObject owner;
        ScrollEngine* engine = makeStackEngine(&owner);
        answer(engine, kA, 300, 100);
        QCOMPARE(crossOf(engine, kA), 500);
        engine->resetStripToDefaults(kS1);
        QCoreApplication::processEvents();
        QCOMPARE(crossOf(engine, kA), 500);
        QCOMPARE(crossOf(engine, kB), 300);
    }

    // A work area that shrinks across the strip under a held cross floor
    // leaves a share the stack can no longer give. It is dropped, announced,
    // and stays dropped when the area grows again, the twin of
    // aWorkAreaShrinkDropsAFloorItCanNoLongerHold. The shrunk area keeps the
    // strip's orientation and its extent along the strip, so the floor's
    // premise is untouched and only the capacity drops it.
    void aWorkAreaShrinkDropsACrossFloorItCanNoLongerHold()
    {
        QObject owner;
        auto available = std::make_shared<QRect>(defaultScreenRect());
        ScrollEngine* engine = makeStackEngine(
            &owner, nullptr, true,
            [](const QString&) {
                return defaultScreenRect();
            },
            [available](const QString&) {
                return *available;
            });
        answer(engine, kA, 300, 100);
        QCOMPARE(crossOf(engine, kA), 500);

        QSignalSpy placement(engine, &PhosphorEngine::PlacementEngineBase::placementChanged);
        *available = Ax::t(QRect(0, 0, 1200, 450));
        engine->retile(kS1);
        QCoreApplication::processEvents();
        QCOMPARE(extentOf(engine, kA), 300);
        QCOMPARE(crossOf(engine, kA), 225);
        QCOMPARE(crossOf(engine, kB), 225);
        QVERIFY(placement.count() > 0);
        QCOMPARE(placement.last().at(0).toString(), kS1);

        *available = defaultScreenRect();
        engine->retile(kS1);
        QCoreApplication::processEvents();
        QCOMPARE(crossOf(engine, kA), 400);
    }

    // A taller answer from a window whose strip is in the BACKGROUND is judged
    // against that strip under its own context, the twin of
    // aBackgroundWindowsAnswerIsLearnedForItsOwnStrip: learned, and holding
    // its share when the desktop returns.
    void aBackgroundWindowsTallerAnswerIsLearnedForItsOwnStrip()
    {
        QObject owner;
        ScrollEngine* engine = engineWith(&owner, makeFloorSettings(&owner), {kS1});
        engine->setCurrentDesktopForScreen(kS1, 1);
        engine->windowOpened(kD1, kS1, 0, 0);
        engine->windowOpened(kA, kS1, 0, 0);
        engine->windowFocused(kD1, kS1);
        engine->consumeWindowIntoColumn(kS1); // a joins d1's column
        engine->retile(kS1);
        QCoreApplication::processEvents();
        QCOMPARE(crossOf(engine, kA), 400);

        switchTo(engine, 2);
        answer(engine, kA, 300, 100);
        switchTo(engine, 1);
        engine->retile(kS1);
        QCoreApplication::processEvents();
        QCOMPARE(crossOf(engine, kA), 500);
        QCOMPARE(crossOf(engine, kD1), 300);
    }

    // A frame under an interactive move is motion, not an answer, across the
    // strip as along it.
    void aDraggedWindowsTallerAnswerTeachesNothing()
    {
        QObject owner;
        ScrollEngine* engine = makeStackEngine(&owner);
        engine->setInteractiveDragWindow(kA);
        answer(engine, kA, 300, 100);
        engine->setInteractiveDragWindow(QString());
        engine->retile(kS1);
        QCoreApplication::processEvents();
        QCOMPARE(crossOf(engine, kA), 400);
    }

    // The cross floor is the window's, read by every desktop's stack it holds
    // a tile in, so it goes only once EVERY one of those tiles is offered at
    // least the floor without it, the twin of
    // aFloorDropsOnlyWhenEveryDesktopSatisfiesIt. The sticky window taught
    // 500 in a stack on desktop 1 and is adopted onto desktop 2. Alone on
    // desktop 1 afterwards, its share satisfies the floor there; whether the
    // floor goes then turns on desktop 2's share, as a sibling that joins on
    // desktop 1 shows. That share, not the tile's extent along the strip
    // (300 on both, under the floor either way), is what is measured.
    void aCrossFloorDropsOnlyWhenEveryDesktopSatisfiesIt()
    {
        // The sticky window's 500 floor learned under d1 on desktop 1, then
        // adopted onto desktop 2 beside d2, stacked under it when
        // @p stackedThere. Desktop 2 is in view.
        const auto adoptedOntoDesktop2 = [](QObject* owner, bool stackedThere) {
            ScrollEngine* engine = engineWith(owner, makeFloorSettings(owner), {kS1});
            engine->setCurrentDesktopForScreen(kS1, 1);
            engine->windowOpened(kD1, kS1, 0, 0);
            engine->windowOpened(kSticky, kS1, 0, 0);
            engine->windowFocused(kD1, kS1);
            engine->consumeWindowIntoColumn(kS1); // the sticky one joins d1's column
            engine->retile(kS1);
            QCoreApplication::processEvents();
            answer(engine, kSticky, 300, 100);
            switchTo(engine, 2);
            engine->windowOpened(kD2, kS1, 0, 0);
            QCoreApplication::processEvents();
            adopt(engine, stickyBesideD2());
            if (stackedThere) {
                engine->windowFocused(kD2, kS1);
                engine->consumeWindowIntoColumn(kS1); // the sticky one joins d2's column
            }
            engine->retile(kS1);
            QCoreApplication::processEvents();
            return engine;
        };
        // Back on desktop 1: d1 closes, so the sticky window is alone there,
        // then c opens and joins its column.
        const auto aloneThenJoined = [](ScrollEngine* engine) {
            switchTo(engine, 1);
            engine->windowClosed(kD1);
            engine->retile(kS1);
            QCoreApplication::processEvents();
            const int alone = crossOf(engine, kSticky);
            engine->windowOpened(kC, kS1, 0, 0);
            engine->windowFocused(kSticky, kS1);
            engine->consumeWindowIntoColumn(kS1); // c joins the sticky one's column
            engine->retile(kS1);
            QCoreApplication::processEvents();
            return alone;
        };
        {
            // Desktop 2 stacks it under d2, a 400 share without the floor:
            // not satisfied there, so the floor holds the rejoined stack.
            QObject owner;
            ScrollEngine* engine = adoptedOntoDesktop2(&owner, true);
            QVERIFY2(engine->heldKeyForWindow(kSticky).has_value(), "precondition: adopted onto desktop 2");
            QCOMPARE(crossOf(engine, kSticky), 500);
            QCOMPARE(aloneThenJoined(engine), 800);
            QCOMPARE(crossOf(engine, kSticky), 500);
            QCOMPARE(crossOf(engine, kC), 300);
        }
        {
            // Desktop 2 shows it alone, the whole 800: satisfied there too, so
            // the floor goes and the rejoined stack splits as asked.
            QObject owner;
            ScrollEngine* engine = adoptedOntoDesktop2(&owner, false);
            QVERIFY2(engine->heldKeyForWindow(kSticky).has_value(), "precondition: adopted onto desktop 2");
            QCOMPARE(crossOf(engine, kSticky), 800);
            QCOMPARE(aloneThenJoined(engine), 800);
            QCOMPARE(crossOf(engine, kSticky), 400);
            QCOMPARE(crossOf(engine, kC), 400);
        }
    }

    // The minimum sizes setting gates the cross floor on both ends, like the
    // floor along the strip, and a maximized-to-edges column neither reads a
    // held one nor learns from its client.
    void theCrossFloorRidesRespectMinimumSizeAndMaximizeToEdges()
    {
        {
            QObject owner;
            StubScrollSettings* settings = nullptr;
            ScrollEngine* engine = makeStackEngine(&owner, &settings, /*respectMinimumSize=*/false);
            answer(engine, kA, 300, 100);
            QCOMPARE(crossOf(engine, kA), 400);
            settings->respectMinimumSize = true;
            engine->refreshConfigFromSettings();
            QCoreApplication::processEvents();
            QCOMPARE(crossOf(engine, kA), 400);
        }
        {
            QObject owner;
            StubScrollSettings* settings = nullptr;
            ScrollEngine* engine = makeStackEngine(&owner, &settings);
            answer(engine, kA, 300, 100);
            QCOMPARE(crossOf(engine, kA), 500);
            settings->respectMinimumSize = false;
            engine->refreshConfigFromSettings();
            QCoreApplication::processEvents();
            QCOMPARE(crossOf(engine, kA), 400);
            settings->respectMinimumSize = true;
            engine->refreshConfigFromSettings();
            QCoreApplication::processEvents();
            QCOMPARE(crossOf(engine, kA), 500);
        }
        {
            QObject owner;
            ScrollEngine* engine = makeStackEngine(&owner);
            answer(engine, kA, 300, 100);
            QVERIFY(engine->toggleMaximizeToEdges(kS1));
            QCoreApplication::processEvents();
            QCOMPARE(crossOf(engine, kA), 400);
            answer(engine, kA, Ax::mainLen(engine->lastManagedRect(kA)), 200);
            QVERIFY(!engine->isWindowFloatingInScroll(kA));
            QVERIFY(engine->toggleMaximizeToEdges(kS1));
            QCoreApplication::processEvents();
            QCOMPARE(crossOf(engine, kA), 500);
        }
    }

    // A lone tile with a Fixed height shorter than its client's answer is
    // raised to the answer and placed the way a declared minimum places it:
    // centred here, since short columns centre, and tiled.
    void aLoneFixedTileIsRaisedToTheCommit()
    {
        QObject owner;
        StubScrollSettings* settings = nullptr;
        ScrollEngine* engine = makeFloorEngine(&owner, &settings);
        settings->centerShortColumns = true;
        engine->refreshConfigFromSettings();
        engine->setWindowHeight(WindowHeight::makeFixed(400), kS1);
        QCoreApplication::processEvents();
        QCOMPARE(crossOf(engine, kA), 400);
        QCOMPARE(Ax::crossPos(engine->lastManagedRect(kA)), 200);

        answer(engine, kA, 300, 100);
        QVERIFY(!engine->isWindowFloatingInScroll(kA));
        QCOMPARE(crossOf(engine, kA), 500);
        QCOMPARE(Ax::crossPos(engine->lastManagedRect(kA)), 150);
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

    // The same refusal under a committed cross floor: the tile renders at
    // the floor whatever shorter preset is asked for.
    void aHeightCycleUnderACrossFloorRefusesAnEntryThatRendersTheSame()
    {
        ScrollLayoutParams params = ScrollTestUtils::defaultParams();
        params.respectMinimumSize = true;
        params.committedCrossFloor = [](const QString& windowId) {
            return windowId == QLatin1String("a") ? 700 : 0;
        };
        ScrollStrip strip;
        QVERIFY(strip.insertWindow(QStringLiteral("a"), ScrollTestUtils::kHalf, ColumnDisplay::Normal, params));
        const WindowHeight third = WindowHeight::makePreset(1.0 / 3.0);
        QVERIFY(strip.setActiveWindowHeight(third));
        QCOMPARE(Ax::crossLen(ScrollTestUtils::rectOf(strip.relayout(params), QStringLiteral("a"))), 700);

        QVERIFY(!strip.cycleActiveWindowPresetHeight(-1, params));
        QVERIFY(strip.columns().at(0).tiles.at(0).height == third);

        QVERIFY(strip.cycleActiveWindowPresetHeight(+1, params));
        QCOMPARE(Ax::crossLen(ScrollTestUtils::rectOf(strip.relayout(params), QStringLiteral("a"))), 800);
    }

    // At the strip level a committed cross floor holds its tile's share of the
    // stack: a is held at 500 and b alone gives way, down to 290 of the 790
    // the 10px gap leaves. Without the lookup the stack splits evenly.
    void aCrossFloorHoldsItsTileWhileTheSiblingGivesWay()
    {
        ScrollLayoutParams params = ScrollTestUtils::defaultParams();
        params.respectMinimumSize = true;
        params.committedCrossFloor = [](const QString& windowId) {
            return windowId == QLatin1String("a") ? 500 : 0;
        };
        ScrollStrip strip;
        QVERIFY(strip.insertWindow(QStringLiteral("a"), ScrollTestUtils::kHalf, ColumnDisplay::Normal, params));
        QVERIFY(strip.insertWindowIntoActiveColumn(QStringLiteral("b"), ScrollTestUtils::kHalf, ColumnDisplay::Normal,
                                                   params));
        QCOMPARE(strip.columnCount(), 1);
        const ResolvedStrip resolved = strip.relayout(params);
        QCOMPARE(Ax::crossLen(ScrollTestUtils::rectOf(resolved, QStringLiteral("a"))), 500);
        QCOMPARE(Ax::crossLen(ScrollTestUtils::rectOf(resolved, QStringLiteral("b"))), 290);

        // Without the lookup the stack splits evenly.
        params.committedCrossFloor = {};
        QCOMPARE(Ax::crossLen(ScrollTestUtils::rectOf(strip.relayout(params), QStringLiteral("a"))), 395);
    }
};

QTEST_GUILESS_MAIN(TestScrollEngineCommittedFloor)
#include "test_scrollengine_committedfloor.moc"
