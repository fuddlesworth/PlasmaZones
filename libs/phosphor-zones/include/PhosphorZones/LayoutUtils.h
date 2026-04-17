// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <phosphorzones_export.h>
#include <QFlags>
#include <QJsonObject>
#include <QList>
#include <QRectF>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

namespace PlasmaZones {

class Layout;
class Zone;

/**
 * @brief Flags controlling which zone fields to include in conversion (OCP-compliant)
 *
 * These flags allow callers to request minimal or full zone data without duplicating
 * conversion logic. Use Minimal for preview thumbnails, Full for overlay rendering.
 */
enum class ZoneField {
    None = 0,
    Name = 1 << 0, ///< Include zone name
    Appearance = 1 << 1, ///< Include colors, opacities, border properties

    Minimal = None, ///< Id, ZoneNumber, RelativeGeometry only (for previews)
    Full = Name | Appearance ///< All fields (for overlay rendering)
};
Q_DECLARE_FLAGS(ZoneFields, ZoneField)
Q_DECLARE_OPERATORS_FOR_FLAGS(ZoneFields)

/**
 * @brief Zone/layout primitive utilities — pure Layout / Zone operations.
 *
 * This header carries only code that operates on Layout and Zone types and
 * has no dependency on autotile, settings services, or higher-layer
 * composition (the picker-UI aggregation that stitches manual layouts
 * together with autotile algorithms lives in unifiedlayoutentry.h).
 * Keeping that split lets these primitives eventually live in a standalone
 * phosphor-zones library without dragging autotile or settings services
 * into the dependency graph.
 */
namespace LayoutUtils {

// ═══════════════════════════════════════════════════════════════════════════
// Allow-list serialization (shared by Layout, LayoutAdaptor, EditorController)
// ═══════════════════════════════════════════════════════════════════════════

/**
 * @brief Serialize visibility allow-lists to JSON (only writes non-empty lists)
 */
PHOSPHORZONES_EXPORT void serializeAllowLists(QJsonObject& json, const QStringList& screens, const QList<int>& desktops,
                                              const QStringList& activities);

/**
 * @brief Deserialize visibility allow-lists from JSON (clears output params first)
 */
PHOSPHORZONES_EXPORT void deserializeAllowLists(const QJsonObject& json, QStringList& screens, QList<int>& desktops,
                                                QStringList& activities);

// ═══════════════════════════════════════════════════════════════════════════
// Zone conversion utilities
// ═══════════════════════════════════════════════════════════════════════════

/**
 * @brief Convert all zones in a layout to QVariantList
 *
 * @param layout The layout containing zones (returns empty list if null)
 * @param fields Which fields to include for each zone
 * @param referenceGeometry Screen geometry for normalizing fixed zones (empty = use raw relativeGeometry)
 * @return QVariantList of zone maps
 */
PHOSPHORZONES_EXPORT QVariantList zonesToVariantList(Layout* layout, ZoneFields fields = ZoneField::Minimal,
                                                     const QRectF& referenceGeometry = QRectF());

/**
 * @brief Convert a layout to a QVariantMap (metadata + zones)
 *
 * Used by LayoutAdaptor for D-Bus and other consumers that want the
 * whole layout as a single map without building a UnifiedLayoutEntry.
 */
PHOSPHORZONES_EXPORT QVariantMap layoutToVariantMap(Layout* layout, ZoneFields zoneFields = ZoneField::Minimal);

} // namespace LayoutUtils

} // namespace PlasmaZones
