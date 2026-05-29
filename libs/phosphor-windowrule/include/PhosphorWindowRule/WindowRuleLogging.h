// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

// Public declaration of the library's logging categories. The category objects
// themselves are defined in the library's TU (windowrulelogging.cpp); this
// header simply re-exposes the declarations so header-only consumers (the
// inline bridges in ContextRuleBridge.h, ExclusionRules.h) can emit
// warnings through the same `org.phosphor.windowrule.*` channels the rest of
// the library uses.

#include <QLoggingCategory>

#include "phosphorwindowrule_export.h"

namespace PhosphorWindowRule {

// Trailing semicolons keep clang-format from continuation-indenting the
// second category — `Q_DECLARE_LOGGING_CATEGORY` is statement-shaped but
// the macro itself doesn't consume one, so two adjacent invocations
// without trailing semicolons render with the second visually nested
// under the first.
PHOSPHORWINDOWRULE_EXPORT Q_DECLARE_LOGGING_CATEGORY(lcWindowRule);
PHOSPHORWINDOWRULE_EXPORT Q_DECLARE_LOGGING_CATEGORY(lcRuleEval);

} // namespace PhosphorWindowRule
