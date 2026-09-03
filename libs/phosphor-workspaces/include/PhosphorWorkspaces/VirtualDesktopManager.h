// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

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

Q_SIGNALS:
    void currentDesktopChanged(int desktop);
    void desktopCountChanged(int count);
    /// The desktop LIST changed — a desktop was added, removed, reordered,
    /// or renamed. Distinct from desktopCountChanged, which a rename does
    /// not move: KWin delivers renames through `desktopDataChanged`, and
    /// without this signal a pager would keep showing the old label until
    /// something unrelated forced a refresh. Emitted only when the ids or
    /// names actually differ from the previous snapshot.
    void desktopsChanged();
    /// A single screen's current virtual desktop changed (per-output virtual
    /// desktops). The primary trigger the daemon's per-screen desktop handler
    /// subscribes to; in single-desktop mode it is driven the same for every
    /// screen so downstream has one code path.
    void screenDesktopChanged(const QString& screenId, int desktop);

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
};

} // namespace PhosphorWorkspaces
