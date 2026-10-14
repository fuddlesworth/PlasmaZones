// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <phosphorzones_export.h>
#include <QFlags>
#include <QHash>
#include <QJsonObject>
#include <QList>
#include <QRectF>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>
#include <QVector>

namespace PhosphorZones {

class IZoneLayoutRegistry;
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
 * together with autotile algorithms lives in unifiedlayoutlist.h).
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
 * whole layout as a single map without going through LayoutPreview.
 */
PHOSPHORZONES_EXPORT QVariantMap layoutToVariantMap(Layout* layout, ZoneFields zoneFields = ZoneField::Minimal);

PHOSPHORZONES_EXPORT void sortZonesByNumber(QVector<Zone*>& zones);
PHOSPHORZONES_EXPORT QHash<QString, int> buildZonePositionMap(Layout* layout);

/**
 * @brief Build a global zoneId → position map merged across every layout in @p layouts.
 *
 * Zone UUIDs are unique across all layouts in the registry (its directory load
 * and import re-mint a copied one, reissueTakenZoneIds), so the merge is
 * unambiguous. Used by the resnap path (lifecycle.cpp + resnap.cpp) when a
 * window's stored zoneId may have come from any layout — for example, after
 * a promote edit shifts the cascade winner from a per-context layout to the
 * screen-level slot.
 */
PHOSPHORZONES_EXPORT QHash<QString, int> buildGlobalZonePositionMap(const QList<Layout*>& layouts);

/**
 * @brief Does @p layout hold EVERY zone in @p zoneIds?
 *
 * Zone UUIDs are unique across all layouts, so a remembered zone id names
 * one layout for good: a zone remembered under the layout a context USED to
 * run resolves geometry just fine against the layout it belongs to, which is
 * how a window came back into a "ghost" layout on a desktop that had since
 * been given another one (discussion #1104). Every path that re-applies a
 * remembered zone into a (screen, desktop, activity) context asks this of the
 * layout the registry resolves for THAT context before trusting the id.
 *
 * A null layout or an empty list answers false: there is nothing to snap into.
 */
PHOSPHORZONES_EXPORT bool layoutHoldsZones(const Layout* layout, const QStringList& zoneIds);

/**
 * @brief layoutHoldsZones() against the layout @p registry resolves for the
 *        (@p screenId, @p virtualDesktop, @p activity) context.
 *
 * Permissive (true) with a null registry, matching the unit-test path where
 * an engine runs without one; a context that resolves no layout answers false.
 */
PHOSPHORZONES_EXPORT bool contextLayoutHoldsZones(const IZoneLayoutRegistry* registry, const QString& screenId,
                                                  int virtualDesktop, const QString& activity,
                                                  const QStringList& zoneIds);

/**
 * @brief The slot remembered @p zoneIds take in @p target, the layout a context
 *        runs now: the zones themselves when @p target holds them all (a span
 *        stays a span), else @p target's zone at the position the first one has
 *        in its own layout (@p positions, buildGlobalZonePositionMap over the
 *        registry's layouts), else empty when @p target has fewer zones or none.
 *
 * The one same-numbered re-slot of a layout change: the snap engine's desktop
 * carry (a window moved to a desktop running another layout) and the
 * layout-switch populate (a minimized window's pre-float zones) share it.
 */
PHOSPHORZONES_EXPORT QStringList sameNumberedZones(const QStringList& zoneIds, const Layout* target,
                                                   const QHash<QString, int>& positions);

/**
 * @brief Give a layout document's zones ids no other layout holds (F552).
 *
 * Every zone id in @p layoutJson's zones array that a layout in @p loaded
 * already holds, or that repeats within the document, is replaced. A layout
 * with the document's own id is not another layout: a user copy overriding a
 * system layout shares its zone ids. The new id is a name-based UUID of the
 * layout id, the old zone id and its index, so the same document gets the same
 * ids on every load even before the caller writes it back, re-derived with a
 * counter while the result is taken or already in the document. A document
 * whose id is not a UUID derives from a random namespace instead, as
 * Layout::fromJson mints it a random id. Returns the number of ids replaced.
 *
 * The registry's directory load and its import run every document through
 * this before Layout::fromJson, which is what makes the uniqueness the
 * position map and layoutHoldsZones rely on hold.
 */
PHOSPHORZONES_EXPORT int reissueTakenZoneIds(QJsonObject& layoutJson, const QList<Layout*>& loaded);

} // namespace LayoutUtils

} // namespace PhosphorZones
