// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "plasmazones_export.h"
#include <QString>
#include <QHash>
#include <functional>

namespace PlasmaZones {

class Layout;
class LayoutManager;

/**
 * @brief Factory for creating layouts by type name
 *
 * Replaces the if-else chain in LayoutAdaptor::createLayout() with
 * a registry-based factory pattern. This makes it easier to add new
 * layout types and keeps the creation logic centralized.
 *
 * Usage:
 *   Layout* layout = LayoutFactory::create("grid", manager);
 *   if (!layout) { // handle unknown type }
 */
class PLASMAZONES_EXPORT LayoutFactory
{
public:
    using CreatorFunc = std::function<Layout*(LayoutManager*)>;

    /**
     * @brief Create a layout of the specified type
     * @param type Layout type name (e.g., "columns", "rows", "grid", "priority", "focus", "custom")
     * @param manager Parent layout manager
     * @return Newly created layout, or nullptr if type is unknown
     *
     * Supported types:
     * - "columns": 3-column layout
     * - "rows": 3-row layout
     * - "grid": 2x2 grid layout
     * - "priority": Priority grid layout (large main + smaller sides)
     * - "focus": Focus layout (centered main + surrounding zones)
     * - "custom" or unknown: Empty custom layout
     */
    static Layout* create(const QString& type, LayoutManager* manager);

    /**
     * @brief Register a custom layout creator
     * @param type Type name to register
     * @param creator Function that creates the layout
     *
     * Allows plugins or extensions to register new layout types.
     */
    static void registerType(const QString& type, CreatorFunc creator);

private:
    static QHash<QString, CreatorFunc>& creators();
    static void ensureDefaults();
    static bool s_defaultsInitialized;
};

} // namespace PlasmaZones
