// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include "phosphorzones_export.h"

#include <PhosphorLayoutApi/ILayoutSource.h>
#include <PhosphorLayoutApi/ILayoutSourceFactory.h>
#include <PhosphorZones/ScrollingTemplate.h>

#include <memory>

namespace PhosphorZones {

class ScrollingTemplateStore;

/// Project a native scrolling template into the shared LayoutPreview shape:
/// the blueprint columns (or, for a vocabulary-only template, its preset
/// widths) become full-height x-band zones laid left to right, so the
/// shared thumbnail renderer draws a strip snapshot with no
/// template-specific code. isScrollingTemplate marks the family.
PHOSPHORZONES_EXPORT PhosphorLayout::LayoutPreview previewFromScrollingTemplate(const ScrollingTemplate& templ);

/// ILayoutSource over a ScrollingTemplateStore — the third card family
/// beside manual zone layouts and autotile algorithms. Null-tolerant like
/// ZonesLayoutSource: a null store reports an empty list.
class PHOSPHORZONES_EXPORT ScrollingTemplateSource : public PhosphorLayout::ILayoutSource
{
    Q_OBJECT

public:
    explicit ScrollingTemplateSource(ScrollingTemplateStore* store, QObject* parent = nullptr);
    ~ScrollingTemplateSource() override;

    QVector<PhosphorLayout::LayoutPreview> availableLayouts() const override;
    PhosphorLayout::LayoutPreview previewAt(const QString& id, int windowCount, const QSize& canvas) override;

private:
    ScrollingTemplateStore* m_store = nullptr;
};

/// Factory + registrar anchor, mirroring ZonesLayoutSourceFactory.
class PHOSPHORZONES_EXPORT ScrollingTemplateSourceFactory : public PhosphorLayout::ILayoutSourceFactory
{
public:
    explicit ScrollingTemplateSourceFactory(ScrollingTemplateStore* store);
    ~ScrollingTemplateSourceFactory() override;

    QString name() const override;
    std::unique_ptr<PhosphorLayout::ILayoutSource> create() override;

private:
    ScrollingTemplateStore* m_store = nullptr;
};

/// Linker anchor — reference from every composition root that expects the
/// self-registered provider (see ZonesLayoutSourceFactory's rationale).
PHOSPHORZONES_EXPORT void ensureScrollingTemplateSourceProviderLinked();

} // namespace PhosphorZones
