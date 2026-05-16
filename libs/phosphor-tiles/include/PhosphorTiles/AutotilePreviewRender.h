// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <phosphortiles_export.h>

#include <PhosphorLayoutApi/LayoutPreview.h>

#include <QSize>
#include <QString>

namespace PhosphorTiles {
class ITileAlgorithmRegistry;
class TilingAlgorithm;
} // namespace PhosphorTiles

namespace PhosphorTiles {

/// Convert a single TilingAlgorithm into a renderer-ready LayoutPreview.
///
/// Pure projection that runs the algorithm at @p windowCount windows
/// against a canvas, normalises the resulting zone rects to 0..1
/// space, and packages them with the algorithm's display metadata.
///
/// @p algorithmId is the registry id for @p algorithm — embedded into the
/// returned preview as `"autotile:<id>"`.
///
/// @p registry — tile-algorithm registry consulted for
///               @c previewParams() (the user-configured master-count /
///               split-ratio / per-algorithm saved settings). Must be
///               non-null; composition roots inject the registry they
///               own.
///
/// @p canvasSize — preview canvas dimensions. Default-empty (or
///                 non-positive on either axis) means "use a square
///                 canvas" — appropriate for generic algorithm thumbnails
///                 shown in screen-agnostic UIs (settings list, layout
///                 picker). Aspect-aware callers (e.g. the per-screen OSD
///                 preview) MUST pass the target screen's size so that
///                 algorithms whose split decisions depend on aspect
///                 ratio (BSP, fibonacci, …) produce a preview matching
///                 what the live tiler will actually render. Only the
///                 ratio matters; output zones are normalised regardless.
///
/// Provided as a free function so consumers that already hold a
/// TilingAlgorithm* can build a preview without going through
/// AutotileLayoutSource (mirrors PhosphorZones::previewFromLayout).
PHOSPHORTILES_EXPORT PhosphorLayout::LayoutPreview
previewFromAlgorithm(const QString& algorithmId, PhosphorTiles::TilingAlgorithm* algorithm, int windowCount,
                     PhosphorTiles::ITileAlgorithmRegistry* registry, QSize canvasSize = {});

/// Convenience overload that reads the algorithm's stable id from
/// @c TilingAlgorithm::registryId() (populated by
/// @c AlgorithmRegistry::registerAlgorithm). Bails with a warning if the
/// algorithm is not currently registered (empty registryId() — a preview
/// has no stable id to reference). The supplied @p registry is consulted
/// only for preview-params resolution, not for an id reverse-lookup —
/// every call is O(1) regardless of registry size.
PHOSPHORTILES_EXPORT PhosphorLayout::LayoutPreview previewFromAlgorithm(PhosphorTiles::TilingAlgorithm* algorithm,
                                                                        int windowCount,
                                                                        PhosphorTiles::ITileAlgorithmRegistry* registry,
                                                                        QSize canvasSize = {});

} // namespace PhosphorTiles
