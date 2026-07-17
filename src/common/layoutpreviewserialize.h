// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

// Canonical serialisation of PhosphorLayout::LayoutPreview to the QML /
// D-Bus wire shapes.  Both @c toVariantMap and @c toJson emit the SAME
// shape (member-for-member mirror of the LayoutPreview struct); they
// differ only in container type.  There is no second QML-legacy or D-Bus-
// legacy shape — the struct is the single source of truth.
//
// Consumers:
//   * SettingsController — localLayoutPreviews(): QML
//   * LayoutAdaptor — getLayoutList / getLayoutPreviewList: D-Bus JSON
//
// One caller adds keys ON TOP of this projection. LayoutAdaptor::getLayoutList
// enriches each entry with Layout state that LayoutPreview does not carry, so
// the shape observable on THAT method is a strict superset of the canonical one
// below — same projection plus an enrichment layer, not a separate projection.
// The enrichment (LayoutAdaptor::getLayoutList, the authoritative list):
//   * manual entries — hasSystemOrigin, hiddenFromSelector, defaultOrder (only
//     when set), allowedScreens / allowedDesktops / allowedActivities (each
//     only when non-empty)
//   * autotile entries — hiddenFromSelector, read from the sidecar
// getLayoutPreviewList / getLayoutPreview do NOT enrich: they are this
// projection verbatim. A consumer that reads an enrichment-only key off a
// preview from any other producer gets `undefined`, not `false`.
//
// Canonical shape (all keys always present unless marked optional):
//   id, displayName, description?, zoneCount, zones[], isAutotile, category,
//   isSystem, recommended, autoAssign, aspectRatioClass (string tag: "any" /
//   "standard" / "ultrawide" / "super-ultrawide" / "portrait"),
//   referenceAspectRatio?, sectionKey?, sectionLabel?, sectionOrder?
//
// `category` is the numeric PhosphorZones::LayoutCategory mirror of the
// `isAutotile` boolean (0 = Manual, 1 = Autotile). Both are emitted
// unconditionally because QML consumers are split: LayoutCard / CategoryBadge
// read the enum, LayoutComboBox reads `category` first and only falls back to
// `isAutotile` for objects from other producers.
//
// When the preview backs an autotile algorithm (@c isAutotile == true)
// the algorithm metadata fields are flattened into the SAME top-level
// object (not a nested `algorithm` sub-object):
//   supportsMasterCount, supportsSplitRatio, producesOverlappingZones,
//   supportsCustomParams, supportsMemory, reflowsOnResize, supportsScriptState,
//   supportsSingleWindow, reflowsOnFocus, isScripted, isUserScript,
//   zoneNumberDisplay?, masterCount
//
// `masterCount` is the resolved count the zones were computed with (a
// LayoutPreview field, not algorithm metadata), so a renderer marking the
// leading zones as masters agrees with the geometry.
//
// Rationale: QML delegates bind to flat scalar properties naturally;
// nesting adds one `.algorithm.` step everywhere without buying
// disambiguation (manual previews simply lack the flag fields).
// Consumers that care about system-entry classification read the
// top-level @c isSystem (populated by the producer at source-build time
// — AutotileLayoutSource derives it from `!isScripted || !isUserScript`,
// ZonesLayoutSource from @c Layout::isSystemLayout).
//
// Each zone in `zones` is flat: {x, y, width, height, zoneNumber}.

#include "plasmazones_export.h"

#include <QJsonObject>
#include <QVariantList>
#include <QVariantMap>
#include <QVector>

namespace PhosphorLayout {
struct LayoutPreview;
}

namespace PlasmaZones {

/// QML-facing QVariantMap projection.
PLASMAZONES_EXPORT QVariantMap toVariantMap(const PhosphorLayout::LayoutPreview& preview);

/// D-Bus wire-format QJsonObject projection. Same shape as toVariantMap.
PLASMAZONES_EXPORT QJsonObject toJson(const PhosphorLayout::LayoutPreview& preview);

/// Convert a list of previews to a QVariantList of toVariantMap results.
PLASMAZONES_EXPORT QVariantList toVariantList(const QVector<PhosphorLayout::LayoutPreview>& previews);

} // namespace PlasmaZones
