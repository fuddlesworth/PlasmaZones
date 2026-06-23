// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <PhosphorRendering/phosphorrendering_export.h>

#include <QImage>
#include <QMetaType>
#include <QPoint>
#include <QSize>
#include <QVector>

namespace PhosphorRendering {

/// One rendered zone-label glyph (number) plus where its top-left sits in the
/// full, screen-addressed labels texture.
struct ZoneLabelTile
{
    QImage image; ///< Small ARGB32-premultiplied glyph tile (glyph bounds + outline margin).
    QPoint dest; ///< Top-left of @ref image within the screen-addressed labels texture.

    bool operator==(const ZoneLabelTile& other) const
    {
        // dest first: a cheap mismatch short-circuits the deep QImage pixel
        // compare in the common "tiles moved" case.
        return dest == other.dest && image == other.image;
    }
    bool operator!=(const ZoneLabelTile& other) const
    {
        return !(*this == other);
    }
};

/// Sparse zone-labels payload: the full screen-addressed texture size plus only
/// the glyph tiles that actually carry a number.
///
/// Zone numbers occupy a tiny fraction of the overlay, so a full-screen
/// ARGB32 image (~23 MB at 4K) is ~99% transparent. Carrying that image through
/// the QML `labelsTexture` binding chain pins tens of MB of CPU heap for the
/// overlay's lifetime. This payload carries only the glyph tiles (a few hundred
/// KB); ZoneShaderNodeRhi composites them into its screen-sized GPU texture at
/// upload time, so the `uZoneLabels` sampler stays screen-addressed and the
/// effect shaders are unchanged.
struct ZoneLabelTexture
{
    QSize size; ///< Full screen-addressed texture size the tiles composite into.
    QVector<ZoneLabelTile> tiles; ///< Sparse glyph tiles; empty ⇒ no labels (transparent everywhere).

    /// No glyphs to show (numbers disabled, no zones, or degenerate size).
    bool isEmpty() const
    {
        return tiles.isEmpty() || size.isEmpty();
    }

    /// Value equality — lets consumers (e.g. ZoneShaderItem::setLabelsTexture)
    /// skip a redundant change signal + repaint when the payload is unchanged.
    /// size is compared first to short-circuit before the per-tile deep compare.
    bool operator==(const ZoneLabelTexture& other) const
    {
        return size == other.size && tiles == other.tiles;
    }
    bool operator!=(const ZoneLabelTexture& other) const
    {
        return !(*this == other);
    }

    /// Composite the sparse tiles into one full screen-addressed image
    /// (ARGB32-premultiplied). Returns a null image when empty. Used by the
    /// render node for GPU upload and by QImage-consuming preview paths.
    PHOSPHORRENDERING_EXPORT QImage toImage() const;

    /// Wrap a full-size image as a single tile at (0,0). The inverse of
    /// toImage() for the common "I already have a full image" case — used by the
    /// QImage→ZoneLabelTexture metatype converter and by callers (e.g. the
    /// shader-render tool) that still produce a full QImage. A null/empty image
    /// yields an empty payload.
    PHOSPHORRENDERING_EXPORT static ZoneLabelTexture fromImage(const QImage& image);
};

} // namespace PhosphorRendering

Q_DECLARE_METATYPE(PhosphorRendering::ZoneLabelTexture)
