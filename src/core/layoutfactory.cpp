// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "layoutfactory.h"
#include "layout.h"

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
    c[QStringLiteral("columns")] = [](LayoutManager* manager) {
        return Layout::createColumnsLayout(3, manager);
    };

    c[QStringLiteral("rows")] = [](LayoutManager* manager) {
        return Layout::createRowsLayout(3, manager);
    };

    c[QStringLiteral("grid")] = [](LayoutManager* manager) {
        return Layout::createGridLayout(2, 2, manager);
    };

    c[QStringLiteral("priority")] = [](LayoutManager* manager) {
        return Layout::createPriorityGridLayout(manager);
    };

    c[QStringLiteral("focus")] = [](LayoutManager* manager) {
        return Layout::createFocusLayout(manager);
    };

    c[QStringLiteral("custom")] = [](LayoutManager* manager) {
        return new Layout(QString(), LayoutType::Custom, manager);
    };
}

Layout* LayoutFactory::create(const QString& type, LayoutManager* manager)
{
    ensureDefaults();

    auto& c = creators();
    auto it = c.find(type);
    if (it != c.end()) {
        return it.value()(manager);
    }

    // Unknown type - create empty custom layout
    return new Layout(QString(), LayoutType::Custom, manager);
}

void LayoutFactory::registerType(const QString& type, CreatorFunc creator)
{
    ensureDefaults();
    creators()[type] = std::move(creator);
}

bool LayoutFactory::isTypeRegistered(const QString& type)
{
    ensureDefaults();
    return creators().contains(type);
}

QStringList LayoutFactory::registeredTypes()
{
    ensureDefaults();
    return creators().keys();
}

} // namespace PlasmaZones
