// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <PhosphorProtocol/BridgeTypes.h>
#include <PhosphorProtocol/phosphorprotocol_export.h>

#include <PhosphorDBus/Streaming.h>

#include <QDBusArgument>
#include <QDBusMetaType>

/// D-Bus marshalling for the compositor-bridge value types (see BridgeTypes.h).

namespace PhosphorProtocol {

PHOSPHORPROTOCOL_EXPORT QDBusArgument& operator<<(QDBusArgument& arg, const BridgeRegistrationResult& e);
PHOSPHORPROTOCOL_EXPORT const QDBusArgument& operator>>(const QDBusArgument& arg, BridgeRegistrationResult& e);

static_assert(PhosphorDBus::HasDBusStreaming<BridgeRegistrationResult>::value,
              "BridgeRegistrationResult missing QDBusArgument operators");

} // namespace PhosphorProtocol
