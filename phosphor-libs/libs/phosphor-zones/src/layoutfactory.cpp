// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorZones/LayoutFactory.h>
#include <PhosphorZones/Layout.h>
#include <PhosphorZones/LayoutRegistry.h>

namespace PhosphorZones {

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

    c[QStringLiteral("columns")] = [](LayoutRegistry* manager) {
        return Layout::createColumnsLayout(3, manager);
    };

    c[QStringLiteral("rows")] = [](LayoutRegistry* manager) {
        return Layout::createRowsLayout(3, manager);
    };

    c[QStringLiteral("grid")] = [](LayoutRegistry* manager) {
        return Layout::createGridLayout(2, 2, manager);
    };

    c[QStringLiteral("priority")] = [](LayoutRegistry* manager) {
        return Layout::createPriorityGridLayout(manager);
    };

    c[QStringLiteral("focus")] = [](LayoutRegistry* manager) {
        return Layout::createFocusLayout(manager);
    };

    c[QStringLiteral("custom")] = [](LayoutRegistry* manager) {
        return new Layout(QString(), manager);
    };
}

Layout* LayoutFactory::create(const QString& type, LayoutRegistry* manager)
{
    ensureDefaults();

    auto& c = creators();
    auto it = c.find(type);
    if (it != c.end()) {
        return it.value()(manager);
    }

    return new Layout(QString(), manager);
}

void LayoutFactory::registerType(const QString& type, CreatorFunc creator)
{
    ensureDefaults();
    creators()[type] = std::move(creator);
}

} // namespace PhosphorZones
