// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

// DecorationPageController surface-taxonomy helper — the parentChain reader
// that walks a decoration surface path to its ancestors for the inheritance
// breadcrumb. Split out so decorationpagecontroller.cpp stays under the
// project's 800-line cap. Same class, separate TU, no API change. (Card
// labels live as i18n() strings in the QML page models, mirroring the
// animation sub-pages — there is no C++ label accessor.)

#include "decorationpagecontroller.h"

#include <QLatin1Char>

namespace PlasmaZones {

QStringList DecorationPageController::parentChain(const QString& path) const
{
    // Self + ancestors, deepest first, terminating at (but excluding) the
    // empty baseline. e.g. "window.tiled" -> ["window.tiled", "window"].
    QStringList chain;
    QString cur = path;
    while (!cur.isEmpty()) {
        chain.append(cur);
        const int dot = cur.lastIndexOf(QLatin1Char('.'));
        cur = (dot < 0) ? QString() : cur.left(dot);
    }
    return chain;
}

} // namespace PlasmaZones
