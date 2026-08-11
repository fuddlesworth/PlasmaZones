// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

// Shared fixture helpers for the whole PhosphorScrollEngine test suite — the
// two strip-model files and the seven engine files. One definition of the
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
#include <PhosphorScrollEngine/ScrollEngine.h>
#include <PhosphorScrollEngine/ScrollStrip.h>
#include <PhosphorScrollEngine/ScrollTypes.h>

#include <QObject>
#include <QRect>
#include <QSet>
#include <QString>

#include <functional>

namespace ScrollTestUtils {

/// The one screen geometry the whole suite fixtures against.
inline constexpr int kScreenWidth = 1200;
inline constexpr int kScreenHeight = 800;

inline QRect defaultScreenRect()
{
    return QRect(0, 0, kScreenWidth, kScreenHeight);
}

inline PhosphorScrollEngine::ScrollLayoutParams defaultParams()
{
    PhosphorScrollEngine::ScrollLayoutParams p;
    p.workArea = defaultScreenRect();
    p.gap = 10;
    return p;
}

/// The inner gap makeGappedProviderEngine injects. Non-zero and not equal to
/// defaultParams()' 10, so a test that accidentally uses the wrong params
/// helper produces visibly wrong numbers rather than a near-miss.
inline constexpr int kEngineInnerGap = 6;

/// Params matching a makeGappedProviderEngine engine, for a test that
/// hand-computes gap-dependent pixel expectations at the strip level. The
/// current gapped-engine tests assert on payloads read back from the engine
/// instead, so nothing uses this yet — it exists so the first test that DOES
/// derive expectations by hand has one definition of the gap to derive them
/// from. The plain engineParams()/makeProviderEngine pair runs at gap 0,
/// which CANNOT observe a gap-dependent defect: a layout that omits the gap
/// term entirely still matches. That blind spot hid a real drop-indicator
/// bug.
inline PhosphorScrollEngine::ScrollLayoutParams gappedEngineParams()
{
    PhosphorScrollEngine::ScrollLayoutParams p;
    p.workArea = defaultScreenRect();
    p.gap = kEngineInnerGap;
    return p;
}

/// Params matching what makeProviderEngine's engine computes internally
/// (no settings, no gap provider → every gap 0). See the header note.
inline PhosphorScrollEngine::ScrollLayoutParams engineParams()
{
    PhosphorScrollEngine::ScrollLayoutParams p;
    p.workArea = defaultScreenRect();
    p.gap = 0;
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

/// S2 sits to the RIGHT of everything; every other direction has no
/// neighbour. Shared by the smoke suite's parking tests and the verbs
/// suite's horizontal crossings — one definition so the topology the pixel
/// expectations assume cannot drift between files.
struct RightNeighbourResolver : PhosphorEngine::ICrossSurfaceResolver
{
    QString neighborOutputInDirection(const QString&, const QString& direction) const override
    {
        return direction == QLatin1String("right") ? QStringLiteral("S2") : QString();
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
