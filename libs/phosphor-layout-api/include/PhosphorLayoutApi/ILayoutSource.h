// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <phosphorlayoutapi_export.h>

#include <PhosphorLayoutApi/LayoutPreview.h>

#include <QSize>
#include <QString>
#include <QVector>

namespace PhosphorLayout {

/// Abstract producer of LayoutPreview values.
///
/// Implemented by phosphor-zones (manual zone layouts) and the future
/// phosphor-tile-algo (autotile algorithms). Editor / settings / overlay
/// code holds an @c ILayoutSource* and renders previews uniformly without
/// branching on the underlying source.
///
/// Two-method contract:
///   1. @c availableLayouts — enumerate everything the source can show
///   2. @c previewAt — produce a fully-realised preview for one entry
///
/// The split exists because manual layouts have a fixed shape (zones are
/// authored, no parameters), while autotile previews depend on a window
/// count and canvas size. Calling @c previewAt re-runs the algorithm at
/// the requested window count; calling it on a manual layout is a thin
/// lookup that ignores the parameter.
///
/// Sources are expected to be cheap to query — the layout-picker UI may
/// call @c availableLayouts after every layout-set change, and
/// @c previewAt once per visible row. Sources that need expensive work
/// (e.g. running a JS algorithm) should cache results internally rather
/// than expecting consumers to debounce.
class PHOSPHORLAYOUTAPI_EXPORT ILayoutSource
{
public:
    virtual ~ILayoutSource();

    /// Enumerate every layout this source can render. The returned
    /// previews are populated enough for the picker UI to render rows
    /// (id, displayName, aspect-ratio class, autotile flag, optional
    /// algorithm metadata). For autotile entries the @c zones field is
    /// populated with a default-window-count preview; consumers wanting
    /// a different count call @c previewAt with the entry's @c id.
    virtual QVector<LayoutPreview> availableLayouts() const = 0;

    /// Produce a fully-realised preview for one layout entry.
    ///
    /// @p id           the LayoutPreview::id from @c availableLayouts
    /// @p windowCount  for autotile entries, the window count to render
    ///                 the algorithm with. Ignored by manual sources
    ///                 (their zones are authored statically). Defaults
    ///                 to a sensible reference count (4 — most picker
    ///                 thumbnails fit this nicely).
    /// @p canvas       optional aspect-ratio + size hint. Sources that
    ///                 implement aspect-ratio filtering use this to set
    ///                 @c LayoutPreview::recommended; sources that don't
    ///                 ignore it. An empty QSize disables the hint.
    ///
    /// Returns a default-constructed preview (empty @c id) when @p id is
    /// not known to this source. Caller checks `result.id.isEmpty()`.
    virtual LayoutPreview previewAt(const QString& id, int windowCount = DefaultPreviewWindowCount,
                                    const QSize& canvas = {}) const = 0;

protected:
    ILayoutSource() = default;

    // Non-copyable — sources are typically owned services that expose
    // pointers to consumers. Avoid accidental slicing.
    ILayoutSource(const ILayoutSource&) = delete;
    ILayoutSource& operator=(const ILayoutSource&) = delete;
};

} // namespace PhosphorLayout
