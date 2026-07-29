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

    if (m_disappearingInstances.contains(instanceId)) {
        m_pendingUpserts.insert(instanceId, metadata);
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
        m_pendingUpserts.remove(instanceId);
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
    const QString canonical = m_canonicalByInstance.value(instanceId);
    m_disappearingInstances.insert(instanceId);
    QPointer<WindowRegistry> guard(this);
    Q_EMIT windowDisappeared(instanceId);
    if (!guard) {
        return;
    }
    m_disappearingInstances.remove(instanceId);
    const bool hasPendingUpsert = m_pendingUpserts.contains(instanceId);
    const WindowMetadata pendingMetadata = m_pendingUpserts.take(instanceId);
    // Retire the mapping only if it still is the pre-emit one: a synchronous
    // subscriber may have re-seeded a FRESH canonical for this instance during
    // the emit (a re-announce racing the close), and clobbering that with a
    // blind remove would strip the next lifecycle's identity translation.
    const QString postEmit = m_canonicalByInstance.value(instanceId);
    if (postEmit == canonical) {
        m_canonicalByInstance.remove(instanceId);
        if (hasPendingUpsert && !canonical.isEmpty()) {
            m_canonicalByInstance.insert(instanceId, canonical);
        }
    }
    if (hasPendingUpsert) {
        upsert(instanceId, pendingMetadata);
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
    return minimizedState(windowId).value_or(false);
}

std::optional<bool> WindowRegistry::minimizedState(const QString& windowId) const
{
    const QString instanceId = PhosphorIdentity::WindowId::extractInstanceId(windowId);
    const auto it = m_records.constFind(instanceId);
    if (it == m_records.constEnd()) {
        return std::nullopt;
    }
    return it->isMinimized;
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
    QPointer<WindowRegistry> guard(this);
    for (const QString& id : ids) {
        remove(id);
        if (!guard) {
            return;
        }
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

int WindowRegistry::pruneStaleInstances(const QSet<QString>& aliveInstanceIds)
{
    // An EMPTY alive set means "wipe everything". The only legitimate empty
    // case (user closed every window) is already covered by per-window
    // remove() on windowClosed — while the illegitimate one is very real: the
    // effect's one-shot alive report at daemon-ready fires before
    // session-restored apps have mapped their windows, and treating that as
    // "all dead" destroys the whole registry. Fail closed.
    if (aliveInstanceIds.isEmpty() && !(m_records.isEmpty() && m_canonicalByInstance.isEmpty())) {
        qWarning("WindowRegistry::pruneStaleInstances: refusing empty alive set with %d records tracked",
                 int(m_records.size()));
        return 0;
    }
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
    QPointer<WindowRegistry> guard(this);
    // Count actual removals, not intent: a windowDisappeared subscriber can
    // re-upsert an instance (leaving it present) or destroy the registry
    // mid-sweep, and the return value is documented as "count removed".
    int removed = 0;
    for (const QString& instanceId : std::as_const(stale)) {
        remove(instanceId);
        if (!guard) {
            break;
        }
        if (!m_records.contains(instanceId) && !m_canonicalByInstance.contains(instanceId)) {
            ++removed;
        }
    }
    return removed;
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
