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

void CompositeLayoutSource::addSource(ILayoutSource* source)
{
    if (!source || m_sources.contains(source)) {
        return;
    }
    m_sources.append(source);
    // Forward child's contentsChanged so callers only need to listen at
    // the composite level. AutoConnection — direct within the same thread,
    // which is the only supported topology for ILayoutSource today.
    connect(source, &ILayoutSource::contentsChanged, this, &ILayoutSource::contentsChanged);
    // Auto-drop the entry if the caller deletes the source without calling
    // removeSource() first. The documented contract says callers keep
    // sources alive, but a dangling raw pointer here would turn a caller
    // bug into a UAF on the next availableLayouts() — too steep a cost
    // for a one-line safety net. The QObject is partially destroyed at this
    // point, so compare by pointer identity without dereferencing through
    // the ILayoutSource subobject.
    connect(source, &QObject::destroyed, this, [this](QObject* obj) {
        const auto before = m_sources.size();
        m_sources.erase(std::remove_if(m_sources.begin(), m_sources.end(),
                                       [obj](ILayoutSource* s) {
                                           return static_cast<QObject*>(s) == obj;
                                       }),
                        m_sources.end());
        if (m_sources.size() != before) {
            Q_EMIT contentsChanged();
        }
    });
    Q_EMIT contentsChanged();
}

void CompositeLayoutSource::removeSource(ILayoutSource* source)
{
    if (!source) {
        return;
    }
    disconnect(source, nullptr, this, nullptr);
    if (m_sources.removeAll(source) > 0) {
        Q_EMIT contentsChanged();
    }
}

void CompositeLayoutSource::clearSources()
{
    if (m_sources.isEmpty()) {
        return;
    }
    for (ILayoutSource* source : std::as_const(m_sources)) {
        if (source) {
            disconnect(source, nullptr, this, nullptr);
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

LayoutPreview CompositeLayoutSource::previewAt(const QString& id, int windowCount, const QSize& canvas) const
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
