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

/// Recursion guard for deeply nested / cyclic tables.
inline constexpr int MaxDepth = 32;

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
QVariant toVariant(lua_State* L, int idx, int depth = 0);

} // namespace Marshal
} // namespace PhosphorScripting
