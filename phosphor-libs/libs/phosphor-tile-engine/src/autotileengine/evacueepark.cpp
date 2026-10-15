// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

// The autotile engine's evacuee park (see IPlacementEngine's evacuee park
// section). Each tiling context of an output that disconnects is parked with
// its window order, float bits and the user's tuning before the prune tears
// it down; an untouched window KWin brings back re-enters its old place.

#include <PhosphorTileEngine/AutotileEngine.h>

#include <PhosphorTileEngine/PerScreenConfigResolver.h>
#include <PhosphorTiles/SplitTree.h>
#include <PhosphorTiles/TilingState.h>
#include <PhosphorIdentity/VirtualScreenId.h>
#include <PhosphorScreens/Manager.h>
#include <PhosphorZones/AssignmentEntry.h>
#include <PhosphorZones/LayoutRegistry.h>
#include "evacueepark_p.h"
#include "tileenginelogging.h"

#include <algorithm>

namespace PhosphorTileEngine {

using PhosphorEngine::TilingStateKey;
namespace VirtualScreenId = PhosphorIdentity::VirtualScreenId;

namespace {

/// @p screenId carried onto @p returnedPhysical, on the bare output when the
/// returned output no longer has that virtual screen.
QString translateOntoOutput(const QString& screenId, const QString& returnedPhysical,
                            PhosphorScreens::ScreenManager* mgr)
{
    const QString screen = VirtualScreenId::onOutput(screenId, returnedPhysical);
    return screen == returnedPhysical || !mgr || mgr->screenGeometry(screen).isValid() ? screen : returnedPhysical;
}

/// How many of @p windowId's predecessors in @p ctx's parked order are in
/// @p state now: the raw window-order index it re-enters at.
int parkedInsertIndex(const ParkedTileContext& ctx, const QString& windowId, const PhosphorTiles::TilingState* state)
{
    int insertAt = 0;
    for (const QString& earlier : ctx.order) {
        if (earlier == windowId) {
            break;
        }
        if (state->containsWindow(earlier)) {
            ++insertAt;
        }
    }
    return insertAt;
}

/// Hands @p ctx's tuning to @p state, once, with the first window the context
/// takes back, before that window is added: the script bag when the state has
/// none of its own and still runs the algorithm that wrote it (the stash's
/// rule), and the split ratio and master count when @p key carries no tuning
/// of its own. Shared by the in-view re-seat and the out-of-view grant.
void restoreParkedTuning(ParkedTileContext& ctx, const TilingStateKey& key, PhosphorTiles::TilingState* state,
                         const QString& effectiveAlgorithmId, QSet<TilingStateKey>& tunedSplitRatio,
                         QSet<TilingStateKey>& tunedMasterCount)
{
    if (!ctx.scriptState.isEmpty() && state->scriptState().isEmpty() && ctx.algorithmId == effectiveAlgorithmId) {
        state->setScriptState(ctx.scriptState);
    }
    if (ctx.splitRatio && !tunedSplitRatio.contains(key)) {
        state->setSplitRatio(*ctx.splitRatio);
        tunedSplitRatio.insert(key);
    }
    if (ctx.masterCount && !tunedMasterCount.contains(key)) {
        state->setMasterCount(*ctx.masterCount);
        tunedMasterCount.insert(key);
    }
    ctx.scriptState = QJsonObject();
    ctx.splitRatio.reset();
    ctx.masterCount.reset();
}

void dropEmptyContexts(QHash<QString, QVector<ParkedTileContext>>& byOutput)
{
    for (auto outIt = byOutput.begin(); outIt != byOutput.end();) {
        QVector<ParkedTileContext>& contexts = outIt.value();
        contexts.erase(std::remove_if(contexts.begin(), contexts.end(),
                                      [](const ParkedTileContext& ctx) {
                                          return ctx.pending.isEmpty() && ctx.granted.isEmpty();
                                      }),
                       contexts.end());
        outIt = contexts.isEmpty() ? byOutput.erase(outIt) : std::next(outIt);
    }
}

} // namespace

QStringList AutotileEngine::parkOutput(const QString& physicalScreenId)
{
    if (physicalScreenId.isEmpty()) {
        return {};
    }
    if (!m_evacueePark) {
        m_evacueePark = std::make_unique<TileEvacueePark>();
    }
    QVector<ParkedTileContext> contexts;
    QStringList windows;
    const auto& allStates = m_states.states();
    for (auto it = allStates.constBegin(); it != allStates.constEnd(); ++it) {
        const PhosphorTiles::TilingState* state = it.value();
        if (!state || it.key().screenId.isEmpty() || !VirtualScreenId::samePhysical(it.key().screenId, physicalScreenId)
            || state->windowOrder().isEmpty()) {
            continue;
        }
        ParkedTileContext ctx;
        ctx.key = it.key();
        ctx.order = state->windowOrder();
        // Only the user's floats. An overflow float re-enters tiled and the
        // cap floats again whatever does not fit; parked as floating it would
        // come back a user float the overflow recovery never returns (F1029).
        // The marks are still live here: the prune that drains them runs after.
        const QStringList floats = state->floatingWindows();
        for (const QString& windowId : floats) {
            if (!m_overflow.isOverflow(windowId)) {
                ctx.floating.insert(windowId);
            }
        }
        ctx.pending = QSet<QString>(ctx.order.begin(), ctx.order.end());
        if (m_userTunedSplitRatio.contains(it.key())) {
            ctx.splitRatio = state->splitRatio();
        }
        if (m_userTunedMasterCount.contains(it.key())) {
            ctx.masterCount = state->masterCount();
        }
        ctx.scriptState = state->scriptState();
        ctx.algorithmId = m_configResolver->effectiveAlgorithmId(it.key().screenId);
        for (const QString& windowId : std::as_const(ctx.order)) {
            if (!windows.contains(windowId)) {
                windows.append(windowId);
            }
        }
        contexts.append(std::move(ctx));
    }
    // A second park of the same output replaces the first, but only with
    // something to park: once the prune has run there is nothing left, and the
    // daemon's retire can run twice for one removal.
    if (!contexts.isEmpty()) {
        m_evacueePark->byOutput.insert(physicalScreenId, std::move(contexts));
        qCInfo(PhosphorTileEngine::lcTileEngine)
            << "AutotileEngine::parkOutput:" << physicalScreenId << "parked" << windows.size() << "window(s)";
    }
    return windows;
}

bool AutotileEngine::readoptParked(const QString& windowId, const QString& parkedPhysicalId,
                                   const QString& returnedPhysicalId)
{
    if (!m_evacueePark || windowId.isEmpty() || returnedPhysicalId.isEmpty()) {
        return false;
    }
    auto outIt = m_evacueePark->byOutput.find(parkedPhysicalId);
    if (outIt == m_evacueePark->byOutput.end()) {
        return false;
    }
    const QString canonical = canonicalizeWindowId(windowId);
    bool seated = false;
    // Out-of-view contexts on a screen this engine tiles, by index into the
    // output's contexts, seated once the walk is done.
    QList<std::pair<qsizetype, TilingStateKey>> outOfView;
    QVector<ParkedTileContext>& contexts = outIt.value();
    for (qsizetype i = 0; i < contexts.size(); ++i) {
        ParkedTileContext& ctx = contexts[i];
        if (!ctx.pending.contains(canonical)) {
            continue;
        }
        ctx.pending.remove(canonical);
        const QString screen = translateOntoOutput(ctx.key.screenId, returnedPhysicalId, m_screenManager);
        const TilingStateKey key{screen, ctx.key.desktop, ctx.key.activity};
        if (key != currentKeyForScreen(screen)) {
            // A context given another mode while the output was away gets
            // nothing back, like one in view below. isAutotileScreen speaks
            // only for the context in view, so its own assignment decides
            // (F1102).
            if (m_layoutManager
                && m_layoutManager->modeForScreen(screen, key.desktop, key.activity)
                    != PhosphorZones::AssignmentEntry::Mode::Autotile) {
                continue;
            }
            if (isAutotileScreen(screen)) {
                // Seated straight into that context after the walk. Nothing
                // announces the window when its desktop is shown (the effect
                // tracked it at the re-adopt without an announce), and the
                // desktop switch's reconcile walks only windows this engine
                // tracks, so a place left to that arrival was never taken
                // (F1099).
                outOfView.append({i, key});
                continue;
            }
            // The screen is not tiling yet: the place is granted, and taken
            // when the window arrives in that context (insertWindow). It does
            // arrive: the screen's return to autotile drops the effect's
            // tracking of every window on it and announces those in view, and
            // the switch to an out-of-view context announces the rest.
            ctx.granted.insert(canonical, key);
            seated = true;
            continue;
        }
        // The context must still be autotile; one that changed mode while the
        // output was away gets nothing back.
        if (!isAutotileScreen(screen)) {
            continue;
        }
        PhosphorTiles::TilingState* state = tilingStateForScreen(screen);
        if (!state) {
            continue;
        }
        // Into the live state, not the stash: a stashed bag is read only when
        // a state is made or its screen joins the set, and setAutotileScreens
        // has usually done both for the returning output already (F1032).
        // Before the add, so handoffReceive's retile lays the window out with it.
        restoreParkedTuning(ctx, key, state, m_configResolver->effectiveAlgorithmId(screen), m_userTunedSplitRatio,
                            m_userTunedMasterCount);
        HandoffContext handoff;
        handoff.windowId = canonical;
        handoff.toScreenId = screen;
        handoff.wasFloating = ctx.floating.contains(canonical);
        handoff.insertIndex = parkedInsertIndex(ctx, canonical, state);
        handoffReceive(handoff);
        if (!state->containsWindow(canonical)) {
            continue;
        }
        seated = true;
    }
    // After the walk, so the in-view re-seat (whose handoffReceive makes its
    // context the window's primary) has run first. A background context: no
    // retile and no focus. The memo retiles it when it comes into view, and a
    // later announce there finds the window held and inserts nothing.
    for (const auto& [index, key] : std::as_const(outOfView)) {
        ParkedTileContext& ctx = contexts[index];
        PhosphorTiles::TilingState* state = tilingStateForKey(key);
        if (!state) {
            continue;
        }
        if (!state->containsWindow(canonical)) {
            restoreParkedTuning(ctx, key, state, m_configResolver->effectiveAlgorithmId(key.screenId),
                                m_userTunedSplitRatio, m_userTunedMasterCount);
            if (!state->addWindow(canonical, parkedInsertIndex(ctx, canonical, state))) {
                continue;
            }
            if (ctx.floating.contains(canonical)) {
                state->setFloating(canonical, true);
                Q_EMIT windowFloatingStateSynced(canonical, true, key.screenId);
            } else {
                notifyAlgorithmWindowAdded(state, key.screenId, canonical);
            }
            m_dirtyBackgroundContexts.insert(key);
        }
        m_states.addMembership(canonical, key);
        seated = true;
    }
    dropEmptyContexts(m_evacueePark->byOutput);
    if (seated) {
        qCInfo(PhosphorTileEngine::lcTileEngine) << "AutotileEngine::readoptParked:" << canonical << "re-seated from"
                                                 << parkedPhysicalId << "on" << returnedPhysicalId;
    }
    return seated;
}

std::optional<std::pair<int, bool>> AutotileEngine::takeGrantedParkedPlace(const QString& windowId,
                                                                           const TilingStateKey& key,
                                                                           PhosphorTiles::TilingState* state)
{
    if (!m_evacueePark || !state) {
        return std::nullopt;
    }
    for (QVector<ParkedTileContext>& contexts : m_evacueePark->byOutput) {
        for (ParkedTileContext& ctx : contexts) {
            const auto it = ctx.granted.constFind(windowId);
            if (it == ctx.granted.constEnd() || it.value() != key) {
                continue;
            }
            // The tuning comes back with the place, as on the in-view re-seat;
            // left in ctx it went with the context once its last grant was
            // taken (F1030).
            restoreParkedTuning(ctx, key, state, m_configResolver->effectiveAlgorithmId(key.screenId),
                                m_userTunedSplitRatio, m_userTunedMasterCount);
            const std::pair<int, bool> place{parkedInsertIndex(ctx, windowId, state), ctx.floating.contains(windowId)};
            ctx.granted.erase(it);
            dropEmptyContexts(m_evacueePark->byOutput);
            return place;
        }
    }
    return std::nullopt;
}

void AutotileEngine::dropParked(const QString& windowId, const QString& physicalScreenId, int desktop,
                                const QString& activity)
{
    if (!m_evacueePark || windowId.isEmpty()) {
        return;
    }
    const QString canonical = canonicalizeForLookup(windowId);
    for (auto outIt = m_evacueePark->byOutput.begin(); outIt != m_evacueePark->byOutput.end(); ++outIt) {
        if (!physicalScreenId.isEmpty() && outIt.key() != physicalScreenId) {
            continue;
        }
        for (ParkedTileContext& ctx : outIt.value()) {
            if ((desktop == 0 || ctx.key.desktop == desktop) && (activity.isEmpty() || ctx.key.activity == activity)) {
                ctx.pending.remove(canonical);
                ctx.granted.remove(canonical);
            }
        }
    }
    dropEmptyContexts(m_evacueePark->byOutput);
}

bool AutotileEngine::hasParked(const QString& windowId, const QString& physicalScreenId) const
{
    if (!m_evacueePark || windowId.isEmpty()) {
        return false;
    }
    const QString canonical = canonicalizeForLookup(windowId);
    for (auto outIt = m_evacueePark->byOutput.cbegin(); outIt != m_evacueePark->byOutput.cend(); ++outIt) {
        if (!physicalScreenId.isEmpty() && outIt.key() != physicalScreenId) {
            continue;
        }
        for (const ParkedTileContext& ctx : outIt.value()) {
            if (ctx.pending.contains(canonical) || ctx.granted.contains(canonical)) {
                return true;
            }
        }
    }
    return false;
}

} // namespace PhosphorTileEngine
