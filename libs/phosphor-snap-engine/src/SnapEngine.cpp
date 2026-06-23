// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorSnapEngine/SnapEngine.h>
#include <PhosphorSnapEngine/SnapState.h>
#include <PhosphorSnapEngine/snapnavigationtargets.h>
#include <PhosphorSnapEngine/INavigationStateProvider.h>
#include <PhosphorSnapEngine/IZoneAdjacencyResolver.h>
#include <PhosphorSnapEngine/ISnapSettings.h>
#include <PhosphorEngine/IGeometrySettings.h>
#include <PhosphorIdentity/VirtualScreenId.h>
#include <PhosphorLayoutApi/EdgeGaps.h>
#include <PhosphorZones/LayoutRegistry.h>
#include <PhosphorZones/Layout.h>
#include <PhosphorZones/AssignmentEntry.h>
#include "snapenginelogging.h"

namespace PhosphorSnapEngine {

using PhosphorEngine::NavigationContext;

// In production (Daemon::start) all dependencies are non-null. Headless unit
// tests deliberately pass nullptr to construct an engine with minimal parents
// for testing peripheral classes (adaptors, bridges) — every method that
// dereferences a dependency guards it locally. Do not Q_ASSERT here.
SnapEngine::SnapEngine(PhosphorZones::LayoutRegistry* layoutManager,
                       PhosphorEngine::IWindowTrackingService* windowTracker,
                       PhosphorZones::IZoneDetector* zoneDetector, PhosphorEngine::IVirtualDesktopManager* vdm,
                       QObject* parent)
    : PlacementEngineBase(parent)
    , m_layoutManager(layoutManager)
    , m_windowTracker(windowTracker)
    , m_snapState(new SnapState(QString(), this))
    , m_zoneDetector(zoneDetector)
    , m_virtualDesktopManager(vdm)
{
}

PhosphorEngine::ISnapSettings* SnapEngine::snapSettings() const
{
    return dynamic_cast<PhosphorEngine::ISnapSettings*>(engineSettings());
}

namespace {
// Resolve outer gaps following the geometry pipeline's precedence (minus the
// context-rule tier the engine can't reach): per-screen override map -> layout
// override -> global. Mirrors GeometryUtils::resolveOuterGapsFromMap + the
// layout/global branches of getEffectiveOuterGaps so the snap engine's
// empty-zone-fill and rotation paths apply the same gaps as the main pipeline.
::PhosphorLayout::EdgeGaps resolveOuterGapsForScreen(const QVariantMap& perScreen, PhosphorZones::Layout* layout,
                                                     PhosphorEngine::IGeometrySettings* gs)
{
    namespace PSK = PhosphorEngine::PerScreenSnappingKey;
    namespace GD = PhosphorEngine::GeometryDefaults;
    const int globalUniform = gs ? gs->outerGap() : GD::OuterGap;

    // Tier 1: per-screen per-side block wins when engaged and any side is set.
    const auto usePerSideIt = perScreen.constFind(PSK::UsePerSideOuterGap);
    if (usePerSideIt != perScreen.constEnd() && usePerSideIt->toBool()) {
        const auto topIt = perScreen.constFind(PSK::OuterGapTop);
        const auto bottomIt = perScreen.constFind(PSK::OuterGapBottom);
        const auto leftIt = perScreen.constFind(PSK::OuterGapLeft);
        const auto rightIt = perScreen.constFind(PSK::OuterGapRight);
        if (topIt != perScreen.constEnd() || bottomIt != perScreen.constEnd() || leftIt != perScreen.constEnd()
            || rightIt != perScreen.constEnd()) {
            const auto uniformIt = perScreen.constFind(PSK::OuterGap);
            const int fallback = (uniformIt != perScreen.constEnd()) ? uniformIt->toInt() : globalUniform;
            return ::PhosphorLayout::EdgeGaps{(topIt != perScreen.constEnd()) ? topIt->toInt() : fallback,
                                              (bottomIt != perScreen.constEnd()) ? bottomIt->toInt() : fallback,
                                              (leftIt != perScreen.constEnd()) ? leftIt->toInt() : fallback,
                                              (rightIt != perScreen.constEnd()) ? rightIt->toInt() : fallback};
        }
    }
    // Tier 1 (uniform): per-screen uniform override.
    const auto uniformIt = perScreen.constFind(PSK::OuterGap);
    if (uniformIt != perScreen.constEnd()) {
        return ::PhosphorLayout::EdgeGaps::uniform(uniformIt->toInt());
    }

    // Tier 2: layout per-side override, filling unset sides from the global
    // per-side values (or the global uniform), mirroring getEffectiveOuterGaps.
    if (layout && layout->usePerSideOuterGap() && layout->hasPerSideOuterGapOverride()) {
        ::PhosphorLayout::EdgeGaps gaps = layout->rawOuterGaps();
        const bool globalPerSide = gs && gs->usePerSideOuterGap();
        if (gaps.top < 0)
            gaps.top = globalPerSide ? gs->outerGapTop() : globalUniform;
        if (gaps.bottom < 0)
            gaps.bottom = globalPerSide ? gs->outerGapBottom() : globalUniform;
        if (gaps.left < 0)
            gaps.left = globalPerSide ? gs->outerGapLeft() : globalUniform;
        if (gaps.right < 0)
            gaps.right = globalPerSide ? gs->outerGapRight() : globalUniform;
        return gaps;
    }
    // Tier 2 (uniform): layout uniform override.
    if (layout && layout->hasOuterGapOverride()) {
        return ::PhosphorLayout::EdgeGaps::uniform(layout->outerGap());
    }

    // Tier 3: global, honoring global per-side gaps.
    if (gs && gs->usePerSideOuterGap()) {
        return ::PhosphorLayout::EdgeGaps{gs->outerGapTop(), gs->outerGapBottom(), gs->outerGapLeft(),
                                          gs->outerGapRight()};
    }
    return ::PhosphorLayout::EdgeGaps::uniform(globalUniform);
}
} // namespace

SnapEngine::GapParams SnapEngine::resolveGapParams(const QString& screenId, PhosphorZones::Layout* layout) const
{
    namespace PSK = PhosphorEngine::PerScreenSnappingKey;
    auto* gs = dynamic_cast<PhosphorEngine::IGeometrySettings*>(engineSettings());
    if (!gs) {
        return {PhosphorEngine::GeometryDefaults::ZonePadding,
                ::PhosphorLayout::EdgeGaps::uniform(PhosphorEngine::GeometryDefaults::OuterGap)};
    }

    // Per-screen snapping override with virtual->physical fallback, mirroring
    // GeometryUtils::getPerScreenSnappingWithFallback so a per-monitor gap set on
    // a physical screen still applies on its virtual sub-screens.
    QVariantMap perScreen;
    if (!screenId.isEmpty()) {
        perScreen = gs->getPerScreenSnappingSettings(screenId);
        if (perScreen.isEmpty() && PhosphorIdentity::VirtualScreenId::isVirtual(screenId)) {
            perScreen =
                gs->getPerScreenSnappingSettings(PhosphorIdentity::VirtualScreenId::extractPhysicalId(screenId));
        }
    }

    // Zone padding precedence: per-screen -> layout override -> global.
    int zonePadding;
    const auto zpIt = perScreen.constFind(PSK::ZonePadding);
    if (zpIt != perScreen.constEnd()) {
        zonePadding = zpIt->toInt();
    } else if (layout && layout->hasZonePaddingOverride()) {
        zonePadding = layout->zonePadding();
    } else {
        zonePadding = gs->zonePadding();
    }

    return {zonePadding, resolveOuterGapsForScreen(perScreen, layout, gs)};
}

// Out-of-line so unique_ptr<SnapNavigationTargetResolver> can destroy the
// pimpl-style owned resolver without its full type being visible in the
// header (forward-declared in SnapEngine.h).
SnapEngine::~SnapEngine() = default;

void SnapEngine::markWindowReported(const QString& windowId)
{
    if (!windowId.isEmpty()) {
        m_effectReportedWindows.insert(windowId);
    }
}

int SnapEngine::pruneStaleWindows(const QSet<QString>& aliveWindowIds)
{
    int pruned = PlacementEngineBase::pruneStaleWindows(aliveWindowIds);
    for (auto it = m_effectReportedWindows.begin(); it != m_effectReportedWindows.end();) {
        if (!aliveWindowIds.contains(*it)) {
            it = m_effectReportedWindows.erase(it);
            ++pruned;
        } else {
            ++it;
        }
    }
    return pruned;
}

void SnapEngine::setAutotileEngine(PhosphorEngine::IPlacementEngine* engine)
{
    auto* obj = dynamic_cast<QObject*>(engine);
    Q_ASSERT(!engine || obj);
    if (m_autotileEngineObj) {
        disconnect(m_autotileEngineObj, &QObject::destroyed, this, nullptr);
    }
    m_autotileEngineObj = obj;
    m_autotileEngineTyped = engine;
    if (obj) {
        connect(obj, &QObject::destroyed, this, [this]() {
            m_autotileEngineTyped = nullptr;
        });
    }
}

void SnapEngine::setZoneAdjacencyResolver(IZoneAdjacencyResolver* resolver)
{
    m_zoneAdjacencyResolver = resolver;
    // Push the resolver into the target resolver if it exists yet.
    // The target resolver constructs lazily on first navigation call;
    // if that hasn't happened yet, ensureTargetResolver() will pick up
    // m_zoneAdjacencyResolver when it first runs.
    if (m_targetResolver) {
        m_targetResolver->setZoneAdjacencyResolver(resolver);
    }
    // Guard against out-of-order destruction: null the raw pointer if the
    // underlying QObject is destroyed before SnapEngine. The interface is
    // not a QObject, but every production implementor (ZoneDetectionAdaptor)
    // is — dynamic_cast recovers the QObject identity for the connection.
    if (auto* qobj = dynamic_cast<QObject*>(resolver)) {
        connect(qobj, &QObject::destroyed, this, [this]() {
            m_zoneAdjacencyResolver = nullptr;
            if (m_targetResolver) {
                m_targetResolver->setZoneAdjacencyResolver(nullptr);
            }
        });
    }
}

void SnapEngine::setCrossSurfaceResolver(PhosphorEngine::ICrossSurfaceResolver* resolver)
{
    m_crossSurfaceResolver = resolver;
    // Push into the target resolver if it has been constructed; otherwise
    // ensureTargetResolver() picks it up on first navigation.
    if (m_targetResolver) {
        m_targetResolver->setCrossSurfaceResolver(resolver);
    }
}

SnapNavigationTargetResolver* SnapEngine::ensureTargetResolver(const QString& action)
{
    if (m_targetResolver) {
        return m_targetResolver.get();
    }
    if (!m_windowTracker || !m_layoutManager) {
        qCWarning(PhosphorSnapEngine::lcSnapEngine) << "ensureTargetResolver: missing deps "
                                                    << "windowTracker=" << static_cast<void*>(m_windowTracker)
                                                    << "layoutManager=" << static_cast<void*>(m_layoutManager);
        if (!action.isEmpty()) {
            // Surface a specific reason so the OSD doesn't silently swallow
            // the shortcut. engine_unavailable is the canonical tag for
            // "engine couldn't be built" — distinct from no_window /
            // invalid_direction / excluded etc.
            Q_EMIT navigationFeedback(false, action, QStringLiteral("engine_unavailable"), QString(), QString(),
                                      QString());
        }
        return nullptr;
    }
    // Feedback callback forwards into SnapEngine's own navigationFeedback
    // signal — SnapAdaptor relays that to WindowTrackingAdaptor's D-Bus
    // navigationFeedback signal, so external consumers see the same
    // wire format as when the resolver lived on WTA.
    m_targetResolver = std::make_unique<SnapNavigationTargetResolver>(
        m_windowTracker, m_layoutManager, m_zoneAdjacencyResolver,
        [this](bool success, const QString& action, const QString& reason, const QString& sourceZoneId,
               const QString& targetZoneId, const QString& screenId) {
            Q_EMIT navigationFeedback(success, action, reason, sourceZoneId, targetZoneId, screenId);
        });
    m_targetResolver->setCrossSurfaceResolver(m_crossSurfaceResolver);
    // The resolver lacks the current (desktop, activity) context needed to read a
    // neighbour output's mode; supply it so move/swap cross-output paths defer an
    // autotile neighbour to the cross-mode handoff instead of snapping onto it.
    m_targetResolver->setNeighbourAutotileProvider([this](const QString& screenId) {
        return m_layoutManager
            && m_layoutManager->modeForScreen(screenId, currentVirtualDesktopForScreen(screenId), currentActivity())
            == PhosphorZones::AssignmentEntry::Autotile;
    });
    return m_targetResolver.get();
}

void SnapEngine::setNavigationStateProvider(INavigationStateProvider* provider)
{
    m_navState = provider;
    // Guard against out-of-order destruction: null the raw pointer if the
    // underlying QObject is destroyed before SnapEngine.
    if (auto* qobj = dynamic_cast<QObject*>(provider)) {
        connect(qobj, &QObject::destroyed, this, [this]() {
            m_navState = nullptr;
        });
    }
}

// ═══════════════════════════════════════════════════════════════════════════════
// IPlacementEngine — lifecycle
// ═══════════════════════════════════════════════════════════════════════════════

bool SnapEngine::isActiveOnScreen(const QString& screenId) const
{
    // SnapEngine is active on any screen where AutotileEngine is NOT active.
    // Guard via QPointer: if the QObject was destroyed, m_autotileEngineTyped is stale.
    if (m_autotileEngineObj && m_autotileEngineTyped) {
        return !m_autotileEngineTyped->isActiveOnScreen(screenId);
    }
    return true; // No autotile engine → all screens use snapping
}

// windowOpened is implemented in snapengine/lifecycle.cpp

void SnapEngine::windowClosed(const QString& windowId)
{
    m_effectReportedWindows.remove(windowId);
}

void SnapEngine::windowFocused(const QString& windowId, const QString& screenId)
{
    Q_UNUSED(windowId)
    m_lastActiveScreenId = screenId;
}

// toggleWindowFloat and setWindowFloat are implemented in snapengine/float.cpp
// Navigation entry points (focusInDirection, moveFocusedInDirection,
// swapFocusedInDirection, moveFocusedToPosition, pushFocusedToEmptyZone,
// restoreFocusedWindow, toggleFocusedFloat, cycleFocus,
// rotateWindowsInLayout, resnapCurrentAssignments, resnapToNewLayout)
// live in snapengine/navigation_actions.cpp and call back into
// INavigationStateProvider (m_navState) for fallback target resolution
// and compositor-layer state. The resnap-by-layout-switch pipeline
// (calculateResnapEntriesFromAutotileOrder, snapAllWindows etc.) lives
// in snapengine/navigation.cpp unchanged.

// SnapEngine::assignToZones was removed — its two callers (windowOpened
// in lifecycle.cpp, unfloatToZone in float.cpp) now go through
// SnapEngine::commitSnap / commitMultiZoneSnap which run the
// full snap orchestration (clear floating, assign zone, emit state
// change). The raw-assign path was the last thin wrapper that bypassed
// the orchestration layer.

void SnapEngine::saveState()
{
    if (m_saveFn) {
        m_saveFn();
    }
}

void SnapEngine::loadState()
{
    if (m_loadFn) {
        m_loadFn();
    }
}

// ═══════════════════════════════════════════════════════════════════════════════
// IPlacementEngine — navigation overrides (thin delegates)
// ═══════════════════════════════════════════════════════════════════════════════

void SnapEngine::rotateWindows(bool clockwise, const NavigationContext& ctx)
{
    rotateWindowsInLayout(clockwise, ctx.screenId);
}

void SnapEngine::reapplyLayout(const NavigationContext& /*ctx*/)
{
    resnapToNewLayout();
}

void SnapEngine::reapplyManagedWindowAppearance()
{
    if (!m_snapState) {
        return;
    }
    // Re-emit the current zone geometry for every snapped, non-floating window.
    // The compositor routes a non-empty-zoneId applyGeometryRequested through
    // its snap-commit path (markWindowSnapped), which re-hides the title bar and
    // redraws the snap border. The window is already in its zone, so the
    // compositor's applyWindowGeometry no-ops the move — this only re-drives the
    // chrome the compositor dropped on bridge reconnect. No zone reassignment.
    const QStringList snapped = m_snapState->snappedWindows();
    for (const QString& windowId : snapped) {
        if (m_snapState->isFloating(windowId)) {
            continue;
        }
        const QStringList zoneIds = m_snapState->zonesForWindow(windowId);
        if (zoneIds.isEmpty()) {
            continue;
        }
        const QString screenId = m_snapState->screenForWindow(windowId);
        if (screenId.isEmpty()) {
            continue;
        }
        const QRect geo = m_windowTracker->resolveZoneGeometry(zoneIds, screenId);
        if (!geo.isValid()) {
            continue;
        }
        Q_EMIT applyGeometryRequested(windowId, geo.x(), geo.y(), geo.width(), geo.height(), zoneIds.first(), screenId,
                                      false);
    }
}

void SnapEngine::snapAllWindows(const NavigationContext& ctx)
{
    snapAllWindows(ctx.screenId);
}

void SnapEngine::pushToEmptyZone(const NavigationContext& ctx)
{
    pushFocusedToEmptyZone(ctx);
}

// ═══════════════════════════════════════════════════════════════════════════════
// IPlacementEngine — state access
//
// Returns the single SnapState instance wired by Daemon::init(). Currently a
// global state (not per-screen); a future PR will introduce per-screen
// ownership. Callers should null-check — headless unit tests may not wire a
// SnapState.
// ═══════════════════════════════════════════════════════════════════════════════

PhosphorEngine::IPlacementState* SnapEngine::stateForScreen(const QString& screenId)
{
    Q_UNUSED(screenId)
    return m_snapState;
}

const PhosphorEngine::IPlacementState* SnapEngine::stateForScreen(const QString& screenId) const
{
    Q_UNUSED(screenId)
    return m_snapState;
}

} // namespace PhosphorSnapEngine
