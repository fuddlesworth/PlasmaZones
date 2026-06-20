// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <PhosphorEngine/IVirtualDesktopManager.h>
#include <phosphorworkspaces_export.h>
#include <QHash>
#include <QObject>
#include <QString>
#include <QStringList>

class QDBusInterface;
class QDBusMessage;

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

    void setCurrentDesktop(int desktop);
    int desktopCount() const;
    /// Number of rows in KWin's virtual-desktop grid (>= 1). With the count,
    /// this gives the grid shape that cross-desktop directional navigation
    /// walks. Defaults to 1 until the first KWin refresh.
    int desktopRows() const;
    QStringList desktopNames() const;

Q_SIGNALS:
    void currentDesktopChanged(int desktop);
    void desktopCountChanged(int count);
    /// A single screen's current virtual desktop changed (per-output virtual
    /// desktops). The primary trigger the daemon's per-screen desktop handler
    /// subscribes to; in single-desktop mode it is driven the same for every
    /// screen so downstream has one code path.
    void screenDesktopChanged(const QString& screenId, int desktop);

private Q_SLOTS:
    void onNumberOfDesktopsChanged(int count);
    void refreshFromKWin();
    void onKWinCurrentChanged(const QString& desktopId);
    void onKWinDesktopCreated();
    void onKWinDesktopRemoved();
    void onKWinDesktopRowsChanged();

private:
    void initKWinDBus();
    void applyDesktopListReply(const QDBusMessage& reply, const QString& currentId);
    /// Clamp any per-screen desktop entry above the live desktop count back down
    /// to the count (KWin renumbers on removal; the effect re-reports the true
    /// value shortly after, this just keeps the map valid in the interim).
    void clampScreenDesktopsToCount();

    QDBusInterface* m_kwinVDInterface = nullptr;
    bool m_running = false;
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
