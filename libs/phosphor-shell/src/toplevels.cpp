// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorShell/Toplevels.h>

#include <QCoreApplication>
#include <QQmlEngine>
#include <QThread>

namespace PhosphorShell {

using PhosphorWayland::ForeignToplevel;
using PhosphorWayland::ForeignToplevelManager;

// =====================================================================
// ToplevelListModel
// =====================================================================

ToplevelListModel::ToplevelListModel(ForeignToplevelManager* manager, QObject* parent)
    : QAbstractListModel(parent)
    , m_manager(manager)
{
    // Wayland protocol bindings dispatch on the GUI thread via Qt's
    // QWaylandDisplay; constructing this model anywhere else races
    // against the manager's signal emissions and the seeding read of
    // toplevels(). Catch misuse hard rather than silently corrupt.
    Q_ASSERT_X(qApp && QThread::currentThread() == qApp->thread(), "ToplevelListModel",
               "must be constructed on the GUI thread");

    if (!m_manager) {
        return;
    }
    // Connect FIRST, then seed — if a toplevel were added between
    // construction and the seed read, the listener catches it. The
    // duplicate-rows defense in onToplevelAdded is unnecessary because
    // the manager's add signal is GUI-thread-synchronous: a `toplevel`
    // event in flight cannot run while we're inside this constructor.
    connect(m_manager.data(), &ForeignToplevelManager::toplevelAdded, this, &ToplevelListModel::onToplevelAdded);
    connect(m_manager.data(), &ForeignToplevelManager::toplevelRemoved, this, &ToplevelListModel::onToplevelRemoved);

    // Seed with the toplevels already known to the manager — the manager
    // is process-wide and may have been collecting toplevels before the
    // first per-engine model was constructed.
    const auto seed = m_manager->toplevels();
    m_rows.reserve(seed.size());
    for (auto* tl : seed) {
        m_rows.append(QPointer<ForeignToplevel>(tl));
    }
}

ToplevelListModel::~ToplevelListModel() = default;

int ToplevelListModel::rowCount(const QModelIndex& parent) const
{
    if (parent.isValid()) {
        return 0;
    }
    return m_rows.size();
}

QVariant ToplevelListModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_rows.size()) {
        return {};
    }
    if (role != ToplevelRole) {
        return {};
    }
    // QPointer auto-clears if the toplevel was destroyed between the
    // model emitting endRemoveRows and a delegate reading data() on
    // a stale row index — return nullptr rather than dangling.
    return QVariant::fromValue(m_rows.at(index.row()).data());
}

QHash<int, QByteArray> ToplevelListModel::roleNames() const
{
    return {
        {ToplevelRole, "toplevel"},
    };
}

void ToplevelListModel::onToplevelAdded(ForeignToplevel* toplevel)
{
    if (!toplevel) {
        return;
    }
    const int row = m_rows.size();
    beginInsertRows({}, row, row);
    m_rows.append(QPointer<ForeignToplevel>(toplevel));
    endInsertRows();
}

void ToplevelListModel::onToplevelRemoved(ForeignToplevel* toplevel)
{
    if (!toplevel) {
        return;
    }
    // Linear scan — toplevel counts are bounded by what fits on screen
    // (typically <50 even on heavy desktops), so O(n) per remove is fine.
    for (int row = 0; row < m_rows.size(); ++row) {
        if (m_rows.at(row).data() == toplevel) {
            beginRemoveRows({}, row, row);
            m_rows.removeAt(row);
            endRemoveRows();
            return;
        }
    }
}

// =====================================================================
// Toplevels (singleton facade)
// =====================================================================

ForeignToplevelManager* Toplevels::sharedManager()
{
    // GUI-thread-only contract: ForeignToplevelManager binds Wayland
    // protocol globals through QPA, which lives on the main thread.
    // The function-static singleton is not protected by a mutex, so a
    // worker-thread caller would race on the unguarded check. Assert
    // rather than try to support multi-thread access — the underlying
    // protocol bindings would also misbehave.
    Q_ASSERT_X(qApp && QThread::currentThread() == qApp->thread(), "Toplevels::sharedManager",
               "must be called from the GUI thread");

    // Process-wide singleton. Parent to qApp so the manager is destroyed
    // exactly once, at QCoreApplication teardown — never during a hot-
    // reload's engine swap. Two managers cannot coexist because the
    // wlroots protocol's `add_listener` call is one-time per proxy and
    // double-binding produces duplicate event streams.
    static ForeignToplevelManager* instance = nullptr;
    if (!instance && qApp) {
        instance = new ForeignToplevelManager(qApp);
    }
    return instance;
}

Toplevels::Toplevels(QObject* parent)
    : QObject(parent)
{
    auto* mgr = sharedManager();
    // Always create the model — even when mgr is null (offscreen tests,
    // unsupported compositor). An empty model is friendlier to QML
    // bindings than a null pointer.
    m_model = new ToplevelListModel(mgr, this);
    if (mgr) {
        connect(mgr, &ForeignToplevelManager::toplevelAdded, this, &Toplevels::toplevelsChanged);
        connect(mgr, &ForeignToplevelManager::toplevelRemoved, this, &Toplevels::toplevelsChanged);
    }
}

Toplevels::~Toplevels() = default;

Toplevels* Toplevels::create(QQmlEngine* engine, QJSEngine* scriptEngine)
{
    Q_UNUSED(engine)
    Q_UNUSED(scriptEngine)
    // Per-engine wrapper around the shared manager. The QML registry
    // deletes the wrapper on engine teardown — the underlying manager
    // (parented to qApp) survives, so a hot-reload that recreates the
    // engine does NOT rebind the protocol global.
    return new Toplevels();
}

QList<ForeignToplevel*> Toplevels::toplevels() const
{
    auto* mgr = sharedManager();
    return mgr ? mgr->toplevels() : QList<ForeignToplevel*>{};
}

QAbstractListModel* Toplevels::model() const
{
    return m_model;
}

bool Toplevels::isSupported() const
{
    return ForeignToplevelManager::isSupported();
}

} // namespace PhosphorShell
