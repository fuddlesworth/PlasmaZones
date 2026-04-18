// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorLayoutApi/CompositeLayoutSource.h>

#include <algorithm>

namespace PhosphorLayout {

CompositeLayoutSource::CompositeLayoutSource(QObject* parent)
    : ILayoutSource(parent)
{
}

CompositeLayoutSource::~CompositeLayoutSource() = default;

void CompositeLayoutSource::connectSource(ILayoutSource* source)
{
    // Forward child's contentsChanged so callers only need to listen at
    // the composite level. AutoConnection — direct within the same thread,
    // which is the only supported topology for ILayoutSource today.
    QMetaObject::Connection changed =
        connect(source, &ILayoutSource::contentsChanged, this, &ILayoutSource::contentsChanged);
    // Auto-drop the entry if the caller deletes the source without calling
    // removeSource() first. The documented contract says callers keep
    // sources alive, but a dangling raw pointer here would turn a caller
    // bug into a UAF on the next availableLayouts() — too steep a cost
    // for a one-line safety net. The QObject is partially destroyed at this
    // point, so compare by pointer identity without dereferencing through
    // the ILayoutSource subobject.
    QMetaObject::Connection destroyed = connect(source, &QObject::destroyed, this, [this](QObject* obj) {
        const auto before = m_sources.size();
        m_sources.erase(std::remove_if(m_sources.begin(), m_sources.end(),
                                       [obj](ILayoutSource* s) {
                                           return static_cast<QObject*>(s) == obj;
                                       }),
                        m_sources.end());
        // Drop the connection-handles entry too — key compared by pointer
        // identity, QObject subobject not dereferenced.
        for (auto it = m_connections.begin(); it != m_connections.end();) {
            if (static_cast<QObject*>(it.key()) == obj) {
                it = m_connections.erase(it);
            } else {
                ++it;
            }
        }
        if (m_sources.size() != before) {
            Q_EMIT contentsChanged();
        }
    });
    m_connections.insert(source, {changed, destroyed});
}

void CompositeLayoutSource::disconnectSource(ILayoutSource* source)
{
    auto it = m_connections.find(source);
    if (it == m_connections.end()) {
        return;
    }
    // Handle-based disconnect is safe on mid-destruction objects — Qt tracks
    // the connection internally without needing to dereference `source`.
    QObject::disconnect(it.value().first);
    QObject::disconnect(it.value().second);
    m_connections.erase(it);
}

void CompositeLayoutSource::addSource(ILayoutSource* source)
{
    if (!source || m_sources.contains(source)) {
        return;
    }
    m_sources.append(source);
    connectSource(source);
    Q_EMIT contentsChanged();
}

void CompositeLayoutSource::removeSource(ILayoutSource* source)
{
    if (!source) {
        return;
    }
    disconnectSource(source);
    if (m_sources.removeAll(source) > 0) {
        Q_EMIT contentsChanged();
    }
}

void CompositeLayoutSource::setSources(QVector<ILayoutSource*> sources)
{
    // Tear down every existing connection first — incremental disconnect +
    // append would emit one signal per step, which is exactly what this
    // batch API exists to avoid.
    for (ILayoutSource* source : std::as_const(m_sources)) {
        if (source) {
            disconnectSource(source);
        }
    }
    m_sources.clear();

    for (ILayoutSource* source : sources) {
        if (!source || m_sources.contains(source)) {
            continue;
        }
        m_sources.append(source);
        connectSource(source);
    }
    Q_EMIT contentsChanged();
}

void CompositeLayoutSource::clearSources()
{
    if (m_sources.isEmpty()) {
        return;
    }
    for (ILayoutSource* source : std::as_const(m_sources)) {
        if (source) {
            disconnectSource(source);
        }
    }
    m_sources.clear();
    Q_EMIT contentsChanged();
}

QVector<LayoutPreview> CompositeLayoutSource::availableLayouts() const
{
    QVector<LayoutPreview> result;
    for (ILayoutSource* source : m_sources) {
        if (!source) {
            continue;
        }
        const auto previews = source->availableLayouts();
        result.reserve(result.size() + previews.size());
        for (const auto& preview : previews) {
            result.append(preview);
        }
    }
    return result;
}

LayoutPreview CompositeLayoutSource::previewAt(const QString& id, int windowCount, const QSize& canvas)
{
    for (ILayoutSource* source : m_sources) {
        if (!source) {
            continue;
        }
        // Each child returns an empty preview when it doesn't recognise the
        // id (per ILayoutSource contract); we walk in order and accept the
        // first non-empty match.
        LayoutPreview preview = source->previewAt(id, windowCount, canvas);
        if (!preview.id.isEmpty()) {
            return preview;
        }
    }
    return {};
}

} // namespace PhosphorLayout
