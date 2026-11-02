// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Internal QVariant<->Lua marshalling. NOT part of the public API — kept in src/
// so the lua_State dependency never reaches consumers.

#pragma once

#include <QVariant>

struct lua_State;

namespace PhosphorScripting {
namespace Marshal {

/// Recursion guard for deeply nested tables.
inline constexpr int MaxDepth = 32;

/// Upper bound on the values one toVariant() call converts, counting every
/// table, element and scalar. Depth alone does not bound the work: a table
/// that references the same subtable twice per level doubles the walk at each
/// of the MaxDepth levels. toVariant() runs after the protected call returns,
/// so the watchdog and heap cap no longer apply and nothing else stops it.
inline constexpr int MaxNodes = 1 << 18;

/// Upper bound on the string bytes, values and keys, that one toVariant() call
/// copies. The node bound does not bound memory: every reference to a string is
/// a new QString, so one large string referenced MaxNodes times would copy it
/// that many times. A VM holds at most LuauEngine's default heap cap, so a
/// result that copies more than that much text repeats the same strings.
inline constexpr qsizetype MaxBytes = qsizetype(64) * 1024 * 1024;

/// Push @p v onto the Lua stack. Bool/number/string map to primitives;
/// QVariantList → array table; QVariantMap → string-keyed table; everything
/// else (incl. invalid) → nil.
void pushVariant(lua_State* L, const QVariant& v, int depth = 0);

/// Convert the value at stack index @p idx to a QVariant. Tables become a
/// QVariantList when they look like a 1..n sequence (a non-zero `#` border),
/// otherwise a QVariantMap with string-coerced keys. Functions/userdata/threads
/// become an invalid QVariant.
///
/// Disambiguation is by the array border, the standard Lua idiom: a *mixed*
/// table that has both a sequence part and extra (string/non-sequence) keys is
/// marshalled as the list and its non-sequence keys are intentionally dropped.
/// Callers that need both parts must return a pure map (no `[1..n]` keys). Keys
/// that cannot coerce to a string (table/function/boolean keys) are skipped.
///
/// A table that contains itself (directly or through a descendant) converts
/// the inner reference to an invalid QVariant, as a function value does. A
/// value that needs more than MaxNodes conversions, or more than MaxBytes of
/// string copies, returns an invalid QVariant as a whole, never a truncated
/// table, and sets @p tooLarge when given, so a caller can tell it apart from
/// a nil.
QVariant toVariant(lua_State* L, int idx, int depth = 0, bool* tooLarge = nullptr);

} // namespace Marshal
} // namespace PhosphorScripting
