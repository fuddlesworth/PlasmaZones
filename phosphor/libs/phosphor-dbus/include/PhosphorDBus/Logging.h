// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <PhosphorDBus/phosphordbus_export.h>

#include <QLoggingCategory>

namespace PhosphorDBus {

/// Default logging category for PhosphorDBus helpers. A `Client` constructed
/// without an explicit category logs call failures here.
PHOSPHORDBUS_EXPORT const QLoggingCategory& lcPhosphorDBus();

} // namespace PhosphorDBus
