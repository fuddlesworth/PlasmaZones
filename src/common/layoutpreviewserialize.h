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
//   * EditorController / SettingsController — localLayoutPreviews(): QML
//   * LayoutAdaptor — getLayoutList / getLayoutPreviewList: D-Bus JSON
//
// Canonical shape (all keys always present unless marked optional):
//   id, displayName, description?, zoneCount, zones[], isAutotile, isSystem,
//   recommended, autoAssign, aspectRatioClass (string tag: "any" /
//   "standard" / "ultrawide" / "super-ultrawide" / "portrait"),
//   referenceAspectRatio?, sectionKey?, sectionLabel?, sectionOrder?
//
// When the preview backs an autotile algorithm (@c isAutotile == true)
// the algorithm metadata fields are flattened into the SAME top-level
// object (not a nested `algorithm` sub-object):
//   supportsMasterCount, supportsSplitRatio, producesOverlappingZones,
//   supportsCustomParams, supportsMemory, isScripted, isUserScript,
//   zoneNumberDisplay?
//
// Rationale: QML delegates bind to flat scalar properties naturally;
// nesting adds one `.algorithm.` step everywhere without buying
// disambiguation (manual previews simply lack the flag fields).
// Consumers that care about system-entry classification read the
// top-level @c isSystem (populated by the producer via
// @c AlgorithmMetadata::isSystemEntry for autotile previews, and via
// @c Layout::isSystemLayout for manual previews).
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
