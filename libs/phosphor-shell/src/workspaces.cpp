// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorShell/Workspaces.h>

#include <PhosphorWorkspaces/VirtualDesktopManager.h>

#include <QCoreApplication>
#include <QLoggingCategory>
#include <QPointer>
#include <QQmlEngine>
#include <QThread>

namespace {
Q_LOGGING_CATEGORY(lcWorkspaces, "phosphorshell.workspaces")
} // namespace

namespace PhosphorShell {

using PhosphorWorkspaces::VirtualDesktopManager;

// =====================================================================
// WorkspaceListModel
// =====================================================================

WorkspaceListModel::WorkspaceListModel(VirtualDesktopManager* manager, QObject* parent)
    : QAbstractListModel(parent)
    , m_manager(manager)
{
    if (!m_manager) {
        return;
    }
    // Connect before seeding, so a change landing between the two is not
    // lost.
    connect(m_manager.data(), &VirtualDesktopManager::desktopsChanged, this, &WorkspaceListModel::reload);
    connect(m_manager.data(), &VirtualDesktopManager::desktopCountChanged, this, &WorkspaceListModel::reload);
    connect(m_manager.data(), &VirtualDesktopManager::currentDesktopChanged, this, &WorkspaceListModel::refreshActive);
    reload();
}

WorkspaceListModel::~WorkspaceListModel() = default;

int WorkspaceListModel::rowCount(const QModelIndex& parent) const
{
    if (parent.isValid()) {
        return 0;
    }
    return m_ids.size();
}

QVariant WorkspaceListModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_ids.size()) {
        return {};
    }
    switch (role) {
    case IdRole:
        return m_ids.at(index.row());
    case NameRole:
        // Names and ids are index-aligned by contract, but the manager
        // pads names to the desktop count independently of the id list, so
        // a transient mismatch is possible mid-refresh. Fall back to the
        // same synthesized label the manager uses for unnamed desktops
        // rather than rendering an empty pill (or indexing out of range).
        return index.row() < m_names.size() ? m_names.at(index.row())
                                            : QStringLiteral("Desktop %1").arg(index.row() + 1);
    case IsActiveRole:
        return index.row() == m_activeRow;
    default:
        return {};
    }
}

QHash<int, QByteArray> WorkspaceListModel::roleNames() const
{
    return {
        {IdRole, "workspaceId"},
        {NameRole, "name"},
        {IsActiveRole, "isActive"},
    };
}

void WorkspaceListModel::reload()
{
    if (!m_manager) {
        return;
    }
    const QStringList ids = m_manager->desktopIds();
    const QStringList names = m_manager->desktopNames();
    const int activeRow = m_manager->currentDesktop() - 1;

    if (ids == m_ids && names == m_names) {
        // Same list. The active workspace may still have moved (both
        // signals routed here can fire for a pure switch), and that is a
        // dataChanged, not a reset.
        refreshActive();
        return;
    }

    beginResetModel();
    m_ids = ids;
    m_names = names;
    m_activeRow = (activeRow >= 0 && activeRow < m_ids.size()) ? activeRow : -1;
    endResetModel();
}

void WorkspaceListModel::refreshActive()
{
    if (!m_manager) {
        return;
    }
    const int row = m_manager->currentDesktop() - 1;
    const int next = (row >= 0 && row < m_ids.size()) ? row : -1;
    if (next == m_activeRow) {
        return;
    }
    const int previous = m_activeRow;
    m_activeRow = next;

    // Two targeted dataChanged emissions rather than a reset: a pager
    // rebuilt on every workspace switch would drop hover state and restart
    // any transition mid-flight.
    if (previous >= 0 && previous < m_ids.size()) {
        Q_EMIT dataChanged(index(previous), index(previous), {IsActiveRole});
    }
    if (m_activeRow >= 0) {
        Q_EMIT dataChanged(index(m_activeRow), index(m_activeRow), {IsActiveRole});
    }
}

// =====================================================================
// Workspaces (singleton facade)
// =====================================================================

VirtualDesktopManager* Workspaces::sharedManager()
{
    // GUI-thread-only: the manager owns a QDBusInterface parented to
    // itself and subscribes session-bus signals. Asserting beats racing on
    // the unguarded function-static below.
    Q_ASSERT_X(qApp && QThread::currentThread() == qApp->thread(), "Workspaces::sharedManager",
               "must be called from the GUI thread");
    // Release-build pair for the assert above. Without it the contract
    // vanishes in release and a worker-thread caller races on the unguarded
    // function-static below, producing exactly the double construction the
    // singleton exists to prevent — two D-Bus subscriptions to the same
    // interface. Every caller already handles a null return.
    if (!qApp || QThread::currentThread() != qApp->thread()) {
        qCCritical(lcWorkspaces) << "sharedManager() called off the GUI thread; refusing to construct";
        return nullptr;
    }

    // QPointer, not a raw pointer: the manager is parented to qApp, but
    // this static outlives it. After ~QCoreApplication a raw pointer
    // would be non-null and dangling, and `!instance` could never
    // detect it — so a second QCoreApplication in the same process (any
    // test binary, an embedder) would hand out freed memory. QPointer
    // auto-clears, restoring the intended construct-on-first-use.
    static QPointer<VirtualDesktopManager> instance;
    if (instance.isNull() && qApp) {
        instance = new VirtualDesktopManager(qApp);
        // start() BEFORE init(), which is not the obvious order. `start()`
        // sets the running flag and then refreshes only `if (m_useKWinDBus)`
        // — still false at this point, since only init() sets it — so its own
        // refresh is a genuine no-op. The flag is what makes the desktop-list
        // fetch inside init() take the ASYNC path; init-then-start would run
        // that fetch as a blocking call on the GUI thread during startup.
        instance->start();
        instance->init();
    }
    return instance.data();
}

Workspaces::Workspaces(QObject* parent)
    : QObject(parent)
{
    auto* mgr = sharedManager();
    // Always build the model, even with no manager, so QML can bind
    // `Repeater { model: Workspaces.model }` unconditionally.
    m_model = new WorkspaceListModel(mgr, this);
    if (!mgr) {
        return;
    }

    m_activeId = activeId();
    m_activeIndex = activeIndex();
    m_count = mgr->desktopIds().size();

    const auto sync = [this, mgr] {
        // activeChanged covers BOTH activeId and activeIndex, so it must be
        // gated on either moving. A desktop added or removed before the
        // current one renumbers the position while the id stays put, which
        // would otherwise leave activeIndex stale with no notification.
        const QString nextActive = activeId();
        const int nextIndex = activeIndex();
        if (nextActive != m_activeId || nextIndex != m_activeIndex) {
            m_activeId = nextActive;
            m_activeIndex = nextIndex;
            Q_EMIT activeChanged();
        }
        const int nextCount = mgr->desktopIds().size();
        if (nextCount != m_count) {
            m_count = nextCount;
            Q_EMIT countChanged();
        }
    };
    connect(mgr, &VirtualDesktopManager::currentDesktopChanged, this, sync);
    connect(mgr, &VirtualDesktopManager::desktopCountChanged, this, sync);
    connect(mgr, &VirtualDesktopManager::desktopsChanged, this, sync);
}

Workspaces::~Workspaces() = default;

Workspaces* Workspaces::create(QQmlEngine* engine, QJSEngine* scriptEngine)
{
    Q_UNUSED(engine)
    Q_UNUSED(scriptEngine)
    // Per-engine wrapper over the process-wide manager, matching
    // Toplevels: the QML registry deletes the wrapper on engine teardown
    // while the manager (parented to qApp) survives the hot reload.
    return new Workspaces();
}

QAbstractListModel* Workspaces::model() const
{
    return m_model;
}

QString Workspaces::activeId() const
{
    auto* mgr = sharedManager();
    if (!mgr) {
        return {};
    }
    const QStringList ids = mgr->desktopIds();
    const int row = mgr->currentDesktop() - 1;
    if (row < 0 || row >= ids.size()) {
        return {};
    }
    return ids.at(row);
}

int Workspaces::activeIndex() const
{
    auto* mgr = sharedManager();
    // 0, not currentDesktop(), when there is no workspace source:
    // VirtualDesktopManager reports 1 in that state while count() reports 0,
    // and publishing "position 1 of 0" is a self-inconsistent readout.
    if (!mgr || !mgr->isAvailable() || mgr->desktopIds().isEmpty()) {
        return 0;
    }
    // Clamped to the id-list size, which is what count() reports: in the
    // window a count shrink opens (the manager commits its clamped
    // m_currentDesktop before the async list refresh lands), the raw
    // position can briefly exceed the still-old id list and a consumer
    // would read "position N of fewer-than-N".
    return qMin(mgr->currentDesktop(), static_cast<int>(mgr->desktopIds().size()));
}

int Workspaces::count() const
{
    auto* mgr = sharedManager();
    return mgr ? mgr->desktopIds().size() : 0;
}

bool Workspaces::isSupported() const
{
    auto* mgr = sharedManager();
    return mgr && mgr->isAvailable();
}

void Workspaces::switchTo(const QString& id)
{
    auto* mgr = sharedManager();
    if (!mgr) {
        return;
    }
    mgr->setCurrentDesktopById(id);
}

} // namespace PhosphorShell
