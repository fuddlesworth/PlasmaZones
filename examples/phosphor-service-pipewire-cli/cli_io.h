// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

class QTextStream;

namespace PhosphorPipeWireCli {

/// Shared stdout stream. Singleton-per-translation-unit would split the
/// stream into independent buffers across TUs; routing every print
/// through a single accessor keeps the interleave order deterministic
/// and matches the original single-file behaviour.
QTextStream& out();

/// Shared stderr stream. Same rationale as out(): one accessor across
/// every TU so diagnostics interleave deterministically.
QTextStream& err();

} // namespace PhosphorPipeWireCli
