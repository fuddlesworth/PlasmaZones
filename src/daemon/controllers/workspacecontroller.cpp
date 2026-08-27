// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "workspacecontroller.h"

#include <PhosphorEngine/WindowRegistry.h>
#include <PhosphorScreens/Manager.h>
#include <PhosphorWorkspaces/VirtualDesktopManager.h>

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
/// Adoption grace: how long to wait for every screen's per-output desktop
/// report before adopting with the global-current fallback.
constexpr int AdoptionTimeoutMs = 3000;
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
                // Owner-wins: return the screen to its own slice (single
                // correction per event; the reconciler's ledger breaks
                // re-assertion loops). The OSD hint rides the signal.
                if (m_reconciler.snapBack(screenId)) {
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
    // Deferred verbs resume once the structural churn settles.
    connect(&m_reconciler, &PhosphorWorkspaces::WorkspaceReconciler::mapChanged, this,
            &WorkspaceController::drainQuietQueue);
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
    connect(m_vdm, &VDM::desktopListChanged, this, [this](const QStringList& ids) {
        if (!m_adopted) {
            tryFirstAdoption();
            return;
        }
        m_reconciler.onDesktopListSettled(ids);
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
        m_reconciler.onScreenAdded(screen.name);
    });
    connect(m_screens, &SM::screenRemoved, this, [this](const PhosphorScreens::PhysicalScreen& screen) {
        m_reconciler.onScreenRemoved(screen.name);
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
        order.append(screen.name);
    }
    m_reconciler.onScreenOrderChanged(order);
}

QString WorkspaceController::stateFilePath()
{
    // Runtime state, deliberately NOT config.json and not GenericDataLocation
    // (that tree is user-visible assets). Reconstructible by definition, so a
    // version mismatch or parse failure just falls back to fresh adoption.
    return QStandardPaths::writableLocation(QStandardPaths::StateLocation)
        + QStringLiteral("/plasmazones/workspaces.json");
}

void WorkspaceController::loadStateFile()
{
    QFile file(stateFilePath());
    if (!file.open(QIODevice::ReadOnly)) {
        return;
    }
    const QString json = QString::fromUtf8(file.readAll());
    if (!m_reconciler.map().fromJson(json)) {
        qCWarning(lcWorkspaceCtl) << "workspace state file unreadable or wrong version; re-adopting fresh";
        return;
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
        liveScreens.append(screen.name);
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
    // Gate on every known screen having a per-output report (plan §4.3); the
    // start() timeout falls back to the global current.
    QHash<QString, QString> currentById;
    for (const QString& screenId : order) {
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
    if (oldId == newId) {
        if (!oldId.isEmpty()) {
            m_windowCensusDesktopId.insert(instanceId, oldId);
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
