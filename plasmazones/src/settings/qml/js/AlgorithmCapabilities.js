// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
.pragma library

// Shared algorithm-capability lookup for every surface that renders or
// configures a tiling algorithm (TilingAlgorithmPage, TilingSimplePage and
// AlgorithmPreview).
//
// Both pages need the same answers about the selected algorithm, and both used
// to implement this scan independently — including a hardcoded
// "three-column || centered-master" fallback for centerLayout, in two places.
// That is domain knowledge about specific algorithms living in QML, duplicated,
// and it drifts the moment a new centre-layout algorithm ships. The C++
// catalog already carries every capability flag (see algorithmservice.cpp,
// which sets centerLayout unconditionally from the algorithm), so the id-based
// fallback only ever fired in the window before availableAlgorithms populates,
// where a stale guess is worse than false.

/// The catalog entry for `algoId`, or null. `algos` is the cached
/// availableAlgorithms list the page already holds.
function capabilitiesFor(algos, algoId) {
    if (!algos || !algoId)
        return null;
    for (var i = 0; i < algos.length; ++i) {
        if (algos[i].id === algoId)
            return algos[i];
    }
    return null;
}

/// Capability flag readers. Each defaults to false for an unknown algorithm —
/// an algorithm the catalog does not describe supports nothing, which is the
/// safe direction (a hidden control beats one that writes a key the algorithm
/// ignores).
function supportsSplitRatio(caps) {
    return caps ? caps.supportsSplitRatio === true : false;
}

function supportsMasterCount(caps) {
    return caps ? caps.supportsMasterCount === true : false;
}

function supportsCustomParams(caps) {
    return caps ? caps.supportsCustomParams === true : false;
}

/// True when the algorithm's zones deliberately overlap, which turns off the
/// preview's inter-zone gaps so the stack reads as intended.
function producesOverlappingZones(caps) {
    return caps ? caps.producesOverlappingZones === true : false;
}

/// True when the algorithm's primary area is a CENTRE column rather than a
/// master area, which changes the ratio/count row labels. Read straight from
/// the catalog — never inferred from the algorithm id.
function centerLayout(caps) {
    return caps ? caps.centerLayout === true : false;
}

/// The ratio a preview should draw when the algorithm exposes no ratio control
/// of its own. Catalog-supplied, so a new algorithm gets its own default
/// without either page learning about it. Every catalog entry carries the
/// field, so `undefined` means the algorithm is not in the catalog at all —
/// the same "we know nothing" answer the flag readers give as false, rather
/// than a QML-side guess at a C++ default. Call sites pick their own fallback
/// for that case; none of them may bind this straight into a `real`.
function defaultSplitRatio(caps) {
    return caps ? caps.defaultSplitRatio : undefined;
}

/// The algorithm's own prose description, empty when the catalog has no entry
/// for it. Every surface that shows the description picks its own answer for
/// the empty case: the pages leave the caption blank, the wizard falls back to
/// its curated string.
function description(caps) {
    return (caps && caps.description) || "";
}

/// Which zone numbers the preview labels. "all" when the catalog is silent,
/// matching the preview's own default.
function zoneNumberDisplay(caps) {
    return (caps && caps.zoneNumberDisplay) || "all";
}
