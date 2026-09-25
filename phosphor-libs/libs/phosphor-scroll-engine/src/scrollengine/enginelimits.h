// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <QtGlobal>

namespace PhosphorScrollEngine {

/// KEEP cap: how many entries this engine will retain out of ONE
/// template-channel list in the per-screen override map (the two preset
/// vocabularies and the seed blueprint). Daemon-side, ScrollingTemplate's
/// normalize caps all three lists to the same value before the push, so a
/// template that came through the store can never exceed it. But
/// applyPerScreenConfig is exported LGPL surface, and the map it is
/// handed is stored verbatim, so the cap is what bounds the work the engine
/// does with an embedder-supplied list. For the preset vocabularies that is a
/// per-relayout walk: presetListFromOverride re-parses and copies the raw list
/// every time a screen resolves its params, so an unbounded list would be
/// walked on every relayout. The blueprint is not walked at all — consumption
/// indexes straight into the stored list by column count — so there the cap
/// bounds how many columns the engine will SEED from a template rather than any
/// traversal.
/// KEEP IN SYNC with the settings validator's kMaxPresetEntries
/// (settingsschema_p.h) and PhosphorZones::MaxTemplateColumns
/// (ScrollingTemplate.h), both hand-mirrored like MinColumnWidthFraction.
///
/// Shared between the resolvers (engine_overrides.cpp) and the blueprint
/// consumption on the fresh-open path (engine_lifecycle.cpp), which is why it
/// lives in an internal header rather than either TU's anonymous namespace.
inline constexpr int kMaxTemplateEntries = 16;

/// SCAN cap: how many raw entries a preset-vocabulary parse will examine at
/// all. The keep cap above bounds what SURVIVES, not the work — a list of ten
/// thousand rejects never fills the output, so every one of them would be
/// converted on every relayout. This is the guard for raw embedder-supplied
/// maps that never went through the daemon-side normalize. Note it bounds the
/// loop only: for a variant that is not already a QVariantList, toList() has
/// materialized the whole thing before the cap is applied. Same reasoning and
/// same value as the settings validator's kMaxPresetScan (settingsschema_p.h).
inline constexpr int kMaxTemplateScan = 256;

/// Restore caps: persisted strip blobs are a user-writable system boundary
/// (the file's own header calls them that), and restoreStripState bounds
/// every NUMERIC field it reads — these bound the COUNTS the same way. A
/// hand-edited or corrupted blob with a huge array would otherwise be
/// staged verbatim into the stash, and restoreFromStripStash walks the
/// staged columns/tiles on every window open for that key until the
/// entries age out over three sessions. The column/tile caps sit far above
/// any real strip; the key cap has to clear the full screens x desktops
/// (KDE allows 20) x activities product, so it is sized for the corrupt
/// blob (thousands of keys), not the power user — 512 covers 4 outputs x
/// 20 desktops x 6 activities with headroom. Skipped keys (stash-exists /
/// live-strip continues) do not consume it, it bounds ONE restore call
/// (loadState may run more than once; the stash-exists skip covers the
/// repeat-blob case), and drops are logged, never silent.
inline constexpr int kMaxRestoredKeys = 512;

/// How long after a stash entry is staged (login restore) or its screen
/// re-enters scrolling (mode round trip) the cross-session appId FUZZY
/// claim stays open — see StashedStrip::fuzzyClaimWindow. Re-announce
/// waves land within seconds of either event; the bound exists for the
/// windows that are NOT part of a wave: seen live, a stash holding several
/// firefox/ghostty tiles from a previous session kept hijacking every new
/// window of those apps for the entire session — inserted off-view at a
/// dead sibling's position, without focus. Sized generously above any
/// observed wave (< 2 s) so a slow-launching session-restored app still
/// reclaims its slot on a cold login.
inline constexpr qint64 kFuzzyClaimGraceMs = 60'000;
inline constexpr int kMaxRestoredColumnsPerKey = 64;
inline constexpr int kMaxRestoredTilesPerColumn = 32;

/// Sanity ceiling for a PIXEL extent minted from an untrusted qreal (the
/// per-screen override map, which applyPerScreenConfig stores verbatim, and
/// the injected ISettings an embedder implements). Both channels reach
/// ColumnWidth::makeFixed / WindowHeight::makeFixed through a qRound, and
/// qRound of a double outside int's range is undefined — infinity and 1e300
/// are values an embedder can hand over. Bounding before the round makes the
/// conversion total. Deliberately far above any real monitor extent: this
/// rejects the malformed, it is not a layout constraint.
inline constexpr double kMaxFixedExtentPx = 65535.0;

/// Sanity bounds for tab-indicator geometry. Deliberately NOT the rules
/// layer's constants — this library does not depend on phosphor-rules — but
/// the same numbers, because both ultimately mirror the config schema's
/// clamps. They exist to reject a grossly malformed value, not to
/// re-implement the cascade's validation, so they are wide.
///
/// Shared between the per-screen override resolver (engine_overrides.cpp) and
/// the settings read (engine_core.cpp), which is why they live here rather
/// than in either TU's anonymous namespace. BOTH channels are untrusted for
/// the same reason: the override map is stored verbatim from exported LGPL
/// surface, and IScrollSettings is an injected interface an embedder
/// implements, so neither is guaranteed to have passed the config schema.
/// Sharing them is what keeps the two sites from drifting apart.
inline constexpr int kMinTabIndicatorGap = -64;
inline constexpr int kMaxTabIndicatorGap = 64;
inline constexpr int kMinTabIndicatorWidth = 1;
inline constexpr int kMaxTabIndicatorWidth = 64;
/// The length proportion's pair, named for the same reason its four siblings
/// above are rather than living as bare literals at the read: the floor is a
/// real decision (a proportion small enough to resolve to a sliver reads as a
/// broken indicator while every setting still reports it on), and hiding it in
/// a `> 0.0` test made it look like a null check. Mirrors the rules layer's
/// Min/MaxTabIndicatorLengthRatio by value, hand-copied on the same terms as
/// the bounds above.
inline constexpr qreal kMinTabIndicatorLengthProportion = 0.05;
inline constexpr qreal kMaxTabIndicatorLengthProportion = 1.0;

} // namespace PhosphorScrollEngine
