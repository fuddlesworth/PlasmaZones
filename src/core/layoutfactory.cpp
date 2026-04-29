// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "layoutfactory.h"

#include <PhosphorZones/Layout.h>
#include <PhosphorZones/LayoutRegistry.h>

namespace PlasmaZones {

bool LayoutFactory::s_defaultsInitialized = false;

QHash<QString, LayoutFactory::CreatorFunc>& LayoutFactory::creators()
{
    static QHash<QString, CreatorFunc> s_creators;
    return s_creators;
}

void LayoutFactory::ensureDefaults()
{
    if (s_defaultsInitialized) {
        return;
    }
    s_defaultsInitialized = true;

    auto& c = creators();

    // Register built-in layout types
    c[QStringLiteral("columns")] = [](PhosphorZones::LayoutRegistry* manager) {
        return PhosphorZones::Layout::createColumnsLayout(3, manager);
    };

    c[QStringLiteral("rows")] = [](PhosphorZones::LayoutRegistry* manager) {
        return PhosphorZones::Layout::createRowsLayout(3, manager);
    };

    c[QStringLiteral("grid")] = [](PhosphorZones::LayoutRegistry* manager) {
        return PhosphorZones::Layout::createGridLayout(2, 2, manager);
    };

    c[QStringLiteral("priority")] = [](PhosphorZones::LayoutRegistry* manager) {
        return PhosphorZones::Layout::createPriorityGridLayout(manager);
    };

    c[QStringLiteral("focus")] = [](PhosphorZones::LayoutRegistry* manager) {
        return PhosphorZones::Layout::createFocusLayout(manager);
    };

    c[QStringLiteral("custom")] = [](PhosphorZones::LayoutRegistry* manager) {
        return new PhosphorZones::Layout(QString(), manager);
    };
}

PhosphorZones::Layout* LayoutFactory::create(const QString& type, PhosphorZones::LayoutRegistry* manager)
{
    ensureDefaults();

    auto& c = creators();
    auto it = c.find(type);
    if (it != c.end()) {
        return it.value()(manager);
    }

    // Unknown type - create empty custom layout
    return new PhosphorZones::Layout(QString(), manager);
}

void LayoutFactory::registerType(const QString& type, CreatorFunc creator)
{
    ensureDefaults();
    creators()[type] = std::move(creator);
}

} // namespace PlasmaZones
