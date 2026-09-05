// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QObject>
#include <QString>
#include <QVariantList>
#include <QVariantMap>

class QDBusServiceWatcher;

namespace PlasmaZones::Overview {

/// The overview plugin's link to the PlasmaZones daemon. Distinct from the
/// main effect's PhosphorCompositor::DaemonClient: this plugin shares no
/// code with the main effect and subscribes to exactly two streams, the
/// dynamic-workspaces map (WindowTracking.workspaceMapChanged) and the
/// overview model (Overview.overviewModelChanged), plus the daemon's
/// toggle/close requests.
///
/// Ordering guards copy the main effect's: every payload carries a
/// generation the receiver compares against its cache, and both caches are
/// scoped by a service epoch that bumps on every daemon (re)registration, so
/// a restarted daemon's counters restarting from zero are accepted. The
/// model additionally names the map generation it was built against; a
/// mismatch re-requests the map and then the model, bounded to three
/// retries, after which the overview is asked to close rather than render
/// an inconsistent pair.
class DaemonClient : public QObject
{
    Q_OBJECT

public:
    explicit DaemonClient(QObject* parent = nullptr);

    bool isAvailable() const
    {
        return m_available;
    }
    /// The parsed workspace map, or empty when the feature is off / unknown.
    const QVariantMap& workspaceMap() const
    {
        return m_map;
    }
    quint64 workspaceMapGeneration() const
    {
        return m_mapGeneration;
    }
    /// The parsed overview model, empty while closed.
    const QVariantMap& model() const
    {
        return m_model;
    }

    /// Open or close the daemon's streaming gate. Opening also requests the
    /// first model replay.
    void setOverviewOpen(bool open);
    /// Report whether the effect actually started.
    void reportOverviewState(bool open);
    /// Fire-and-forget a verb on org.plasmazones.Overview.
    void callOverview(const QString& method, const QVariantList& args);

Q_SIGNALS:
    void availableChanged(bool available);
    void workspaceMapChanged();
    void modelChanged();
    void toggleRequested();
    void closeRequested();

private:
    void onServiceRegistered();
    void onServiceUnregistered();
    void replayWorkspaceMap();
    void requestModel();
    void applyWorkspaceMap(const QString& json);
    void applyModel(const QString& json);

    // D-Bus slots (string signatures for QDBusConnection::connect).
    Q_SLOT void slotWorkspaceMapChanged(const QString& json);
    Q_SLOT void slotOverviewModelChanged(const QString& json);
    Q_SLOT void slotToggleRequested();
    Q_SLOT void slotCloseRequested();

    bool m_available = false;
    bool m_open = false;
    QDBusServiceWatcher* m_watcher = nullptr;
    quint64 m_epoch = 0;
    QVariantMap m_map;
    quint64 m_mapGeneration = 0;
    QVariantMap m_model;
    quint64 m_modelGeneration = 0;
    int m_consistencyRetries = 0;
};

} // namespace PlasmaZones::Overview
