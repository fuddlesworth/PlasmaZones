// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

namespace PhosphorScrollEngine {

/// Cap on how many entries this engine will read out of ONE template-channel
/// list in the per-screen override map (the two preset vocabularies and the
/// seed blueprint). The daemon's extractor caps its own output, but
/// applyPerScreenConfig is exported LGPL surface, and the map it is handed is
/// stored verbatim, so the cap is what bounds the work the engine does with an
/// embedder-supplied list. For the preset vocabularies that is a per-relayout
/// walk: presetListFromOverride re-parses and copies the raw list every time a
/// screen resolves its params, so an unbounded list would be walked on every
/// relayout. The blueprint is not walked at all — consumption indexes straight
/// into the stored list by column count — so there the cap bounds how many
/// columns the engine will SEED from a template rather than any traversal.
/// KEEP IN SYNC with the settings validator's kMaxPresetEntries
/// (hand-mirrored, like MinColumnWidthFraction).
///
/// Shared between the resolvers (engine_overrides.cpp) and the blueprint
/// consumption on the fresh-open path (engine_lifecycle.cpp), which is why it
/// lives in an internal header rather than either TU's anonymous namespace.
inline constexpr int kMaxTemplateEntries = 16;

} // namespace PhosphorScrollEngine
