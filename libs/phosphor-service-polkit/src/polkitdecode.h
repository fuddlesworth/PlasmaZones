// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

// Internal (not installed) decode helpers shared between the agent and its unit
// test. They convert polkit-qt's request data into the plain Qt types the
// AuthRequest exposes. Inline so they need no separate translation unit.

#include <polkitqt1-details.h>
#include <polkitqt1-identity.h>

#include <QStringList>
#include <QVariantMap>

namespace PhosphorServicePolkit::detail {

/// polkit `Details` (a string→string map) to a `QVariantMap`.
inline QVariantMap detailsToMap(const PolkitQt1::Details& details)
{
    QVariantMap map;
    const QStringList keys = details.keys();
    for (const QString& key : keys)
        map.insert(key, details.lookup(key));
    return map;
}

/// The identities the user may authenticate as, as display strings (e.g.
/// `unix-user:alice`).
inline QStringList identityNames(const PolkitQt1::Identity::List& identities)
{
    QStringList names;
    names.reserve(identities.size());
    for (const PolkitQt1::Identity& identity : identities)
        names << identity.toString();
    return names;
}

} // namespace PhosphorServicePolkit::detail
