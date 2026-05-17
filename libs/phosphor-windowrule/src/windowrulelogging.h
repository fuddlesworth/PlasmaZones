// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

// Window-rule-local logging categories. Distinct names from the daemon-side
// categories so library and daemon each own their own log filtering knob
// (org.phosphor.windowrule.* — see windowrulelogging.cpp for the registered
// names).

#include <QLoggingCategory>

namespace PhosphorWindowRule {

Q_DECLARE_LOGGING_CATEGORY(lcWindowRule)
Q_DECLARE_LOGGING_CATEGORY(lcRuleEval)

} // namespace PhosphorWindowRule
