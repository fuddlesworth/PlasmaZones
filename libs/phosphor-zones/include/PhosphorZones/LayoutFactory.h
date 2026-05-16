// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <phosphorzones_export.h>
#include <QString>
#include <QHash>
#include <functional>

namespace PhosphorZones {

class Layout;
class LayoutRegistry;

class PHOSPHORZONES_EXPORT LayoutFactory
{
public:
    using CreatorFunc = std::function<Layout*(LayoutRegistry*)>;

    static Layout* create(const QString& type, LayoutRegistry* manager);
    static void registerType(const QString& type, CreatorFunc creator);

private:
    static QHash<QString, CreatorFunc>& creators();
    static void ensureDefaults();
    static bool s_defaultsInitialized;
};

} // namespace PhosphorZones
