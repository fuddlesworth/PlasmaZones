// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorProtocol/ClientHelpers.h>

namespace PhosphorProtocol {

static const QLoggingCategory s_category("phosphor.protocol", QtInfoMsg);

const QLoggingCategory& lcPhosphorProtocol()
{
    return s_category;
}

} // namespace PhosphorProtocol
