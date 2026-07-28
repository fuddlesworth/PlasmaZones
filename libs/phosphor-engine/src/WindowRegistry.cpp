// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorEngine/WindowRegistry.h>
#include <PhosphorIdentity/WindowId.h>

#include <QPointer>

namespace PhosphorEngine {

WindowRegistry::WindowRegistry(QObject* parent)
    : QObject(parent)
{
}

WindowRegistry::~WindowRegistry() = default;

void WindowRegistry::upsert(const QString& instanceId, const WindowMetadata& metadata)
{
    if (instanceId.isEmpty()) {
        qWarning("WindowRegistry::upsert: rejecting empty instance id");
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
        return;
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
    if (m_disappearingInstances.contains(instanceId)) {
        if (m_records.contains(instanceId)) {
            m_removeAfterDisappearance.insert(instanceId);
        }
        return;
    }

    auto it = m_records.find(instanceId);
    const bool hasCanonical = m_canonicalByInstance.contains(instanceId);
    if (it == m_records.end() && !hasCanonical) {
        return;
    }
    if (it != m_records.end()) {
        indexRemove(instanceId, it.value().appId);
        m_records.erase(it);
    }
    // Emit while the canonical mapping is still available to synchronous
    // subscribers, then retire it. Canonical-only instances still represent a
    // lifecycle entry and therefore receive the same exactly-once signal.
    m_disappearingInstances.insert(instanceId);
    QPointer<WindowRegistry> guard(this);
    Q_EMIT windowDisappeared(instanceId);
    if (!guard) {
        return;
    }
    m_disappearingInstances.remove(instanceId);
    if (m_removeAfterDisappearance.remove(instanceId)) {
        remove(instanceId);
        return;
    }
    if (!m_records.contains(instanceId)) {
        m_canonicalByInstance.remove(instanceId);
    }
}

std::optional<WindowMetadata> WindowRegistry::metadata(const QString& instanceId) const
{
    auto it = m_records.constFind(instanceId);
    if (it == m_records.constEnd()) {
        return std::nullopt;
    }
    return it.value();
}

std::optional<WindowRegistry::WindowContext> WindowRegistry::windowContext(const QString& instanceId) const
{
    auto it = m_records.constFind(instanceId);
    if (it == m_records.constEnd()) {
        return std::nullopt;
    }
    return WindowContext{it.value().virtualDesktop, it.value().virtualDesktops, it.value().activity};
}

QString WindowRegistry::appIdFor(const QString& instanceId) const
{
    auto it = m_records.constFind(instanceId);
    return it != m_records.constEnd() ? it.value().appId : QString();
}

bool WindowRegistry::isMinimized(const QString& windowId) const
{
    const QString instanceId = PhosphorIdentity::WindowId::extractInstanceId(windowId);
    const auto it = m_records.constFind(instanceId);
    return it != m_records.constEnd() && it->isMinimized.value_or(false);
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
    QSet<QString> ids(m_records.keyBegin(), m_records.keyEnd());
    ids.unite(QSet<QString>(m_canonicalByInstance.keyBegin(), m_canonicalByInstance.keyEnd()));
    for (const QString& id : ids) {
        remove(id);
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

int WindowRegistry::pruneStaleInstances(const QSet<QString>& aliveInstanceIds)
{
    // Union of both maps' keys — a window may carry a canonical entry without
    // a metadata record (or vice versa) depending on which signals it received
    // before dying. Collect first, then mutate (remove() invalidates iterators
    // and fires windowDisappeared, which subscribers may react to re-entrantly).
    QSet<QString> stale;
    for (auto it = m_records.constBegin(); it != m_records.constEnd(); ++it) {
        if (!aliveInstanceIds.contains(it.key())) {
            stale.insert(it.key());
        }
    }
    for (auto it = m_canonicalByInstance.constBegin(); it != m_canonicalByInstance.constEnd(); ++it) {
        if (!aliveInstanceIds.contains(it.key())) {
            stale.insert(it.key());
        }
    }
    for (const QString& instanceId : std::as_const(stale)) {
        remove(instanceId);
    }
    return stale.size();
}

void WindowRegistry::indexInsert(const QString& instanceId, const QString& appId)
{
    if (appId.isEmpty()) {
        return;
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

} // namespace PhosphorEngine
