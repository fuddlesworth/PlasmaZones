// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <PhosphorProtocol/DragTypes.h>
#include <PhosphorProtocol/ZoneMarshalling.h>
#include <PhosphorProtocol/phosphorprotocol_export.h>

#include <PhosphorDBus/Streaming.h>

#include <QDBusArgument>
#include <QDBusMetaType>

/// D-Bus marshalling for the window-drag value types (see DragTypes.h).
///
/// Pulls in ZoneMarshalling.h because the DragOutcome marshaller streams a
/// nested EmptyZoneList.

namespace PhosphorProtocol {

PHOSPHORPROTOCOL_EXPORT QDBusArgument& operator<<(QDBusArgument& arg, const DragPolicy& p);
PHOSPHORPROTOCOL_EXPORT const QDBusArgument& operator>>(const QDBusArgument& arg, DragPolicy& p);
PHOSPHORPROTOCOL_EXPORT QDBusArgument& operator<<(QDBusArgument& arg, const DragOutcome& o);
PHOSPHORPROTOCOL_EXPORT const QDBusArgument& operator>>(const QDBusArgument& arg, DragOutcome& o);

static_assert(PhosphorDBus::HasDBusStreaming<DragPolicy>::value, "DragPolicy missing QDBusArgument operators");
static_assert(PhosphorDBus::HasDBusStreaming<DragOutcome>::value, "DragOutcome missing QDBusArgument operators");

} // namespace PhosphorProtocol
