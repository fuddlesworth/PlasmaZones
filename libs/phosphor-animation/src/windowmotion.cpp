// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorAnimation/WindowMotion.h>

// All current methods are inline in the header (they're small and
// compositor-side callers want them inlined into per-frame hot paths).
// This TU exists as a vtable / debug-symbol anchor and a hook for
// Phase-2 polymorphic-curve support that will move the heavy
// implementation out of the header.

namespace PhosphorAnimation {
} // namespace PhosphorAnimation
