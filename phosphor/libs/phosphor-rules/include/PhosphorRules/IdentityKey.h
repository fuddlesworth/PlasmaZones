// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <QChar>
#include <QString>

/**
 * @file IdentityKey.h
 * @brief Shared length-prefixed segment encoder used by every bridge that
 *        derives a stable identity key from a tuple of arbitrary strings.
 *
 * Every bridge and any other call site that derives a stable identity key
 * from a tuple of arbitrary strings builds a v5-UUID seed by concatenating
 * the tuple's segments — a pattern, a screen id, an event path, etc. Any of
 * those strings can legally contain a `|`, so a plain `|`-joined key could
 * collide for two distinct tuples. Length-prefixing each segment with
 * `"<size>:<segment>"` makes every tuple's encoding unique: the parser can
 * always recover the original boundary positions from the length prefixes,
 * so distinct tuples necessarily yield distinct concatenations.
 *
 * Header-only and Qt6::Core-only — the function is `inline` so callers in
 * other libraries can use it without a link edge.
 */

namespace PhosphorRules {

namespace Detail {

/// Length-prefix a segment so concatenated identity keys are unambiguous. See
/// the file-level comment for the rationale.
inline QString encodeSegment(const QString& segment)
{
    return QString::number(segment.size()) + QLatin1Char(':') + segment;
}

} // namespace Detail

} // namespace PhosphorRules
