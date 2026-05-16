// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QString>

namespace PlasmaZones::animfileutil {

QString slugify(const QString& name);
QString jsonFilePath(const QString& dir, const QString& slug);

} // namespace PlasmaZones::animfileutil
