// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <phosphorlayoutapi_export.h>

#include <PhosphorLayoutApi/AlgorithmMetadata.h>

#include <QRectF>
#include <QString>
#include <QVector>

#include <optional>

namespace PhosphorLayout {

/// Reference window count used by `ILayoutSource::previewAt` when the caller
/// doesn't specify one. Picked so most picker thumbnails render a visually
/// representative layout without visiting the algorithm's empty state.
inline constexpr int DefaultPreviewWindowCount = 4;

/// Renderer-ready snapshot of one layout entry (manual zone layout OR autotile
/// algorithm result). Plain data — no Qt object lifecycle, no signals.
///
/// Both phosphor-zones (manual `Layout` -> preview) and the future
/// phosphor-tile-algo (autotile algorithm @ N windows -> preview) produce
/// values of this type. Editor / settings / overlay code consumes
/// LayoutPreview uniformly without branching on the underlying source —
/// the only renderer-relevant difference between manual and autotile is
/// whether @c isAutotile is set (used for badge / icon styling).
///
/// Coordinate system: every QRectF in @c zones is in 0.0–1.0 relative
/// space, ready to be scaled into any pixel rectangle. For autotile entries
/// the "preview" geometry is computed for some specific window count (see
/// @c ILayoutSource::previewAt) — different counts yield different
/// previews from the same algorithm.
struct PHOSPHORLAYOUTAPI_EXPORT LayoutPreview
{
    /// Stable identifier for this layout entry. For manual layouts this is
    /// the layout's UUID string (with braces); for autotile entries it's
    /// the prefixed form `"autotile:<algorithmId>"` so manual + autotile
    /// IDs share a single namespace at the consumer level.
    QString id;

    /// Human-readable name for the picker UI (i18n'd by the source).
    QString displayName;

    /// Optional longer description shown in tooltips / detail views.
    QString description;

    /// Zone rectangles in 0.0–1.0 relative coordinates. Renderer scales
    /// these into the preview canvas. Order matches @c zoneNumbers.
    QVector<QRectF> zones;

    /// Per-zone display label. Same length as @c zones (or empty when the
    /// source doesn't number its zones — autotile algorithms often emit
    /// just the count). Numbering is 1-based; consumer renders the literal
    /// integer.
    QVector<int> zoneNumbers;

    /// Number of zones in the preview. Identical to @c zones.size() for
    /// finished previews — kept as an explicit field because some autotile
    /// algorithms expose a logical "this layout supports N windows" value
    /// distinct from the number of preview rectangles (e.g. unlimited
    /// algorithms use a sentinel here while @c zones renders a fixed
    /// example geometry).
    int zoneCount = 0;

    /// True when the layout matches the rendering canvas's aspect ratio
    /// well enough to be a "recommended" pick. False entries can still
    /// render (they just appear in a collapsed "Other" section in the
    /// picker). Sources fill this when called with an aspect-ratio hint;
    /// otherwise leave the default (true) so unranked previews show
    /// normally.
    bool recommended = true;

    /// For fixed-geometry manual layouts: the reference aspect ratio the
    /// zones were authored for. Renderer uses this so the preview tile
    /// shows the layout in its native aspect rather than stretched. Zero
    /// when the layout has no fixed-geometry zones (relative layouts
    /// adapt to any aspect).
    qreal referenceAspectRatio = 0.0;

    /// Aspect-ratio class hint propagated up from the layout source.
    /// 0 = "Any monitor", 1 = "Standard 16:9", 2 = "Ultrawide 21:9",
    /// 3 = "Super-Ultrawide 32:9", 4 = "Portrait 9:16". Picker uses this
    /// for section grouping; renderer ignores. Manual layouts source this
    /// from their AspectRatioClass tag; autotile entries leave it at 0.
    int aspectRatioClass = 0;

    /// Section-grouping metadata for the picker UI. Sources fill these
    /// to drive grouped headers ("Built-in", "Custom", "Standard 16:9",
    /// etc.). All optional; empty values mean "no section header".
    QString sectionKey;
    QString sectionLabel;
    int sectionOrder = 0;

    /// True when new windows should auto-fill the first empty zone (manual
    /// layouts only — the autotile equivalent is implicit). Drives
    /// auto-snap behaviour at the daemon side; renderer ignores.
    bool autoAssign = false;

    /// True when the layout is "system-owned" and should render with a
    /// lock badge in the picker. For manual layouts this reflects whether
    /// the layout file came from the system install (vs. a user-created
    /// copy). For autotile layouts it mirrors
    /// @c AlgorithmMetadata::isSystemEntry (built-in C++ algorithms and
    /// system-installed scripts = system; user scripts = not system).
    /// Sources populate this at preview construction — consumers treat
    /// it as authoritative.
    bool isSystem = false;

    /// Optional autotile algorithm metadata. Presence is the sole signal
    /// that this preview backs an autotile algorithm rather than a static
    /// manual layout — @c isAutotile() reads the optional's has_value().
    /// Picker reads the metadata for capability flags (supports master
    /// count / split ratio editors, lock badge, etc.).
    std::optional<AlgorithmMetadata> algorithm;

    /// True when this preview backs an autotile algorithm (equivalent to
    /// `algorithm.has_value()`). Consumers branch on this to toggle UI
    /// affordances like the system-vs-user badge and algorithm-specific
    /// parameter editors. The invariant `isAutotile() == algorithm.has_value()`
    /// holds by construction — the flag is computed, not stored.
    bool isAutotile() const noexcept
    {
        return algorithm.has_value();
    }
};

} // namespace PhosphorLayout
