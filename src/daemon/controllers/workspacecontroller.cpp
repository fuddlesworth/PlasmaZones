// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "workspacecontroller.h"

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

Q_LOGGING_CATEGORY(lcWorkspaceCtl, "plasmazones.workspaces.controller", QtWarningMsg)

namespace PlasmaZones {

namespace {
/// The map, the census, and the reconciler all key screens by the id the
/// KWin effect REPORTS (the EDID-style "Vendor:Model:Serial" form of
/// outputScreenId). ScreenManager hands out CONNECTOR names ("DP-2"), so
/// every ScreenManager-sourced id must pass through here — mixing the two
/// spaces made every owner check fail, which turned every ordinary desktop
/// switch into a "foreign" one and looped snap-back against the user (the
/// desktop churn that crashed plasmashell's Pager).
QString canonicalScreenId(const QString& connectorOrId)
{
    const QString id = PhosphorScreens::ScreenIdentity::idForName(connectorOrId);
    return id.isEmpty() ? connectorOrId : id;
}

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
                // Reap by identity first, then shift the survivors — the
                // contract every engine arm implements.
                for (int desktop : removed) {
                    Q_EMIT desktopReapRequested(desktop);
                }
                if (!oldToNew.isEmpty()) {
                    Q_EMIT desktopRenumberRequested(oldToNew);
                }
            });
    connect(&m_reconciler, &PhosphorWorkspaces::WorkspaceReconciler::resyncRequested, this, [this]() {
        // A ledger entry expired: re-pull the authoritative desktop list.
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
                }
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
    // Read as plain INI: the key is a flat bool in [Windows], none of the
    // KConfig dialect (cascading, $e expansion) applies to it, and reading it
    // this way spares the Qt-only build a KConfig dependency. The value
    // applies live on KWin reconfigure() (verified against KWin 6.7 source),
    // so this read at controller-gate time reflects the effective mode.
    const QString path =
        QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) + QStringLiteral("/kwinrc");
    const QSettings kwinrc(path, QSettings::IniFormat);
    return kwinrc.value(QStringLiteral("Windows/PerOutputVirtualDesktops"), false).toBool();
}

void WorkspaceController::wireVirtualDesktops()
{
    using VDM = PhosphorWorkspaces::VirtualDesktopManager;
    connect(m_vdm, &VDM::kwinDesktopCreated, &m_reconciler,
            &PhosphorWorkspaces::WorkspaceReconciler::onKwinDesktopCreated);
    connect(m_vdm, &VDM::kwinDesktopRemoved, &m_reconciler,
            &PhosphorWorkspaces::WorkspaceReconciler::onKwinDesktopRemoved);
    // Removal-race consumption BEFORE the reconciler processes the removal:
    // the snapshot taken at removalRaceDetected re-routes each swept window
    // to the owner's current workspace (plan §4.3 step 4).
    connect(m_vdm, &VDM::kwinDesktopRemoved, this, [this](const QString& desktopId) {
        const auto it = m_displacedByRemoval.constFind(desktopId);
        if (it == m_displacedByRemoval.constEnd()) {
            return;
        }
        const DisplacedByRemoval record = it.value();
        m_displacedByRemoval.erase(it);
        bool any = false;
        for (const QString& windowId : record.windowIds) {
            const QString ownerScreen = record.ownerScreenId;
            runWhenQuiet([this, windowId, ownerScreen]() {
                const QString target = m_reconciler.currentDesktopIdOf(ownerScreen);
                const int desktop = target.isEmpty() ? 0 : m_vdm->desktopIndexOf(target);
                if (desktop <= 0) {
                    return;
                }
                watchWindowMove(windowId, target);
                Q_EMIT windowWorkspaceMoveRequested(windowId, ownerScreen, desktop, QStringLiteral("down"));
            });
            any = true;
        }
        if (any) {
            Q_EMIT windowDisplacedByRemoval(record.ownerScreenId);
        }
    });
    connect(m_vdm, &VDM::desktopListChanged, this, [this](const QStringList& ids) {
        if (!m_adopted) {
            tryFirstAdoption();
            return;
        }
        m_reconciler.onDesktopListSettled(ids);
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
        refreshScreenOrder();
        m_reconciler.onScreenAdded(canonicalScreenId(screen.name));
    });
    connect(m_screens, &SM::screenRemoved, this, [this](const PhosphorScreens::PhysicalScreen& screen) {
        m_reconciler.onScreenRemoved(canonicalScreenId(screen.name));
        refreshScreenOrder();
    });
    connect(m_screens, &SM::screenGeometryChanged, this, [this](const PhosphorScreens::PhysicalScreen&) {
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
    QHash<QString, int> currentByScreen;
    const QStringList order = m_reconciler.map().screenOrder();
    for (const QString& screenId : order) {
        currentByScreen.insert(screenId, m_vdm->currentDesktopForScreen(screenId));
    }
    const QString json = m_reconciler.map().toJson(
        m_reconciler.generation(), currentByScreen,
        [this](const QString& id) {
            return m_vdm->desktopIndexOf(id);
        },
        /*includeState=*/true);
    file.write(json.toUtf8());
    file.commit();
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

    m_stateSaveTimer.setSingleShot(true);
    m_stateSaveTimer.setInterval(StateSaveDebounceMs);
    connect(&m_stateSaveTimer, &QTimer::timeout, this, &WorkspaceController::saveStateFile);
    connect(&m_reconciler, &PhosphorWorkspaces::WorkspaceReconciler::mapChanged, this,
            &WorkspaceController::scheduleStateSave);

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
                const QStringList ids = m_vdm->desktopIds();
                const auto order = m_reconciler.map().screenOrder();
                for (const QString& screenId : order) {
                    const int current = m_vdm->currentDesktopForScreen(screenId);
                    currentById.insert(screenId, m_vdm->desktopIdAt(current));
                }
                m_reconciler.adoptAll(ids, currentById);
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
    if (!m_namedApplied && !m_namedEntries.isEmpty()) {
        applyNamedDeclarations(m_namedEntries);
    }
}

void WorkspaceController::setWindowScreenResolver(std::function<QString(const QString&)> resolver)
{
    m_windowScreenResolver = std::move(resolver);
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
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (now - m_lastReunionMs.value(instanceId, 0) < SnapBackCooldownMs) {
        return;
    }
    m_lastReunionMs.insert(instanceId, now);
    runWhenQuiet([this, instanceId, desktopId, owner]() {
        if (m_reconciler.map().ownerOf(desktopId) != owner) {
            return; // ownership moved while deferred
        }
        const int desktop = m_vdm->desktopIndexOf(desktopId);
        if (desktop <= 0) {
            return;
        }
        qCInfo(lcWorkspaceCtl) << "reuniting window" << instanceId << "with its workspace's owner screen" << owner;
        Q_EMIT windowWorkspaceMoveRequested(instanceId, owner, desktop, QStringLiteral("down"));
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

void WorkspaceController::adjustPopulation(int desktopInt, int delta)
{
    if (desktopInt <= 0) {
        return;
    }
    const QString id = m_vdm->desktopIdAt(desktopInt);
    if (id.isEmpty()) {
        return;
    }
    const int next = qMax(0, m_populationById.value(id, 0) + delta);
    m_populationById.insert(id, next);
    m_reconciler.onPopulationChanged(id, next);
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
    const QString id = m_windowCensusDesktopId.take(instanceId);
    if (id.isEmpty()) {
        return;
    }
    const int next = qMax(0, m_populationById.value(id, 0) - 1);
    m_populationById.insert(id, next);
    m_reconciler.onPopulationChanged(id, next);
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
    if (!oldId.isEmpty()) {
        const int next = qMax(0, m_populationById.value(oldId, 0) - 1);
        m_populationById.insert(oldId, next);
        m_reconciler.onPopulationChanged(oldId, next);
    }
    if (!newId.isEmpty()) {
        m_windowCensusDesktopId.insert(instanceId, newId);
        const int next = m_populationById.value(newId, 0) + 1;
        m_populationById.insert(newId, next);
        m_reconciler.onPopulationChanged(newId, next);
        reuniteWindowWithOwner(instanceId, newId);
    }
}

PhosphorWorkspaces::WorkspaceReconciler& WorkspaceController::reconciler()
{
    return m_reconciler;
}

QString WorkspaceController::currentMapJson() const
{
    QHash<QString, int> currentByScreen;
    const QStringList order = m_reconciler.map().screenOrder();
    for (const QString& screenId : order) {
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
