// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

// DecorationPageController surface-taxonomy helper — the parentChain reader
// that walks a decoration surface path to its ancestors for the inheritance
// breadcrumb. Same class as decorationpagecontroller.cpp, separate TU, no API
// change. (Card labels live as i18n() strings in the QML page models, mirroring
// the animation sub-pages — there is no C++ label accessor.)

#include "decorationpagecontroller.h"

#include <PhosphorSurface/DecorationSupportedPaths.h>

namespace PlasmaZones {

QStringList DecorationPageController::parentChain(const QString& path) const
{
    // Self + ancestors, deepest first, terminating at (but excluding) the
    // empty baseline. e.g. "window.tiled" -> ["window.tiled", "window"].
    // Walk with decorationParentPath rather than re-inlining the dot split: that
    // function is the SSOT the decoration tree's resolve() and its supported-path
    // walk both use, and a third hand-rolled copy here is how the breadcrumb ends
    // up disagreeing with what actually resolves.
    QStringList chain;
    QString cur = path;
    while (!cur.isEmpty()) {
        chain.append(cur);
        cur = PhosphorSurfaceShaders::decorationParentPath(cur);
    }
    return chain;
}

bool DecorationPageController::isBaselineIsolated(const QString& path) const
{
    return PhosphorSurfaceShaders::decorationPathIsBaselineIsolated(path);
}

} // namespace PlasmaZones
