// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorSnapEngine/snapnavigationtargets.h>

#include <PhosphorEngine/ICrossSurfaceResolver.h>
#include <PhosphorSnapEngine/IZoneAdjacencyResolver.h>
#include <PhosphorZones/Layout.h>
#include <PhosphorZones/LayoutRegistry.h>
#include "snapenginelogging.h"
#include <PhosphorScreens/Manager.h>
#include <PhosphorScreens/VirtualScreen.h>
#include <PhosphorZones/Zone.h>

#include <QRect>
#include <QStringList>
#include <PhosphorScreens/ScreenIdentity.h>

namespace PhosphorSnapEngine {

namespace {

// ═══════════════════════════════════════════════════════════════════════════
// Result builders
// ═══════════════════════════════════════════════════════════════════════════

PhosphorProtocol::MoveTargetResult moveResult(bool success, const QString& reason, const QString& zoneId,
                                              const QRect& geometry, const QString& sourceZoneId,
                                              const QString& screenId)
{
    return {success,           reason,       zoneId,  geometry.x(), geometry.y(), geometry.width(),
            geometry.height(), sourceZoneId, screenId};
}

PhosphorProtocol::FocusTargetResult focusResult(bool success, const QString& reason, const QString& windowIdToActivate,
                                                const QString& sourceZoneId, const QString& targetZoneId,
                                                const QString& screenId)
{
    return {success, reason, windowIdToActivate, sourceZoneId, targetZoneId, screenId};
}

PhosphorProtocol::CycleTargetResult cycleResult(bool success, const QString& reason, const QString& windowIdToActivate,
                                                const QString& zoneId, const QString& screenId)
{
    return {success, reason, windowIdToActivate, zoneId, screenId};
}

PhosphorProtocol::SwapTargetResult swapResult(bool success, const QString& reason, const QString& windowId1, int x1,
                                              int y1, int w1, int h1, const QString& zoneId1, const QString& windowId2,
                                              int x2, int y2, int w2, int h2, const QString& zoneId2,
                                              const QString& screenId, const QString& sourceZoneId,
                                              const QString& targetZoneId)
{
    // screenName2 is left empty here (in-surface default); cross-output callers
    // set it directly on the returned result.
    return {success, reason, windowId1, x1, y1,      w1,       h1,           zoneId1,      windowId2,
            x2,      y2,     w2,        h2, zoneId2, screenId, sourceZoneId, targetZoneId, QString()};
}

// Pre-call contract checks. Target-resolver callers (the WTA slots) are
// supposed to run validation *before* calling the resolver — dispatcher
// responsibility, not resolver responsibility. But defensively check here
// too so a misrouted call returns a clean no-op result instead of crashing.
bool checkWindowId(const QString& windowId)
{
    return !windowId.isEmpty();
}

bool checkDirection(const QString& direction)
{
    return !direction.isEmpty();
}

} // anonymous namespace

// ═══════════════════════════════════════════════════════════════════════════
// Stored Screen Validation (shared with SnapEngine's resolveNavScreen)
// ═══════════════════════════════════════════════════════════════════════════

bool isStoredScreenValid(PhosphorScreens::ScreenManager* mgr, const QString& storedScreen)
{
    if (storedScreen.isEmpty()) {
        return false;
    }
    if (PhosphorIdentity::VirtualScreenId::isVirtual(storedScreen)) {
        QString physId = PhosphorIdentity::VirtualScreenId::extractPhysicalId(storedScreen);
        if (!PhosphorScreens::ScreenIdentity::findByIdOrName(physId)) {
            return false;
        }
        return mgr && mgr->effectiveScreenIds().contains(storedScreen);
    }
    return PhosphorScreens::ScreenIdentity::findByIdOrName(storedScreen) != nullptr;
}

// ═══════════════════════════════════════════════════════════════════════════
// Construction
// ═══════════════════════════════════════════════════════════════════════════

SnapNavigationTargetResolver::SnapNavigationTargetResolver(PhosphorEngine::IWindowTrackingService* service,
                                                           PhosphorZones::LayoutRegistry* layoutManager,
                                                           IZoneAdjacencyResolver* zoneAdjacency, FeedbackFn feedback)
    : m_service(service)
    , m_layoutManager(layoutManager)
    , m_zoneAdjacency(zoneAdjacency)
    , m_feedback(std::move(feedback))
{
    Q_ASSERT(service);
    Q_ASSERT(layoutManager);
}

void SnapNavigationTargetResolver::setZoneAdjacencyResolver(IZoneAdjacencyResolver* resolver)
{
    m_zoneAdjacency = resolver;
}

void SnapNavigationTargetResolver::setCrossSurfaceResolver(PhosphorEngine::ICrossSurfaceResolver* resolver)
{
    m_crossSurface = resolver;
}

void SnapNavigationTargetResolver::setNeighbourAutotileProvider(NeighbourAutotileFn fn)
{
    m_neighbourIsAutotile = std::move(fn);
}

PhosphorProtocol::MoveTargetResult SnapNavigationTargetResolver::crossOutputEntryTarget(const QString& currentZoneId,
                                                                                        const QString& direction,
                                                                                        const QString& sourceScreenId,
                                                                                        bool requireSnapNeighbour) const
{
    const QString fail = QString();
    // m_service is a ctor invariant (Q_ASSERT'd, never null and never reset):
    // SnapEngine::ensureTargetResolver is the sole construction site and returns
    // early without building this resolver when its tracker is null, so no live
    // resolver can hold a null m_service. Only the late-bound resolvers (set
    // post-construction) are genuinely optional here.
    if (!m_crossSurface || !m_zoneAdjacency) {
        return moveResult(false, fail, QString(), QRect(), currentZoneId, sourceScreenId);
    }
    const QString neighborScreen = m_crossSurface->neighborOutputInDirection(sourceScreenId, direction);
    if (neighborScreen.isEmpty()) {
        return moveResult(false, fail, QString(), QRect(), currentZoneId, sourceScreenId);
    }
    // A MOVE/SWAP must not snap the window onto an autotile neighbour: that screen
    // has no snap zones to own the window even though its assigned layout still
    // enumerates zones. Report no crossing so the engine's cross-mode handoff
    // takes over. FOCUS (requireSnapNeighbour=false) is not gated.
    if (requireSnapNeighbour && m_neighbourIsAutotile && m_neighbourIsAutotile(neighborScreen)) {
        return moveResult(false, fail, QString(), QRect(), currentZoneId, sourceScreenId);
    }
    // Enter the neighbour output from the edge facing back toward the source.
    const QString entryZone =
        m_zoneAdjacency->getFirstZoneInDirection(oppositeCrossingDirection(direction), neighborScreen);
    if (entryZone.isEmpty()) {
        return moveResult(false, fail, QString(), QRect(), currentZoneId, sourceScreenId);
    }
    const QRect geo = m_service->zoneGeometry(entryZone, neighborScreen);
    if (!geo.isValid()) {
        return moveResult(false, fail, QString(), QRect(), currentZoneId, sourceScreenId);
    }
    // Success: the entry zone on the neighbour output. Feedback is emitted by
    // the caller so the move/focus tag is correct.
    return moveResult(true, QString(), entryZone, geo, currentZoneId, neighborScreen);
}

PhosphorProtocol::SwapTargetResult
SnapNavigationTargetResolver::crossOutputSwapTarget(const QString& windowId, const QString& currentZoneId,
                                                    const QString& direction, const QString& sourceScreenId) const
{
    const auto fail = [&](const QString& reason) {
        return swapResult(false, reason, QString(), 0, 0, 0, 0, QString(), QString(), 0, 0, 0, 0, QString(),
                          sourceScreenId, currentZoneId, QString());
    };
    // m_service is a ctor invariant; only the late-bound resolvers are optional.
    if (!m_crossSurface || !m_zoneAdjacency) {
        return fail(QString());
    }
    const QString neighborScreen = m_crossSurface->neighborOutputInDirection(sourceScreenId, direction);
    if (neighborScreen.isEmpty()) {
        return fail(QString());
    }
    // A swap onto an autotile neighbour is a cross-MODE exchange (bidirectional
    // handoff) — report no crossing so the snap engine doesn't snap the window
    // onto a tiled screen. Handled by the cross-mode swap phase, not here.
    if (m_neighbourIsAutotile && m_neighbourIsAutotile(neighborScreen)) {
        return fail(QString());
    }
    // Enter the neighbour output from the edge facing back toward the source.
    const QString entryZone =
        m_zoneAdjacency->getFirstZoneInDirection(oppositeCrossingDirection(direction), neighborScreen);
    if (entryZone.isEmpty()) {
        return fail(QString());
    }
    // window1 (the focused window) lands in the neighbour's entry zone; window2
    // (the entry-zone occupant, if any) returns to window1's old zone on the
    // source output. Both geometries are zone-relative to their target output.
    const QRect entryGeom = m_service->zoneGeometry(entryZone, neighborScreen);
    const QRect sourceGeom = m_service->zoneGeometry(currentZoneId, sourceScreenId);
    if (!entryGeom.isValid() || !sourceGeom.isValid()) {
        return fail(QStringLiteral("geometry_error"));
    }
    // Partner = the entry zone's occupant ON THE NEIGHBOUR output. Pinning to the
    // neighbour screen mirrors the in-surface swap: the zone UUID is shared by
    // every output the layout drives, so an unfiltered windowsInZone could
    // surface a window on the wrong monitor.
    const QStringList occupants = windowsInZoneOnScreen(entryZone, neighborScreen);
    if (occupants.isEmpty()) {
        // Empty entry zone → move-to-empty across the boundary: window1 crosses,
        // there is no counterpart to send back. screenName2 stays empty.
        return swapResult(true, QStringLiteral("moved_to_empty"), windowId, entryGeom.x(), entryGeom.y(),
                          entryGeom.width(), entryGeom.height(), entryZone, QString(), 0, 0, 0, 0, QString(),
                          neighborScreen, currentZoneId, entryZone);
    }
    const QString partner = occupants.first();
    PhosphorProtocol::SwapTargetResult r =
        swapResult(true, QString(), windowId, entryGeom.x(), entryGeom.y(), entryGeom.width(), entryGeom.height(),
                   entryZone, partner, sourceGeom.x(), sourceGeom.y(), sourceGeom.width(), sourceGeom.height(),
                   currentZoneId, neighborScreen, currentZoneId, entryZone);
    // window2 returns to the source output; screenName carries window1's target.
    r.screenName2 = sourceScreenId;
    return r;
}

QStringList SnapNavigationTargetResolver::windowsInZoneOnScreen(const QString& zoneId, const QString& screenName) const
{
    QStringList result;
    const QStringList windows = m_service->windowsInZone(zoneId);
    for (const QString& windowId : windows) {
        // screenForWindow canonicalizes the id (issue #628); the exact-equality
        // screen comparison is unchanged.
        if (m_service->screenForWindow(windowId) == screenName) {
            result.append(windowId);
        }
    }
    return result;
}

QString SnapNavigationTargetResolver::firstWindowInZoneOnScreen(const QString& zoneId, const QString& screenName) const
{
    const QStringList onScreen = windowsInZoneOnScreen(zoneId, screenName);
    return onScreen.isEmpty() ? QString() : onScreen.first();
}

// Feedback callback emission goes through SnapNavigationTargetResolver::emitFeedback
// (defined inline in the header) so call sites don't need to null-check the
// optional std::function at every call.

// ═══════════════════════════════════════════════════════════════════════════
// Navigation Target Computation
// ═══════════════════════════════════════════════════════════════════════════

PhosphorProtocol::MoveTargetResult SnapNavigationTargetResolver::getMoveTargetForWindow(const QString& windowId,
                                                                                        const QString& direction,
                                                                                        const QString& screenId)
{
    if (!checkWindowId(windowId)) {
        qCWarning(PhosphorSnapEngine::lcSnapEngine) << "Cannot getMoveTargetForWindow - empty window ID";
        return moveResult(false, QStringLiteral("invalid_window"), QString(), QRect(), QString(), screenId);
    }
    if (!checkDirection(direction)) {
        qCWarning(PhosphorSnapEngine::lcSnapEngine) << "Cannot move - empty direction";
        // Feedback and result carry the caller's screenId: a half-supplied
        // feedback (empty here, populated on the result) confuses OSD/telemetry
        // consumers that correlate the two.
        emitFeedback(false, QStringLiteral("move"), QStringLiteral("invalid_direction"), QString(), QString(),
                     screenId);
        return moveResult(false, QStringLiteral("invalid_direction"), QString(), QRect(), QString(), screenId);
    }
    if (!m_zoneAdjacency) {
        // Only reachable before the adjacency resolver is wired (broken
        // init) — surface it on the OSD instead of failing silently.
        emitFeedback(false, QStringLiteral("move"), QStringLiteral("no_zone_detection"), QString(), QString(),
                     screenId);
        return moveResult(false, QStringLiteral("no_zone_detection"), QString(), QRect(), QString(), screenId);
    }

    QString currentZoneId = m_service->zoneForWindow(windowId);

    // When the window is snapped, trust the daemon's stored screen assignment
    // over the effect-reported screenId.  KWin's EffectWindow::screen() can
    // return the wrong output for same-model multi-monitor setups (e.g. dual
    // Samsung Odyssey G93SC with different serials).  The daemon's assignment
    // is authoritative because it was set at snap time.
    //
    // When the window is NOT snapped (e.g. moved between monitors via KDE's
    // Move-to-Screen shortcut — outputChanged fires and unsnaps it), we use
    // the effect-provided screen since there's no stored assignment.
    //
    // Only use the stored screen if it's still connected — when a monitor
    // enters standby, KWin rehomes windows but the stored assignment points
    // at the dead output.
    QString effectiveScreenId = screenId;
    if (!currentZoneId.isEmpty()) {
        QString storedScreen = m_service->screenForWindow(windowId);
        if (isStoredScreenValid(m_service->screenManager(), storedScreen)) {
            effectiveScreenId = storedScreen;
        }
    }

    QString targetZoneId;
    if (currentZoneId.isEmpty()) {
        targetZoneId = m_zoneAdjacency->getFirstZoneInDirection(direction, effectiveScreenId);
        if (targetZoneId.isEmpty()) {
            emitFeedback(false, QStringLiteral("move"), QStringLiteral("no_zones"), QString(), QString(),
                         effectiveScreenId);
            return moveResult(false, QStringLiteral("no_zones"), QString(), QRect(), QString(), effectiveScreenId);
        }
    } else {
        targetZoneId = m_zoneAdjacency->getAdjacentZone(currentZoneId, direction, effectiveScreenId);
        if (targetZoneId.isEmpty()) {
            // No adjacent zone on this output — cross into the adjacent output's
            // entry zone before giving up. requireSnapNeighbour: an autotile
            // neighbour is handed off cross-mode by the engine, not snapped here.
            const PhosphorProtocol::MoveTargetResult cross =
                crossOutputEntryTarget(currentZoneId, direction, effectiveScreenId, /*requireSnapNeighbour=*/true);
            if (cross.success) {
                emitFeedback(true, QStringLiteral("move"), QStringLiteral("screen:") + direction, currentZoneId,
                             cross.zoneId, cross.screenName);
                return cross;
            }
            // Defer the boundary decision AND its feedback to the caller:
            // SnapEngine tries the cross-mode and cross-desktop axes and
            // emits the boundary feedback itself when they fail, in every
            // configuration — emitting here too would double the OSD when
            // no cross-surface resolver is wired.
            return moveResult(false, QStringLiteral("no_adjacent_zone"), QString(), QRect(), currentZoneId,
                              effectiveScreenId);
        }
    }

    QRect geo = m_service->zoneGeometry(targetZoneId, effectiveScreenId);
    if (!geo.isValid()) {
        emitFeedback(false, QStringLiteral("move"), QStringLiteral("geometry_error"), currentZoneId, targetZoneId,
                     effectiveScreenId);
        return moveResult(false, QStringLiteral("geometry_error"), targetZoneId, QRect(), currentZoneId,
                          effectiveScreenId);
    }

    emitFeedback(true, QStringLiteral("move"), direction, currentZoneId, targetZoneId, effectiveScreenId);
    return moveResult(true, QString(), targetZoneId, geo, currentZoneId, effectiveScreenId);
}

namespace {

/// Signed 1-D interval overlap of two rects on the axis PERPENDICULAR to the
/// travel axis. Positive means the rects are genuinely side-by-side for a
/// horizontal/vertical grow; zero or negative means corner/diagonal only.
int perpendicularOverlap(const QRect& a, const QRect& b, bool horizontal)
{
    if (horizontal) {
        return qMin(a.y() + a.height(), b.y() + b.height()) - qMax(a.y(), b.y());
    }
    return qMin(a.x() + a.width(), b.x() + b.width()) - qMax(a.x(), b.x());
}

/// Low/high coordinate of @p r on the travel axis, half-open (hi = lo + size)
/// so edge math avoids QRect's inclusive right()/bottom() off-by-one.
int travelLo(const QRect& r, bool horizontal)
{
    return horizontal ? r.x() : r.y();
}
int travelHi(const QRect& r, bool horizontal)
{
    return horizontal ? r.x() + r.width() : r.y() + r.height();
}

/// Zone rects rounded from relative geometry can disagree by a pixel; edges
/// within this tolerance count as the same edge for shrink-band membership.
constexpr int kSpanEdgeTolerancePx = 2;

} // anonymous namespace

SpanTargetResult SnapNavigationTargetResolver::getSpanTargetForWindow(const QString& windowId, const QString& direction,
                                                                      const QString& screenId)
{
    SpanTargetResult result;
    result.screenName = screenId;
    if (!checkWindowId(windowId)) {
        qCWarning(PhosphorSnapEngine::lcSnapEngine) << "Cannot getSpanTargetForWindow - empty window ID";
        result.reason = QStringLiteral("invalid_window");
        return result;
    }
    const bool horizontal = (direction == QLatin1String("left") || direction == QLatin1String("right"));
    const bool vertical = (direction == QLatin1String("up") || direction == QLatin1String("down"));
    if (!horizontal && !vertical) {
        qCWarning(PhosphorSnapEngine::lcSnapEngine) << "Cannot span - invalid direction" << direction;
        emitFeedback(false, QStringLiteral("span"), QStringLiteral("invalid_direction"), QString(), QString(),
                     screenId);
        result.reason = QStringLiteral("invalid_direction");
        return result;
    }
    // Travel toward increasing coordinate? (screen space: y grows downward)
    const bool positive = (direction == QLatin1String("right") || direction == QLatin1String("down"));

    QStringList currentZones = m_service->zonesForWindow(windowId);

    // Trust stored screen for snapped windows — see getMoveTargetForWindow comment.
    QString effectiveScreenId = screenId;
    if (!currentZones.isEmpty()) {
        const QString storedScreen = m_service->screenForWindow(windowId);
        if (isStoredScreenValid(m_service->screenManager(), storedScreen)) {
            effectiveScreenId = storedScreen;
        }
    }
    result.screenName = effectiveScreenId;

    // Member rects. A stale member whose zone no longer resolves (layout
    // switched under the span) is dropped from the working set rather than
    // carried forward as a dead id.
    QVector<QPair<QString, QRect>> members;
    for (const QString& zoneId : std::as_const(currentZones)) {
        const QRect geo = m_service->zoneGeometry(zoneId, effectiveScreenId);
        if (geo.isValid()) {
            members.append({zoneId, geo});
        }
    }

    if (members.isEmpty()) {
        // Unsnapped (or fully stale) — mirror move's entry behaviour: snap
        // into the edge zone in the pressed direction.
        if (!m_zoneAdjacency) {
            // Only reachable before the adjacency resolver is wired (broken
            // init) — surface it on the OSD instead of failing silently.
            emitFeedback(false, QStringLiteral("span"), QStringLiteral("no_zone_detection"), QString(), QString(),
                         effectiveScreenId);
            result.reason = QStringLiteral("no_zone_detection");
            return result;
        }
        const QString entryZone = m_zoneAdjacency->getFirstZoneInDirection(direction, effectiveScreenId);
        if (entryZone.isEmpty()) {
            emitFeedback(false, QStringLiteral("span"), QStringLiteral("no_zones"), QString(), QString(),
                         effectiveScreenId);
            result.reason = QStringLiteral("no_zones");
            return result;
        }
        const QRect geo = m_service->zoneGeometry(entryZone, effectiveScreenId);
        if (!geo.isValid()) {
            emitFeedback(false, QStringLiteral("span"), QStringLiteral("geometry_error"), QString(), entryZone,
                         effectiveScreenId);
            result.reason = QStringLiteral("geometry_error");
            return result;
        }
        result.success = true;
        result.grew = true;
        result.zoneIds = QStringList{entryZone};
        result.geometry = geo;
        // "snap:", not "grow:" — this is an initial snap of an unsnapped
        // window, and the OSD words it as a snap rather than an extension.
        emitFeedback(true, QStringLiteral("span"), QStringLiteral("snap:") + direction, QString(), entryZone,
                     effectiveScreenId);
        return result;
    }

    QRect unionRect = members.first().second;
    for (const auto& m : std::as_const(members)) {
        unionRect = unionRect.united(m.second);
    }
    result.sourceZoneId = members.first().first;

    // Candidate rects: every non-member zone of this screen's layout.
    auto* layout = m_layoutManager->resolveLayoutForScreen(effectiveScreenId);
    if (!layout) {
        emitFeedback(false, QStringLiteral("span"), QStringLiteral("no_active_layout"), result.sourceZoneId, QString(),
                     effectiveScreenId);
        result.reason = QStringLiteral("no_active_layout");
        return result;
    }
    QVector<QPair<QString, QRect>> candidates;
    const auto layoutZones = layout->zones();
    for (PhosphorZones::Zone* zone : layoutZones) {
        const QString zoneId = zone->id().toString();
        if (currentZones.contains(zoneId)) {
            continue;
        }
        const QRect geo = m_service->zoneGeometry(zoneId, effectiveScreenId);
        if (geo.isValid()) {
            candidates.append({zoneId, geo});
        }
    }

    // ── Grow: nearest candidate extending past the span's leading edge ──────
    // A candidate qualifies when it genuinely enlarges the union toward
    // @p direction (its far edge lies beyond the union's) and it sits
    // side-by-side with the union (positive perpendicular overlap — a
    // diagonal zone is not a grow target). Nearest edge gap wins, clamped to
    // zero so overlapping-zone layouts tie at the edge and break by
    // perpendicular centre distance — the same ranking the shared
    // directionalNeighbor raycast uses.
    const int uLo = travelLo(unionRect, horizontal);
    const int uHi = travelHi(unionRect, horizontal);
    const int uPerpCenter = horizontal ? unionRect.center().y() : unionRect.center().x();
    int bestIndex = -1;
    int bestGap = 0;
    int bestPerp = 0;
    for (int i = 0; i < candidates.size(); ++i) {
        const QRect& r = candidates[i].second;
        if (perpendicularOverlap(unionRect, r, horizontal) <= 0) {
            continue;
        }
        // Tolerance mirrors shrink-band membership: a candidate whose far
        // edge exceeds the union's by only rounding jitter is the same edge,
        // not a grow target - without this, a 1-2px sliver registers as a
        // gap-0 "grow" that adds a jitter zone and blocks grow-else-retract.
        const bool extends = positive ? (travelHi(r, horizontal) > uHi + kSpanEdgeTolerancePx)
                                      : (travelLo(r, horizontal) < uLo - kSpanEdgeTolerancePx);
        if (!extends) {
            continue;
        }
        const int gap = positive ? qMax(0, travelLo(r, horizontal) - uHi) : qMax(0, uLo - travelHi(r, horizontal));
        const int perp = qAbs((horizontal ? r.center().y() : r.center().x()) - uPerpCenter);
        if (bestIndex < 0 || gap < bestGap || (gap == bestGap && perp < bestPerp)) {
            bestIndex = i;
            bestGap = gap;
            bestPerp = perp;
        }
    }

    if (bestIndex >= 0) {
        // Extension band: from the union's old leading edge to the picked
        // neighbour's far edge, spanning the union's perpendicular extent.
        // Every candidate the band overlaps joins the span, so growing a
        // full-height span into a column of stacked zones takes the whole
        // column instead of leaving an L-shaped set whose bounding rect
        // covers zones that were never added.
        //
        // Bounding-rect semantics: the applied geometry is the union of the
        // member rects, matching multiZoneGeometry everywhere else in the
        // codebase. When an added zone extends past the band's perpendicular
        // extent (irregular or overlapping layouts), the union can therefore
        // overlap zones that are not in the set — inherent to bounding-rect
        // spans, and identical to what a drag multi-zone snap of the same
        // set would produce. The band is deliberately NOT iterated to a
        // fixpoint: flood-filling would swallow overlapping zones the user
        // never aimed at (see ZoneDetector::detectMultiZone's matching
        // choice for the drag path).
        const QRect& winner = candidates[bestIndex].second;
        const int newEdge = positive ? travelHi(winner, horizontal) : travelLo(winner, horizontal);
        QRect band;
        if (horizontal) {
            band = positive ? QRect(uHi, unionRect.y(), newEdge - uHi, unionRect.height())
                            : QRect(newEdge, unionRect.y(), uLo - newEdge, unionRect.height());
        } else {
            band = positive ? QRect(unionRect.x(), uHi, unionRect.width(), newEdge - uHi)
                            : QRect(unionRect.x(), newEdge, unionRect.width(), uLo - newEdge);
        }
        // Base the new set on the validated members, not raw currentZones —
        // a stale id the member pass dropped must not be re-included (it
        // could even become the primary zone the commit keys off).
        result.zoneIds.clear();
        for (const auto& m : std::as_const(members)) {
            result.zoneIds.append(m.first);
        }
        QRect newUnion = unionRect;
        for (int i = 0; i < candidates.size(); ++i) {
            const QRect& candidateRect = candidates[i].second;
            // Sweep membership needs more than rounding jitter: zone rects
            // rounded from relative geometry can leak 1-2px into the band
            // (see kSpanEdgeTolerancePx), and an exact intersection test
            // would let such a sliver of a perpendicular neighbour silently
            // join the span. The winner is exempt - it defines the band and
            // always joins its own extension.
            const QRect overlap = band.intersected(candidateRect);
            const bool beyondJitter = overlap.width() > kSpanEdgeTolerancePx && overlap.height() > kSpanEdgeTolerancePx;
            if (i == bestIndex || beyondJitter) {
                result.zoneIds.append(candidates[i].first);
                newUnion = newUnion.united(candidateRect);
            }
        }
        result.success = true;
        result.grew = true;
        result.geometry = newUnion;
        emitFeedback(true, QStringLiteral("span"), QStringLiteral("grow:") + direction, result.sourceZoneId,
                     candidates[bestIndex].first, effectiveScreenId);
        return result;
    }

    // ── Shrink: nothing to grow into, so retract the opposite edge ──────────
    // Pressing toward a boundary undoes the last grow away from it: the
    // trailing-edge member band (the zones whose far edge forms the union's
    // edge opposite to @p direction) drops out. A span that is a single band
    // along this axis has nothing to retract and reports the boundary.
    if (members.size() > 1) {
        QStringList kept;
        QRect keptUnion;
        QString removedZoneId;
        for (const auto& [memberId, memberRect] : std::as_const(members)) {
            const bool inTrailingBand = positive ? (travelLo(memberRect, horizontal) <= uLo + kSpanEdgeTolerancePx)
                                                 : (travelHi(memberRect, horizontal) >= uHi - kSpanEdgeTolerancePx);
            if (inTrailingBand) {
                // Record the FIRST removed member (members preserve span
                // order) so the feedback's target zone is deterministic
                // when the band drops several zones at once.
                if (removedZoneId.isEmpty()) {
                    removedZoneId = memberId;
                }
                continue;
            }
            kept.append(memberId);
            keptUnion = keptUnion.isNull() ? memberRect : keptUnion.united(memberRect);
        }
        if (!kept.isEmpty()) {
            result.success = true;
            result.grew = false;
            result.zoneIds = kept;
            result.geometry = keptUnion;
            emitFeedback(true, QStringLiteral("span"), QStringLiteral("shrink:") + direction, result.sourceZoneId,
                         removedZoneId, effectiveScreenId);
            return result;
        }
    }

    emitFeedback(false, QStringLiteral("span"), QStringLiteral("no_adjacent_zone"), result.sourceZoneId, QString(),
                 effectiveScreenId);
    result.reason = QStringLiteral("no_adjacent_zone");
    return result;
}

PhosphorProtocol::FocusTargetResult SnapNavigationTargetResolver::getFocusTargetForWindow(const QString& windowId,
                                                                                          const QString& direction,
                                                                                          const QString& screenId)
{
    if (!checkWindowId(windowId)) {
        qCWarning(PhosphorSnapEngine::lcSnapEngine) << "Cannot getFocusTargetForWindow - empty window ID";
        return focusResult(false, QStringLiteral("invalid_window"), QString(), QString(), QString(), screenId);
    }
    if (!checkDirection(direction)) {
        qCWarning(PhosphorSnapEngine::lcSnapEngine) << "Cannot focus - empty direction";
        // Same feedback/result screenId consistency rule as getMoveTargetForWindow.
        emitFeedback(false, QStringLiteral("focus"), QStringLiteral("invalid_direction"), QString(), QString(),
                     screenId);
        return focusResult(false, QStringLiteral("invalid_direction"), QString(), QString(), QString(), screenId);
    }
    if (!m_zoneAdjacency) {
        // See getMoveTargetForWindow — broken-init state, surfaced on the OSD.
        emitFeedback(false, QStringLiteral("focus"), QStringLiteral("no_zone_detection"), QString(), QString(),
                     screenId);
        return focusResult(false, QStringLiteral("no_zone_detection"), QString(), QString(), QString(), screenId);
    }

    QString currentZoneId = m_service->zoneForWindow(windowId);
    if (currentZoneId.isEmpty()) {
        emitFeedback(false, QStringLiteral("focus"), QStringLiteral("not_snapped"), QString(), QString(), screenId);
        return focusResult(false, QStringLiteral("not_snapped"), QString(), QString(), QString(), screenId);
    }

    // Trust stored screen for snapped windows — see getMoveTargetForWindow comment
    QString effectiveScreenId = screenId;
    {
        QString storedScreen = m_service->screenForWindow(windowId);
        if (isStoredScreenValid(m_service->screenManager(), storedScreen)) {
            effectiveScreenId = storedScreen;
        }
    }

    QString targetZoneId = m_zoneAdjacency->getAdjacentZone(currentZoneId, direction, effectiveScreenId);
    if (targetZoneId.isEmpty()) {
        // No adjacent zone on this output — try focusing into the adjacent
        // output's entry zone. The neighbour's mode is not gated
        // (requireSnapNeighbour=false), but the landing below still needs a
        // snap-tracked occupant, so an autotile neighbour dead-ends to
        // no_adjacent_zone anyway.
        const PhosphorProtocol::MoveTargetResult cross =
            crossOutputEntryTarget(currentZoneId, direction, effectiveScreenId, /*requireSnapNeighbour=*/false);
        if (cross.success) {
            // Pin the entry window to the neighbour output: windowsInZone is
            // screen-agnostic and the entry zone's UUID can also exist on the
            // source output when one layout drives both monitors.
            const QString entryWindow = firstWindowInZoneOnScreen(cross.zoneId, cross.screenName);
            if (!entryWindow.isEmpty()) {
                emitFeedback(true, QStringLiteral("focus"), QStringLiteral("screen:") + direction, currentZoneId,
                             cross.zoneId, cross.screenName);
                return focusResult(true, QString(), entryWindow, currentZoneId, cross.zoneId, cross.screenName);
            }
        }
        // Reaching here means either no cross-output entry zone was found OR one
        // was found but is unoccupied on the neighbour (firstWindowInZoneOnScreen
        // empty — you cannot focus an empty zone). Both collapse to the same
        // "no_adjacent_zone" reason: from the caller's perspective there is no
        // window to land on that way, so it should try the desktop axis next. The
        // reason intentionally does not distinguish the empty-zone case.
        // Defer the boundary decision AND its feedback to the caller —
        // SnapEngine tries the cross-desktop axis and emits the boundary
        // feedback itself when it fails, in every configuration (see the
        // matching note in getMoveTargetForWindow).
        return focusResult(false, QStringLiteral("no_adjacent_zone"), QString(), currentZoneId, QString(),
                           effectiveScreenId);
    }

    // Prefer the occupant actually on this output so a shared zone UUID on a
    // sibling monitor can't hijack same-surface focus. Unlike the cross-output
    // path, this is best-effort: the target zone already belongs to
    // effectiveScreenId, so if the screen filter finds nothing — e.g. the stored
    // assignment was recorded under a different screen-id form (virtual vs bare
    // physical) than effectiveScreenId resolved to — fall back to the unfiltered
    // occupant rather than spuriously reporting an empty zone. The strict filter
    // only has to be authoritative across the cross-OUTPUT boundary.
    QString targetWindow = firstWindowInZoneOnScreen(targetZoneId, effectiveScreenId);
    if (targetWindow.isEmpty()) {
        const QStringList windowsInZone = m_service->windowsInZone(targetZoneId);
        if (!windowsInZone.isEmpty()) {
            targetWindow = windowsInZone.first();
        }
    }
    if (targetWindow.isEmpty()) {
        emitFeedback(false, QStringLiteral("focus"), QStringLiteral("no_window_in_zone"), currentZoneId, targetZoneId,
                     effectiveScreenId);
        return focusResult(false, QStringLiteral("no_window_in_zone"), QString(), currentZoneId, targetZoneId,
                           effectiveScreenId);
    }

    emitFeedback(true, QStringLiteral("focus"), direction, currentZoneId, targetZoneId, effectiveScreenId);
    return focusResult(true, QString(), targetWindow, currentZoneId, targetZoneId, effectiveScreenId);
}

PhosphorProtocol::RestoreTargetResult SnapNavigationTargetResolver::getRestoreForWindow(const QString& windowId,
                                                                                        const QString& screenId)
{
    if (!checkWindowId(windowId)) {
        qCWarning(PhosphorSnapEngine::lcSnapEngine) << "Cannot getRestoreForWindow - empty window ID";
        return {false, false, 0, 0, 0, 0};
    }

    int x = 0, y = 0, w = 0, h = 0;
    auto geo = m_service->validatedUnmanagedGeometry(windowId, screenId);
    bool found = geo.has_value();
    if (found) {
        x = geo->x();
        y = geo->y();
        w = geo->width();
        h = geo->height();
    }
    bool success = found && w > 0 && h > 0;
    if (!success) {
        emitFeedback(false, QStringLiteral("restore"), QStringLiteral("not_snapped"), QString(), QString(), screenId);
    } else {
        emitFeedback(true, QStringLiteral("restore"), QString(), QString(), QString(), screenId);
    }
    return {success, success, x, y, w, h};
}

PhosphorProtocol::CycleTargetResult
SnapNavigationTargetResolver::getCycleTargetForWindow(const QString& windowId, bool forward, const QString& screenId)
{
    if (!checkWindowId(windowId)) {
        qCWarning(PhosphorSnapEngine::lcSnapEngine) << "Cannot getCycleTargetForWindow - empty window ID";
        return cycleResult(false, QStringLiteral("invalid_window"), QString(), QString(), screenId);
    }

    QString currentZoneId = m_service->zoneForWindow(windowId);
    if (currentZoneId.isEmpty()) {
        emitFeedback(false, QStringLiteral("cycle"), QStringLiteral("not_snapped"), QString(), QString(), screenId);
        return cycleResult(false, QStringLiteral("not_snapped"), QString(), QString(), screenId);
    }

    // Cycle only among co-located windows on this output: a layout shared across
    // two monitors shares the zone UUID, so the unfiltered ring would include
    // sibling-monitor windows and cycle focus would jump outputs. Resolve the
    // window's effective screen the same way the move/focus paths do (the stored
    // assignment is authoritative over the effect-reported screen for same-model
    // multi-monitor setups), then pin the ring to it.
    QString effectiveScreenId = screenId;
    {
        const QString storedScreen = m_service->screenForWindow(windowId);
        if (isStoredScreenValid(m_service->screenManager(), storedScreen)) {
            effectiveScreenId = storedScreen;
        }
    }
    QStringList windowsInZone = windowsInZoneOnScreen(currentZoneId, effectiveScreenId);
    if (windowsInZone.isEmpty()) {
        // Best-effort skew rescue (mirrors focus/swap, which fall back only on an
        // EMPTY filtered result): an empty filtered ring means even the calling
        // window's own stored screen-id form didn't match effectiveScreenId
        // (virtual vs bare physical), so fall back to the unfiltered ring. When
        // the filtered ring is non-empty it already contains the calling window,
        // so a size-1 filtered ring is a genuine single-occupant-on-this-output
        // zone — NOT a reason to pull in sibling-monitor windows (doing so would
        // jump cycle focus to another output, the bug this filter prevents).
        windowsInZone = m_service->windowsInZone(currentZoneId);
    }
    if (windowsInZone.size() < 2) {
        emitFeedback(false, QStringLiteral("cycle"), QStringLiteral("single_window"), currentZoneId, currentZoneId,
                     effectiveScreenId);
        return cycleResult(false, QStringLiteral("single_window"), QString(), currentZoneId, effectiveScreenId);
    }

    int currentIndex = windowsInZone.indexOf(windowId);
    if (currentIndex < 0) {
        currentIndex = 0;
        // Fallback: match by current class — handles the case where the
        // stored windowId and the incoming windowId represent the same
        // instance but carry different appIds due to a mid-session rename.
        const QString targetAppId = m_service->currentAppIdFor(windowId);
        for (int i = 0; i < windowsInZone.size(); ++i) {
            const QString entryAppId = m_service->currentAppIdFor(windowsInZone[i]);
            if (entryAppId == targetAppId) {
                currentIndex = i;
                break;
            }
        }
    }
    int nextIndex = forward ? (currentIndex + 1) % windowsInZone.size()
                            : (currentIndex - 1 + windowsInZone.size()) % windowsInZone.size();
    QString targetWindowId = windowsInZone.at(nextIndex);

    emitFeedback(true, QStringLiteral("cycle"), QString(), currentZoneId, currentZoneId, effectiveScreenId);
    return cycleResult(true, QString(), targetWindowId, currentZoneId, effectiveScreenId);
}

PhosphorProtocol::SwapTargetResult SnapNavigationTargetResolver::getSwapTargetForWindow(const QString& windowId,
                                                                                        const QString& direction,
                                                                                        const QString& screenId)
{
    // On failure, windowId1 is returned empty so that a caller which forgets
    // to check `success` cannot accidentally act on the calling window.
    if (!checkWindowId(windowId)) {
        qCWarning(PhosphorSnapEngine::lcSnapEngine) << "Cannot getSwapTargetForWindow - empty window ID";
        return swapResult(false, QStringLiteral("invalid_window"), QString(), 0, 0, 0, 0, QString(), QString(), 0, 0, 0,
                          0, QString(), screenId, QString(), QString());
    }
    if (!checkDirection(direction)) {
        qCWarning(PhosphorSnapEngine::lcSnapEngine) << "Cannot swap - empty direction";
        // Same feedback/result screenId consistency rule as getMoveTargetForWindow.
        emitFeedback(false, QStringLiteral("swap"), QStringLiteral("invalid_direction"), QString(), QString(),
                     screenId);
        return swapResult(false, QStringLiteral("invalid_direction"), QString(), 0, 0, 0, 0, QString(), QString(), 0, 0,
                          0, 0, QString(), screenId, QString(), QString());
    }
    if (!m_zoneAdjacency) {
        // See getMoveTargetForWindow — broken-init state, surfaced on the OSD.
        emitFeedback(false, QStringLiteral("swap"), QStringLiteral("no_zone_detection"), QString(), QString(),
                     screenId);
        return swapResult(false, QStringLiteral("no_zone_detection"), QString(), 0, 0, 0, 0, QString(), QString(), 0, 0,
                          0, 0, QString(), screenId, QString(), QString());
    }

    QString currentZoneId = m_service->zoneForWindow(windowId);
    if (currentZoneId.isEmpty()) {
        emitFeedback(false, QStringLiteral("swap"), QStringLiteral("not_snapped"), QString(), QString(), screenId);
        return swapResult(false, QStringLiteral("not_snapped"), QString(), 0, 0, 0, 0, QString(), QString(), 0, 0, 0, 0,
                          QString(), screenId, QString(), QString());
    }

    // Trust stored screen for snapped windows — see getMoveTargetForWindow comment
    QString effectiveScreenId = screenId;
    {
        QString storedScreen = m_service->screenForWindow(windowId);
        if (isStoredScreenValid(m_service->screenManager(), storedScreen)) {
            effectiveScreenId = storedScreen;
        }
    }

    QString targetZoneId = m_zoneAdjacency->getAdjacentZone(currentZoneId, direction, effectiveScreenId);
    if (targetZoneId.isEmpty()) {
        // No adjacent zone on this output — treat monitors as one constructed
        // surface and cross into the neighbour output's entry zone, swapping with
        // its occupant (or moving into it if empty) before giving up.
        const PhosphorProtocol::SwapTargetResult cross =
            crossOutputSwapTarget(windowId, currentZoneId, direction, effectiveScreenId);
        if (cross.success) {
            emitFeedback(true, QStringLiteral("swap"), QStringLiteral("screen:") + direction, currentZoneId,
                         cross.targetZoneId, cross.screenName);
            return cross;
        }
        // Defer the boundary decision AND its feedback to the caller —
        // SnapEngine tries the cross-mode axis and emits the boundary
        // feedback itself when it fails, in every configuration (see the
        // matching note in getMoveTargetForWindow).
        return swapResult(false, QStringLiteral("no_adjacent_zone"), QString(), 0, 0, 0, 0, QString(), QString(), 0, 0,
                          0, 0, QString(), effectiveScreenId, currentZoneId, QString());
    }

    QRect targetGeom = m_service->zoneGeometry(targetZoneId, effectiveScreenId);
    QRect currentGeom = m_service->zoneGeometry(currentZoneId, effectiveScreenId);
    if (!targetGeom.isValid() || !currentGeom.isValid()) {
        emitFeedback(false, QStringLiteral("swap"), QStringLiteral("geometry_error"), currentZoneId, targetZoneId,
                     effectiveScreenId);
        return swapResult(false, QStringLiteral("geometry_error"), QString(), 0, 0, 0, 0, QString(), QString(), 0, 0, 0,
                          0, QString(), effectiveScreenId, currentZoneId, targetZoneId);
    }

    // The swap counterpart must be on THIS output: the target zone's UUID is
    // shared by every monitor the layout drives, so an unfiltered windowsInZone
    // could surface a sibling-monitor occupant. Pin BOTH the empty-zone decision
    // and the partner pick to effectiveScreenId — a zone empty on THIS output is a
    // move-to-empty (never yank a window off another monitor), and the partner is
    // the same-output occupant. Unlike the focus path, swap does NOT fall back to
    // the unfiltered ring on a miss: mis-swapping a window across outputs is far
    // more disruptive than focus, so the safe degradation is move-to-empty.
    const QStringList windowsInTargetZone = windowsInZoneOnScreen(targetZoneId, effectiveScreenId);
    if (windowsInTargetZone.isEmpty()) {
        emitFeedback(true, QStringLiteral("swap"), direction, currentZoneId, targetZoneId, effectiveScreenId);
        return swapResult(true, QStringLiteral("moved_to_empty"), windowId, targetGeom.x(), targetGeom.y(),
                          targetGeom.width(), targetGeom.height(), targetZoneId, QString(), 0, 0, 0, 0, QString(),
                          effectiveScreenId, currentZoneId, targetZoneId);
    }

    const QString targetWindowId = windowsInTargetZone.first();
    emitFeedback(true, QStringLiteral("swap"), direction, currentZoneId, targetZoneId, effectiveScreenId);
    return swapResult(true, QString(), windowId, targetGeom.x(), targetGeom.y(), targetGeom.width(),
                      targetGeom.height(), targetZoneId, targetWindowId, currentGeom.x(), currentGeom.y(),
                      currentGeom.width(), currentGeom.height(), currentZoneId, effectiveScreenId, currentZoneId,
                      targetZoneId);
}

PhosphorProtocol::MoveTargetResult SnapNavigationTargetResolver::getPushTargetForWindow(const QString& windowId,
                                                                                        const QString& screenId)
{
    if (!checkWindowId(windowId)) {
        qCWarning(PhosphorSnapEngine::lcSnapEngine) << "Cannot getPushTargetForWindow - empty window ID";
        return moveResult(false, QStringLiteral("invalid_window"), QString(), QRect(), QString(), screenId);
    }

    QString emptyZoneId = m_service->findEmptyZone(screenId);
    if (emptyZoneId.isEmpty()) {
        emitFeedback(false, QStringLiteral("push"), QStringLiteral("no_empty_zone"), QString(), QString(), screenId);
        return moveResult(false, QStringLiteral("no_empty_zone"), QString(), QRect(), QString(), screenId);
    }

    QRect geo = m_service->zoneGeometry(emptyZoneId, screenId);
    if (!geo.isValid()) {
        emitFeedback(false, QStringLiteral("push"), QStringLiteral("geometry_error"), QString(), emptyZoneId, screenId);
        return moveResult(false, QStringLiteral("geometry_error"), emptyZoneId, QRect(), QString(), screenId);
    }

    emitFeedback(true, QStringLiteral("push"), QString(), QString(), emptyZoneId, screenId);
    return moveResult(true, QString(), emptyZoneId, geo, QString(), screenId);
}

PhosphorProtocol::MoveTargetResult SnapNavigationTargetResolver::getSnapToZoneByNumberTarget(const QString& windowId,
                                                                                             int zoneNumber,
                                                                                             const QString& screenId)
{
    if (!checkWindowId(windowId)) {
        qCWarning(PhosphorSnapEngine::lcSnapEngine) << "Cannot getSnapToZoneByNumberTarget - empty window ID";
        return moveResult(false, QStringLiteral("invalid_window"), QString(), QRect(), QString(), screenId);
    }

    if (zoneNumber < 1 || zoneNumber > 9) {
        emitFeedback(false, QStringLiteral("snap"), QStringLiteral("invalid_zone_number"), QString(), QString(),
                     screenId);
        return moveResult(false, QStringLiteral("invalid_zone_number"), QString(), QRect(), QString(), screenId);
    }

    // resolveLayoutForScreen accepts both connector names and screen IDs;
    // screenIdForName is idempotent (returns input if already a screen ID).
    auto* layout = m_layoutManager->resolveLayoutForScreen(screenId);
    if (!layout) {
        emitFeedback(false, QStringLiteral("snap"), QStringLiteral("no_active_layout"), QString(), QString(), screenId);
        return moveResult(false, QStringLiteral("no_active_layout"), QString(), QRect(), QString(), screenId);
    }

    PhosphorZones::Zone* targetZone = nullptr;
    for (PhosphorZones::Zone* zone : layout->zones()) {
        if (zone->zoneNumber() == zoneNumber) {
            targetZone = zone;
            break;
        }
    }

    if (!targetZone) {
        emitFeedback(false, QStringLiteral("snap"), QStringLiteral("zone_not_found"), QString(), QString(), screenId);
        return moveResult(false, QStringLiteral("zone_not_found"), QString(), QRect(), QString(), screenId);
    }

    QString zoneId = targetZone->id().toString();
    QRect geo = m_service->zoneGeometry(zoneId, screenId);
    if (!geo.isValid()) {
        emitFeedback(false, QStringLiteral("snap"), QStringLiteral("geometry_error"), QString(), zoneId, screenId);
        return moveResult(false, QStringLiteral("geometry_error"), zoneId, QRect(), QString(), screenId);
    }

    emitFeedback(true, QStringLiteral("snap"), QString(), QString(), zoneId, screenId);
    return moveResult(true, QString(), zoneId, geo, QString(), screenId);
}

} // namespace PhosphorSnapEngine
