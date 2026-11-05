// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

// Internal alias for the public logging-category declarations. The actual
// `Q_LOGGING_CATEGORY` definitions live in rulelogging.cpp; the public
// header re-declares the categories so header-only consumers can emit through
// the same `org.phosphor.rule.*` channels. Distinct names from the
// daemon-side categories so library and daemon each own their own log
// filtering knob.

#include <PhosphorRules/RuleLogging.h>
