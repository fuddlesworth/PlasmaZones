// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "daemonclient.h"

#include "overviewlogging.h"

#include <PhosphorProtocol/ClientHelpers.h>
#include <PhosphorProtocol/ServiceConstants.h>

#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDBusServiceWatcher>
#include <QJsonDocument>
#include <QJsonObject>

namespace PlasmaZones::Overview {

namespace {
constexpr int MaxConsistencyRetries = 3;

quint64 generationOf(const QJsonObject& obj, QLatin1String key)
{
    // toVariant().toULongLong(), not toDouble(): the counters are integers
    // and the double round-trip loses exactness past 2^53.
    return obj.value(key).toVariant().toULongLong();
}
} // namespace

DaemonClient::DaemonClient(QObject* parent)
    : QObject(parent)
{
    using namespace PhosphorProtocol::Service;
    QDBusConnection bus = QDBusConnection::sessionBus();
    bus.connect(Name, ObjectPath, Interface::WindowTracking, QStringLiteral("workspaceMapChanged"), this,
                SLOT(slotWorkspaceMapChanged(QString)));
    bus.connect(Name, ObjectPath, Interface::Overview, QStringLiteral("overviewModelChanged"), this,
                SLOT(slotOverviewModelChanged(QString)));
    bus.connect(Name, ObjectPath, Interface::Overview, QStringLiteral("toggleOverviewRequested"), this,
                SLOT(slotToggleRequested()));
    bus.connect(Name, ObjectPath, Interface::Overview, QStringLiteral("closeOverviewRequested"), this,
                SLOT(slotCloseRequested()));

    m_watcher = new QDBusServiceWatcher(
        Name, bus, QDBusServiceWatcher::WatchForRegistration | QDBusServiceWatcher::WatchForUnregistration, this);
    connect(m_watcher, &QDBusServiceWatcher::serviceRegistered, this, &DaemonClient::onServiceRegistered);
    connect(m_watcher, &QDBusServiceWatcher::serviceUnregistered, this, &DaemonClient::onServiceUnregistered);
    if (bus.interface() && bus.interface()->isServiceRegistered(Name)) {
        onServiceRegistered();
    }
}

void DaemonClient::onServiceRegistered()
{
    // A new daemon cycle: its counters restart, so the caches and their
    // generation floors belong to the dead cycle.
    ++m_epoch;
    m_map.clear();
    m_mapGeneration = 0;
    m_model.clear();
    m_modelGeneration = 0;
    m_consistencyRetries = 0;
    m_open = false;
    if (!m_available) {
        m_available = true;
        Q_EMIT availableChanged(true);
    }
    replayWorkspaceMap();
}

void DaemonClient::onServiceUnregistered()
{
    m_map.clear();
    m_mapGeneration = 0;
    m_model.clear();
    m_modelGeneration = 0;
    m_open = false;
    if (m_available) {
        m_available = false;
        Q_EMIT availableChanged(false);
    }
    Q_EMIT workspaceMapChanged();
    Q_EMIT modelChanged();
    // The daemon's service watcher resets its own open state; the effect
    // has nothing to render without a model.
    Q_EMIT closeRequested();
}

void DaemonClient::replayWorkspaceMap()
{
    const quint64 epoch = m_epoch;
    auto* watcher = new QDBusPendingCallWatcher(
        PhosphorProtocol::ClientHelpers::asyncCall(PhosphorProtocol::Service::Interface::WindowTracking,
                                                   QStringLiteral("workspaceMap")),
        this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, epoch](QDBusPendingCallWatcher* w) {
        w->deleteLater();
        if (epoch != m_epoch) {
            return;
        }
        QDBusPendingReply<QString> reply = *w;
        if (!reply.isValid()) {
            qCWarning(lcOverview) << "workspaceMap replay failed:" << reply.error().message();
            return;
        }
        applyWorkspaceMap(reply.value());
        // A model waiting on this map (the consistency re-request) is asked
        // for again now that the map moved.
        if (m_open && m_consistencyRetries > 0) {
            requestModel();
        }
    });
}

void DaemonClient::requestModel()
{
    const quint64 epoch = m_epoch;
    auto* watcher = new QDBusPendingCallWatcher(
        PhosphorProtocol::ClientHelpers::asyncCall(PhosphorProtocol::Service::Interface::Overview,
                                                   QStringLiteral("overviewModel")),
        this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, epoch](QDBusPendingCallWatcher* w) {
        w->deleteLater();
        if (epoch != m_epoch || !m_open) {
            return;
        }
        QDBusPendingReply<QString> reply = *w;
        if (!reply.isValid()) {
            qCWarning(lcOverview) << "overviewModel replay failed:" << reply.error().message();
            return;
        }
        applyModel(reply.value());
    });
}

void DaemonClient::applyWorkspaceMap(const QString& json)
{
    if (json.isEmpty()) {
        // The interface promises an empty payload while the feature is off:
        // drop the cache and the generation floor so a later re-enable is
        // accepted.
        m_map.clear();
        m_mapGeneration = 0;
        Q_EMIT workspaceMapChanged();
        return;
    }
    const QJsonDocument doc = QJsonDocument::fromJson(json.toUtf8());
    if (!doc.isObject()) {
        qCWarning(lcOverview) << "workspaceMapChanged: non-object payload, ignoring";
        return;
    }
    const quint64 generation = generationOf(doc.object(), QLatin1String("generation"));
    if (!m_map.isEmpty() && generation < m_mapGeneration) {
        return;
    }
    m_mapGeneration = generation;
    m_map = doc.object().toVariantMap();
    Q_EMIT workspaceMapChanged();
}

void DaemonClient::applyModel(const QString& json)
{
    if (json.isEmpty()) {
        m_model.clear();
        m_modelGeneration = 0;
        Q_EMIT modelChanged();
        return;
    }
    const QJsonDocument doc = QJsonDocument::fromJson(json.toUtf8());
    if (!doc.isObject()) {
        qCWarning(lcOverview) << "overviewModelChanged: non-object payload, ignoring";
        return;
    }
    const QJsonObject obj = doc.object();
    const quint64 generation = generationOf(obj, QLatin1String("generation"));
    if (!m_model.isEmpty() && generation <= m_modelGeneration) {
        return;
    }
    const quint64 mapGeneration = generationOf(obj, QLatin1String("workspaceMapGeneration"));
    if (mapGeneration != m_mapGeneration) {
        // The model was built against a map this client does not hold yet
        // (or no longer holds). Re-request the map, then the model, bounded:
        // a daemon that keeps answering with a moving pair is not something
        // the overview can render honestly.
        if (++m_consistencyRetries > MaxConsistencyRetries) {
            qCWarning(lcOverview) << "overview model and workspace map never agreed after" << MaxConsistencyRetries
                                  << "retries, closing";
            m_consistencyRetries = 0;
            Q_EMIT closeRequested();
            return;
        }
        qCDebug(lcOverview) << "overview model built against map generation" << mapGeneration << "but holding"
                            << m_mapGeneration << ", re-requesting";
        replayWorkspaceMap();
        return;
    }
    m_consistencyRetries = 0;
    m_modelGeneration = generation;
    m_model = obj.toVariantMap();
    Q_EMIT modelChanged();
}

void DaemonClient::setOverviewOpen(bool open)
{
    if (!m_available) {
        return;
    }
    m_open = open;
    PhosphorProtocol::ClientHelpers::fireAndForget(this, PhosphorProtocol::Service::Interface::Overview,
                                                   QStringLiteral("setOverviewOpen"), {open},
                                                   QStringLiteral("overview setOverviewOpen"));
    if (open) {
        m_consistencyRetries = 0;
        requestModel();
    } else {
        m_model.clear();
        m_modelGeneration = 0;
        Q_EMIT modelChanged();
    }
}

void DaemonClient::reportOverviewState(bool open)
{
    if (!m_available) {
        return;
    }
    PhosphorProtocol::ClientHelpers::fireAndForget(this, PhosphorProtocol::Service::Interface::Overview,
                                                   QStringLiteral("reportOverviewState"), {open},
                                                   QStringLiteral("overview reportOverviewState"));
}

void DaemonClient::callOverview(const QString& method, const QVariantList& args)
{
    if (!m_available) {
        qCDebug(lcOverview) << "overview verb" << method << "dropped, daemon absent";
        return;
    }
    PhosphorProtocol::ClientHelpers::fireAndForget(this, PhosphorProtocol::Service::Interface::Overview, method, args,
                                                   QStringLiteral("overview ") + method);
}

void DaemonClient::slotWorkspaceMapChanged(const QString& json)
{
    applyWorkspaceMap(json);
}

void DaemonClient::slotOverviewModelChanged(const QString& json)
{
    if (!m_open) {
        return;
    }
    applyModel(json);
}

void DaemonClient::slotToggleRequested()
{
    Q_EMIT toggleRequested();
}

void DaemonClient::slotCloseRequested()
{
    Q_EMIT closeRequested();
}

} // namespace PlasmaZones::Overview
