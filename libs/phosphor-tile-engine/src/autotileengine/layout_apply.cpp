// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

// Qt headers
#include <algorithm>
#include <cmath>
#include <QDebug>
#include <QJsonArray>
#include <QJsonDocument>
#include <QPointer>
#include <QScopeGuard>
#include <QScreen>
#include <QTimer>
#include <QVarLengthArray>

// Project headers
#include <PhosphorTileEngine/AutotileEngine.h>
#include <PhosphorTiles/AlgorithmRegistry.h>
#include <PhosphorTiles/ITileAlgorithmRegistry.h>
#include <PhosphorGeometry/GeometryUtils.h>
#include <PhosphorTileEngine/AutotileConfig.h>
#include <PhosphorTileEngine/NavigationController.h>
#include <PhosphorTileEngine/PerScreenConfigResolver.h>
#include <PhosphorTiles/AlgorithmPreviewParams.h>
#include <PhosphorTiles/TilingAlgorithm.h>
// DwindleMemoryAlgorithm.h no longer needed — prepareTilingState() is virtual on PhosphorTiles::TilingAlgorithm
#include <PhosphorTiles/TilingState.h>
#include <PhosphorTiles/SplitTree.h>
#include <PhosphorEngine/PerScreenKeys.h>
#include <PhosphorTiles/AutotileConstants.h>
#include <PhosphorZones/Layout.h>
#include <PhosphorZones/LayoutRegistry.h>
#include "tileenginelogging.h"
#include <PhosphorIdentity/WindowId.h>
#include <PhosphorScreens/Manager.h>
#include <PhosphorScreens/VirtualScreen.h>
#include <PhosphorZones/Zone.h>
#include <PhosphorScreens/ScreenIdentity.h>
#include "engine_internal.h"

namespace PhosphorTileEngine {

bool AutotileEngine::recalculateLayout(const QString& screenId)
{
    if (screenId.isEmpty()) {
        qCWarning(PhosphorTileEngine::lcTileEngine) << "AutotileEngine::recalculateLayout: empty screen name";
        return false;
    }

    PhosphorTiles::TilingState* state = tilingStateForScreen(screenId);
    if (!state) {
        return false;
    }

    PhosphorTiles::TilingAlgorithm* algo = effectiveAlgorithm(screenId);
    if (!algo) {
        qCWarning(PhosphorTileEngine::lcTileEngine) << "AutotileEngine::recalculateLayout: no algorithm set";
        return false;
    }

    const int tiledCount = state->tiledWindowCount();
    if (tiledCount == 0) {
        state->setCalculatedZones({}); // Clear zones when no windows
        return true; // Successfully computed (empty) layout
    }

    // Cap to user's max windows setting — excess windows are not tiled.
    // Also cap at MaxZones: every scripted-algorithm zone path caps its output
    // there, so the engine and algorithm must agree on that ceiling (relevant
    // under Unlimited overflow, where effectiveMaxWindows returns a huge
    // sentinel). Without it, >MaxZones tiled windows would fail the
    // zones.size() == windowCount check below on every retile.
    const int windowCount =
        std::min({tiledCount, effectiveMaxWindows(screenId), PhosphorTiles::AutotileDefaults::MaxZones});

    const QRect screen = screenGeometry(screenId);
    if (!screen.isValid()) {
        qCWarning(PhosphorTileEngine::lcTileEngine)
            << "AutotileEngine::recalculateLayout: invalid screen geometry for" << screenId;
        return false;
    }

    const QString algoId = effectiveAlgorithmId(screenId);

    qCDebug(PhosphorTileEngine::lcTileEngine)
        << "recalculateLayout: screen=" << screenId << "geometry=" << screen << "windows=" << windowCount
        << "algo=" << algoId << "splitRatio=" << state->splitRatio();

    // Calculate zone geometries using the algorithm, with gap-aware zones.
    // Algorithms apply gaps directly using their topology knowledge, eliminating
    // the fragile post-processing step that previously guessed adjacency.
    const bool skipGaps = effectiveSmartGaps(screenId) && windowCount == 1;
    const int innerGap = skipGaps ? 0 : effectiveInnerGap(screenId);
    ::PhosphorLayout::EdgeGaps outerGaps =
        skipGaps ? ::PhosphorLayout::EdgeGaps::uniform(0) : effectiveOuterGaps(screenId);

    // Canonical per-window minimum sizes (logical pixels — same units as zone/
    // screen geometry; do not divide by devicePixelRatio or we under-report).
    // Always populated regardless of effectiveRespectMinimumSize: KWin enforces
    // min sizes whether the user opted in or not, so the bounds clamp below
    // must run unconditionally. The flag only gates whether the *algorithm*
    // sees them (and therefore whether enforceMinSizes runs).
    const QStringList tiled = state->tiledWindows();
    QVector<QSize> windowMinSizes(windowCount, QSize(0, 0));
    for (int i = 0; i < windowCount && i < tiled.size(); ++i) {
        windowMinSizes[i] = m_windowMinSizes.value(tiled[i], QSize(0, 0));
    }
    const bool respectMin = effectiveRespectMinimumSize(screenId);
    const QVector<QSize> minSizes = respectMin ? windowMinSizes : QVector<QSize>{};
    if (respectMin && Q_UNLIKELY(PhosphorTileEngine::lcTileEngine().isDebugEnabled())) {
        for (int i = 0; i < windowCount && i < tiled.size(); ++i) {
            if (windowMinSizes[i].width() > 0 || windowMinSizes[i].height() > 0) {
                qCDebug(PhosphorTileEngine::lcTileEngine)
                    << "  minSize[" << i << "]:" << tiled[i] << "=" << windowMinSizes[i];
            }
        }
    }

    // Build per-window metadata for algorithm context. Live class lookup
    // via currentAppIdFor so scripted algorithms see the CURRENT app class
    // rather than a first-seen string baked into the instance id.
    int focusedIndex = -1;
    QVector<PhosphorTiles::WindowInfo> windowInfos = PhosphorTiles::buildWindowInfos(
        state, windowCount,
        [this](const QString& wid) {
            return currentAppIdFor(wid);
        },
        focusedIndex);

    // Build screen metadata for orientation-aware algorithms
    PhosphorTiles::TilingScreenInfo screenInfo;
    screenInfo.id = screenId;
    {
        QScreen* qscreen = PhosphorScreens::ScreenIdentity::findByIdOrName(screenId);
        if (qscreen) {
            const QRect geom = qscreen->geometry();
            screenInfo.portrait = geom.height() > geom.width();
            screenInfo.aspectRatio = geom.height() > 0 ? static_cast<qreal>(geom.width()) / geom.height() : 0.0;
        } else if (m_screenManager) {
            // Virtual screen IDs have no QScreen — use PhosphorScreens::ScreenManager geometry
            const QRect geom = m_screenManager->screenGeometry(screenId);
            if (geom.isValid()) {
                screenInfo.portrait = geom.height() > geom.width();
                screenInfo.aspectRatio =
                    (geom.width() > 0 && geom.height() > 0) ? static_cast<double>(geom.width()) / geom.height() : 0.0;
            }
        }
    }

    // Resolve custom params for this algorithm from saved settings.
    // Filter out stale params that no longer match the algorithm's declarations
    // (e.g., user edited the .luau file and renamed/removed a custom param).
    QVariantMap customParams;
    if (m_config) {
        const auto it = m_config->savedAlgorithmSettings.constFind(algoId);
        if (it != m_config->savedAlgorithmSettings.constEnd() && !it->customParams.isEmpty()) {
            if (algo->supportsCustomParams()) {
                for (auto pit = it->customParams.constBegin(); pit != it->customParams.constEnd(); ++pit) {
                    if (algo->hasCustomParam(pit.key())) {
                        customParams[pit.key()] = pit.value();
                    }
                }
            }
            // else: algorithm doesn't support custom params — don't pass any
        }
    }
    // Layer a per-context SetAlgorithmParam rule override on top of the config
    // (rule wins per-param). The daemon injected these only when the rule's target
    // algorithm is this screen's effective algorithm; the hasCustomParam filter is
    // a second guard so a stale key for another algorithm is dropped.
    if (m_configResolver && algo->supportsCustomParams()) {
        const QVariantMap ruleParams = m_configResolver->effectiveCustomParamsOverride(screenId);
        for (auto pit = ruleParams.constBegin(); pit != ruleParams.constEnd(); ++pit) {
            if (algo->hasCustomParam(pit.key())) {
                customParams[pit.key()] = pit.value();
            }
        }
    }

    // Before the algorithm gets a chance to build a fresh tree: a toggle-off
    // rescued the old one, and prepareTilingState would otherwise replace the
    // user's manual splits with uniform defaults. Runs here rather than at state
    // creation because it needs the re-added windows to check against, and no-ops
    // unless the tree still matches them.
    restoreStashedSplitTree(currentKeyForScreen(screenId), state, algo);

    // Let memory-based algorithms prepare their state (e.g., lazily create a PhosphorTiles::SplitTree)
    // before calculateZones(). Virtual dispatch avoids concrete type casts here.
    algo->prepareTilingState(state);

    // Pass minSizes to algorithm so it can incorporate them directly into zone
    // calculations using its topology knowledge (split tree, column structure, etc.)
    PhosphorTiles::TilingParams tilingParams;
    tilingParams.windowCount = windowCount;
    tilingParams.screenGeometry = screen;
    tilingParams.state = state;
    tilingParams.innerGap = innerGap;
    tilingParams.outerGaps = outerGaps;
    tilingParams.minSizes = minSizes;
    tilingParams.windowInfos = windowInfos;
    tilingParams.focusedIndex = focusedIndex;
    tilingParams.screenInfo = screenInfo;
    tilingParams.customParams = customParams;
    // Previous applied zones, exposed to scripts as ctx.currentGeometries.
    // Captured before the algorithm runs (state->calculatedZones() is not
    // overwritten until setCalculatedZones below), so it is the prior layout.
    tilingParams.currentGeometries = state->calculatedZones();
    QVector<QRect> zones = algo->calculateZones(tilingParams);

    qCInfo(PhosphorTileEngine::lcTileEngine)
        << "recalculateLayout: screen=" << screenId << "tiledCount=" << tiledCount << "windowCount=" << windowCount
        << "splitRatio=" << state->splitRatio() << "zones=" << zones;

    // Validate algorithm returned correct number of zones
    if (zones.size() != windowCount) {
        qCWarning(PhosphorTileEngine::lcTileEngine) << "AutotileEngine::recalculateLayout: algorithm returned"
                                                    << zones.size() << "zones for" << windowCount << "windows";
        return false;
    }

    // Lightweight safety net: the algorithm handles min sizes directly, but
    // enforceMinSizes catches any residual deficits from rounding or
    // edge cases the algorithm couldn't fully solve (e.g., unsatisfiable
    // constraints).
    //
    // Two opt-outs, and both are the algorithm's own declaration:
    //   - producesOverlappingZones() (Monocle, Cascade, Stair, Deck, Paper,
    //     Spread, horizontal-deck and any future opt-in): zones intentionally
    //     overlap and the implicit removeRectOverlaps inside enforceMinSizes
    //     would destroy the intended layout. These take the grow-in-place
    //     else-branch below instead: no stealing, no overlap removal, just
    //     each zone grown to the min size KWin enforces anyway.
    //   - !supportsMinSizes(): the algorithm says min sizes are not a concept
    //     it works in. Running the pass anyway would apply the very treatment
    //     the flag opts out of, and would silently overrule a shipped script.
    //     Two bundled algorithms declare it: tatami and cluster (theater and
    //     floating-center used to, until their scripts grew min-size handling
    //     and rejoined this pass). Both resolve producesOverlappingZones to
    //     false, so before this gate they were min-size corrected despite
    //     opting out.
    //
    // minSizes is populated iff respectMin (see above). windowCount is
    // tiledCount (>= 1 past the early return at the top) capped by
    // effectiveMaxWindows and by MaxZones. Every source feeding the
    // effectiveMaxWindows cap is clamped to MinMaxWindows or above (the
    // settings-schema validator on the global setting, perAlgoFromVariantMap
    // on per-algorithm entries, AutotileConfig::fromJson, the algorithms'
    // defaultMaxWindows, the qBound on the per-screen MaxWindows override in
    // PerScreenConfigResolver::effectiveMaxWindows, and the Unlimited
    // sentinel), and MaxZones is well above 1, so a zero windowCount is not
    // reachable here. It is a pure backstop anyway:
    // a zero windowCount yields empty zones, which pass the equality check
    // above and leave this block a no-op.
    if (respectMin && algo->supportsMinSizes() && !algo->producesOverlappingZones()) {
        const int threshold = effectiveInnerGap(screenId) + PhosphorTiles::AutotileDefaults::GapEdgeThresholdPx;
        const QVector<QRect> preEnforceZones = zones;
        PhosphorGeometry::enforceMinSizes(zones, minSizes, threshold, innerGap);
        if (Q_UNLIKELY(PhosphorTileEngine::lcTileEngine().isDebugEnabled()) && zones != preEnforceZones) {
            qCDebug(PhosphorTileEngine::lcTileEngine) << "enforceMinSizes: zones adjusted"
                                                      << "before=" << preEnforceZones << "after=" << zones;
        }
    } else if (respectMin && algo->supportsMinSizes() && algo->producesOverlappingZones()) {
        // Overlap layouts skip enforceMinSizes (its removeRectOverlaps would
        // destroy the intended stack), but the compositor still forces every
        // window up to its declared min size. Grow each undersized zone in
        // place so the emitted rect matches what KWin will produce anyway —
        // otherwise the engine's model (centering targets, drag-insert hit
        // tests, lastManagedRect comparisons) disagrees with the real frame.
        // Position is untouched here; clampZonesToScreen below shifts a grown
        // zone back on-screen, which for edge-anchored peeks (deck cards
        // flush against the screen edge) is exactly "grow toward the
        // interior, keep the anchored edge".
        for (int i = 0; i < zones.size() && i < minSizes.size(); ++i) {
            if (minSizes[i].width() > zones[i].width()) {
                zones[i].setWidth(minSizes[i].width());
            }
            if (minSizes[i].height() > zones[i].height()) {
                zones[i].setHeight(minSizes[i].height());
            }
        }
    }

    // Bounds clamp: shift zone position so the effective window rect (after
    // the compositor enforces declared min sizes) stays inside the screen.
    // Without this, a zone narrower/shorter than the window's minSize lets
    // KWin grow the window past the screen edge — and if an adjacent monitor
    // is butted up to that edge, the window's center crosses into it, KWin
    // reassigns its output, and the autotile engine ejects the window.
    //
    // Runs unconditionally — the user's "respect minimum size" preference
    // controls whether the algorithm reflows around min sizes, but it does
    // NOT change KWin's compositor-side enforcement, so we always need this
    // safety net. Position-only; never grows or shrinks zones (size changes
    // are owned by enforceMinSizes, which is unsafe for any algorithm
    // where producesOverlappingZones() is true).
    //
    // Post-clamp zones may overlap. For overlap stacks (any algo with
    // producesOverlappingZones() true — Cascade, Stair, etc.) this is
    // intentional and a shift just changes the visible offset slightly. For
    // non-overlapping algorithms it can also occur when a window's min-size
    // pressure shifts a zone leftward/upward into its neighbor; we
    // deliberately do NOT re-run removeRectOverlaps because it splits the
    // overlap at the midpoint, moving the shifted zone back toward the edge
    // it was just clamped away from — exactly re-introducing the overflow we
    // just fixed.
    //
    // Most downstream consumers (applyTiling, geometry batch builders) index
    // calculatedZones by window position, so they're insensitive to overlap.
    // The one spatial consumer is computeDragInsertIndexAtPoint, which does
    // zones[i].contains(cursorPos) and returns the first hit. In the rare
    // overlap region produced by clamp shift, the lower-indexed zone wins —
    // an acceptable tie-break given (a) overlap area is bounded by the
    // min-size deficit (typically a few hundred pixels at most), (b) the
    // alternative is window ejection to an adjacent monitor, and (c) the
    // cascade/stair algorithms already exercised first-match-wins for years.
    const QVector<QRect> preClampZones = zones;
    PhosphorGeometry::clampZonesToScreen(zones, windowMinSizes, screen);
    if (Q_UNLIKELY(PhosphorTileEngine::lcTileEngine().isDebugEnabled()) && zones != preClampZones) {
        qCDebug(PhosphorTileEngine::lcTileEngine) << "clampZonesToScreen: zones adjusted"
                                                  << "before=" << preClampZones << "after=" << zones;
    }

    // Clamp zones to minimum 1x1 — algorithms or the constraint solver can
    // produce non-positive dimensions when minimum sizes exceed available space.
    for (QRect& zone : zones) {
        if (zone.width() < 1) {
            zone.setWidth(1);
        }
        if (zone.height() < 1) {
            zone.setHeight(1);
        }
    }

    // Store calculated zones in the state for later application
    state->setCalculatedZones(zones);
    return true;
}

// ═══════════════════════════════════════════════════════════════════════════
// Drag-insert preview
// ═══════════════════════════════════════════════════════════════════════════

void AutotileEngine::applyTiling(const QString& screenId)
{
    PhosphorTiles::TilingState* state = tilingStateForScreen(screenId);
    if (!state) {
        return;
    }

    // Drag-insert preview: skip emitting geometry for the dragged window so
    // KWin's interactive move isn't fought. Other windows still animate to
    // their new tile positions, producing the OrderingPage-style shift.
    const bool filterForPreview = m_dragInsertPreview && m_dragInsertPreview->targetScreenId == screenId;
    const QString filteredWindowId = filterForPreview ? m_dragInsertPreview->windowId : QString();

    const QStringList windows = state->tiledWindows();
    const QVector<QRect> zones = state->calculatedZones();

    // zones.size() may be less than windows.size() when maxWindows caps the layout.
    // Only the first zones.size() windows receive tiled geometries; the rest are untouched.
    if (zones.isEmpty()) {
        qCDebug(PhosphorTileEngine::lcTileEngine)
            << "AutotileEngine::applyTiling: no zones calculated for screen" << screenId;
        return;
    }
    if (zones.size() > windows.size()) {
        qCWarning(PhosphorTileEngine::lcTileEngine)
            << "AutotileEngine::applyTiling: zone count exceeds window count" << windows.size() << "vs" << zones.size();
        return;
    }

    const int tileCount = zones.size();

    // Auto-float overflow windows that exceed maxWindows cap.
    // Daemon's windowFloatingChanged handler restores their pre-autotile geometry.
    // Batch: mutate state first, then collect signals for deferred emission.
    QStringList newlyOverflowed = m_overflow.applyOverflow(screenId, windows, tileCount);
    for (const QString& wid : std::as_const(newlyOverflowed)) {
        state->setFloating(wid, true);
    }

    // Build batch JSON and emit once to avoid race when effect applies many geometries.
    // Flag monocle-style layouts where all zones share identical geometry,
    // so the KWin effect can set maximize state on stacked windows.
    // This catches both intentional monocle layouts and degenerate fillArea
    // fallbacks (tiny screens) — maximize is appropriate in both cases since
    // windows are stacked identically.
    // Requires >= 2 zones: a single window is just normal tiling, not monocle.
    const bool useMonocleMode = tileCount >= 2 && std::all_of(zones.begin() + 1, zones.end(), [&](const QRect& z) {
                                    return z == zones[0];
                                });

    // Overlap layouts declare a deterministic z-order for the tiled stack
    // (every bundled overlap layout stacks the last tiled index on top;
    // custom scripts may declare the reverse).
    // Emit it per entry so the effect can restack after applying geometry;
    // the batch is already in tiling order, so the direction is all it needs.
    const PhosphorTiles::TilingAlgorithm* stackAlgo = effectiveAlgorithm(screenId);
    const QString stacking =
        (stackAlgo && stackAlgo->producesOverlappingZones()) ? stackAlgo->overlapStacking() : QString();

    QJsonArray arr;
    for (int i = 0; i < tileCount; ++i) {
        if (filterForPreview && windows[i] == filteredWindowId) {
            continue;
        }
        // No inset: the KWin effect's border shader recolours each window's own
        // outermost band (inside the frame), so a tiled window fills its zone
        // exactly and the border never pushes past the slot into the neighbour
        // (mirrors the snap side, DaemonGeometryResolver::snapBorderInset == 0).
        // Tile spacing comes from the zone gap/padding settings, not the border.
        const QRect& geo = zones[i];
        // Remember the emitted rect for lastManagedRect(): the float-toggle
        // capture path compares the live frame against it AFTER the tiled bit
        // has already cleared (see the header doc on m_lastAppliedTileRect).
        m_lastAppliedTileRect.insert(windows[i], geo);
        QJsonObject obj;
        obj[QLatin1String("windowId")] = windows[i];
        obj[QLatin1String("screenId")] = screenId;
        obj[QLatin1String("x")] = geo.x();
        obj[QLatin1String("y")] = geo.y();
        obj[QLatin1String("width")] = geo.width();
        obj[QLatin1String("height")] = geo.height();
        // Flag monocle entries so the effect can set KWin maximize state,
        // which makes Plasma panels recognize the window and unfloat.
        if (useMonocleMode) {
            obj[QLatin1String("monocle")] = true;
        }
        if (!stacking.isEmpty()) {
            obj[QLatin1String("stacking")] = stacking;
        }
        arr.append(obj);
    }
    // Include overflow windows in the batch with "floating" flag so the effect
    // can restore their pre-autotile geometry in one pass, instead of receiving
    // individual D-Bus windowFloatingChanged + applyGeometryRequested per window.
    for (const QString& wid : std::as_const(newlyOverflowed)) {
        QJsonObject obj;
        obj[QLatin1String("windowId")] = wid;
        obj[QLatin1String("floating")] = true;
        obj[QLatin1String("screenId")] = screenId;
        arr.append(obj);
    }

    if (Q_UNLIKELY(PhosphorTileEngine::lcTileEngine().isDebugEnabled())) {
        for (int i = 0; i < tileCount; ++i) {
            qCDebug(PhosphorTileEngine::lcTileEngine) << "  applyTiling:" << windows[i] << "zone=" << zones[i];
        }
    }

    Q_EMIT windowsTiled(QString::fromUtf8(QJsonDocument(arr).toJson(QJsonDocument::Compact)));

    // Emit deferred focus AFTER windowsTiled so KWin processes tiles first
    // (including the onComplete raise loop), then focuses the new window on top.
    // Consumed per screen: only this screen's pending request may fire here.
    if (const QString pendingFocus = m_pendingFocusByScreen.take(screenId); !pendingFocus.isEmpty()) {
        Q_EMIT activateWindowRequested(pendingFocus);
    }

    // Batch-notify daemon of overflow float state (replaces per-window
    // windowFloatingChanged). The daemon handler updates WTS state without
    // emitting per-window D-Bus signals since the effect processes float entries
    // from the windowsTileRequested batch.
    if (!newlyOverflowed.isEmpty()) {
        Q_EMIT windowsBatchFloated(newlyOverflowed, screenId);
    }
}

bool AutotileEngine::shouldTileWindow(const QString& rawWindowId) const
{
    if (rawWindowId.isEmpty()) {
        return false;
    }
    const QString windowId = canonicalizeForLookup(rawWindowId);

    // Respect autotile-specific sticky window handling setting.
    // IgnoreAll: sticky windows are never autotiled.
    // RestoreOnly: sticky windows are not auto-managed (autotiling is active management).
    // TreatAsNormal: sticky windows are tiled like any other window.
    if (m_windowTracker && m_windowTracker->isWindowSticky(windowId)) {
        auto* s = autotileSettings();
        if (s) {
            const auto handling = s->autotileStickyWindowHandling();
            if (handling == PhosphorEngine::StickyWindowHandling::IgnoreAll
                || handling == PhosphorEngine::StickyWindowHandling::RestoreOnly) {
                qCDebug(PhosphorTileEngine::lcTileEngine)
                    << "Window" << windowId << "is sticky, handling=" << static_cast<int>(handling)
                    << ", skipping tile";
                return false;
            }
        }
    }

    // Check if window is floating in any screen's PhosphorTiles::TilingState
    // (floating windows are excluded from autotiling).
    // Only check states for the current desktop/activity.
    for (const QString& screen : m_autotileScreens) {
        const TilingStateKey key = currentKeyForScreen(screen);
        auto it = m_states.states().constFind(key);
        if (it != m_states.states().constEnd() && it.value() && it.value()->isFloating(windowId)) {
            qCDebug(PhosphorTileEngine::lcTileEngine) << "Window" << windowId << "is floating, skipping tile";
            return false;
        }
    }

    // Note: Other exclusions (special windows, dialogs, fullscreen, etc.)
    // are already handled by KWin effect's shouldHandleWindow() before
    // sending window events to daemon.

    return true;
}

QString AutotileEngine::screenForWindow(const QString& rawWindowId) const
{
    const QString windowId = canonicalizeForLookup(rawWindowId);
    // Check if already tracked
    auto it = m_states.windowKeys().constFind(windowId);
    if (it != m_states.windowKeys().constEnd()) {
        return it->screenId;
    }

    // R6 fix: Warn when falling back to primary screen — this may indicate a
    // missing screen name in windowOpened() or a stale m_states entry.
    if (m_screenManager) {
        const PhosphorScreens::PhysicalScreen primary = m_screenManager->primaryScreen();
        if (primary.isValid()) {
            qCWarning(PhosphorTileEngine::lcTileEngine)
                << "screenForWindow: window" << windowId << "not in m_states, falling back to primary screen";
            // If the primary monitor is subdivided into virtual screens, return
            // the first virtual screen ID instead of the physical ID.
            const QString physId = primary.identifier;
            const QStringList vsIds = m_screenManager->virtualScreenIdsFor(physId);
            return vsIds.isEmpty() ? physId : vsIds.first();
        }
    }

    qCWarning(PhosphorTileEngine::lcTileEngine) << "screenForWindow: no screen found for window" << windowId;
    return QString();
}

QRect AutotileEngine::screenGeometry(const QString& screenId) const
{
    if (!m_screenManager) {
        return QRect();
    }

    // Virtual screens: use PhosphorScreens::ScreenManager's virtual-aware geometry
    if (PhosphorIdentity::VirtualScreenId::isVirtual(screenId)) {
        return m_screenManager->screenAvailableGeometry(screenId);
    }

    // Physical screens: resolve through the manager's cache-backed string
    // overload, which reads the tracked-screen snapshot (from the screen
    // provider) rather than a live QScreen. This is behaviourally identical to
    // the old findByIdOrName + actualAvailableGeometry(QScreen*) path on a real
    // system (both feed the same available-geometry/strut cache) AND resolves
    // synthetic, QScreen-less screens from a test provider — without it,
    // directional cross-output navigation cannot be exercised headlessly.
    const QRect geom = m_screenManager->screenAvailableGeometry(screenId);
    if (geom.isValid()) {
        return geom;
    }

    // Last resort: a live QScreen the manager has not tracked yet (a hotplug
    // race). The QScreen* overload resolves the connector and falls back to
    // QScreen::availableGeometry().
    QScreen* screen = PhosphorScreens::ScreenIdentity::findByIdOrName(screenId);
    if (!screen) {
        return QRect();
    }
    return m_screenManager->actualAvailableGeometry(screen);
}

bool AutotileEngine::isKnownScreen(const QString& screenId) const
{
    if (!m_screenManager) {
        // Without PhosphorScreens::ScreenManager, skip validation (test environments)
        return true;
    }
    if (PhosphorIdentity::VirtualScreenId::isVirtual(screenId)) {
        return m_screenManager->screenGeometry(screenId).isValid();
    }
    // Physical screens: resolve via the manager's tracked-screen snapshot
    // (the screen provider), which is equivalent to a live-QScreen lookup on a
    // real system but also recognises synthetic, QScreen-less screens from a
    // test provider — keeping this consistent with screenGeometry() above.
    // Fall back to findByIdOrName for a not-yet-tracked hotplug race.
    if (m_screenManager->screenGeometry(screenId).isValid()) {
        return true;
    }
    return PhosphorScreens::ScreenIdentity::findByIdOrName(screenId) != nullptr;
}

// PropagateScope rationale: CurrentContext is the passive refresh — only the
// current desktop/activity's states are written, so a per-desktop tuning on
// another desktop survives. AllContexts is for a refresh that has just DROPPED
// the per-key user-tuned flags because the user changed the global value in
// Settings. The clear spans every key, so the write must too — a clear that
// outruns the write leaves a state holding a tuned value with no flag protecting
// it, and the next CurrentContext propagate to run while that state's desktop is
// current overwrites the user's value out of nowhere. Same clear-scope/
// write-scope pairing as setGlobalSplitRatio and its master-count twin.
void AutotileEngine::propagateGlobalSplitRatio(PropagateScope scope)
{
    // A passive refresh (CurrentContext) propagates only to current
    // desktop/activity states — per-desktop split ratio adjustments (via
    // increaseMasterRatio) are preserved on other desktops. States the user
    // explicitly tuned (m_userTunedSplitRatio) and screens with a per-screen
    // override are skipped, so a local ratio tweak is never clobbered by a
    // settings refresh. AllContexts is the caller saying it has just dropped
    // every tuned flag for this value (see PropagateScope).
    for (auto it = m_states.states().constBegin(); it != m_states.states().constEnd(); ++it) {
        if (scope == PropagateScope::CurrentContext
            && (it.key().desktop != currentKeyForScreen(it.key().screenId).desktop
                || it.key().activity != m_context.currentActivity())) {
            continue;
        }
        if (it.value() && !hasPerScreenOverride(it.key().screenId, PerScreenKeys::SplitRatio)
            && !m_userTunedSplitRatio.contains(it.key())) {
            it.value()->setSplitRatio(m_config->splitRatio);
        }
    }
}

void AutotileEngine::propagateGlobalMasterCount(PropagateScope scope)
{
    // A passive refresh (CurrentContext) propagates only to current
    // desktop/activity states — per-desktop master count adjustments are
    // preserved on other desktops. States the user explicitly tuned
    // (m_userTunedMasterCount) and per-screen-override screens are skipped, so a
    // local master-count tweak is never clobbered by a refresh. AllContexts is
    // the caller saying it has just dropped every tuned flag for this value (see
    // PropagateScope).
    for (auto it = m_states.states().constBegin(); it != m_states.states().constEnd(); ++it) {
        if (scope == PropagateScope::CurrentContext
            && (it.key().desktop != currentKeyForScreen(it.key().screenId).desktop
                || it.key().activity != m_context.currentActivity())) {
            continue;
        }
        if (it.value() && !hasPerScreenOverride(it.key().screenId, PerScreenKeys::MasterCount)
            && !m_userTunedMasterCount.contains(it.key())) {
            it.value()->setMasterCount(m_config->masterCount);
        }
    }
}

void AutotileEngine::backfillWindows()
{
    // Algorithm lifecycle ADD hooks are deliberately not fired here (nor by
    // the strict pending-order eager-consume in setAutotileScreens or the
    // drag-insert-preview reorders): backfill runs on algorithm SWITCHES,
    // where the incoming algorithm builds its bookkeeping from the full
    // tiledWindows() list on its first retile rather than incrementally;
    // per-window add hooks before that retile would double-count. The
    // incremental hooks cover steady-state add/remove/migrate only.
    // The same reconciliation covers the drag-insert preview's PRIOR-state
    // mutations too: beginDragInsertPreview's cross-screen adoption
    // (priorState->removeWindow) and cancelDragInsertPreview's restore skip
    // the REMOVE/ADD hooks, and the scheduled retile on the prior screen
    // reconciles that algorithm's bookkeeping against the state's full
    // window list via prepareTilingState().
    for (const QString& screenId : m_autotileScreens) {
        // Overflow recovery is NOT done here — it is handled by retileScreen()
        // which defers signal emission until after the full retile cycle.
        // Doing it here would emit windowFloatingChanged synchronously before
        // the deferred retile fires, creating a feedback loop where the KWin
        // effect processes float state changes mid-transition.

        PhosphorTiles::TilingState* state = tilingStateForScreen(screenId);
        if (!state) {
            continue;
        }
        const int maxWin = effectiveMaxWindows(screenId);
        if (state->tiledWindowCount() >= maxWin) {
            continue;
        }
        // Collect candidates to avoid modifying m_states during iteration
        // (insertWindow mutates m_states, which is unsafe during const iteration)
        QStringList candidates;
        for (auto it = m_states.windowKeys().constBegin(); it != m_states.windowKeys().constEnd(); ++it) {
            if (it.value().screenId == screenId
                && it.value().desktop == currentKeyForScreen(it.value().screenId).desktop
                && it.value().activity == m_context.currentActivity() && !state->containsWindow(it.key())
                && shouldTileWindow(it.key())) {
                candidates.append(it.key());
            }
        }
        for (const QString& windowId : candidates) {
            const bool inserted = insertWindow(windowId, screenId);
            // Same passive float-state sync onWindowAdded does: a window that
            // insertWindow floats here (matched Float rule / restored saved float)
            // — or whose stale WTS float must be cleared because it was placed
            // tiled — would otherwise desync from the daemon until its next add.
            // emitInsertFloatStateSync uses windowFloatingStateSynced (NOT
            // windowFloatingChanged), so it applies no geometry and cannot drive
            // the mid-transition feedback loop the overflow-recovery note warns of.
            if (inserted) {
                emitInsertFloatStateSync(windowId, screenId);
            }
            if (state->tiledWindowCount() >= maxWin) {
                break;
            }
        }
    }
}

} // namespace PhosphorTileEngine
