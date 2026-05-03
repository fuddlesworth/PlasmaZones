// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "animationfileutils.h"

#include <QLatin1Char>
#include <QStringLiteral>

namespace PlasmaZones::animfileutil {

QString slugify(const QString& name)
{
    QString out;
    out.reserve(name.size());
    bool lastWasDash = false;
    for (QChar c : name) {
        const QChar lower = c.toLower();
        if (lower.isLetterOrNumber()) {
            out.append(lower);
            lastWasDash = false;
        } else if (!lastWasDash) {
            out.append(QLatin1Char('-'));
            lastWasDash = true;
        }
    }
    while (out.startsWith(QLatin1Char('-')))
        out.remove(0, 1);
    while (out.endsWith(QLatin1Char('-')))
        out.chop(1);
    return out;
}

QString jsonFilePath(const QString& dir, const QString& stem)
{
    if (stem.isEmpty())
        return {};
    return dir + QLatin1Char('/') + stem + QStringLiteral(".json");
}

} // namespace PlasmaZones::animfileutil
