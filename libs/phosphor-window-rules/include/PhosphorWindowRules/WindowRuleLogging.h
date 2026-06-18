// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

// Public declaration of the library's logging categories. The category objects
// themselves are defined in the library's TU (windowrulelogging.cpp); this
// header simply re-exposes the declarations so header-only consumers (the
// inline bridge in ContextRuleBridge.h) can emit warnings through the same
// `org.phosphor.windowrule.*` channels the rest of the library uses.
// (ExclusionRules moved its bodies into src/exclusionrules.cpp and includes
// this header from the .cpp directly, not from its public header.)

#include <QLoggingCategory>

#include "phosphorwindowrules_export.h"

namespace PhosphorWindowRules {

// Trailing semicolons keep clang-format from continuation-indenting the
// second category — `Q_DECLARE_LOGGING_CATEGORY` is statement-shaped but
// the macro itself doesn't consume one, so two adjacent invocations
// without trailing semicolons render with the second visually nested
// under the first.
PHOSPHORWINDOWRULES_EXPORT Q_DECLARE_LOGGING_CATEGORY(lcWindowRule);
PHOSPHORWINDOWRULES_EXPORT Q_DECLARE_LOGGING_CATEGORY(lcRuleEval);

} // namespace PhosphorWindowRules
