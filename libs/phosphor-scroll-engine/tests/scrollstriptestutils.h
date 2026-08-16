// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

// Shared fixture helpers for the PhosphorScrollEngine suites that fixture
// against a screen — the two strip-model files and the nine engine files.
// (test_stripaxis and test_scrollpark stand alone: they exercise the mapper
// and the park geometry directly and need no screen fixture.) One definition of the
// 1200x800 geometry, because a work area that drifts between files quietly
// changes what every hardcoded pixel expectation means, and one definition
// of the headless engine fixture, because its two geometry providers are
// the seam the parking tests need to tell apart.
//
// THREE params helpers, deliberately: defaultParams() (10px gap) is for the
// PURE-STRIP fixtures; engineParams() (0 gap) mirrors what makeProviderEngine
// actually computes — its engine has no IScrollSettings and no gap provider,
// so layoutParamsForScreen leaves every gap at 0 — and gappedEngineParams()
// (kEngineInnerGap) pairs with makeGappedProviderEngine for gap-dependent
// geometry (its own doc below). An engine test that mixes direct strip calls
// with engine-driven relayouts must use the helper matching its factory, or
// the same strip is laid out against two different gap values in one test
// body.

#include <PhosphorEngine/ICrossSurfaceResolver.h>
#include <PhosphorProtocol/ScrollAxisEnum.h>
#include <PhosphorScrollEngine/ScrollEngine.h>
#include <PhosphorScrollEngine/ScrollStrip.h>
#include <PhosphorScrollEngine/ScrollTypes.h>
#include <PhosphorScrollEngine/StripAxis.h>

#include <QByteArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QObject>
#include <QPoint>
#include <QRect>
#include <QSet>
#include <QSize>
#include <QString>
// The fixture guard below asserts, so this header owns its QTest dependency
// rather than relying on every includer having reached for it first.
#include <QTest>

#include <functional>

namespace ScrollTestUtils {

/// The one strip geometry the whole suite fixtures against, named for ROLES
/// rather than for screen dimensions. MAIN is always 1200 and CROSS always
/// 800, under BOTH orientations — that is the whole point, because it keeps
/// every hardcoded pixel literal in the suite meaning the same thing after a
/// transpose. Do not reconstruct a physical rect from these by hand; ask
/// Ax::defaultScreenRect() for it.
inline constexpr int kMainExtent = 1200;
inline constexpr int kCrossExtent = 800;

/// Axis harness. Every suite runs TWICE — once per orientation, as two ctest
/// registrations over one binary — and the axis is a property of the PROCESS,
/// read once from PZ_SCROLL_TEST_AXIS.
///
/// The role readers here DELEGATE to the engine's own StripAxis rather than
/// reimplementing the mapping. That is deliberate and load-bearing: a second,
/// independent coordinate mapper inside the tests would make a bug in
/// StripAxis invisible to every test that reaches geometry through these
/// helpers, and the suite would be verifying its own transpose instead of the
/// engine's.
///
/// Ax::t() is the ONE piece of mapping the harness owns outright, because it
/// is the test's DEFINITION of correctness and must not be able to agree with
/// the engine by construction.
namespace Ax {

/// Resolved ONCE per process. The engine's own enum, never a test-local twin,
/// so an engine-side rename breaks the harness at compile time.
inline PhosphorProtocol::ScrollAxis axis()
{
    static const PhosphorProtocol::ScrollAxis resolved = [] {
        return qgetenv("PZ_SCROLL_TEST_AXIS") == QByteArrayLiteral("vertical")
            ? PhosphorProtocol::ScrollAxis::Vertical
            : PhosphorProtocol::ScrollAxis::Horizontal;
    }();
    return resolved;
}

inline bool vertical()
{
    return axis() == PhosphorProtocol::ScrollAxis::Vertical;
}

/// The engine's mapper, for the role readers below to delegate to.
inline PhosphorScrollEngine::StripAxis stripAxis()
{
    return PhosphorScrollEngine::StripAxis(axis());
}

// -- The transpose, owned by the harness -------------------------------
//
// T(QRect(x, y, w, h)) = QRect(y, x, h, w) — a reflection through the line
// y = x. Every fixture in the suite is anchored at the origin, so there is no
// offset term and T is an involution: T(T(r)) == r.

inline QRect t(const QRect& r)
{
    return vertical() ? QRect(r.y(), r.x(), r.height(), r.width()) : r;
}
inline QSize t(const QSize& s)
{
    return vertical() ? QSize(s.height(), s.width()) : s;
}
inline QPoint t(const QPoint& p)
{
    return vertical() ? QPoint(p.y(), p.x()) : p;
}

// -- Role readers over a PHYSICAL rect the engine produced --------------

inline int mainPos(const QRect& r)
{
    return stripAxis().mainPos(r);
}
inline int mainLen(const QRect& r)
{
    return stripAxis().mainSize(r);
}
inline int mainEnd(const QRect& r)
{
    return stripAxis().mainHigh(r);
}
inline int crossPos(const QRect& r)
{
    return stripAxis().crossPos(r);
}
inline int crossLen(const QRect& r)
{
    return stripAxis().crossSize(r);
}
inline int crossEnd(const QRect& r)
{
    return stripAxis().crossHigh(r);
}

/// A point built from ROLE coordinates: @p main runs along the strip and
/// @p cross across it. The inverse of the mainPos/crossPos readers, for the
/// hit-test probes that have to name a spot inside a resolved tile.
inline QPoint point(int main, int cross)
{
    return vertical() ? QPoint(cross, main) : QPoint(main, cross);
}

/// A fraction of the way along / across a rect the engine produced — the shape
/// every hit-test probe wants ("the upper quarter of this tile" is a CROSS
/// quarter, whichever way the strip runs).
inline QPoint pointIn(const QRect& r, qreal mainFraction, qreal crossFraction)
{
    return point(mainPos(r) + qRound(mainFraction * mainLen(r)), crossPos(r) + qRound(crossFraction * crossLen(r)));
}

// -- Role names for direction tokens and wire keys ----------------------
//
// Strip travel runs lead -> trail along the MAIN axis; the within-column
// stack runs crossLead -> crossTrail. Under a transpose the two vocabularies
// swap physical words, which is why no assertion about BEHAVIOUR should ever
// spell "left" or "up" directly.

inline QString edgeLead()
{
    return vertical() ? QStringLiteral("top") : QStringLiteral("left");
}
inline QString edgeTrail()
{
    return vertical() ? QStringLiteral("bottom") : QStringLiteral("right");
}
inline QString crossLead()
{
    return vertical() ? QStringLiteral("left") : QStringLiteral("up");
}
inline QString crossTrail()
{
    return vertical() ? QStringLiteral("right") : QStringLiteral("down");
}

/// The NAVIGATION token that walks the strip, which is a different vocabulary
/// from edgeLead/edgeTrail above even though both name the main axis: the
/// navigation verbs and the neighbour resolver speak up/down/left/right, while
/// a park edge and a tab indicator speak top/bottom/left/right. On a
/// horizontal strip the two coincide and the distinction is invisible, which
/// is exactly why it has to be spelled out here — passing edgeTrail() to
/// moveFocusedInDirection sends "bottom", which is not a direction token at
/// all, and the verb answers no_target rather than crossing.
inline QString navLead()
{
    return vertical() ? QStringLiteral("up") : QStringLiteral("left");
}
inline QString navTrail()
{
    return vertical() ? QStringLiteral("down") : QStringLiteral("right");
}

inline QLatin1String mainPosKey()
{
    return vertical() ? QLatin1String("y") : QLatin1String("x");
}
inline QLatin1String mainLenKey()
{
    return vertical() ? QLatin1String("height") : QLatin1String("width");
}
/// Role readers over a windowsTiled batch ENTRY, whose four geometry keys are
/// physical for the same reason the rects are. Same rule as the rect readers:
/// x/width are the main quantities on a horizontal strip, y/height the cross
/// ones, and both swap physical words under a transpose.
inline int entryMainPos(const QJsonObject& o)
{
    return o.value(mainPosKey()).toInt();
}
inline int entryMainLen(const QJsonObject& o)
{
    return o.value(mainLenKey()).toInt();
}
/// The entry's INCLUSIVE far main edge, matching QRect::right()/bottom().
inline int entryMainEnd(const QJsonObject& o)
{
    return entryMainPos(o) + entryMainLen(o) - 1;
}

/// The suite's screen rect for the running axis: 1200x800 horizontally,
/// 800x1200 vertically, both at the origin. Main stays 1200 either way.
inline QRect defaultScreenRect()
{
    return vertical() ? QRect(0, 0, kCrossExtent, kMainExtent) : QRect(0, 0, kMainExtent, kCrossExtent);
}

} // namespace Ax

/// The suite's screen rect. Axis-aware via the harness, so a fixture cannot
/// go on describing a landscape screen after a transpose.
inline QRect defaultScreenRect()
{
    return Ax::defaultScreenRect();
}

inline PhosphorScrollEngine::ScrollLayoutParams defaultParams()
{
    PhosphorScrollEngine::ScrollLayoutParams p;
    p.workArea = defaultScreenRect();
    p.gap = 10;
    p.axis = Ax::stripAxis();
    return p;
}

/// The inner gap makeGappedProviderEngine injects. Non-zero and not equal to
/// defaultParams()' 10, so a test that accidentally uses the wrong params
/// helper produces visibly wrong numbers rather than a near-miss.
inline constexpr int kEngineInnerGap = 6;

/// Params matching a makeGappedProviderEngine engine, for a test that
/// hand-computes gap-dependent pixel expectations at the strip level (the
/// snapshot suite's gapsShareTheColumnCrossExtent drives its strip mutations with
/// it). The plain engineParams()/makeProviderEngine pair runs at gap 0,
/// which CANNOT observe a gap-dependent defect: a layout that omits the gap
/// term entirely still matches. That blind spot hid a real drop-indicator
/// bug.
inline PhosphorScrollEngine::ScrollLayoutParams gappedEngineParams()
{
    PhosphorScrollEngine::ScrollLayoutParams p;
    p.workArea = defaultScreenRect();
    p.gap = kEngineInnerGap;
    p.axis = Ax::stripAxis();
    return p;
}

/// Params matching what makeProviderEngine's engine computes internally
/// (no settings, no gap provider → every gap 0). See the header note.
inline PhosphorScrollEngine::ScrollLayoutParams engineParams()
{
    PhosphorScrollEngine::ScrollLayoutParams p;
    p.workArea = defaultScreenRect();
    p.gap = 0;
    p.axis = Ax::stripAxis();
    return p;
}

inline constexpr PhosphorScrollEngine::ColumnWidth kHalf = PhosphorScrollEngine::ColumnWidth::makeProportion(0.5);

using GeometryFn = std::function<QRect(const QString&)>;

/// A headless ScrollEngine on the geometry-provider seam (no ScreenManager,
/// no tracking service), already active on @p screens. Only with the
/// providers wired does the apply path resolve real rects, and only then do
/// lastManagedRect / visibleTiles / windowsTiled have anything to say.
///
/// @p screenGeometry defaults to the suite's single 1200x800 output.
/// @p availableGeometry defaults to @p screenGeometry — pass a DIFFERENT one
/// (a panel inset) when the test needs to tell the work area apart from the
/// screen rect, which is what the parking bounds are measured against.
/// @p windowTracker is null for every suite but the behaviour one: the sticky
/// gate and the client-decides width short-circuit on a null tracker, so
/// those two paths need a stub (scrollstubtracking.h) to be observable at all.
inline PhosphorScrollEngine::ScrollEngine*
makeProviderEngine(QObject* parent, const QSet<QString>& screens, GeometryFn screenGeometry = {},
                   GeometryFn availableGeometry = {}, PhosphorEngine::IWindowTrackingService* windowTracker = nullptr)
{
    if (!screenGeometry) {
        screenGeometry = [](const QString&) {
            return defaultScreenRect();
        };
    }
    if (!availableGeometry) {
        availableGeometry = screenGeometry;
    }
    auto* engine = new PhosphorScrollEngine::ScrollEngine(windowTracker, nullptr, parent);
    engine->setScreenGeometryProviders(availableGeometry, screenGeometry);
    // A well-behaved compositor answers every activation request with a
    // windowFocused report (the effect relays each KWin windowActivated back
    // through notifyWindowFocused). The engine pairs those echoes against its
    // pending-self-activation queue, so a fixture that never echoes leaves
    // the queue populated and the NEXT simulated user focus of that window
    // is consumed as the missing echo. Direct connection is safe: the echo
    // handler consumes the queue entry and returns before touching STRIP
    // state — it does set m_activeScreen to the activated window's screen
    // first, which is the value the real relay carries anyway, so the
    // re-entry into the engine mid-applyLayout changes no layout outcome.
    //
    // FIDELITY NOTE: this echo is synchronous and UNCONDITIONAL, unlike the
    // daemon's relay — an emit the engine deliberately does not queue (the
    // switch-focus tiling→float arm) is echoed here too, and it is delivered
    // inside the emit rather than on a later event-loop turn. Tests that
    // depend on observing the window between the press and the echo use the
    // bare no-echo fixture instead.
    QObject::connect(engine, &PhosphorEngine::PlacementEngineBase::activateWindowRequested, engine,
                     [engine](const QString& windowId) {
                         engine->windowFocused(windowId, engine->screenForTrackedWindow(windowId));
                     });
    engine->setActiveScreens(screens);
    return engine;
}

/// makeProviderEngine plus a context gap provider, so the engine resolves a
/// NON-ZERO inner gap (kEngineInnerGap; gappedEngineParams() is its
/// strip-level twin for hand-computed expectations).
///
/// This exists because the plain factory is structurally unable to observe a
/// gap-dependent defect — at gap 0 a layout that drops the gap term entirely
/// still produces the right numbers. Any test whose expected geometry would
/// change if the gap changed belongs here.
inline PhosphorScrollEngine::ScrollEngine* makeGappedProviderEngine(QObject* parent, const QSet<QString>& screens,
                                                                    GeometryFn screenGeometry = {},
                                                                    GeometryFn availableGeometry = {})
{
    auto* engine = makeProviderEngine(parent, screens, screenGeometry, availableGeometry);
    // PerScreenKeys-shaped, per the ContextGapProvider contract. Only the
    // inner gap is set: the outer gaps interact with smart gaps, which would
    // make the expected numbers depend on column count as well.
    engine->setContextGapProvider([](const QString&) {
        QVariantMap gaps;
        gaps.insert(QStringLiteral("InnerGap"), kEngineInnerGap);
        return gaps;
    });
    return engine;
}

/// S2 sits one step further ALONG THE STRIP, so a walk that runs off the far
/// strip edge continues onto it. Every other direction has no neighbour.
/// Shared by the smoke suite's parking tests and the verbs suite's main-axis
/// crossings, so the topology the pixel expectations assume cannot drift
/// between files. It answers the navigation token for the main axis rather
/// than the literal "right": on a vertical strip the same relation is spelled
/// "down", and a resolver still hearing only "right" would refuse every
/// crossing and make the tests that exercise them pass by never crossing at
/// all.
struct TrailNeighbourResolver : PhosphorEngine::ICrossSurfaceResolver
{
    QString neighborOutputInDirection(const QString&, const QString& direction) const override
    {
        return direction == Ax::navTrail() ? QStringLiteral("S2") : QString();
    }
    int neighborDesktopInDirection(int, const QString&) const override
    {
        return 0;
    }
};

/// The resolved tile for @p windowId, or nullptr when it is ABSENT from the
/// resolve. The three accessors below are thin readers over this one walk.
inline const PhosphorScrollEngine::ResolvedTile* findTile(const PhosphorScrollEngine::ResolvedStrip& resolved,
                                                          const QString& windowId)
{
    for (const PhosphorScrollEngine::ResolvedColumn& rc : resolved.columns) {
        for (const PhosphorScrollEngine::ResolvedTile& rt : rc.tiles) {
            if (rt.windowId == windowId) {
                return &rt;
            }
        }
    }
    return nullptr;
}

/// The tile's resolved rect, or a null QRect when the window is ABSENT
/// from the resolve. Callers asserting on a rect property should pair it
/// with `resolveContains` — a null rect makes negative assertions (e.g.
/// `!isHidden`) pass vacuously for a dropped tile.
inline QRect rectOf(const PhosphorScrollEngine::ResolvedStrip& resolved, const QString& windowId)
{
    const PhosphorScrollEngine::ResolvedTile* tile = findTile(resolved, windowId);
    return tile ? tile->rect : QRect();
}

/// True when @p windowId is resolved HIDDEN. Returns false for an ABSENT
/// window too — pair negative assertions with `resolveContains`, exactly
/// like rectOf, or a dropped tile passes them vacuously.
inline bool isHidden(const PhosphorScrollEngine::ResolvedStrip& resolved, const QString& windowId)
{
    const PhosphorScrollEngine::ResolvedTile* tile = findTile(resolved, windowId);
    return tile && tile->hidden;
}

/// True when @p windowId appears anywhere in the resolve.
inline bool resolveContains(const PhosphorScrollEngine::ResolvedStrip& resolved, const QString& windowId)
{
    return findTile(resolved, windowId) != nullptr;
}

} // namespace ScrollTestUtils

/// THE VACUITY GUARD. Every suite calls this from its FIRST private slot, and
/// never gates it — it is the one slot that must run on both arms.
///
/// Without it, a CMake edit that drops the ENVIRONMENT property would leave
/// the vertical arm silently re-running the HORIZONTAL suite, reporting a full
/// set of green targets that between them cover one axis. The printed line is
/// the out-of-band half: the vertical ctest registration carries a
/// FAIL_REGULAR_EXPRESSION on "axis=horizontal", so the check is enforced by
/// ctest rather than by the binary that would be doing the lying.
///
/// A plain function rather than a slot-defining macro on purpose — moc's
/// preprocessor is its own dialect, and a macro that expands to a member
/// definition inside a Q_OBJECT class body is exactly the kind of thing it
/// parses differently from the compiler.
namespace ScrollTestUtils::Ax {

inline void assertFixtureIsTransposed()
{
    // Printed BEFORE any assertion, because the ctest-side
    // FAIL_REGULAR_EXPRESSION reads this line and a suite that dies on the
    // assertions below still has to produce it.
    qInfo("PZ scroll test axis=%s", vertical() ? "vertical" : "horizontal");
    const QRect screen = ScrollTestUtils::defaultScreenRect();
    QCOMPARE(screen, vertical() ? QRect(0, 0, kCrossExtent, kMainExtent) : QRect(0, 0, kMainExtent, kCrossExtent));
    // Main is 1200 and cross 800 under BOTH axes. That is the property which
    // lets every hardcoded pixel literal in the suite keep its meaning across
    // a transpose, so it is worth asserting rather than assuming.
    QCOMPARE(mainLen(screen), kMainExtent);
    QCOMPARE(crossLen(screen), kCrossExtent);
    QCOMPARE(ScrollTestUtils::defaultParams().axis, stripAxis());
}

} // namespace ScrollTestUtils::Ax

/// Every suite's initTestCase calls this. It proves the fixture really did
/// transpose, so a vertical arm that quietly reverted to the horizontal
/// geometry fails here instead of reporting a green second axis.
///
/// Kept as a named macro rather than folded into a bare call so every suite's
/// first slot names the guard it is invoking, and so a future gate that has to
/// QSKIP has one place to expand (QSKIP is `qSkip(); return;`, which cannot
/// live inside the helper function).
#define AX_GUARD_SUITE()                                                                                               \
    do {                                                                                                               \
        ScrollTestUtils::Ax::assertFixtureIsTransposed();                                                              \
    } while (false)
