// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "windowregistry.h"
#include "logging.h"
#include "utils.h"

namespace PlasmaZones {

WindowRegistry::WindowRegistry(QObject* parent)
    : QObject(parent)
{
}

WindowRegistry::~WindowRegistry() = default;

void WindowRegistry::upsert(const QString& instanceId, const WindowMetadata& metadata)
{
    if (instanceId.isEmpty()) {
        qCWarning(lcCore) << "WindowRegistry::upsert: rejecting empty instance id";
        return;
    }

    auto it = m_records.find(instanceId);
    if (it == m_records.end()) {
        m_records.insert(instanceId, metadata);
        indexInsert(instanceId, metadata.appId);
        Q_EMIT windowAppeared(instanceId);
        return;
    }

    if (it.value() == metadata) {
        return; // No change — bridges call this unconditionally
    }

    const WindowMetadata oldMeta = it.value();
    if (oldMeta.appId != metadata.appId) {
        indexRemove(instanceId, oldMeta.appId);
        indexInsert(instanceId, metadata.appId);
    }
    it.value() = metadata;
    Q_EMIT metadataChanged(instanceId, oldMeta, metadata);
}

void WindowRegistry::remove(const QString& instanceId)
{
    auto it = m_records.find(instanceId);
    if (it == m_records.end()) {
        return;
    }
    indexRemove(instanceId, it.value().appId);
    m_records.erase(it);
    Q_EMIT windowDisappeared(instanceId);
}

std::optional<WindowMetadata> WindowRegistry::metadata(const QString& instanceId) const
{
    auto it = m_records.constFind(instanceId);
    if (it == m_records.constEnd()) {
        return std::nullopt;
    }
    return it.value();
}

QString WindowRegistry::appIdFor(const QString& instanceId) const
{
    auto it = m_records.constFind(instanceId);
    return it != m_records.constEnd() ? it.value().appId : QString();
}

QStringList WindowRegistry::instancesWithAppId(const QString& appId) const
{
    if (appId.isEmpty()) {
        return {};
    }
    return m_appIdIndex.values(appId);
}

bool WindowRegistry::contains(const QString& instanceId) const
{
    return m_records.contains(instanceId);
}

QStringList WindowRegistry::allInstances() const
{
    return m_records.keys();
}

int WindowRegistry::size() const
{
    return m_records.size();
}

void WindowRegistry::clear()
{
    if (m_records.isEmpty() && m_canonicalByInstance.isEmpty()) {
        return;
    }
    // Emit windowDisappeared for each so consumers can clean up.
    const QStringList ids = m_records.keys();
    m_records.clear();
    m_appIdIndex.clear();
    m_canonicalByInstance.clear();
    for (const QString& id : ids) {
        Q_EMIT windowDisappeared(id);
    }
}

QString WindowRegistry::canonicalizeWindowId(const QString& rawWindowId)
{
    if (rawWindowId.isEmpty()) {
        return rawWindowId;
    }
    const QString instanceId = PhosphorIdentity::WindowId::extractInstanceId(rawWindowId);
    auto it = m_canonicalByInstance.constFind(instanceId);
    if (it != m_canonicalByInstance.constEnd()) {
        return it.value();
    }
    m_canonicalByInstance.insert(instanceId, rawWindowId);
    return rawWindowId;
}

QString WindowRegistry::canonicalizeForLookup(const QString& rawWindowId) const
{
    if (rawWindowId.isEmpty()) {
        return rawWindowId;
    }
    const QString instanceId = PhosphorIdentity::WindowId::extractInstanceId(rawWindowId);
    auto it = m_canonicalByInstance.constFind(instanceId);
    return (it != m_canonicalByInstance.constEnd()) ? it.value() : rawWindowId;
}

void WindowRegistry::releaseCanonical(const QString& anyWindowId)
{
    if (anyWindowId.isEmpty()) {
        return;
    }
    const QString instanceId = PhosphorIdentity::WindowId::extractInstanceId(anyWindowId);
    m_canonicalByInstance.remove(instanceId);
}

void WindowRegistry::indexInsert(const QString& instanceId, const QString& appId)
{
    if (appId.isEmpty()) {
        return; // Don't index empty classes — they're transient
    }
    m_appIdIndex.insert(appId, instanceId);
}

void WindowRegistry::indexRemove(const QString& instanceId, const QString& appId)
{
    if (appId.isEmpty()) {
        return;
    }
    auto it = m_appIdIndex.find(appId);
    while (it != m_appIdIndex.end() && it.key() == appId) {
        if (it.value() == instanceId) {
            it = m_appIdIndex.erase(it);
        } else {
            ++it;
        }
    }
}

} // namespace PlasmaZones
