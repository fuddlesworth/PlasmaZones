// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorZones/ScrollingTemplateSource.h>

#include <PhosphorLayoutApi/LayoutSourceProviderRegistry.h>
#include <PhosphorZones/ScrollingTemplateStore.h>

#include <QUuid>

namespace PhosphorZones {

PhosphorLayout::LayoutPreview previewFromScrollingTemplate(const ScrollingTemplate& templ)
{
    PhosphorLayout::LayoutPreview preview;
    if (!templ.isValid()) {
        return preview;
    }
    preview.id = templ.id.toString();
    preview.displayName = templ.name;
    preview.description = templ.description;
    preview.isSystem = templ.isSystem;
    preview.isScrollingTemplate = true;

    // Strip snapshot: blueprint columns (else the preset width vocabulary)
    // laid left to right as full-height bands. The preview is a fixed-size
    // card, so a strip wider than the screen is clamped to fit rather than
    // drawn overhanging: the band that crosses the right edge ends at 1.0,
    // and any column that would start at or past the edge is dropped.
    // A defaults-only template previews as one column at its own default
    // width.
    QList<qreal> widths;
    widths.reserve(templ.columns.size());
    for (const ScrollingTemplateColumn& column : templ.columns) {
        widths.append(column.width);
    }
    if (widths.isEmpty()) {
        widths = templ.presetColumnWidths;
    }
    if (widths.isEmpty()) {
        // Kind Preset cannot reach here: normalize() demotes it when the
        // preset list is empty, and a non-empty list was consumed above.
        // Fixed pixels and ClientDecides have no fraction to draw, so they
        // preview at half width.
        const qreal fallback =
            templ.defaultColumnWidthKind == 0 ? qBound<qreal>(0.05, templ.defaultColumnWidthValue, 1.0) : 0.5;
        widths = {fallback};
    }
    preview.zones.reserve(widths.size());
    preview.zoneNumbers.reserve(widths.size());
    qreal x = 0.0;
    int zoneNumber = 1;
    for (qreal width : widths) {
        if (x >= 1.0) {
            break;
        }
        preview.zones.append(QRectF(x, 0.0, qMin(width, 1.0 - x), 1.0));
        preview.zoneNumbers.append(zoneNumber++);
        x += width;
    }
    preview.zoneCount = preview.zones.size();
    return preview;
}

// ─── ScrollingTemplateSource ────────────────────────────────────────────────

ScrollingTemplateSource::ScrollingTemplateSource(ScrollingTemplateStore* store, QObject* parent)
    : PhosphorLayout::ILayoutSource(parent)
    , m_store(store)
{
    // Null store is the documented empty-source case (parity with
    // ZonesLayoutSource's null registry).
    if (m_store) {
        connect(m_store, &ScrollingTemplateStore::templatesChanged, this,
                &PhosphorLayout::ILayoutSource::contentsChanged);
    }
}

ScrollingTemplateSource::~ScrollingTemplateSource() = default;

QVector<PhosphorLayout::LayoutPreview> ScrollingTemplateSource::availableLayouts() const
{
    QVector<PhosphorLayout::LayoutPreview> result;
    if (!m_store) {
        return result;
    }
    const QList<ScrollingTemplate> templates = m_store->templates();
    result.reserve(templates.size());
    for (const ScrollingTemplate& templ : templates) {
        result.append(previewFromScrollingTemplate(templ));
    }
    return result;
}

PhosphorLayout::LayoutPreview ScrollingTemplateSource::previewAt(const QString& id, int /*windowCount*/,
                                                                 const QSize& /*canvas*/)
{
    if (!m_store || id.isEmpty()) {
        return {};
    }
    const QUuid uuid = QUuid::fromString(id);
    if (uuid.isNull()) {
        return {};
    }
    return previewFromScrollingTemplate(m_store->templateById(uuid));
}

// ─── Factory + registrar ────────────────────────────────────────────────────

void ensureScrollingTemplateSourceProviderLinked()
{
}

ScrollingTemplateSourceFactory::ScrollingTemplateSourceFactory(ScrollingTemplateStore* store)
    : m_store(store)
{
}

ScrollingTemplateSourceFactory::~ScrollingTemplateSourceFactory() = default;

QString ScrollingTemplateSourceFactory::name() const
{
    return QStringLiteral("scrolling-templates");
}

std::unique_ptr<PhosphorLayout::ILayoutSource> ScrollingTemplateSourceFactory::create()
{
    return std::make_unique<ScrollingTemplateSource>(m_store);
}

namespace {
// Priority 200: after zones (0) and autotile (100) so template entries land
// last in the composite's iteration order.
PhosphorLayout::LayoutSourceProviderRegistrar
    registrar(QStringLiteral("scrolling-templates"), /*priority=*/200,
              &PhosphorLayout::makeProviderFactory<ScrollingTemplateStore, ScrollingTemplateSourceFactory>);
} // anonymous namespace

} // namespace PhosphorZones
