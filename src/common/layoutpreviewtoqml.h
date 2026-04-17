// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

// Translation between PhosphorLayout::LayoutPreview (the library type) and
// the QVariantMap shape that PlasmaZones' QML expects from the legacy
// LayoutAdaptor::getLayoutList path.
//
// Used by both EditorController::localLayoutPreviews and
// SettingsController::localLayoutPreviews so QML preview-rendering call
// sites (LayoutThumbnail, ZonePreview, LayoutsPage, etc.) can switch from
// the D-Bus m_layouts to localLayoutPreviews() without QML changes.
//
// The QML shape has accumulated a few quirks over time that this helper
// reproduces verbatim:
//   * "name"  not "displayName"
//   * "aspectRatioClass" as STRING ("any" / "standard" / ... via
//     ScreenClassification::toString) not the enum int
//   * Each zone carries its rect in a NESTED "relativeGeometry" object,
//     not flat x/y/width/height keys
//   * Autotile algorithm capability flags (supportsMasterCount,
//     producesOverlappingZones, zoneNumberDisplay, etc.) live as flat
//     top-level keys on the entry, not under an "algorithm" sub-object

#include "plasmazones_export.h"

#include <QVariantMap>

namespace PhosphorLayout {
struct LayoutPreview;
}

namespace PlasmaZones {

/// Convert a single LayoutPreview into the QML-compatible QVariantMap.
PLASMAZONES_EXPORT QVariantMap layoutPreviewToQmlMap(const PhosphorLayout::LayoutPreview& preview);

} // namespace PlasmaZones
