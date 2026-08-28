// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <PhosphorEngine/IVirtualDesktopManager.h>
#include <phosphorworkspaces_export.h>
#include <QHash>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QTimer>

class QDBusInterface;
class QDBusMessage;
class QDBusServiceWatcher;

namespace PhosphorWorkspaces {

class PHOSPHORWORKSPACES_EXPORT VirtualDesktopManager : public QObject, public PhosphorEngine::IVirtualDesktopManager
{
    Q_OBJECT

public:
    explicit VirtualDesktopManager(QObject* parent = nullptr);
    ~VirtualDesktopManager() override;

    /// Bind to KWin's D-Bus VirtualDesktopManager and seed the desktop cache.
    /// Returns true when the interface answered; false when KWin is absent.
    ///
    /// The return is ADVISORY and deliberately not [[nodiscard]]: a false is
    /// never a permanent verdict, because a service watcher binds as soon as
    /// KWin appears and start() picks it up from there. Callers that only want
    /// the manager running (the daemon's startup path) correctly ignore it;
    /// the value exists for a caller that wants to log or report whether the
    /// compositor was there at that instant.
    bool init();
    /// Subscribe to KWin's signals and refresh. Idempotent while running.
    void start();
    /// Drop KWin's signal subscriptions and any pending refresh retry. A
    /// stopped manager takes no further KWin events; start() re-subscribes.
    void stop();

    int currentDesktop() const override;
    int currentDesktopForScreen(const QString& screenId) const override;
    /// Whether a REAL per-output report exists for this screen (exact key, or
    /// the parent output of a virtual id). currentDesktopForScreen falls back
    /// to the global current when this is false — callers that must not act
    /// on the fallback (first-run adoption) gate on this instead.
    bool hasScreenDesktopReport(const QString& screenId) const;
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

    /// Ask KWin to create a desktop at the given 0-based global position (KWin's
    /// D-Bus signature; the caller computes the position, this wrapper only
    /// forwards). Fire-and-forget: the result arrives as a desktopCreated signal
    /// followed by a settled desktopListChanged.
    void createDesktop(uint position, const QString& name);
    /// Ask KWin to remove the desktop with the given UUID string. Result arrives
    /// as desktopRemoved + desktopListChanged.
    void removeDesktop(const QString& desktopId);
    /// Ask KWin to rename the desktop with the given UUID string.
    void setDesktopName(const QString& desktopId, const QString& name);

    /// Ordered KWin desktop UUID strings (global order, refreshed from the
    /// `desktops` property). Empty until the first refresh or without KWin.
    QStringList desktopIds() const;
    /// UUID at 1-based global index, or empty when out of range.
    QString desktopIdAt(int desktop) const;
    /// 1-based global index of the UUID, or 0 when unknown.
    int desktopIndexOf(const QString& desktopId) const;

    int desktopCount() const;
    /// Number of rows in KWin's virtual-desktop grid (>= 1). With the count,
    /// this gives the grid shape that cross-desktop directional navigation
    /// walks. Defaults to 1 until the first KWin refresh. Re-read only on
    /// KWin's rowsChanged (and at bind time) — a grid reshape is the only
    /// thing that moves it, and it is off the desktop-event hot path.
    int desktopRows() const;
    /// KWin's names EXACTLY as KWin reports them, aligned 1:1 with
    /// desktopIds(). An entry is empty when that desktop carries no KWin name.
    /// This is the identity-comparison form: named-workspace claiming must use
    /// it, because a placeholder would let a workspace literally named
    /// "Desktop 3" claim an unnamed desktop.
    QStringList rawDesktopNames() const;
    /// Display form of rawDesktopNames(): unnamed desktops are filled with a
    /// positional placeholder. The placeholder is NOT localized — this library
    /// is LGPL and deliberately links no i18n. Callers rendering these to a
    /// user should read rawDesktopNames() and substitute their own translated
    /// fallback for the empty entries.
    QStringList desktopNames() const;

Q_SIGNALS:
    void currentDesktopChanged(int desktop);
    /// The desktop count changed — and, once per failed episode, a re-announce
    /// of the UNCHANGED count when a desktop-list refresh exhausts its retries,
    /// so consumers re-diff instead of sitting on state the lost refresh was
    /// meant to correct. Handlers that do more than re-diff (the daemon also
    /// cancels drag-insert previews here) run on that re-announce too; see
    /// applyDesktopListReply for why that is accepted.
    void desktopCountChanged(int count);
    /// A single screen's current virtual desktop changed (per-output virtual
    /// desktops). The primary trigger the daemon's per-screen desktop handler
    /// subscribes to; in single-desktop mode it is driven the same for every
    /// screen so downstream has one code path.
    void screenDesktopChanged(const QString& screenId, int desktop);
    /// A KWin desktopCreated arrived carrying this UUID. Emitted BEFORE the
    /// async list refresh settles — id-only, positions still stale. The
    /// reconciler matches its ledger on this, then acts on desktopListChanged.
    void kwinDesktopCreated(const QString& desktopId);
    /// A KWin desktopRemoved arrived carrying this UUID (same timing contract).
    void kwinDesktopRemoved(const QString& desktopId);
    /// The ordered global id list settled after a refresh (create/remove/rename
    /// /reorder). Emitted only when the ordered id list actually changed.
    void desktopListChanged(const QStringList& desktopIds);

private Q_SLOTS:
    void onNumberOfDesktopsChanged(int count);
    void refreshFromKWin();
    void onKWinCurrentChanged(const QString& desktopId);
    void onKWinDesktopCreated(const QString& desktopId);
    void onKWinDesktopRemoved(const QString& desktopId);
    void onKWinDesktopRowsChanged();

private:
    /// Bind (or re-bind after a KWin restart) the D-Bus proxy and re-read the
    /// grid rows. Binding ONLY: it neither subscribes nor refreshes the list,
    /// so it is safe on a stopped manager. init() seeds, start() subscribes.
    void initKWinDBus();
    /// Connect (or disconnect) KWin's VirtualDesktopManager signals.
    void subscribeKWinSignals(bool subscribe);
    void applyDesktopListReply(const QDBusMessage& reply);
    /// Re-read KWin's grid row count (blocking, but only on rowsChanged / bind).
    void refreshRowsFromKWin();
    /// The single writer of m_desktopCount: change-gated, and it clamps the
    /// global current desktop plus every per-screen entry BEFORE announcing the
    /// new count (the daemon's desktopCountChanged handler documents that
    /// ordering — its per-screen arm must have re-diffed by the time it runs).
    void setDesktopCount(int count);
    /// Clamp any per-screen desktop entry above the live desktop count back down
    /// to the count (KWin renumbers on removal; the effect re-reports the true
    /// value shortly after, this just keeps the map valid in the interim).
    void clampScreenDesktopsToCount();
    /// Re-resolve m_currentDesktop from m_currentDesktopId against the settled
    /// id list, emitting currentDesktopChanged when the INDEX moved. A renumber
    /// shifts the index while the id stays put, and KWin sends no currentChanged
    /// for that, so this is the only place the shift is observed.
    void resolveCurrentFromId();

    QDBusInterface* m_kwinVDInterface = nullptr;
    /// Watches for org.kde.KWin appearing so a daemon that started before the
    /// compositor (or outlived a KWin restart) binds instead of latching off.
    QDBusServiceWatcher* m_kwinWatcher = nullptr;
    bool m_running = false;
    bool m_subscribed = false;
    bool m_useKWinDBus = false;
    int m_currentDesktop = 1;
    /// The current desktop's KWin UUID — the identity m_currentDesktop is an
    /// index into. Kept so a settled list can re-resolve the index after a
    /// renumber without a blocking property read.
    QString m_currentDesktopId;
    /// Per-screen current virtual desktop (screenId → 1-based), fed by the
    /// effect's per-output report. Empty until the first report arrives, and
    /// currentDesktopForScreen() falls back to the global m_currentDesktop for
    /// screens with no entry (which is every screen in single-desktop mode).
    QHash<QString, int> m_screenDesktops;
    int m_desktopCount = 1;
    int m_desktopRows = 1;
    /// KWin's names, raw and aligned with m_desktopIds; entries may be empty.
    QStringList m_desktopNames;
    QStringList m_desktopIds;
    uint m_refreshGeneration = 0;
    /// Re-ask after a refresh whose reply carried no desktops while we knew of
    /// some — a failed refresh, never a compositor with zero desktops.
    QTimer m_refreshRetryTimer;
    int m_refreshRetries = 0;
    static constexpr int MaxRefreshRetries = 5;
    static constexpr int RefreshRetryMs = 400;
};

} // namespace PhosphorWorkspaces
