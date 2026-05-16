// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include "screenslogging.h"

namespace Phosphor::Screens {

// Default Info threshold matches the rest of the Phosphor* libraries —
// Debug-level output stays off unless the consumer enables it via
// QT_LOGGING_RULES.
Q_LOGGING_CATEGORY(lcPhosphorScreens, "phosphor.screens", QtInfoMsg)

} // namespace Phosphor::Screens
