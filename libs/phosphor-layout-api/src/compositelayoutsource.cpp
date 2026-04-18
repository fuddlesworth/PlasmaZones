// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorLayoutApi/CompositeLayoutSource.h>

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
    // the composite level. Q_EMIT-by-signal-to-signal connect preserves
    // the default direct-connection semantics.
    connect(source, &ILayoutSource::contentsChanged, this, &ILayoutSource::contentsChanged);
}

void CompositeLayoutSource::removeSource(ILayoutSource* source)
{
    if (!source) {
        return;
    }
    disconnect(source, &ILayoutSource::contentsChanged, this, &ILayoutSource::contentsChanged);
    m_sources.removeAll(source);
}

void CompositeLayoutSource::clearSources()
{
    for (ILayoutSource* source : std::as_const(m_sources)) {
        if (source) {
            disconnect(source, &ILayoutSource::contentsChanged, this, &ILayoutSource::contentsChanged);
        }
    }
    m_sources.clear();
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
