// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorServiceClipboard/ClipboardService.h>

#include <PhosphorWayland/ClipboardDevice.h>

namespace PhosphorServiceClipboard {

ClipboardService::ClipboardService(QObject* parent)
    : QObject(parent)
{
}

bool ClipboardService::isSupported() const
{
    // The compositor must advertise wlr-data-control (surfaced by
    // PhosphorWayland::ClipboardDevice) for the service to watch the clipboard.
    return PhosphorWayland::ClipboardDevice::isSupported();
}

} // namespace PhosphorServiceClipboard
