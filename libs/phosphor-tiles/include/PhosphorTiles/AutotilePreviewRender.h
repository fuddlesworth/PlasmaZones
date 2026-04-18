// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <phosphortiles_export.h>

#include <PhosphorLayoutApi/LayoutPreview.h>

#include <QString>

namespace PhosphorTiles {
class AlgorithmRegistry;
class TilingAlgorithm;
} // namespace PhosphorTiles

namespace PhosphorTiles {

/// Convert a single TilingAlgorithm into a renderer-ready LayoutPreview.
///
/// Pure projection that runs the algorithm at @p windowCount windows
/// against a unit canvas, normalises the resulting zone rects to 0..1
/// space, and packages them with the algorithm's display metadata.
///
/// @p algorithmId is the registry id for @p algorithm — embedded into the
/// returned preview as `"autotile:<id>"`.
///
/// @p registry    — registry consulted for `previewParams()` (the
///                  user-configured master-count / split-ratio / per-algorithm
///                  saved settings). Pass @c nullptr to fall back to
///                  @c AlgorithmRegistry::instance() for backwards compatibility.
///
/// Provided as a free function so consumers that already hold a
/// TilingAlgorithm* can build a preview without going through
/// AutotileLayoutSource (mirrors PhosphorZones::previewFromLayout).
PHOSPHORTILES_EXPORT PhosphorLayout::LayoutPreview
previewFromAlgorithm(const QString& algorithmId, PhosphorTiles::TilingAlgorithm* algorithm,
                     int windowCount = PhosphorLayout::DefaultPreviewWindowCount,
                     PhosphorTiles::AlgorithmRegistry* registry = nullptr);

/// Convenience overload that reverse-looks-up the algorithm id via the
/// supplied registry (or @c AlgorithmRegistry::instance() when @p registry
/// is null). Prefer the id-taking form on hot paths — the reverse lookup
/// is O(N) per call and emits a qCWarning.
PHOSPHORTILES_EXPORT PhosphorLayout::LayoutPreview
previewFromAlgorithm(PhosphorTiles::TilingAlgorithm* algorithm,
                     int windowCount = PhosphorLayout::DefaultPreviewWindowCount,
                     PhosphorTiles::AlgorithmRegistry* registry = nullptr);

} // namespace PhosphorTiles
