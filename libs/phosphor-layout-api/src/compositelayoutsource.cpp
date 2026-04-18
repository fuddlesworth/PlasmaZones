// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorLayoutApi/CompositeLayoutSource.h>

namespace PhosphorLayout {

CompositeLayoutSource::~CompositeLayoutSource() = default;

void CompositeLayoutSource::addSource(ILayoutSource* source)
{
    if (source) {
        m_sources.append(source);
    }
}

void CompositeLayoutSource::removeSource(ILayoutSource* source)
{
    if (!source) {
        return;
    }
    m_sources.removeAll(source);
}

void CompositeLayoutSource::clearSources()
{
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
