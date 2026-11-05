// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorShell/Toplevels.h>

#include <QCoreApplication>
#include <QLoggingCategory>
#include <QPointer>
#include <QQmlEngine>
#include <QThread>

namespace {
Q_LOGGING_CATEGORY(lcToplevels, "phosphorshell.toplevels")
} // namespace

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
    // Release-build pair, matching sharedManager(). Off-thread construction
    // leaves an empty, inert model rather than racing the manager's
    // emissions and the seeding read below. Unreachable in practice — an
    // off-thread sharedManager() already returns null and the manager check
    // below would catch it — but the defensive-pair rule should not be
    // applied inconsistently within one file.
    if (!qApp || QThread::currentThread() != qApp->thread()) {
        qCCritical(lcToplevels) << "ToplevelListModel constructed off the GUI thread; leaving it empty";
        return;
    }

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

    // Relay the per-toplevel change signals as dataChanged. The model exposes
    // each toplevel through one role, so a consumer that caches anything READ
    // off that object (a title, an app id) has no other way to learn it
    // changed: without this the model only ever announces adds and removes,
    // and a window renaming itself was invisible until something else moved.
    // The row is looked up at emit time rather than captured, because rows
    // shift as windows come and go.
    const auto announce = [this, toplevel] {
        for (int i = 0; i < m_rows.size(); ++i) {
            if (m_rows.at(i).data() == toplevel) {
                const QModelIndex idx = index(i, 0);
                Q_EMIT dataChanged(idx, idx, {ToplevelRole});
                return;
            }
        }
    };
    connect(toplevel, &PhosphorWayland::ForeignToplevel::titleChanged, this, announce);
    connect(toplevel, &PhosphorWayland::ForeignToplevel::appIdChanged, this, announce);
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
    // Release-build pair for the assert above. Without it the contract
    // vanishes in release and a worker-thread caller races on the unguarded
    // function-static below — two ForeignToplevelManagers means two protocol
    // bindings to a global the wlroots protocol only allows one of. Every
    // caller already handles a null return.
    if (!qApp || QThread::currentThread() != qApp->thread()) {
        qCCritical(lcToplevels) << "sharedManager() called off the GUI thread; refusing to construct";
        return nullptr;
    }

    // Process-wide singleton. Parent to qApp so the manager is destroyed
    // exactly once, at QCoreApplication teardown — never during a hot-
    // reload's engine swap. Two managers cannot coexist because the
    // wlroots protocol's `add_listener` call is one-time per proxy and
    // double-binding produces duplicate event streams.
    // QPointer, not a raw pointer: the manager is parented to qApp, but
    // this static outlives it. After ~QCoreApplication a raw pointer
    // would be non-null and dangling, and `!instance` could never
    // detect it — so a second QCoreApplication in the same process (any
    // test binary, an embedder) would hand out freed memory. QPointer
    // auto-clears, restoring the intended construct-on-first-use.
    static QPointer<ForeignToplevelManager> instance;
    if (instance.isNull() && qApp) {
        instance = new ForeignToplevelManager(qApp);
    }
    return instance.data();
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

        // Active-toplevel tracking. Subscribe first, then seed from the
        // toplevels the shared manager already knows about: this wrapper
        // is per-engine and the manager is process-wide, so on a hot
        // reload every existing window arrived long before this
        // constructor and would otherwise never be watched.
        connect(mgr, &ForeignToplevelManager::toplevelAdded, this, &Toplevels::watchToplevel);
        connect(mgr, &ForeignToplevelManager::toplevelRemoved, this, &Toplevels::recomputeActive);
        // Subscribe each, then scan ONCE. watchToplevel's own trailing
        // scan is what the per-toplevel path needs, but running it per
        // element while seeding would make startup O(n^2) for a result only
        // the final scan can determine.
        const auto existing = mgr->toplevels();
        for (auto* tl : existing) {
            subscribeToplevel(tl);
        }
        recomputeActive();
    }
}

void Toplevels::subscribeToplevel(ForeignToplevel* toplevel)
{
    if (!toplevel) {
        return;
    }
    // `activated`, `closed` and the rest share one stateChanged signal, so
    // this fires more often than activation strictly moves; recomputeActive
    // is change-gated, which is what keeps that from reaching QML.
    connect(toplevel, &ForeignToplevel::stateChanged, this, &Toplevels::recomputeActive);
    // A window can be closed while still flagged activated — the compositor
    // has no obligation to clear the flag on its way out — so the close
    // must be a scan trigger in its own right.
    connect(toplevel, &ForeignToplevel::closedChanged, this, &Toplevels::recomputeActive);
}

void Toplevels::watchToplevel(ForeignToplevel* toplevel)
{
    subscribeToplevel(toplevel);
    // A newly-arrived toplevel can already be the activated one, so the
    // scan is not optional on this path.
    recomputeActive();
}

void Toplevels::recomputeActive()
{
    ForeignToplevel* found = nullptr;
    auto* mgr = sharedManager();
    if (mgr) {
        const auto all = mgr->toplevels();
        for (auto* tl : all) {
            // Skip closed windows: during a close the compositor may leave
            // `activated` set on a toplevel that is on its way out, and
            // publishing it would leave a consumer showing a dead window's
            // title until some unrelated event re-scanned.
            if (tl && tl->isActivated() && !tl->isClosed()) {
                found = tl;
                break;
            }
        }
    }
    // First-wins on the (transient) case where two toplevels both report
    // activated mid-focus-change. The protocol sends the two state events
    // separately, so the overlap is real but resolves on the next event.
    // Compare against the last PUBLISHED pointer, not against m_active.
    // m_active is a QPointer and self-clears when its toplevel is destroyed
    // without a preceding toplevelRemoved (the manager's own destructor
    // deletes every toplevel that way). It would then read null, `found`
    // would be null too, and the gate would suppress the notification while
    // QML bindings still cache the old title.
    if (m_publishedActive == found) {
        return;
    }
    m_publishedActive = found;
    m_active = found;
    Q_EMIT activeToplevelChanged();
}

ForeignToplevel* Toplevels::activeToplevel() const
{
    return m_active.data();
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
