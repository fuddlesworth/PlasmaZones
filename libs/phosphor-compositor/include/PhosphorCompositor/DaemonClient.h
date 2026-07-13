// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <phosphorcompositor_export.h>
#include <PhosphorCompositor/IGeometryHandler.h>
#include <PhosphorCompositor/IDragHandler.h>
#include <PhosphorCompositor/ILifecycleHandler.h>
#include <PhosphorProtocol/WindowTypes.h>
#include <PhosphorProtocol/ZoneTypes.h>

#include <QObject>
#include <QRect>
#include <QString>
#include <QStringList>

class QDBusServiceWatcher;

namespace PhosphorCompositor {

class PHOSPHORCOMPOSITOR_EXPORT DaemonClient : public QObject
{
    Q_OBJECT

public:
    explicit DaemonClient(QObject* parent = nullptr);
    ~DaemonClient() override;

    bool isDaemonReady() const
    {
        return m_daemonReady;
    }

    /// @param handler Not owned; caller retains lifetime. Must outlive this client.
    void setDragHandler(IDragHandler* handler)
    {
        m_dragHandler = handler;
    }
    /// @param handler Not owned; caller retains lifetime. Must outlive this client.
    void setGeometryHandler(IGeometryHandler* handler)
    {
        m_geometryHandler = handler;
    }
    /// @param handler Not owned; caller retains lifetime. Must outlive this client.
    void setLifecycleHandler(ILifecycleHandler* handler)
    {
        m_lifecycleHandler = handler;
    }

    // Registration
    void registerBridge(const QString& compositorId, int apiVersion, const QStringList& capabilities);

    // Window lifecycle notifications (plugin → daemon)
    void notifyWindowOpened(const QString& windowId, const QString& screenId, int minWidth = 0, int minHeight = 0);
    void notifyWindowOpenedBatch(const PhosphorProtocol::WindowOpenedList& windows);
    void notifyWindowClosed(const QString& windowId);
    void notifyWindowActivated(const QString& windowId, const QString& screenId);

    // Drag operations (plugin → daemon)
    void dragStarted(const QString& windowId, const QString& screenId, const QRect& geometry);
    void dragMoved(const QString& windowId, int cursorX, int cursorY);
    void dragStopped(const QString& windowId, const QString& screenId, const QString& zoneId);

    // Screen notifications (plugin → daemon)
    void notifyCursorScreenChanged(const QString& screenId);
    void notifyPrimaryScreen(const QString& screenName);

    // Queries (plugin → daemon, async results via signals)
    void queryFloatingWindows();
    void querySnappedWindows();
    void queryPendingRestoreGeometries();
    void queryVirtualScreens(const QString& screenId);
    void pruneStaleWindows(const QStringList& liveWindowIds);

    // Daemon availability probe (async — emits daemonReady if responsive)
    void probeDaemonAvailable(int timeoutMs = 3000);

Q_SIGNALS:
    void daemonReady();
    void daemonDisconnected();
    void bridgeRegistered(const QString& sessionId, int peerApiVersion);
    void bridgeRejected(const QString& reason);

    void floatingWindowsReceived(const QStringList& windowIds);
    void snappedWindowsReceived(const QStringList& windowIds);
    void pendingRestoreGeometriesReceived(const QString& json);
    void virtualScreensReceived(const QString& screenId, const PhosphorProtocol::WindowGeometryList& geometries);

    void settingsChanged();
    void virtualScreensChanged(const QString& screenId);

    void snapAssistReady(const QString& windowId, const QString& screenId,
                         const PhosphorProtocol::EmptyZoneList& zones);
    void pendingRestoresAvailable();
    void reapplyGeometriesRequested();
    void runningWindowsRequested();

private Q_SLOTS:
    void onDaemonReadySignal();
    void onServiceRegistered();
    void onServiceUnregistered();
    void handleApplyGeometry(const QString& windowId, int x, int y, int w, int h, const QString& zoneId,
                             const QString& screenId, bool sizeOnly);
    void handleApplyGeometriesBatch(const PhosphorProtocol::WindowGeometryList& geometries, const QString& action);
    void handleRaiseWindows(const QStringList& windowIds);
    void handleActivateWindow(const QString& windowId);
    void handleDragPolicyChanged(const QString& windowId, int newPolicy);
    void handleWindowFloatingChanged(const QString& windowId, bool isFloating, const QString& screenId);
    void handleRestoreSizeDuringDrag(const QString& windowId, int width, int height);
    void handleMoveWindowToZone(const QString& windowId, const QString& screenId, int x, int y, int w, int h);
    void handleSnapAllWindows(const QString& screenId);
    void handleSnapAssistReady(const QString& windowId, const QString& screenId,
                               const PhosphorProtocol::EmptyZoneList& zones);

private:
    void connectDaemonSignals();
    void disconnectDaemonSignals();

    QDBusServiceWatcher* m_serviceWatcher = nullptr;
    IDragHandler* m_dragHandler = nullptr;
    IGeometryHandler* m_geometryHandler = nullptr;
    ILifecycleHandler* m_lifecycleHandler = nullptr;

    bool m_daemonReady = false;
    bool m_registrationInFlight = false;
    QString m_sessionId;
};

} // namespace PhosphorCompositor
