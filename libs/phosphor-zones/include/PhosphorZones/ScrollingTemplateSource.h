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
/// widths) become band zones laid along the strip axis, so the shared
/// thumbnail renderer draws a strip snapshot with no template-specific code.
/// isScrollingTemplate marks the family.
///
/// @p verticalAxis picks the axis the bands run along. False (the default)
/// lays full-HEIGHT bands left to right, the shape a horizontal strip adopts.
/// True lays full-WIDTH bands top to bottom, which is what the engine actually
/// produces on a screen whose strip runs vertically — a card drawn the other
/// way depicts a shape that screen will never show. A template carries no axis
/// of its own (a column vocabulary is fractions along the strip, whichever way
/// it runs), so the axis comes from the SCREEN, and the callers that have no
/// screen in scope (the screen-agnostic management catalogue, this file's
/// ILayoutSource) keep the horizontal default deliberately.
///
/// PRECONDITION: @p templ is normalized (every template the store hands out
/// is, and ScrollingTemplate::fromJson normalizes on parse). The width fallback
/// for a template with neither blueprint columns nor a preset vocabulary relies
/// on normalize() having demoted a Preset default kind that has nothing to
/// index; an un-normalized Preset kind previews at half width rather than at
/// the width it names.
PHOSPHORZONES_EXPORT PhosphorLayout::LayoutPreview previewFromScrollingTemplate(const ScrollingTemplate& templ,
                                                                                bool verticalAxis = false);

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
