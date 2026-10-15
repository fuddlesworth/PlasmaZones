// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <PhosphorEngine/DesktopRemap.h>
#include <PhosphorEngine/IVirtualDesktopManager.h>
#include <phosphorworkspaces_export.h>
#include <QHash>
#include <QObject>
#include <QString>
#include <QStringList>

QT_BEGIN_NAMESPACE
class QDBusArgument;
class QDBusInterface;
class QDBusMessage;
QT_END_NAMESPACE

namespace PhosphorWorkspaces {

class PHOSPHORWORKSPACES_EXPORT VirtualDesktopManager : public QObject, public PhosphorEngine::IVirtualDesktopManager
{
    Q_OBJECT

public:
    explicit VirtualDesktopManager(QObject* parent = nullptr);
    ~VirtualDesktopManager() override;

    bool init();
    void start();
    void stop();

    int currentDesktop() const override;
    int currentDesktopForScreen(const QString& screenId) const override;
    bool perScreenModeActive() const override;

    /// Record a screen's current virtual desktop (1-based). This is fed by the
    /// KWin effect's per-output desktopChanged report (Plasma 6.7 per-output
    /// virtual desktops) — KWin's own D-Bus VirtualDesktopManager interface only
    /// exposes the GLOBAL current desktop, so per-screen data arrives this way.
    /// Emits screenDesktopChanged only when the value actually changes.
    void updateScreenDesktop(const QString& screenId, int desktop);

    /// Drop a screen's recorded per-output desktop when the output is removed,
    /// so the map doesn't retain stale entries (and perScreenModeActive() doesn't
    /// keep counting a gone screen) across monitor hot-plug. Driven by the
    /// daemon's screenRemoved handler.
    void removeScreenDesktop(const QString& screenId);

    void setCurrentDesktop(int desktop);
    /// Switch by KWin's desktop UUID rather than by position. Positions
    /// renumber whenever a desktop is created or removed, so anything that
    /// holds a desktop across such an event (a pager's pills, a rule) must
    /// key on the id. Unknown ids are ignored.
    void setCurrentDesktopById(const QString& desktopId);
    int desktopCount() const;
    /// Number of rows in KWin's virtual-desktop grid (>= 1). With the count,
    /// this gives the grid shape that cross-desktop directional navigation
    /// walks. Defaults to 1 until the first KWin refresh.
    int desktopRows() const;
    QStringList desktopNames() const;
    /// KWin's desktop UUIDs, in position order and index-aligned with
    /// desktopNames(). The stable identity of a desktop: names are
    /// user-editable and positions renumber, ids do neither.
    QStringList desktopIds() const;
    /// True once KWin's VirtualDesktopManager interface answered on the
    /// session bus. False on a non-KWin compositor or before init(), where
    /// every getter degrades to a single synthetic desktop — a UI should
    /// hide itself rather than show a pager with one permanent pill.
    /// init() returns true unconditionally and so cannot be used for this.
    bool isAvailable() const;

    /// Record the desktop an output shows as it is connected or comes back.
    /// KWin picks it without a desktop change, so it is a seed, not a switch:
    /// it emits screenDesktopSeeded, never screenDesktopChanged, and always,
    /// because the engines dropped the output's desktop when it went away.
    void seedScreenDesktop(const QString& screenId, int desktop);

    /// The desktop ids a previous session saw, in position order, so a change
    /// KWin made while nothing was listening (desktops added, removed or moved
    /// while the daemon was down) still renumbers the stores keyed on the old
    /// positions. Compared against the first committed snapshot, or against
    /// the current one at once when it has already landed; a difference goes
    /// out as desktopsRenumbered and nothing else (the per-screen desktops and
    /// desktopRemovedAt are about live changes only). An empty list is ignored.
    void setBaselineDesktopIds(const QStringList& desktopIds);

Q_SIGNALS:
    void currentDesktopChanged(int desktop);
    /// A GROWING count goes out as KWin reports it, ahead of the snapshot that
    /// carries the new desktop (F1022), so desktopIds() and desktopNames() still
    /// hold the old list here. desktopsChanged follows once that snapshot
    /// commits, and is the signal a consumer of the names listens to (F1101).
    void desktopCountChanged(int count);
    /// The desktop LIST changed — a desktop was added, removed, reordered,
    /// or renamed. Distinct from desktopCountChanged, which a rename does
    /// not move: KWin delivers renames through `desktopDataChanged`, and
    /// without this signal a pager would keep showing the old label until
    /// something unrelated forced a refresh. Emitted only when the ids or
    /// names actually differ from the previous snapshot.
    void desktopsChanged();
    /// Exactly one desktop was removed, and this is the 1-based POSITION it
    /// held before it went. Emitted ahead of desktopCountChanged, because a
    /// consumer keyed on positions has to act in that order: drop the removed
    /// position's state first, then shift everything above it down one.
    ///
    /// desktopCountChanged cannot carry this. It reports the new count, and
    /// after a mid-list removal every surviving position is still within it —
    /// so a consumer seeing only the count cannot tell 2-of-4 was deleted from
    /// 4-of-4, and pruning "everything past the new count" drops the TOPMOST
    /// desktop's state instead of the removed one's.
    ///
    /// Only for the unambiguous case: exactly one id gone and none added. A
    /// reorder, a simultaneous add and remove, or a multi-desktop change
    /// leaves this silent.
    ///
    /// Superseded by desktopsRenumbered, which goes out just before it and
    /// covers every shape of change. Kept for existing consumers: connect one
    /// of the two, never both, or a store is shifted twice.
    void desktopRemovedAt(int removedPosition);
    /// A single screen's current virtual desktop changed (per-output virtual
    /// desktops). The primary trigger the daemon's per-screen desktop handler
    /// subscribes to; in single-desktop mode it is driven the same for every
    /// screen so downstream has one code path.
    void screenDesktopChanged(const QString& screenId, int desktop);
    /// A screen's desktop was recorded as the output (re)connected
    /// (seedScreenDesktop). Not a switch.
    void screenDesktopSeeded(const QString& screenId, int desktop);
    /// The desktop positions moved: a removal, an insert, a reorder, or any
    /// mix of them, as one old-to-new map (F787). Every store keyed on a
    /// desktop NUMBER applies @p remap once. By the time it goes out the
    /// manager's own state is renumbered: the ids, the count, the current
    /// desktop, and each screen's desktop (moved through
    /// DesktopRemap::successor, silently, for the reason desktopRemovedAt's
    /// shift is silent). Ahead of desktopCountChanged, like desktopRemovedAt,
    /// except for a GROWING count, which goes out as KWin reports it so a
    /// desktop the compositor already uses is in range: an insert before the
    /// last desktop sends the count first and this after. Never sent for a
    /// change that moves no number (a rename, a desktop appended after the
    /// last one).
    void desktopsRenumbered(const PhosphorEngine::DesktopRemap& remap);
    /// KWin reported a change to its desktop list, and the snapshot that says
    /// what changed is on its way. Sent once per change, on the first of the
    /// several signals KWin sends for it, and again only after a snapshot has
    /// committed (F1023). Not sent before the first snapshot, whose map
    /// compares against a previous session. A change that moves no number is
    /// never followed by desktopsRenumbered. A store whose entries the
    /// compositor re-reports in the new numbering on its own (the window
    /// registry) marks here which entries predate the change.
    void desktopListChanging();

private Q_SLOTS:
    /// KWin's countChanged carries `u`, not `i`. A slot declared `int`
    /// registers successfully and is then NEVER invoked, because the hook
    /// signature must match the message signature exactly.
    void onNumberOfDesktopsChanged(uint count);
    void refreshFromKWin();
    void onKWinCurrentChanged(const QString& desktopId);
    void onKWinDesktopCreated();
    void onKWinDesktopRemoved();
    void onKWinDesktopRowsChanged();
    /// KWin's per-desktop metadata changed (a rename, or a position move).
    /// The count is unchanged, so only a list refresh can pick it up.
    void onKWinDesktopDataChanged();

private:
    void initKWinDBus();
    /// Install the six session-bus signal subscriptions. Idempotent via
    /// m_kwinSubscribed, so a stop()/start() cycle re-subscribes without
    /// ever doubling the hooks.
    void subscribeToKWin();
    void applyDesktopListReply(const QDBusMessage& reply, const QString& currentId, int rows);
    /// Parse the a(uss) desktop array and commit the snapshot; the shared
    /// core behind both the blocking Get reply and the async GetAll path.
    void applyDesktopListArg(const QDBusArgument& arg, const QString& currentId, int rows);
    /// Clamp any per-screen desktop entry above the live desktop count back down
    /// to the count (KWin renumbers on removal; the effect re-reports the true
    /// value shortly after, this just keeps the map valid in the interim).
    void clampScreenDesktopsToCount();
    /// Emit desktopListChanging unless this change already did, then refresh.
    void announceDesktopListChange();

    QDBusInterface* m_kwinVDInterface = nullptr;
    bool m_running = false;
    /// True while the six session-bus subscriptions are installed; guards
    /// subscribeToKWin against doubling them.
    bool m_kwinSubscribed = false;
    bool m_useKWinDBus = false;
    int m_currentDesktop = 1;
    /// Per-screen current virtual desktop (screenId → 1-based), populated only
    /// in per-output mode via updateScreenDesktop. Empty otherwise, so every
    /// currentDesktopForScreen() falls back to the global m_currentDesktop.
    QHash<QString, int> m_screenDesktops;
    int m_desktopCount = 1;
    int m_desktopRows = 1;
    QStringList m_desktopNames;
    QStringList m_desktopIds;
    uint m_refreshGeneration = 0;
    /// setBaselineDesktopIds' list, held until the first snapshot commits.
    QStringList m_baselineDesktopIds;
    /// desktopListChanging went out and no snapshot has committed since
    /// (announceDesktopListChange). Appended last: installed class.
    bool m_desktopListChangePending = false;
};

} // namespace PhosphorWorkspaces
