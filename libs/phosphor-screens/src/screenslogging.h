// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

// Internal logging category for PhosphorScreens. NOT installed; this header
// stays under src/ so consumers don't accidentally inherit our category and
// drown their own logs in our debug output.

#include <QLoggingCategory>

namespace Phosphor::Screens {

Q_DECLARE_LOGGING_CATEGORY(lcPhosphorScreens)

} // namespace Phosphor::Screens
