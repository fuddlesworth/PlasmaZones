// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

// The Pointer group of the settings schema: the pointer decoration chain's
// master switch and its PointerProfile blob. Its own TU rather than another
// appendXxxSchema in settingsschema.cpp, which is already in the file-size
// grace band and carries an explicit "split the next domain out" note; the
// entry point is declared alongside every other appendXxxSchema in
// settingsschema.h.
//
// The pointer chain is its own config domain, not a Decorations sub-group: the
// pointer is not a surface, and the chain is a flat ordered pack list rather
// than a path-keyed profile tree. The blob carries no sanitizer, for the same
// reason the DecorationProfileTree blob carries none — the per-pack override
// schema is not known to the config layer.

#include "settingsschema.h"

#include "configdefaults.h"

namespace PlasmaZones {

void appendPointerSchema(PhosphorConfig::Schema& schema)
{
    using CD = ConfigDefaults;
    schema.groups[CD::pointerGroup()] = {
        {CD::enabledKey(), CD::pointerEnabled(), QMetaType::Bool,
         QStringLiteral("Draw the pointer decoration chain. Off leaves the cursor alone and costs nothing.")},
        // The stored default is the empty chain, so an untouched config keeps
        // this key absent entirely (sparse persistence drops a default-equal
        // write).
        {CD::chainKey(), CD::pointerChain().toJson().toVariantMap(), QMetaType::QVariantMap,
         QStringLiteral("The pointer packs themselves, in paint order, with their settings. The pointer page writes "
                        "this, so it is not meant to be edited by hand.")},
    };
}

} // namespace PlasmaZones
