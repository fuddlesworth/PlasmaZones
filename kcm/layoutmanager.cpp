// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "layoutmanager.h"
#include "kcm_plasmazones.h"
#include <algorithm>
#include <QDBusConnection>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTimer>
#include "../src/config/settings.h"
#include "../src/core/constants.h"
#include "../src/core/logging.h"

namespace PlasmaZones {

LayoutManager::LayoutManager(KCMPlasmaZones* kcm, Settings* settings, QObject* parent)
    : QObject(parent)
    , m_kcm(kcm)
    , m_settings(settings)
{
    // Debounce timer for coalescing rapid D-Bus layout signals into a single loadAsync() call
    m_loadTimer = new QTimer(this);
    m_loadTimer->setSingleShot(true);
    m_loadTimer->setInterval(50);
    connect(m_loadTimer, &QTimer::timeout, this, &LayoutManager::loadAsync);
}

// ═══════════════════════════════════════════════════════════════════════════════
// D-Bus helpers (local copies — avoid tight coupling to KCM's private API)
// ═══════════════════════════════════════════════════════════════════════════════

QDBusMessage LayoutManager::callDaemon(const QString& interface, const QString& method, const QVariantList& args) const
{
    QDBusMessage msg =
        QDBusMessage::createMethodCall(QString(DBus::ServiceName), QString(DBus::ObjectPath), interface, method);
    if (!args.isEmpty()) {
        msg.setArguments(args);
    }
    QDBusMessage reply = QDBusConnection::sessionBus().call(msg, QDBus::Block, 5000);
    if (reply.type() == QDBusMessage::ErrorMessage) {
        qCWarning(lcKcm) << "D-Bus call failed:" << interface << "::" << method << "-" << reply.errorName() << ":"
                         << reply.errorMessage();
    }
    return reply;
}

void LayoutManager::watchAsyncDbusCall(QDBusPendingCall call, const QString& operation)
{
    auto* watcher = new QDBusPendingCallWatcher(std::move(call), const_cast<LayoutManager*>(this));
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [operation](QDBusPendingCallWatcher* w) {
        w->deleteLater();
        QDBusPendingReply<> reply = *w;
        if (reply.isError()) {
            qCWarning(lcKcm) << operation << "D-Bus call failed:" << reply.error().message();
        }
    });
}

// ═══════════════════════════════════════════════════════════════════════════════
// CRUD operations
// ═══════════════════════════════════════════════════════════════════════════════

void LayoutManager::createNewLayout()
{
    QDBusMessage reply = callDaemon(QString(DBus::Interface::LayoutManager), QStringLiteral("createLayout"),
                                    {QStringLiteral("New Layout"), QStringLiteral("custom")});

    if (reply.type() == QDBusMessage::ReplyMessage && !reply.arguments().isEmpty()) {
        QString newLayoutId = reply.arguments().first().toString();
        if (!newLayoutId.isEmpty()) {
            m_layoutToSelect = newLayoutId;
            editLayout(newLayoutId);
        }
    }
}

void LayoutManager::deleteLayout(const QString& layoutId)
{
    QDBusMessage reply =
        callDaemon(QString(DBus::Interface::LayoutManager), QStringLiteral("deleteLayout"), {layoutId});
    if (reply.type() == QDBusMessage::ErrorMessage) {
        qCWarning(lcKcm) << "deleteLayout failed:" << reply.errorMessage();
    }
}

void LayoutManager::duplicateLayout(const QString& layoutId)
{
    QDBusMessage reply =
        callDaemon(QString(DBus::Interface::LayoutManager), QStringLiteral("duplicateLayout"), {layoutId});

    if (reply.type() == QDBusMessage::ReplyMessage && !reply.arguments().isEmpty()) {
        QString newLayoutId = reply.arguments().first().toString();
        if (!newLayoutId.isEmpty()) {
            m_layoutToSelect = newLayoutId;
        }
    }
}

void LayoutManager::importLayout(const QString& filePath)
{
    QDBusMessage reply =
        callDaemon(QString(DBus::Interface::LayoutManager), QStringLiteral("importLayout"), {filePath});

    if (reply.type() == QDBusMessage::ReplyMessage && !reply.arguments().isEmpty()) {
        QString newLayoutId = reply.arguments().first().toString();
        if (!newLayoutId.isEmpty()) {
            m_layoutToSelect = newLayoutId;
        }
    }
}

void LayoutManager::exportLayout(const QString& layoutId, const QString& filePath)
{
    QDBusMessage msg =
        QDBusMessage::createMethodCall(QString(DBus::ServiceName), QString(DBus::ObjectPath),
                                       QString(DBus::Interface::LayoutManager), QStringLiteral("exportLayout"));
    msg << layoutId << filePath;
    watchAsyncDbusCall(QDBusConnection::sessionBus().asyncCall(msg), QStringLiteral("exportLayout"));
}

void LayoutManager::editLayout(const QString& layoutId)
{
    QString screenName = m_kcm->currentScreenName();

    QDBusMessage msg = QDBusMessage::createMethodCall(QString(DBus::ServiceName), QString(DBus::ObjectPath),
                                                      QString(DBus::Interface::LayoutManager),
                                                      QStringLiteral("openEditorForLayoutOnScreen"));
    msg << layoutId << screenName;
    watchAsyncDbusCall(QDBusConnection::sessionBus().asyncCall(msg), QStringLiteral("editLayout"));
}

void LayoutManager::openEditor()
{
    QString screenName = m_kcm->currentScreenName();

    QDBusMessage msg;
    if (!screenName.isEmpty()) {
        msg = QDBusMessage::createMethodCall(QString(DBus::ServiceName), QString(DBus::ObjectPath),
                                             QString(DBus::Interface::LayoutManager),
                                             QStringLiteral("openEditorForScreen"));
        msg << screenName;
    } else {
        msg = QDBusMessage::createMethodCall(QString(DBus::ServiceName), QString(DBus::ObjectPath),
                                             QString(DBus::Interface::LayoutManager), QStringLiteral("openEditor"));
    }
    watchAsyncDbusCall(QDBusConnection::sessionBus().asyncCall(msg), QStringLiteral("openEditor"));
}

// ═══════════════════════════════════════════════════════════════════════════════
// Pending state staging
// ═══════════════════════════════════════════════════════════════════════════════

void LayoutManager::setLayoutHidden(const QString& layoutId, bool hidden)
{
    m_pendingHiddenStates[layoutId] = hidden;

    bool changed = false;
    for (int i = 0; i < m_layouts.size(); ++i) {
        QVariantMap layout = m_layouts[i].toMap();
        if (layout[QStringLiteral("id")].toString() == layoutId) {
            if (layout[QStringLiteral("hiddenFromSelector")].toBool() != hidden) {
                layout[QStringLiteral("hiddenFromSelector")] = hidden;
                m_layouts[i] = layout;
                changed = true;
            }
            break;
        }
    }

    if (changed) {
        Q_EMIT layoutsChanged();
        Q_EMIT needsSave();
    }
}

void LayoutManager::setLayoutAutoAssign(const QString& layoutId, bool enabled)
{
    m_pendingAutoAssignStates[layoutId] = enabled;

    bool changed = false;
    for (int i = 0; i < m_layouts.size(); ++i) {
        QVariantMap layout = m_layouts[i].toMap();
        if (layout[QStringLiteral("id")].toString() == layoutId) {
            if (layout[QStringLiteral("autoAssign")].toBool() != enabled) {
                layout[QStringLiteral("autoAssign")] = enabled;
                m_layouts[i] = layout;
                changed = true;
            }
            break;
        }
    }

    if (changed) {
        Q_EMIT layoutsChanged();
        Q_EMIT needsSave();
    }
}

// ═══════════════════════════════════════════════════════════════════════════════
// Load operations
// ═══════════════════════════════════════════════════════════════════════════════

void LayoutManager::scheduleLoad()
{
    if (m_saveInProgress)
        return;
    m_loadTimer->start();
}

void LayoutManager::loadAsync()
{
    const int generation = ++m_loadGeneration;

    QDBusMessage msg =
        QDBusMessage::createMethodCall(QString(DBus::ServiceName), QString(DBus::ObjectPath),
                                       QString(DBus::Interface::LayoutManager), QStringLiteral("getLayoutList"));

    auto* watcher = new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(msg), this);

    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, generation](QDBusPendingCallWatcher* w) {
        w->deleteLater();

        if (generation != m_loadGeneration)
            return;

        QVariantList newLayouts;
        QDBusPendingReply<QStringList> reply = *w;

        if (!reply.isError()) {
            const QStringList layoutJsonList = reply.value();
            for (const QString& layoutJson : layoutJsonList) {
                QJsonDocument doc = QJsonDocument::fromJson(layoutJson.toUtf8());
                if (!doc.isNull() && doc.isObject()) {
                    newLayouts.append(doc.object().toVariantMap());
                }
            }
        } else {
            qCWarning(lcKcm) << "Failed to load layouts:" << reply.error().message();
        }

        m_unfilteredLayouts = newLayouts;
        applyLayoutFilter();
    });
}

void LayoutManager::loadSync()
{
    ++m_loadGeneration;

    QVariantList newLayouts;
    QDBusMessage reply = callDaemon(QString(DBus::Interface::LayoutManager), QStringLiteral("getLayoutList"));

    if (reply.type() == QDBusMessage::ReplyMessage && !reply.arguments().isEmpty()) {
        QStringList layoutJsonList = reply.arguments().first().toStringList();
        for (const QString& layoutJson : layoutJsonList) {
            QJsonDocument doc = QJsonDocument::fromJson(layoutJson.toUtf8());
            if (!doc.isNull() && doc.isObject()) {
                newLayouts.append(doc.object().toVariantMap());
            }
        }
    }

    m_unfilteredLayouts = newLayouts;
    applyLayoutFilter();
}

void LayoutManager::applyLayoutFilter()
{
    QVariantList newLayouts = m_unfilteredLayouts;

    // Filter out autotile entries when the feature is disabled
    if (!m_settings->autotileEnabled()) {
        newLayouts.erase(std::remove_if(newLayouts.begin(), newLayouts.end(),
                                        [](const QVariant& v) {
                                            return v.toMap().value(QStringLiteral("isAutotile")).toBool();
                                        }),
                         newLayouts.end());
    }

    // Sort: manual layouts first (alphabetical), then autotile (alphabetical)
    std::sort(newLayouts.begin(), newLayouts.end(), [](const QVariant& a, const QVariant& b) {
        const QVariantMap mapA = a.toMap();
        const QVariantMap mapB = b.toMap();
        const bool aIsAutotile = mapA.value(QStringLiteral("isAutotile")).toBool();
        const bool bIsAutotile = mapB.value(QStringLiteral("isAutotile")).toBool();
        if (aIsAutotile != bIsAutotile) {
            return !aIsAutotile;
        }
        return mapA.value(QStringLiteral("name")).toString().toLower()
            < mapB.value(QStringLiteral("name")).toString().toLower();
    });

    // Compare by ID list — if order hasn't changed, update data in-place
    // to avoid full model swap (which resets scroll position in QML GridView)
    auto extractIds = [](const QVariantList& list) {
        QStringList ids;
        ids.reserve(list.size());
        for (const auto& v : list) {
            ids.append(v.toMap().value(QStringLiteral("id")).toString());
        }
        return ids;
    };
    if (extractIds(m_layouts) == extractIds(newLayouts)) {
        // IDs unchanged — update entries in-place (zone data may have changed,
        // e.g. when autotile maxWindows changes preview zone count)
        bool dataChanged = false;
        for (int i = 0; i < newLayouts.size(); ++i) {
            if (m_layouts[i] != newLayouts[i]) {
                m_layouts[i] = newLayouts[i];
                dataChanged = true;
            }
        }
        if (dataChanged) {
            Q_EMIT layoutsChanged();
        }
        return;
    }

    const bool hasExternalSelect = !m_layoutToSelect.isEmpty();

    m_layouts = newLayouts;
    Q_EMIT layoutsChanged();

    // Auto-select default layout on first successful load
    if (!m_initialLayoutLoadDone && !newLayouts.isEmpty()) {
        m_initialLayoutLoadDone = true;

        QString defaultId = m_settings->defaultLayoutId();
        if (defaultId.isEmpty()) {
            int bestOrder = 999;
            for (const QVariant& v : newLayouts) {
                const QVariantMap layoutMap = v.toMap();
                int order = layoutMap.value(QStringLiteral("defaultOrder"), 999).toInt();
                if (order < bestOrder) {
                    bestOrder = order;
                    defaultId = layoutMap.value(QStringLiteral("id")).toString();
                }
            }
            if (!defaultId.isEmpty()) {
                m_settings->setDefaultLayoutId(defaultId);
            }
        }
        if (!defaultId.isEmpty()) {
            m_layoutToSelect = defaultId;
        }
    }

    if (hasExternalSelect || !m_layoutToSelect.isEmpty()) {
        if (!m_layoutToSelect.isEmpty()) {
            Q_EMIT layoutToSelectChanged();
            m_layoutToSelect.clear();
        }
    }
}

// ═══════════════════════════════════════════════════════════════════════════════
// Save/defaults integration
// ═══════════════════════════════════════════════════════════════════════════════

void LayoutManager::savePendingStates(QStringList& failedOperations)
{
    const QString layoutInterface = QString(DBus::Interface::LayoutManager);

    if (!m_pendingHiddenStates.isEmpty()) {
        for (auto it = m_pendingHiddenStates.cbegin(); it != m_pendingHiddenStates.cend(); ++it) {
            QDBusMessage reply = callDaemon(layoutInterface, QStringLiteral("setLayoutHidden"), {it.key(), it.value()});
            if (reply.type() == QDBusMessage::ErrorMessage) {
                failedOperations.append(QStringLiteral("Layout visibility (%1)").arg(it.key()));
            }
        }
        m_pendingHiddenStates.clear();
    }

    if (!m_pendingAutoAssignStates.isEmpty()) {
        for (auto it = m_pendingAutoAssignStates.cbegin(); it != m_pendingAutoAssignStates.cend(); ++it) {
            QDBusMessage reply =
                callDaemon(layoutInterface, QStringLiteral("setLayoutAutoAssign"), {it.key(), it.value()});
            if (reply.type() == QDBusMessage::ErrorMessage) {
                failedOperations.append(QStringLiteral("Layout auto-assign (%1)").arg(it.key()));
            }
        }
        m_pendingAutoAssignStates.clear();
    }
}

void LayoutManager::resetAllToDefaults()
{
    m_pendingHiddenStates.clear();
    m_pendingAutoAssignStates.clear();

    for (int i = 0; i < m_layouts.size(); ++i) {
        QVariantMap layout = m_layouts[i].toMap();
        bool changed = false;
        if (layout[QStringLiteral("hiddenFromSelector")].toBool()) {
            layout[QStringLiteral("hiddenFromSelector")] = false;
            m_pendingHiddenStates[layout[QStringLiteral("id")].toString()] = false;
            changed = true;
        }
        if (layout[QStringLiteral("autoAssign")].toBool()) {
            layout[QStringLiteral("autoAssign")] = false;
            m_pendingAutoAssignStates[layout[QStringLiteral("id")].toString()] = false;
            changed = true;
        }
        if (changed) {
            m_layouts[i] = layout;
        }
    }
    Q_EMIT layoutsChanged();
}

void LayoutManager::clearPendingStates()
{
    m_pendingHiddenStates.clear();
    m_pendingAutoAssignStates.clear();
}

} // namespace PlasmaZones
