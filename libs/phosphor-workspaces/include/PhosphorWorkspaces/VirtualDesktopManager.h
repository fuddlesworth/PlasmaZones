// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <PhosphorEngine/IVirtualDesktopManager.h>
#include <phosphorworkspaces_export.h>
#include <QObject>
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

    QDBusInterface* m_kwinVDInterface = nullptr;
    bool m_running = false;
    bool m_useKWinDBus = false;
    int m_currentDesktop = 1;
    int m_desktopCount = 1;
    int m_desktopRows = 1;
    QStringList m_desktopNames;
    QStringList m_desktopIds;
    uint m_refreshGeneration = 0;
};

} // namespace PhosphorWorkspaces
