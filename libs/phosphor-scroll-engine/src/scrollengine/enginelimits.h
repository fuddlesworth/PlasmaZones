// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

namespace PhosphorScrollEngine {

/// Cap on how many entries this engine will read out of ONE template-channel
/// list in the per-screen override map (the two preset vocabularies and the
/// seed blueprint). The daemon's extractor caps its own output, but
/// applyPerScreenConfig is exported LGPL surface: an embedder-supplied map
/// must not make every relayout copy — or every open walk — an unbounded
/// list. KEEP IN SYNC with the settings validator's kMaxPresetEntries
/// (hand-mirrored, like MinColumnWidthFraction).
///
/// Shared between the resolvers (engine_overrides.cpp) and the blueprint
/// consumption on the fresh-open path (engine_lifecycle.cpp), which is why it
/// lives in an internal header rather than either TU's anonymous namespace.
inline constexpr int kMaxTemplateEntries = 16;

} // namespace PhosphorScrollEngine
