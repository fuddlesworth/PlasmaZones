// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorDBus/Logging.h>

namespace PhosphorDBus {

const QLoggingCategory& lcPhosphorDBus()
{
    static const QLoggingCategory category("phosphor.dbus", QtInfoMsg);
    return category;
}

} // namespace PhosphorDBus
