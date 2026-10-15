// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

// ═══════════════════════════════════════════════════════════════════════════════
// WindowTrackingAdaptor — the evacuee park
//
// When an output disconnects (unplugged, or a sleeping monitor that drops its
// connector), KWin moves its windows to another output. They float where KWin
// puts them, each engine parks what it held of them, and when the output comes
// back KWin returns the windows nobody touched. The effect reports each
// screen-change settle; this classifies every reported window against the
// ledger and answers with a verdict.
// ═══════════════════════════════════════════════════════════════════════════════

#include "windowtrackingadaptor.h"
#include "evacueeledger.h"
#include "internal.h"
#include "core/platform/logging.h"
#include "core/utils/utils.h"
#include <PhosphorEngine/IPlacementEngine.h>
#include <PhosphorEngine/WindowPlacement.h>
#include <PhosphorIdentity/VirtualScreenId.h>
#include <PhosphorScreens/Manager.h>
#include <PhosphorScrollEngine/ScrollEngine.h>
#include <PhosphorSnapEngine/SnapEngine.h>
#include <PhosphorTileEngine/AutotileEngine.h>
#include <PhosphorWorkspaces/ActivityManager.h>
#include <PhosphorWorkspaces/VirtualDesktopManager.h>
#include <PhosphorZones/LayoutRegistry.h>
#include <QScopeGuard>

#include <array>

namespace PlasmaZones {

using PhosphorProtocol::OutputSettleRow;
using PhosphorProtocol::OutputSettleVerdict;
namespace VirtualScreenId = PhosphorIdentity::VirtualScreenId;

namespace {

bool outputConnected(PhosphorScreens::ScreenManager* mgr, const QString& physicalId)
{
    return mgr && !physicalId.isEmpty() && mgr->physicalScreenFor(physicalId).isValid();
}

bool stateEqualsS0(const OutputSettleRow& row, const EvacueeLedger::Entry& entry)
{
    return entry.hasS0 && row.moveResizeCount == entry.moveResizeCount && row.maximizeMode == entry.maximizeMode
        && row.quickTileMode == entry.quickTileMode && row.fullscreen == entry.fullscreen;
}

/// The screen a row's window is on: the virtual screen of the reported
/// output its frame is in, else the output itself.
QString rowScreen(PhosphorScreens::ScreenManager* mgr, const OutputSettleRow& row)
{
    if (mgr && row.geometry().isValid()) {
        const QString vs = Utils::effectiveScreenIdAt(mgr, row.geometry().center());
        if (!vs.isEmpty() && VirtualScreenId::samePhysical(vs, row.screenId)) {
            return vs;
        }
    }
    return row.screenId;
}

/// Whether @p windowId, placed by PlasmaZones while an output it was parked
/// for was away, is reported on that output: KWin pulling it back. Any other
/// crossing is not a pull-back (F1049).
bool pulledBackOntoParkedOutput(const EvacueeLedger& ledger, const QString& windowId, const QString& screenId)
{
    const auto it = ledger.placedAfterEvacuation.constFind(windowId);
    return it != ledger.placedAfterEvacuation.constEnd()
        && std::any_of(it->cbegin(), it->cend(), [&screenId](const QString& physicalId) {
               return VirtualScreenId::samePhysical(physicalId, screenId);
           });
}

} // namespace

QStringList WindowTrackingAdaptor::parkOutput(const QString& physicalScreenId)
{
    if (physicalScreenId.isEmpty()) {
        return {};
    }
    QStringList parked;
    for (PhosphorEngine::PlacementEngineBase* engine : std::array<PhosphorEngine::PlacementEngineBase*, 3>{
             m_snapEngine.data(), m_autotileEngine.data(), m_scrollEngine.data()}) {
        if (!engine) {
            continue;
        }
        for (const QString& windowId : engine->parkOutput(physicalScreenId)) {
            if (!parked.contains(windowId)) {
                parked.append(windowId);
            }
        }
    }
    for (const QString& rawId : std::as_const(parked)) {
        const QString windowId = shadowWindowId(rawId);
        QList<EvacueeLedger::Entry>& entries = m_evacuees->entries[windowId];
        // Read before the prunes, which drop the float with its store (F670).
        const bool snapFloating = m_cachedSnapEngine && m_cachedSnapEngine->isFloating(windowId);
        const auto known = std::find_if(entries.begin(), entries.end(), [&](const EvacueeLedger::Entry& e) {
            return e.physicalId == physicalScreenId;
        });
        if (known != entries.end()) {
            known->snapFloating = snapFloating;
        } else {
            EvacueeLedger::Entry entry;
            entry.physicalId = physicalScreenId;
            entry.snapFloating = snapFloating;
            entries.append(entry);
        }
        // The record's snap slot naming the output that went away goes before
        // the prunes: a restore or resnap reading it would otherwise bring the
        // window back to that output's zone while it is parked, and a window
        // touched while away must not keep it.
        if (m_service) {
            const auto rec = m_service->placementStore().peekExact(windowId);
            if (rec && VirtualScreenId::samePhysical(rec->screenId, physicalScreenId)) {
                m_service->releaseEngineSlot(windowId, PhosphorEngine::WindowPlacement::snapEngineId());
            }
        }
    }
    if (!parked.isEmpty()) {
        qCInfo(lcDbusWindow) << "parkOutput:" << physicalScreenId << "parked" << parked.size() << "window(s)";
    }
    return parked;
}

void WindowTrackingAdaptor::releaseParkedSlots(const QString& physicalScreenId)
{
    if (!m_service) {
        return;
    }
    // After the tiling prunes, whose teardown captured each window's slot into
    // its record: those slots name the output that went away (F697).
    for (auto it = m_evacuees->entries.cbegin(); it != m_evacuees->entries.cend(); ++it) {
        for (PhosphorEngine::PlacementEngineBase* engine :
             std::array<PhosphorEngine::PlacementEngineBase*, 2>{m_autotileEngine.data(), m_scrollEngine.data()}) {
            if (engine && engine->hasParked(it.key(), physicalScreenId)) {
                m_service->releaseEngineSlot(it.key(), engine->engineId());
            }
        }
    }
}

void WindowTrackingAdaptor::setOutputRetirer(std::function<void(const QString&)> retirer)
{
    m_evacuees->retirer = std::move(retirer);
}

void WindowTrackingAdaptor::setContextModeResolver(
    std::function<PhosphorZones::AssignmentEntry::Mode(const QString&, int, const QString&)> resolver)
{
    m_contextModeResolver = std::move(resolver);
}

PhosphorProtocol::OutputSettleVerdictList
WindowTrackingAdaptor::reportOutputSettle(const PhosphorProtocol::OutputSettleRowList& rows)
{
    PhosphorProtocol::OutputSettleVerdictList verdicts;
    PhosphorScreens::ScreenManager* mgr = m_service ? m_service->screenManager() : nullptr;
    QSet<QString> reassertedScreens;
    for (const OutputSettleRow& row : rows) {
        if (const QString error = row.validationError(); !error.isEmpty()) {
            qCWarning(lcDbusWindow) << "reportOutputSettle: rejecting row:" << error;
            continue;
        }
        // One retire primitive, whichever side learns of the removal first:
        // the effect's settle can name an output the daemon's own
        // screen-removed handler has not reached yet (F690).
        if (!row.sourceConnected && m_evacuees->retirer && outputConnected(mgr, row.sourceScreenId)) {
            m_evacuees->retirer(row.sourceScreenId);
        }
        const QString windowId = shadowWindowId(row.windowId);
        OutputSettleVerdict verdict;
        verdict.windowId = row.windowId;

        auto entriesIt = m_evacuees->entries.find(windowId);
        if (entriesIt != m_evacuees->entries.end()) {
            QList<EvacueeLedger::Entry>& entries = entriesIt.value();
            // S0 is written once per entry while it lives (F766).
            for (EvacueeLedger::Entry& entry : entries) {
                if (row.hasS0 && !entry.hasS0 && VirtualScreenId::samePhysical(entry.physicalId, row.s0ScreenId)) {
                    entry.hasS0 = true;
                    entry.outputUuid = row.s0Uuid;
                    entry.moveResizeCount = row.s0MoveResizeCount;
                    entry.maximizeMode = row.s0MaximizeMode;
                    entry.quickTileMode = row.s0QuickTileMode;
                    entry.fullscreen = row.s0Fullscreen;
                }
            }
            // Returned to the output it was parked for, by uuid when known (a
            // connector renamed on a re-dock keeps its uuid, F748).
            const auto returned = std::find_if(entries.begin(), entries.end(), [&](const EvacueeLedger::Entry& e) {
                return !e.outputUuid.isEmpty() && !row.outputUuid.isEmpty()
                    ? e.outputUuid == row.outputUuid
                    : VirtualScreenId::samePhysical(e.physicalId, row.screenId);
            });
            const bool touched = std::any_of(entries.cbegin(), entries.cend(),
                                             [&](const EvacueeLedger::Entry& e) {
                                                 return e.hasS0 && !stateEqualsS0(row, e);
                                             })
                || !row.kwinOnly;
            // Read before the touched arm drops the entries.
            const bool snapFloating = std::any_of(entries.cbegin(), entries.cend(), [](const EvacueeLedger::Entry& e) {
                return e.snapFloating;
            });
            if (returned != entries.end() && !touched) {
                // KWin returned it untouched: back into its parked place.
                const QString parkedFor = returned->physicalId;
                if (readoptEvacuee(row.windowId, rowScreen(mgr, row), parkedFor)) {
                    verdict.verdict = OutputSettleVerdict::Readopt;
                    verdict.screenId = rowScreen(mgr, row);
                }
            } else if (!touched) {
                // Untouched, but not (yet) back on that output: the output is
                // still gone, or KWin put it on another real one. The park
                // stays and the window floats where it is (F746, F768).
                floatEvacuee(row, snapFloating);
                verdict.verdict = OutputSettleVerdict::EvacueeFloat;
                verdict.screenId = rowScreen(mgr, row);
            } else {
                // Touched while away: the park is dropped, and the crossing
                // is the user's move unless its source is the output going
                // away, which still makes it an evacuee.
                dropEvacueeParks(windowId);
                if (!row.sourceConnected) {
                    floatEvacuee(row, snapFloating);
                    verdict.verdict = OutputSettleVerdict::EvacueeFloat;
                    verdict.screenId = rowScreen(mgr, row);
                } else {
                    verdict.verdict = OutputSettleVerdict::UserMove;
                }
            }
        } else if (!row.sourceConnected) {
            // KWin moved it off an output that went away, and no engine had
            // it to park: an evacuee all the same.
            floatEvacuee(row, false);
            verdict.verdict = OutputSettleVerdict::EvacueeFloat;
            verdict.screenId = rowScreen(mgr, row);
        } else if (pulledBackOntoParkedOutput(*m_evacuees, windowId, row.screenId) || row.kwinOnly) {
            // KWin moved a window PlasmaZones placed: the placement is
            // re-asserted (a PlasmaZones verb while the output was away, and
            // KWin pulled it back onto that output, F1049; or a window KWin
            // reverted in place, F770). kwinOnly is the effect's word that the
            // user did not move it, so a Move to Screen from the keyboard or
            // window menu must not arrive as kwinOnly (F1009).
            const QString screen = reassertEvacuee(row, reassertedScreens);
            if (!screen.isEmpty()) {
                verdict.verdict = OutputSettleVerdict::Reassert;
                verdict.screenId = screen;
            }
            // Only the output it was pulled back onto is done: a pull-back
            // onto another one it was parked for is still to come (F1107).
            if (auto mark = m_evacuees->placedAfterEvacuation.find(windowId);
                mark != m_evacuees->placedAfterEvacuation.end()) {
                mark->removeIf([&row](const QString& physicalId) {
                    return VirtualScreenId::samePhysical(physicalId, row.screenId);
                });
                if (mark->isEmpty()) {
                    m_evacuees->placedAfterEvacuation.erase(mark);
                }
            }
        } else {
            verdict.verdict = OutputSettleVerdict::UserMove;
        }
        qCInfo(lcDbusWindow) << "reportOutputSettle:" << row.windowId << "on" << row.screenId << "from"
                             << row.sourceScreenId << "connected=" << row.sourceConnected << "verdict"
                             << verdict.verdict;
        verdicts.append(verdict);
    }
    // A placement mark lives while an output it names is away. Once all are
    // back this settle has read KWin's pull-backs, and a later crossing is the
    // user's own (F1049).
    m_evacuees->placedAfterEvacuation.removeIf([mgr](const auto& it) {
        return std::all_of(it.value().cbegin(), it.value().cend(), [mgr](const QString& physicalId) {
            return outputConnected(mgr, physicalId);
        });
    });
    return verdicts;
}

bool WindowTrackingAdaptor::readoptEvacuee(const QString& windowId, const QString& onScreen,
                                           const QString& parkedPhysicalId)
{
    const QString canonical = shadowWindowId(windowId);
    // Back on the output it left: the float it could not take where KWin had
    // put it is moot, and a later announce must not float it out of its
    // place (F1046).
    m_evacuees->evacueeFloatPending.remove(canonical);
    m_evacuees->adoptInProgress = true;
    const auto guard = qScopeGuard([this] {
        m_evacuees->adoptInProgress = false;
    });
    const QString returnedPhysical = VirtualScreenId::extractPhysicalId(onScreen);
    // The float hold it took where KWin had put it goes first, silently.
    const std::array<PhosphorEngine::PlacementEngineBase*, 3> engines{m_snapEngine.data(), m_autotileEngine.data(),
                                                                      m_scrollEngine.data()};
    for (PhosphorEngine::PlacementEngineBase* engine : engines) {
        if (engine) {
            engine->releaseWindowOffScreen(canonical, returnedPhysical);
        }
    }
    bool seated = false;
    for (PhosphorEngine::PlacementEngineBase* engine : engines) {
        if (engine) {
            seated = engine->readoptParked(canonical, parkedPhysicalId, returnedPhysical) || seated;
        }
    }
    // No engine holds it floating any more: the daemon's float bit and the
    // subscribers follow (F716).
    const bool floatingSomewhere = (m_cachedSnapEngine && m_cachedSnapEngine->isFloating(canonical))
        || (m_cachedAutotileEngine && m_cachedAutotileEngine->isWindowFloatingInAutotile(canonical))
        || (m_cachedScrollEngine && m_cachedScrollEngine->isWindowFloatingInScroll(canonical));
    if (!floatingSomewhere && m_service && m_service->isWindowFloating(canonical)) {
        m_service->setWindowFloating(canonical, false);
        relayWindowFloatingChanged(windowId, false, onScreen);
    }
    QList<EvacueeLedger::Entry>& entries = m_evacuees->entries[canonical];
    for (auto it = entries.begin(); it != entries.end();) {
        if (it->physicalId != parkedPhysicalId) {
            ++it;
            continue;
        }
        // Every seat is said, ahead of any drop: alone when a context stays
        // parked there (a place granted for later), followed by parkDropped
        // when the entry retires. An arrival that seated nothing (its context
        // runs another mode now) is placed by its open, and the effect must
        // not give back a float as at a re-seat (F1147).
        if (seated) {
            Q_EMIT parkReseated(windowId, it->outputUuid);
        }
        // Retired only once no engine keeps a context of it parked there (F771).
        const bool stillParked = std::any_of(engines.cbegin(), engines.cend(), [&](auto* engine) {
            return engine && engine->hasParked(canonical, parkedPhysicalId);
        });
        if (stillParked) {
            ++it;
            continue;
        }
        Q_EMIT parkDropped(windowId, it->outputUuid);
        it = entries.erase(it);
    }
    if (entries.isEmpty()) {
        m_evacuees->entries.remove(canonical);
    }
    if (seated) {
        captureWindowPlacement(windowId, QString(), /*fromStateChange=*/true);
        qCInfo(lcDbusWindow) << "readoptEvacuee:" << windowId << "re-seated on" << onScreen;
    }
    return seated;
}

void WindowTrackingAdaptor::floatEvacuee(const OutputSettleRow& row, bool snapFloating)
{
    const QString windowId = shadowWindowId(row.windowId);
    PhosphorScreens::ScreenManager* mgr = m_service ? m_service->screenManager() : nullptr;
    const QString screen = rowScreen(mgr, row);
    // The window's own context where it landed: the one in view (0), one
    // hidden desktop, or nullopt when it is on several hidden desktops or on
    // another activity, whose mode cannot be told.
    PhosphorZones::LayoutRegistry* const layouts = layoutRegistry();
    const QString activity = layouts ? layouts->currentActivity() : QString();
    std::optional<int> ownDesktop = 0;
    if (!row.placeableNow) {
        ownDesktop = WindowTrackingInternal::ownDesktopOn(
            windowDesktopContext(windowId), WindowTrackingInternal::shownDesktop(*this, screen), activity);
    }
    // The mode that context runs, not the one in view: a hidden desktop can
    // run another (F1105), and a tiling mode switched off or disabled there
    // runs snapping, as it would in view (F1122).
    bool tiling = false;
    if (ownDesktop && *ownDesktop >= 1) {
        const PhosphorZones::AssignmentEntry::Mode mode = m_contextModeResolver
            ? m_contextModeResolver(screen, *ownDesktop, activity)
            : (layouts ? layouts->modeForScreen(screen, *ownDesktop, activity)
                       : PhosphorZones::AssignmentEntry::Snapping);
        tiling = mode == PhosphorZones::AssignmentEntry::Autotile || mode == PhosphorZones::AssignmentEntry::Scrolling;
    } else if (ownDesktop) {
        tiling = (m_autotileEngine && m_autotileEngine->isActiveOnScreen(screen))
            || (m_scrollEngine && m_scrollEngine->isActiveOnScreen(screen));
    }
    // @p snapFloating is the float the park saw, which the prune of the
    // output's stores has dropped by now.
    const bool snapHeldFloating = m_snapEngine && m_service && (snapFloating || m_service->isWindowFloating(windowId));
    if (!ownDesktop || (!row.placeableNow && tiling)) {
        // Minimized, or out of view in a tiling context or one whose mode is
        // not known: adopted floating at its first arrival where it landed
        // instead of being placed, and an unknown mode is the arrival's to
        // decide (F643, F1047, F1105).
        m_evacuees->evacueeFloatPending.insert(windowId,
                                               EvacueeLedger::PendingFloat{screen, !ownDesktop, snapHeldFloating});
        return;
    }
    // A snap float applies no geometry, so one that cannot be placed yet is
    // stated now, in its own desktop's store (F1047).
    const bool snapFloat = !tiling && snapHeldFloating;
    m_evacuees->adoptInProgress = true;
    const auto guard = qScopeGuard([this] {
        m_evacuees->adoptInProgress = false;
    });
    if (tiling) {
        // Floats where KWin put it; the tiles already there do not move.
        setWindowFloatingForScreen(row.windowId, screen, true);
    } else if (snapFloat) {
        // A snap float keeps floating on the output it landed on, with a
        // residence there and no home on the output it left (F670, F639).
        PhosphorEngine::IPlacementEngine::HandoffContext ctx;
        ctx.windowId = row.windowId;
        ctx.toScreenId = screen;
        ctx.toDesktop = *ownDesktop;
        ctx.wasFloating = true;
        m_snapEngine->handoffReceive(ctx);
    }
}

QString WindowTrackingAdaptor::reassertEvacuee(const OutputSettleRow& row, QSet<QString>& reassertedScreens)
{
    const QString windowId = shadowWindowId(row.windowId);
    // A zone: applied again on the screen it is stored for, whatever
    // "Re-snap on resolution change" says.
    if (m_cachedSnapEngine && !m_cachedSnapEngine->zoneForWindow(windowId).isEmpty() && m_service) {
        const QString screen = m_service->screenForWindow(windowId);
        const QStringList zones = m_service->zonesForWindow(windowId);
        const QRect geo = m_service->resolveZoneGeometry(zones, screen);
        if (geo.isValid()) {
            Q_EMIT applyGeometryRequested(row.windowId, geo.x(), geo.y(), geo.width(), geo.height(), zones.first(),
                                          screen, false,
                                          static_cast<int>(PhosphorProtocol::PlacementPurpose::Restatement));
            return screen;
        }
    }
    // A tile or a column: its screen laid out again, once per settle.
    if (m_cachedAutotileEngine) {
        const QString screen = m_cachedAutotileEngine->screenForTrackedWindow(windowId);
        if (!screen.isEmpty()) {
            if (!reassertedScreens.contains(screen)) {
                reassertedScreens.insert(screen);
                m_cachedAutotileEngine->retile(screen);
            }
            return screen;
        }
    }
    if (m_cachedScrollEngine) {
        const QString screen = m_cachedScrollEngine->screenForTrackedWindow(windowId);
        if (!screen.isEmpty()) {
            if (!reassertedScreens.contains(screen)) {
                reassertedScreens.insert(screen);
                m_cachedScrollEngine->forceReemit(screen);
            }
            return screen;
        }
    }
    // A free window PlasmaZones placed while the output was away: back to
    // where it was before KWin pulled it.
    if (m_evacuees->placedAfterEvacuation.contains(windowId) && row.baseline().isValid()) {
        const QString screen = row.sourceScreenId;
        Q_EMIT applyGeometryRequested(row.windowId, row.baseX, row.baseY, row.baseWidth, row.baseHeight, QString(),
                                      screen, false, static_cast<int>(PhosphorProtocol::PlacementPurpose::Restatement));
        return screen;
    }
    return QString();
}

bool WindowTrackingAdaptor::adoptPendingEvacueeFloat(const QString& windowId, const QString& screenId)
{
    const auto it = m_evacuees->evacueeFloatPending.constFind(shadowWindowId(windowId));
    if (it == m_evacuees->evacueeFloatPending.constEnd()) {
        return false;
    }
    // Taken on any arrival: one on another output than where it landed moved
    // meanwhile, and a tiling float arriving where snapping runs is not owed
    // it (float is per mode), so neither floats (F1047). One whose context's
    // mode was not known floats as an evacuee of the mode it arrives in: in
    // place on a tiling screen, and in snapping only if snap held it floating
    // (F1105).
    const EvacueeLedger::PendingFloat pending = it.value();
    m_evacuees->evacueeFloatPending.erase(it);
    const bool tilingHere = (m_autotileEngine && m_autotileEngine->isActiveOnScreen(screenId))
        || (m_scrollEngine && m_scrollEngine->isActiveOnScreen(screenId));
    const bool owed = tilingHere || (pending.modeUnknown && pending.snapFloat);
    if (!VirtualScreenId::samePhysical(pending.screenId, screenId) || !owed) {
        return false;
    }
    OutputSettleRow row;
    row.windowId = windowId;
    row.screenId = screenId;
    floatEvacuee(row, pending.snapFloat);
    return true;
}

bool WindowTrackingAdaptor::readoptOnArrival(const QString& windowId, const QString& screenId)
{
    // The belt behind the effect's own skip: a window announced on the output
    // it is parked for, after an effect reload lost the records that would
    // have classified it, goes back into its parked place.
    const auto it = m_evacuees->entries.constFind(shadowWindowId(windowId));
    if (it == m_evacuees->entries.constEnd()) {
        return false;
    }
    for (const EvacueeLedger::Entry& entry : it.value()) {
        if (VirtualScreenId::samePhysical(entry.physicalId, screenId)) {
            return readoptEvacuee(windowId, screenId, entry.physicalId);
        }
    }
    return false;
}

void WindowTrackingAdaptor::dropParkedOutsideSpan(const QString& windowId, const PhosphorEngine::DesktopSpan& span)
{
    const QString canonical = shadowWindowId(windowId);
    if (!span.known || !m_evacuees->entries.contains(canonical)) {
        return;
    }
    // A desktop or activity the window left while away is a move: its parked
    // context there goes (F689, memory_clears_on_move).
    const std::array<PhosphorEngine::PlacementEngineBase*, 3> engines{m_snapEngine.data(), m_autotileEngine.data(),
                                                                      m_scrollEngine.data()};
    const int desktops = m_virtualDesktopManager ? m_virtualDesktopManager->desktopCount() : 0;
    for (int desktop = 1; desktop <= desktops; ++desktop) {
        if (span.coversDesktop(desktop)) {
            continue;
        }
        for (PhosphorEngine::PlacementEngineBase* engine : engines) {
            if (engine) {
                engine->dropParked(canonical, QString(), desktop, QString());
            }
        }
    }
    if (m_activityManager && !span.activity.isEmpty()) {
        for (const QString& activity : m_activityManager->activities()) {
            if (span.coversActivity(activity)) {
                continue;
            }
            for (PhosphorEngine::PlacementEngineBase* engine : engines) {
                if (engine) {
                    engine->dropParked(canonical, QString(), 0, activity);
                }
            }
        }
    }
    // An output with nothing parked left is retired from the ledger.
    QList<EvacueeLedger::Entry>& entries = m_evacuees->entries[canonical];
    for (auto it = entries.begin(); it != entries.end();) {
        const bool stillParked = std::any_of(engines.cbegin(), engines.cend(), [&](auto* engine) {
            return engine && engine->hasParked(canonical, it->physicalId);
        });
        if (stillParked) {
            ++it;
            continue;
        }
        Q_EMIT parkDropped(windowId, it->outputUuid);
        it = entries.erase(it);
    }
    if (entries.isEmpty()) {
        m_evacuees->entries.remove(canonical);
    }
}

void WindowTrackingAdaptor::dropEvacueeParks(const QString& windowId)
{
    const QString canonical = shadowWindowId(windowId);
    m_evacuees->evacueeFloatPending.remove(canonical);
    m_evacuees->placedAfterEvacuation.remove(canonical);
    const auto it = m_evacuees->entries.find(canonical);
    if (it == m_evacuees->entries.end()) {
        return;
    }
    for (PhosphorEngine::PlacementEngineBase* engine : std::array<PhosphorEngine::PlacementEngineBase*, 3>{
             m_snapEngine.data(), m_autotileEngine.data(), m_scrollEngine.data()}) {
        if (engine) {
            engine->dropParked(canonical, QString(), 0, QString());
        }
    }
    for (const EvacueeLedger::Entry& entry : std::as_const(it.value())) {
        Q_EMIT parkDropped(windowId, entry.outputUuid);
    }
    m_evacuees->entries.erase(it);
    qCInfo(lcDbusWindow) << "dropEvacueeParks:" << windowId << "touched, closed or gone while its output was away";
}

void WindowTrackingAdaptor::noteEvacueeTouched(const QString& windowId, const QString& placedOnScreen)
{
    if (m_evacuees->adoptInProgress) {
        return;
    }
    const QString canonical = shadowWindowId(windowId);
    const auto it = m_evacuees->entries.constFind(canonical);
    if (it == m_evacuees->entries.constEnd()) {
        return;
    }
    // A placement on the output it is parked for is its own return, not a touch.
    const bool onParkedOutput =
        !placedOnScreen.isEmpty() && std::any_of(it->cbegin(), it->cend(), [&](const EvacueeLedger::Entry& e) {
            return VirtualScreenId::samePhysical(e.physicalId, placedOnScreen);
        });
    if (onParkedOutput) {
        return;
    }
    // Added to a mark an earlier placement left, and only for outputs still
    // away: one already back has had its pull-back (F1107). KWin pulls its
    // windows back inside the Workspace::updateOutputs call that adds the
    // output, before any client hears of it, so a placement made once the
    // daemon sees the output lands after that pull-back (F1125).
    PhosphorScreens::ScreenManager* const mgr = m_service ? m_service->screenManager() : nullptr;
    QStringList awayOutputs = m_evacuees->placedAfterEvacuation.value(canonical);
    for (const EvacueeLedger::Entry& entry : it.value()) {
        if (!outputConnected(mgr, entry.physicalId) && !awayOutputs.contains(entry.physicalId)) {
            awayOutputs.append(entry.physicalId);
        }
    }
    dropEvacueeParks(windowId);
    if (!awayOutputs.isEmpty()) {
        m_evacuees->placedAfterEvacuation.insert(canonical, awayOutputs);
    }
}

void WindowTrackingAdaptor::wireEvacueeTouches()
{
    for (const QMetaObject::Connection& c : std::as_const(m_evacuees->touchConnections)) {
        disconnect(c);
    }
    m_evacuees->touchConnections.clear();
    // A snap commit off the output a window is parked for is a PlasmaZones
    // placement while it was away (F686).
    if (m_cachedSnapEngine) {
        m_evacuees->touchConnections.append(
            connect(m_cachedSnapEngine, &PhosphorSnapEngine::SnapEngine::windowSnapStateChanged, this,
                    [this](const QString& windowId, const PhosphorProtocol::WindowStateEntry& state) {
                        if (state.changeType == QLatin1String("snapped")) {
                            noteEvacueeTouched(windowId, state.screenId);
                        }
                    }));
    }
    // A float verb on it: the engines announce user floats on this channel
    // only (the passive channel carries their own transitions). A minimize
    // or unminimize is a suspension float, not a placement, and KWin still
    // returns a window that was minimized while away.
    for (PhosphorEngine::PlacementEngineBase* engine : std::array<PhosphorEngine::PlacementEngineBase*, 3>{
             m_snapEngine.data(), m_autotileEngine.data(), m_scrollEngine.data()}) {
        if (!engine) {
            continue;
        }
        m_evacuees->touchConnections.append(connect(engine, &PhosphorEngine::PlacementEngineBase::windowFloatingChanged,
                                                    this,
                                                    [this](const QString& windowId, bool, const QString& screenId) {
                                                        if (m_service && m_service->isSuspensionFloat(windowId)) {
                                                            return;
                                                        }
                                                        noteEvacueeTouched(windowId, screenId);
                                                    }));
    }
}

} // namespace PlasmaZones
