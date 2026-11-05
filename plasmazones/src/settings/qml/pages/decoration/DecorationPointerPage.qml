// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick

// Decoration → Pointer. One card for the "pointer" surface, whose chain is
// stored in the same DecorationProfileTree as every other surface. The path is
// baseline-isolated (DecorationSupportedPaths.h), so it inherits nothing and
// the pointer stays undecorated until a pointer pack is added here.
DecorationSurfaceCardList {
    Accessible.name: i18n("Pointer decoration surface")
    headerText: i18n("Decoration for the mouse pointer.")
    surfaceModel: [
        {
            "surfacePath": "pointer",
            "cardLabel": i18n("Mouse Pointer"),
            "alwaysEnabled": false,
            "isParentNode": false
        }
    ]
}
