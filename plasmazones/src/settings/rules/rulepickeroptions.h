// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QString>
#include <QVariantList>
#include <QVector>

namespace PhosphorZones {
class Layout;
}

/// The option lists behind the rule editor's value pickers that are built from
/// live state rather than from the rule vocabulary: the Zone condition's zones
/// and the running-windows picker's rows. Free functions over plain inputs, so
/// each is unit-testable without a SettingsController or a daemon.
namespace PlasmaZones::RulePickerOptions {

/// One Zone-condition option per zone of every layout in @p layouts. Each entry
/// is `{ id, displayName, zoneName, layoutName, layoutOrder }`:
///   - `id` is the zone UUID with braces (QUuid::toString()), the form the
///     daemon and the KWin effect stamp into a window's Zone rule field;
///   - `zoneName` is the zone's name, or "Zone <number>" when it has none;
///   - `layoutName` is the layout's name, or "Unnamed layout" when it has none;
///   - a layout name that another layout already took, ignoring case, gets
///     a "(2)", "(3)" suffix, and so does a zone name repeated within one
///     layout, so no two options read alike;
///   - `displayName` is "<layoutName> — <zoneName>", the label the picker and
///     the rule list both show;
///   - `layoutOrder` is the layout's position, so a picker grouping by layout
///     keeps this order.
/// Layouts are ordered the way the settings app's layout pickers order them
/// (by name, ignoring case; see SettingsController::sortMergedLayoutList), and
/// the zones of one layout by zone number. Null layouts and zones are skipped.
QVariantList zoneMatchOptions(const QVector<PhosphorZones::Layout*>& layouts);

/// Parse the running-windows JSON payload the KWin effect sends (relayed by the
/// daemon) into `{ windowClass, rawWindowClass, appName, caption, desktopFile }`
/// maps for the picker. A payload that is not a JSON array gives an empty list
/// and a warning, and a row that is not an object is skipped. A row from an
/// effect older than `rawWindowClass` gets its `windowClass` there instead, so
/// Classes mode keeps working until the next login loads the new effect.
QVariantList runningWindows(const QString& json);

} // namespace PlasmaZones::RulePickerOptions
