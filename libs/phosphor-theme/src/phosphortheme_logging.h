// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <QLoggingCategory>

// Library-internal logging category. Defined once in templateengine.cpp;
// every other TU in libs/phosphor-theme/src/ that needs to log includes
// this header and routes through qCWarning(lcPhosphorTheme) so the whole
// library is filterable via a single phosphor.theme=false switch.
//
// Internal-only: do NOT install this header. Consumers should not see
// our logging category.
Q_DECLARE_LOGGING_CATEGORY(lcPhosphorTheme)
