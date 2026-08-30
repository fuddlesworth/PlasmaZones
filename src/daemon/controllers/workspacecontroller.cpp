// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "workspacecontroller.h"

#include "common/kwinperoutputdesktops.h"

#include <PhosphorEngine/PerScreenStates.h>
#include <PhosphorEngine/WindowRegistry.h>
#include <PhosphorIdentity/VirtualScreenId.h>
#include <PhosphorScreens/Manager.h>
#include <PhosphorScreens/ScreenIdentity.h>
#include <PhosphorWorkspaces/VirtualDesktopManager.h>

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLoggingCategory>
#include <QSaveFile>
#include <QSettings>
#include <QStandardPaths>
#include <QTimer>

#include <algorithm>
#include <iterator>
#include <memory>

Q_LOGGING_CATEGORY(lcWorkspaceCtl, "plasmazones.workspaces.controller", QtWarningMsg)

namespace PlasmaZones {

namespace {
/// Adoption grace: how long to wait for every screen's per-output desktop
/// report before adopting with the global-current fallback.
constexpr int AdoptionTimeoutMs = 3000;
/// Snap-back cooldown per screen: the ledger already enforces one correction
/// in flight, but nothing bounded the rate ACROSS corrections — a sustained
/// disagreement (a bug like the id mismatch above, or an external tool
/// fighting us) would ping-pong desktop switches at report rate and take
/// plasmashell down with the churn. One correction per screen per second is
/// ample for real snap-back and harmless as a ceiling.
constexpr qint64 SnapBackCooldownMs = 1000;
/// State-file write debounce: map churn (a create + settle + maintenance
/// run) coalesces into one atomic write.
constexpr int StateSaveDebounceMs = 1000;
}

/// The map, the census, and the reconciler all key screens by the id the
/// KWin effect REPORTS (the EDID-style "Vendor:Model:Serial" form of
/// outputScreenId). ScreenManager hands out CONNECTOR names ("DP-2"), so
/// every ScreenManager-sourced id must pass through here — mixing the two
/// spaces made every owner check fail, which turned every ordinary desktop
/// switch into a "foreign" one and looped snap-back against the user (the
/// desktop churn that crashed plasmashell's Pager).
QString WorkspaceController::canonicalScreenId(const QString& connectorOrId)
{
    const QString id = PhosphorScreens::ScreenIdentity::idForName(connectorOrId);
    return id.isEmpty() ? connectorOrId : id;
}

WorkspaceController::WorkspaceController(PhosphorWorkspaces::VirtualDesktopManager* vdm,
                                         PhosphorEngine::WindowRegistry* registry,
                                         PhosphorScreens::ScreenManager* screens, QObject* parent)
    : QObject(parent)
    , m_vdm(vdm)
    , m_registry(registry)
    , m_screens(screens)
    , m_reconciler(this)
{
    wireVirtualDesktops();
    wireWindows();
    wireScreens();

    connect(&m_reconciler, &PhosphorWorkspaces::WorkspaceReconciler::mapChanged, this,
            &WorkspaceController::publishIfChanged);
    connect(&m_reconciler, &PhosphorWorkspaces::WorkspaceReconciler::requestCreateDesktop, m_vdm,
            &PhosphorWorkspaces::VirtualDesktopManager::createDesktop);
    connect(&m_reconciler, &PhosphorWorkspaces::WorkspaceReconciler::requestRemoveDesktop, m_vdm,
            &PhosphorWorkspaces::VirtualDesktopManager::removeDesktop);
    connect(&m_reconciler, &PhosphorWorkspaces::WorkspaceReconciler::requestSetDesktopName, m_vdm,
            &PhosphorWorkspaces::VirtualDesktopManager::setDesktopName);
    connect(&m_reconciler, &PhosphorWorkspaces::WorkspaceReconciler::renumberComputed, this,
            [this](const QHash<int, int>& oldToNew, const QList<int>& removed) {
                // The engine-side stores refuse a poisoned mapping WHOLE
                // (PhosphorEngine::desktopRenumberMappingIsValid), and the
                // fan-out below this signal walks three more stores entry by
                // entry, one of them persisted. Only the SHIFT is refused
                // though. The two halves answer to different inputs: the
                // reconciler derives `removed` from the desktop-ID delta (the
                // old position of an id that is gone from the settled list),
                // never from `oldToNew`, so a bad NUMBER mapping cannot poison
                // it. Refusing the reap as well left the removed desktops'
                // engine state, order keys and persisted disabled-desktop
                // entries behind with nothing to clean them: the count-based
                // sweeps stand down whenever this controller is adopted, and
                // desktopListChanged only fires when the ordered id list
                // actually changes, so a resync that re-derives the same
                // settled list raises no second renumberComputed to retry on.
                //
                // Reap by identity first (always), then shift the survivors —
                // the contract every engine arm implements. On a refusal the
                // survivors are left one renumber behind, which the resync
                // below is asked to repair with a clean mapping.
                const bool shiftPoisoned =
                    !oldToNew.isEmpty() && !PhosphorEngine::desktopRenumberMappingIsValid(oldToNew);
                if (!removed.isEmpty()) {
                    Q_EMIT desktopsReapRequested(removed);
                }
                if (shiftPoisoned) {
                    qCWarning(lcWorkspaceCtl)
                        << "refusing a poisoned desktop renumber mapping; the reap ran, the shift did not:" << oldToNew;
                    QMetaObject::invokeMethod(m_vdm, "refreshFromKWin");
                    return;
                }
                if (!oldToNew.isEmpty()) {
                    Q_EMIT desktopRenumberRequested(oldToNew);
                }
            });
    connect(&m_reconciler, &PhosphorWorkspaces::WorkspaceReconciler::resyncRequested, this, [this]() {
        // A ledger entry expired: re-pull the authoritative desktop list.
        // String-based on purpose: refreshFromKWin is a PRIVATE slot of
        // VirtualDesktopManager, so a pointer-to-member form does not compile
        // from here. The name is covered by the invoke's own runtime warning
        // if it ever moves.
        QMetaObject::invokeMethod(m_vdm, "refreshFromKWin");
    });
    connect(&m_reconciler, &PhosphorWorkspaces::WorkspaceReconciler::foreignSwitchDetected, this,
            [this](const QString& screenId, const QString& desktopId, const QString& ownerScreenId) {
                qCInfo(lcWorkspaceCtl) << "foreign switch:" << screenId << "showed" << desktopId << "owned by"
                                       << ownerScreenId;
                // Owner-wins: return the screen to its own slice. Two guards:
                // the reconciler's ledger allows one correction in flight, and
                // the cooldown bounds the rate ACROSS corrections so a
                // sustained disagreement degrades to a once-a-second nudge
                // instead of a desktop-switch storm (see SnapBackCooldownMs).
                const qint64 now = QDateTime::currentMSecsSinceEpoch();
                if (now - m_lastSnapBackMs.value(screenId, 0) < SnapBackCooldownMs) {
                    return;
                }
                if (m_reconciler.snapBack(screenId)) {
                    m_lastSnapBackMs.insert(screenId, now);
                    Q_EMIT snapBackOccurred(screenId);
                }
            });
    // The reconciler's per-screen switches surface as effect commands with
    // the desktop int resolved at emit time (ids renumber-safe until here).
    connect(&m_reconciler, &PhosphorWorkspaces::WorkspaceReconciler::requestSetCurrent, this,
            [this](const QString& screenId, const QString& desktopId) {
                const int desktop = m_vdm->desktopIndexOf(desktopId);
                if (desktop > 0) {
                    Q_EMIT screenDesktopSwitchRequested(screenId, desktop);
                    return;
                }
                // The id has no live number right now. This is now a narrow
                // window rather than a standing hazard: issueSetCurrent
                // refuses an unsettled target ahead of its own ledger, which
                // covers the reconciler's internal snap-back as well as every
                // verb path, so nothing reaches here with an entry already
                // open. What can still land is a settled list that moved
                // between that refusal check and this emit. Say so rather
                // than dropping it silently.
                qCWarning(lcWorkspaceCtl) << "refusing a per-screen switch for" << screenId << "to" << desktopId
                                          << "- that desktop has no live number; its ledger entry will expire";
            });
    // Deferred verbs resume once the structural churn settles — and also
    // after a ledger expiry, which clears the structural op WITHOUT a map
    // change (the cap-refusal probe path); without this hook, verbs queued
    // behind a refused create would strand until an unrelated map change.
    connect(&m_reconciler, &PhosphorWorkspaces::WorkspaceReconciler::mapChanged, this,
            &WorkspaceController::drainQuietQueue);
    connect(&m_reconciler, &PhosphorWorkspaces::WorkspaceReconciler::resyncRequested, this,
            &WorkspaceController::drainQuietQueue);
    // Destroy race (plan §4.3 step 4): snapshot the census of the doomed
    // desktop; the removal echo consumes it and re-routes each window to the
    // owner's current workspace (KWin swept them to an arbitrary neighbour).
    connect(&m_reconciler, &PhosphorWorkspaces::WorkspaceReconciler::removalRaceDetected, this,
            [this](const QString& desktopId, const QString& ownerScreenId) {
                DisplacedByRemoval& record = m_displacedByRemoval[desktopId];
                record.ownerScreenId = ownerScreenId;
                record.windowIds.clear();
                for (auto it = m_windowCensusDesktopId.constBegin(); it != m_windowCensusDesktopId.constEnd(); ++it) {
                    if (it.value() == desktopId) {
                        record.windowIds.append(it.key());
                    }
                }
            });
}

WorkspaceController::~WorkspaceController()
{
    if (m_adopted) {
        saveStateFile();
    }
}

bool WorkspaceController::kwinPerOutputEnabled()
{
    // One shared reader with the settings app, deliberately: the two used to
    // carry separate implementations and this one was the weaker, so a kwinrc
    // written by KConfig (marker suffix, or the words "on"/"yes") read as off
    // here while the settings page read it as on. The value applies live on
    // KWin reconfigure(), so reading at gate time reflects the effective mode.
    return kwinPerOutputVirtualDesktopsEnabled();
}

void WorkspaceController::wireVirtualDesktops()
{
    using VDM = PhosphorWorkspaces::VirtualDesktopManager;
    connect(m_vdm, &VDM::kwinDesktopCreated, &m_reconciler,
            &PhosphorWorkspaces::WorkspaceReconciler::onKwinDesktopCreated);
    connect(m_vdm, &VDM::kwinDesktopRemoved, &m_reconciler,
            &PhosphorWorkspaces::WorkspaceReconciler::onKwinDesktopRemoved);
    // Removal-race consumption. Connected AFTER the reconciler's own handler
    // above, so it runs second — and that is fine either way: the snapshot it
    // consumes was taken at removalRaceDetected, and the re-route it queues
    // resolves the owner's current workspace inside runWhenQuiet, once the
    // structural churn has settled. Nothing here reads reconciler state that
    // the removal is in the middle of changing.
    connect(m_vdm, &VDM::kwinDesktopRemoved, this, [this](const QString& desktopId) {
        const auto it = m_displacedByRemoval.constFind(desktopId);
        if (it == m_displacedByRemoval.constEnd()) {
            return;
        }
        const DisplacedByRemoval record = it.value();
        m_displacedByRemoval.erase(it);
        // The hint says "the window moved". Emitting it here would have said
        // that as soon as the riders were QUEUED, and every one of the bodies
        // below can still bail (owner has no current desktop, desktop gone) —
        // leaving the user a card about a move that never happened. Shared so
        // the FIRST body that actually issues a move raises it, exactly once.
        const auto hinted = std::make_shared<bool>(false);
        for (const QString& windowId : record.windowIds) {
            const QString ownerScreen = record.ownerScreenId;
            runWhenQuiet([this, windowId, ownerScreen, hinted]() {
                const QString target = m_reconciler.currentDesktopIdOf(ownerScreen);
                const int desktop = target.isEmpty() ? 0 : m_vdm->desktopIndexOf(target);
                if (desktop <= 0) {
                    return;
                }
                // A sticky window was never displaced (it is on every
                // workspace), so no re-route is issued and no hint is raised
                // on its account.
                if (!watchWindowMove(windowId, target)) {
                    return;
                }
                Q_EMIT windowWorkspaceMoveRequested(windowId, ownerScreen, desktop, target, QStringLiteral("down"),
                                                    /*moveOutput=*/true);
                if (!*hinted) {
                    *hinted = true;
                    Q_EMIT windowDisplacedByRemoval(ownerScreen);
                }
            });
        }
    });
    // Census rows for a desktop that no longer exists. Nothing else clears
    // them: the population map is keyed by desktop ID, which is exactly what
    // survives renumbering, so a dead desktop's row would sit there for the
    // life of the daemon and keep reporting a population for a workspace the
    // user destroyed.
    connect(m_vdm, &VDM::kwinDesktopRemoved, this, [this](const QString& desktopId) {
        m_populationById.remove(desktopId);
        for (auto it = m_windowCensusDesktopId.begin(); it != m_windowCensusDesktopId.end();) {
            if (it.value() == desktopId) {
                it = m_windowCensusDesktopId.erase(it);
            } else {
                ++it;
            }
        }
    });
    connect(m_vdm, &VDM::desktopListChanged, this, [this](const QStringList& ids) {
        if (!m_adopted) {
            tryFirstAdoption();
            return;
        }
        m_reconciler.onDesktopListSettled(ids);
        // A DETECTED removal that was then aborted (KWin kept the desktop)
        // leaves its census snapshot behind for the life of the daemon: the
        // record is consumed only by kwinDesktopRemoved, which now never
        // comes. The settled list is the authority on which desktops exist,
        // so anything it still names has nothing left to re-route.
        const QSet<QString> settled(ids.cbegin(), ids.cend());
        for (auto it = m_displacedByRemoval.begin(); it != m_displacedByRemoval.end();) {
            it = settled.contains(it.key()) ? m_displacedByRemoval.erase(it) : std::next(it);
        }
        // Named declarations re-verify against the settled truth (idempotent;
        // heals a create-echo FIFO mismatch via KWin's own name list).
        if (m_namedApplied && !m_namedEntries.isEmpty()) {
            applyNamedDeclarations(m_namedEntries);
        }
    });
    connect(m_vdm, &VDM::screenDesktopChanged, this, [this](const QString& screenId, int desktop) {
        if (!m_adopted) {
            tryFirstAdoption();
        }
        m_reconciler.onScreenDesktopReport(screenId, desktop);
        // The stream carries per-screen currents; a pure switch changes it
        // without a map mutation, so re-publish (change-gated regardless).
        publishIfChanged();
    });
}

void WorkspaceController::wireWindows()
{
    using Reg = PhosphorEngine::WindowRegistry;
    connect(m_registry, &Reg::windowAppeared, this, &WorkspaceController::onWindowAppeared);
    connect(m_registry, &Reg::windowDisappeared, this, &WorkspaceController::onWindowDisappeared);
    connect(m_registry, &Reg::metadataChanged, this, &WorkspaceController::onMetadataChanged);
}

void WorkspaceController::wireScreens()
{
    using SM = PhosphorScreens::ScreenManager;
    connect(m_screens, &SM::screenAdded, this, [this](const PhosphorScreens::PhysicalScreen& screen) {
        // onScreenAdded FIRST, refreshScreenOrder second — the mirror of the
        // removal path below. ScreenManager emits screenAdded only after it has
        // synced its tracked set, so refreshing the order first would already
        // have registered the new output and onScreenAdded's own
        // "do I know this screen" guard would then early-return on every
        // replug, skipping the home-migration loop and the slice maintenance
        // the replug exists to run. onScreenAdded appends the id itself, and
        // the refresh that follows re-sorts the whole order by geometry.
        m_reconciler.onScreenAdded(canonicalScreenId(screen.name));
        refreshScreenOrder();
    });
    connect(m_screens, &SM::screenRemoved, this, [this](const PhosphorScreens::PhysicalScreen& screen) {
        const QString screenId = canonicalScreenId(screen.name);
        m_reconciler.onScreenRemoved(screenId);
        // The snap-back cooldown is keyed by screen and nothing else clears
        // it, so an unplugged output's stamp would outlive it — and on a
        // replug within the cooldown window it would swallow the first real
        // correction the new session needs.
        m_lastSnapBackMs.remove(screenId);
        refreshScreenOrder();
    });
    connect(m_screens, &SM::screenGeometryChanged, this, [this](const PhosphorScreens::PhysicalScreen&) {
        refreshScreenOrder();
    });
    // A same-model hotplug can flip a screen's id between its bare and
    // "/CONNECTOR"-qualified spellings with NO screenAdded / screenRemoved
    // pair. Without an arm here the map keeps its slice under the dead id,
    // refreshScreenOrder appends the live one alongside it, and the state file
    // persists a phantom screen while the real output owns nothing. Treat it
    // as an unplug of the old id followed by a plug of the new one, which is
    // exactly what the reconciler's migration and home-stamping already model:
    // the slice migrates with homeScreenId stamped, and onScreenAdded brings
    // it home under the new id.
    connect(m_screens, &SM::screenIdentifierChanged, this, [this](const QString& oldId, const QString& newId) {
        const QString from = canonicalScreenId(oldId);
        const QString to = canonicalScreenId(newId);
        if (from.isEmpty() || to.isEmpty() || from == to) {
            return;
        }
        qCInfo(lcWorkspaceCtl) << "screen id changed from" << from << "to" << to << "- re-keying its slice";
        // Re-key the per-output desktop row BEFORE the reconciler's remove and
        // add. onScreenAdded can consult the arriving screen's current desktop,
        // and a rename applied afterwards would have it read the global
        // fallback instead of the value the effect actually reported.
        m_vdm->renameScreen(from, to);
        m_reconciler.onScreenRemoved(from);
        m_lastSnapBackMs.remove(from);
        m_reconciler.onScreenAdded(to);
        refreshScreenOrder();
    });
}

void WorkspaceController::refreshScreenOrder()
{
    // Slice-concatenation order (fork 5): left-to-right by geometry, ties by
    // connector name for determinism.
    auto screens = m_screens->screens();
    std::sort(screens.begin(), screens.end(),
              [](const PhosphorScreens::PhysicalScreen& a, const PhosphorScreens::PhysicalScreen& b) {
                  if (a.geometry.x() != b.geometry.x()) {
                      return a.geometry.x() < b.geometry.x();
                  }
                  if (a.geometry.y() != b.geometry.y()) {
                      return a.geometry.y() < b.geometry.y();
                  }
                  return a.name < b.name;
              });
    QStringList order;
    order.reserve(screens.size());
    for (const auto& screen : screens) {
        // Effect-reported id space, not the connector (see canonicalScreenId).
        order.append(canonicalScreenId(screen.name));
    }
    m_reconciler.onScreenOrderChanged(order);
}

QString WorkspaceController::stateFilePath()
{
    // Runtime state, deliberately NOT config.json and not GenericDataLocation
    // (that tree is user-visible assets). Reconstructible by definition, so a
    // version mismatch or parse failure just falls back to fresh adoption.
    // StateLocation is ALREADY app-scoped (~/.local/state/<org>/<app>), so
    // nothing extra is appended — an earlier build nested a redundant
    // "/plasmazones" level, relocated below.
    return QStandardPaths::writableLocation(QStandardPaths::StateLocation) + QStringLiteral("/workspaces.json");
}

void WorkspaceController::loadStateFile()
{
    // One-time relocation from the earlier double-nested path (state files
    // must move, not drop: the sibling kwin-shortcut backup holds the user's
    // stolen chords, and this file is what keeps ownership stable across a
    // restart).
    const QString legacyPath = QStandardPaths::writableLocation(QStandardPaths::StateLocation)
        + QStringLiteral("/plasmazones/workspaces.json");
    if (!QFile::exists(stateFilePath()) && QFile::exists(legacyPath)) {
        // The parent of the new path is not guaranteed to exist: the legacy
        // file lives one level deeper, so finding it says nothing about the
        // parent directory. rename fails silently without this and the
        // session re-adopts from scratch, losing stable ownership.
        QDir().mkpath(QFileInfo(stateFilePath()).absolutePath());
        QFile::rename(legacyPath, stateFilePath());
    }

    QFile file(stateFilePath());
    if (!file.open(QIODevice::ReadOnly)) {
        return;
    }
    const QString json = QString::fromUtf8(file.readAll());
    if (!m_reconciler.map().fromJson(json)) {
        qCWarning(lcWorkspaceCtl) << "workspace state file unreadable or wrong version; re-adopting fresh";
        return;
    }

    // Canonicalize restored home stamps: entries written before the screen-id
    // fix carry CONNECTOR homes ("DP-2") that can never match a live screen.
    // A home resolving to the entry's current owner means "not displaced" —
    // clear it; anything else is normalized into the reported-id space.
    const QStringList ids = m_reconciler.map().allDesktopIds();
    for (const QString& id : ids) {
        const QString home = m_reconciler.map().entryFor(id).homeScreenId;
        if (home.isEmpty()) {
            continue;
        }
        const QString canonical = canonicalScreenId(home);
        m_reconciler.map().setHomeScreen(id, canonical == m_reconciler.map().ownerOf(id) ? QString() : canonical);
    }

    qCInfo(lcWorkspaceCtl) << "loaded workspace state candidate:" << m_reconciler.map().allDesktopIds().size()
                           << "desktops," << m_reconciler.map().screenOrder().size() << "screens";
}

void WorkspaceController::saveStateFile() const
{
    const QString path = stateFilePath();
    QDir().mkpath(QFileInfo(path).absolutePath());
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        qCWarning(lcWorkspaceCtl) << "cannot write workspace state file" << path;
        return;
    }
    // Only screens that actually REPORTED a current desktop contribute one.
    // currentDesktopForScreen falls back to the GLOBAL current for an unknown
    // screen, and publishing that asserts a current workspace the screen is
    // not showing — the same reason the adoption gate asks for report presence
    // explicitly rather than trusting the fallback.
    QHash<QString, int> currentByScreen;
    const QStringList order = m_reconciler.map().screenOrder();
    for (const QString& screenId : order) {
        if (!m_vdm->hasScreenDesktopReport(screenId)) {
            continue;
        }
        currentByScreen.insert(screenId, m_vdm->currentDesktopForScreen(screenId));
    }
    const QString json = m_reconciler.map().toJson(
        m_reconciler.generation(), currentByScreen,
        [this](const QString& id) {
            return m_vdm->desktopIndexOf(id);
        },
        /*includeState=*/true);
    file.write(json.toUtf8());
    if (!file.commit()) {
        // QSaveFile swallows a failed rename otherwise, and this is the one
        // write that decides whether ownership survives the restart.
        qCWarning(lcWorkspaceCtl) << "could not commit workspace state file" << path << ":" << file.errorString();
    }
}

void WorkspaceController::scheduleStateSave()
{
    m_stateSaveTimer.start();
}

void WorkspaceController::start()
{
    // Candidate map from the previous session; adoption reconciles it against
    // reality (ids in both keep owner/name/home; vanished ids drop; new ids
    // adopt). Loaded BEFORE the screen order so a stored slice for a screen
    // that is gone survives long enough to be migrated with home stamping.
    // The save wiring goes up FIRST. loadStateFile, refreshScreenOrder and the
    // stored-screen migration loop below all mutate the map, and with the
    // connects made afterwards those mutations raised no save at all — the
    // migrated homes only reached disk if something unrelated dirtied the map
    // later in the session.
    m_stateSaveTimer.setSingleShot(true);
    m_stateSaveTimer.setInterval(StateSaveDebounceMs);
    connect(&m_stateSaveTimer, &QTimer::timeout, this, &WorkspaceController::saveStateFile);
    connect(&m_reconciler, &PhosphorWorkspaces::WorkspaceReconciler::mapChanged, this,
            &WorkspaceController::scheduleStateSave);

    loadStateFile();
    const QStringList storedScreens = m_reconciler.map().screenOrder();

    refreshScreenOrder();

    // A stored screen that is not connected right now: its slice migrates to
    // a surviving screen with homeScreenId stamped, exactly like a live
    // unplug — replugging it later brings the workspaces home.
    QStringList liveScreens;
    const auto screens = m_screens->screens();
    liveScreens.reserve(screens.size());
    for (const auto& screen : screens) {
        liveScreens.append(canonicalScreenId(screen.name));
    }
    for (const QString& stored : storedScreens) {
        if (!liveScreens.contains(stored)) {
            m_reconciler.onScreenRemoved(stored);
        }
    }

    // Census seed from the registry's current truth (windows that registered
    // before the feature came up).
    const QStringList ids = m_registry->instanceIds();
    for (const QString& instanceId : ids) {
        onWindowAppeared(instanceId);
    }

    tryFirstAdoption();
    if (!m_adopted) {
        QTimer::singleShot(AdoptionTimeoutMs, this, [this]() {
            if (!m_adopted) {
                qCWarning(lcWorkspaceCtl)
                    << "adoption timeout: not every screen reported a desktop; using global fallback";
                m_adopted = true;
                QHash<QString, QString> currentById;
                const QStringList desktopIds = m_vdm->desktopIds();
                const auto order = m_reconciler.map().screenOrder();
                for (const QString& screenId : order) {
                    const int current = m_vdm->currentDesktopForScreen(screenId);
                    const QString id = m_vdm->desktopIdAt(current);
                    if (id.isEmpty()) {
                        // The global fallback can resolve to a desktop index
                        // with no id (a screen that never reported, and a
                        // current outside the list). An empty id is not a
                        // desktop; inserting one would make the map adopt a
                        // slice pointing at nothing. tryFirstAdoption refuses
                        // outright for this; here we cannot refuse, so the
                        // screen adopts with no current instead.
                        continue;
                    }
                    currentById.insert(screenId, id);
                }
                m_reconciler.adoptAll(desktopIds, currentById);
                // The SAME tail tryFirstAdoption runs. Without it, a session
                // that adopted on the timeout never realized its named
                // workspaces at all: the declarations arrived before adoption,
                // were parked in m_namedEntries, and nothing here applied
                // them — so every named chord, quick slot and RouteToWorkspace
                // rule was a no-op for the life of that session.
                applyNamedDeclarationsAfterAdoption();
            }
        });
    }
}

void WorkspaceController::tryFirstAdoption()
{
    if (m_adopted) {
        return;
    }
    const QStringList ids = m_vdm->desktopIds();
    if (ids.isEmpty()) {
        return;
    }
    const QStringList order = m_reconciler.map().screenOrder();
    if (order.isEmpty()) {
        return;
    }
    // Gate on every known screen having a REAL per-output report (plan §4.3).
    // currentDesktopForScreen falls back to the global current for unknown
    // screens, which would satisfy this loop before the effect's bringup
    // re-sync (the §4.6 ordering anchor) and hand nearly everything to the
    // first screen — so ask for report presence explicitly; the start()
    // timeout is the only path allowed to adopt on the global fallback.
    QHash<QString, QString> currentById;
    for (const QString& screenId : order) {
        if (!m_vdm->hasScreenDesktopReport(screenId)) {
            return;
        }
        const int current = m_vdm->currentDesktopForScreen(screenId);
        const QString id = m_vdm->desktopIdAt(current);
        if (id.isEmpty()) {
            return;
        }
        currentById.insert(screenId, id);
    }
    m_adopted = true;
    m_reconciler.adoptAll(ids, currentById);
    qCInfo(lcWorkspaceCtl) << "adopted" << ids.size() << "desktops across" << order.size() << "screens";
    applyNamedDeclarationsAfterAdoption();
}

void WorkspaceController::applyNamedDeclarationsAfterAdoption()
{
    // applyNamedDeclarations parks its entries and returns while !m_adopted,
    // so BOTH adoption paths owe it this call once adoption completes.
    if (!m_namedApplied && !m_namedEntries.isEmpty()) {
        applyNamedDeclarations(m_namedEntries);
    }
    // Owner-wins sweep over the seeded census. start() feeds the registry's
    // existing windows through onWindowAppeared BEFORE adoption, and
    // reuniteWindowWithOwner refuses while !m_adopted — so every window that
    // existed at bring-up had its reunion check silently skipped and would
    // sit on a foreign output until its next metadata event, which for an
    // idle window never comes. Re-run the check now that the map is real.
    // The per-window cooldown and the one-in-flight guard bound the burst.
    const QHash<QString, QString> seeded = m_windowCensusDesktopId;
    for (auto it = seeded.constBegin(); it != seeded.constEnd(); ++it) {
        reuniteWindowWithOwner(it.key(), it.value());
    }
    drainParkedNamedRoutes();
}

void WorkspaceController::setWindowScreenResolver(std::function<QString(const QString&)> resolver)
{
    m_windowScreenResolver = std::move(resolver);
}

void WorkspaceController::setWindowStickyPredicate(std::function<bool(const QString&)> predicate)
{
    m_windowStickyPredicate = std::move(predicate);
}

void WorkspaceController::reuniteWindowWithOwner(const QString& instanceId, const QString& desktopId)
{
    // Owner-wins, second arm (plan §4.7): the window follows its desktop's
    // owner screen. A window whose desktop belongs to another output would
    // otherwise sit invisible (its own output may never show that desktop
    // again once snap-back holds).
    if (!m_adopted || !m_windowScreenResolver || desktopId.isEmpty()) {
        return;
    }
    if (m_pendingWindowMoves.contains(instanceId)) {
        return; // our own move in flight; its arrival re-runs this check
    }
    const QString owner = m_reconciler.map().ownerOf(desktopId);
    if (owner.isEmpty()) {
        return;
    }
    const QString reported = m_windowScreenResolver(instanceId);
    if (reported.isEmpty()) {
        return; // screen unknown (untracked window): nothing to vouch for
    }
    const QString windowScreen = canonicalScreenId(PhosphorIdentity::VirtualScreenId::extractPhysicalId(reported));
    if (windowScreen == owner) {
        return;
    }
    if (QDateTime::currentMSecsSinceEpoch() - m_lastReunionMs.value(instanceId, 0) < SnapBackCooldownMs) {
        return;
    }
    // One reunion in flight per window. The cooldown alone could not bound the
    // QUEUE: while a structural op is pending every arriving report defers
    // another body, and they all run in one burst when the ledger quiets,
    // each re-emitting the same move.
    if (m_pendingReunions.contains(instanceId)) {
        return;
    }
    m_pendingReunions.insert(instanceId);
    runWhenQuiet([this, instanceId, desktopId, owner]() {
        m_pendingReunions.remove(instanceId);
        if (m_reconciler.map().ownerOf(desktopId) != owner) {
            return; // ownership moved while deferred
        }
        // Re-checked HERE, not only at entry: a displacement re-route (the
        // removal-race arm) can have queued its own move for this window in
        // the meantime, and both bodies drain from the same quiet queue. Two
        // moves for one window is last-write-wins at the compositor and makes
        // the displacement watchdog warn about an arrival its move did get.
        if (m_pendingWindowMoves.contains(instanceId)) {
            return;
        }
        const int desktop = m_vdm->desktopIndexOf(desktopId);
        if (desktop <= 0) {
            return;
        }
        // Cooldown stamped at the EMIT, not at entry: a body that bailed on
        // any guard above issued nothing, and consuming the cooldown for it
        // would suppress the next real reunion for a full second.
        m_lastReunionMs.insert(instanceId, QDateTime::currentMSecsSinceEpoch());
        qCInfo(lcWorkspaceCtl) << "reuniting window" << instanceId << "with its workspace's owner screen" << owner;
        Q_EMIT windowWorkspaceMoveRequested(instanceId, owner, desktop, desktopId, QStringLiteral("down"),
                                            /*moveOutput=*/true);
    });
}

int WorkspaceController::censusDesktop(const PhosphorEngine::WindowMetadata& meta)
{
    if (meta.isSticky.value_or(false)) {
        return 0;
    }
    if (meta.virtualDesktops.size() > 1) {
        return 0;
    }
    return meta.virtualDesktop;
}

void WorkspaceController::adjustPopulationById(const QString& desktopId, int delta)
{
    if (desktopId.isEmpty()) {
        return;
    }
    const int next = qMax(0, m_populationById.value(desktopId, 0) + delta);
    if (next == 0) {
        // An empty desktop and an unknown one read the same to every consumer
        // (value(id, 0)), so drop the row instead of keeping a zero around
        // for a desktop that may since have been destroyed.
        m_populationById.remove(desktopId);
    } else {
        m_populationById.insert(desktopId, next);
    }
    m_reconciler.onPopulationChanged(desktopId, next);
}

void WorkspaceController::adjustPopulation(int desktopInt, int delta)
{
    if (desktopInt <= 0) {
        return;
    }
    adjustPopulationById(m_vdm->desktopIdAt(desktopInt), delta);
}

void WorkspaceController::onWindowAppeared(const QString& instanceId)
{
    const auto meta = m_registry->metadata(instanceId);
    if (!meta) {
        return;
    }
    const int desktop = censusDesktop(*meta);
    if (desktop > 0) {
        const QString id = m_vdm->desktopIdAt(desktop);
        if (!id.isEmpty()) {
            m_windowCensusDesktopId.insert(instanceId, id);
            reuniteWindowWithOwner(instanceId, id);
        }
    }
    adjustPopulation(desktop, +1);
}

void WorkspaceController::onWindowDisappeared(const QString& instanceId)
{
    // Per-window bookkeeping outlives the census entry and has to go with it.
    // The reunion cooldown and the move watchdog are both keyed by window id:
    // left behind, they grow without bound for the life of the daemon, and a
    // reused instance id inherits a cooldown that suppresses the new window's
    // first reunion.
    m_lastReunionMs.remove(instanceId);
    m_pendingReunions.remove(instanceId);
    m_pendingWindowMoves.remove(instanceId);
    m_windowMoveSequences.remove(instanceId);
    m_parkedNamedRoutes.remove(instanceId);
    // A closed window also leaves the removal-race snapshots. Otherwise the
    // deferred re-route emits a move for a window that no longer exists, and
    // the watchdog then warns about the arrival that was never coming.
    // A record whose last window just went away has nothing left to re-route,
    // so drop it here rather than waiting for the kwinDesktopRemoved arm that
    // normally consumes it — that signal may never land for this desktop.
    for (auto it = m_displacedByRemoval.begin(); it != m_displacedByRemoval.end();) {
        it.value().windowIds.removeAll(instanceId);
        if (it.value().windowIds.isEmpty()) {
            it = m_displacedByRemoval.erase(it);
        } else {
            ++it;
        }
    }

    adjustPopulationById(m_windowCensusDesktopId.take(instanceId), -1);
}

void WorkspaceController::onMetadataChanged(const QString& instanceId, const PhosphorEngine::WindowMetadata& oldMeta,
                                            const PhosphorEngine::WindowMetadata& newMeta)
{
    Q_UNUSED(oldMeta)
    // Decrement by remembered ID (renumber-proof), increment by the new int.
    const QString oldId = m_windowCensusDesktopId.take(instanceId);
    const int newDesktop = censusDesktop(newMeta);
    const QString newId = newDesktop > 0 ? m_vdm->desktopIdAt(newDesktop) : QString();
    // A watched move verb confirmed by its arrival (watchdog, plan §4.2).
    if (!newId.isEmpty() && m_pendingWindowMoves.value(instanceId) == newId) {
        m_pendingWindowMoves.remove(instanceId);
        m_windowMoveSequences.remove(instanceId);
    } else if (m_pendingWindowMoves.contains(instanceId)
               && (newMeta.isSticky.value_or(false) || newMeta.virtualDesktops.size() > 1)) {
        // Sticky (or on several desktops) is the OTHER way a move can be
        // satisfied: the window is on the target workspace now, censusDesktop
        // answers 0 for it and the arrival branch above can never match. The
        // watch would otherwise sit until its timeout and warn about an
        // arrival that has, in the only sense the verb asked for, happened.
        m_pendingWindowMoves.remove(instanceId);
        m_windowMoveSequences.remove(instanceId);
    }
    if (oldId == newId) {
        if (!oldId.isEmpty()) {
            m_windowCensusDesktopId.insert(instanceId, oldId);
            // The desktop did not change, but the window's OUTPUT may have
            // (an external drag to another monitor): re-check ownership.
            reuniteWindowWithOwner(instanceId, oldId);
        }
        return;
    }
    adjustPopulationById(oldId, -1);
    if (!newId.isEmpty()) {
        m_windowCensusDesktopId.insert(instanceId, newId);
        adjustPopulationById(newId, +1);
        reuniteWindowWithOwner(instanceId, newId);
    }
}

PhosphorWorkspaces::WorkspaceReconciler& WorkspaceController::reconciler()
{
    return m_reconciler;
}

bool WorkspaceController::isAdopted() const
{
    return m_adopted;
}

QString WorkspaceController::currentMapJson() const
{
    // Only screens that actually REPORTED a current desktop contribute one.
    // currentDesktopForScreen falls back to the GLOBAL current for an unknown
    // screen, and publishing that asserts a current workspace the screen is
    // not showing — the same reason the adoption gate asks for report presence
    // explicitly rather than trusting the fallback.
    QHash<QString, int> currentByScreen;
    const QStringList order = m_reconciler.map().screenOrder();
    for (const QString& screenId : order) {
        if (!m_vdm->hasScreenDesktopReport(screenId)) {
            continue;
        }
        currentByScreen.insert(screenId, m_vdm->currentDesktopForScreen(screenId));
    }
    return m_reconciler.map().toJson(
        m_reconciler.generation(), currentByScreen,
        [this](const QString& id) {
            return m_vdm->desktopIndexOf(id);
        },
        /*includeState=*/false);
}

void WorkspaceController::publishIfChanged()
{
    if (!m_adopted) {
        return;
    }
    const QString json = currentMapJson();
    if (json == m_lastPublishedJson) {
        return;
    }
    m_lastPublishedJson = json;
    Q_EMIT workspaceMapPublished(json);
}

} // namespace PlasmaZones
