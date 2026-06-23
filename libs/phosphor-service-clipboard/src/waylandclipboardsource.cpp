// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include "waylandclipboardsource.h"

namespace PhosphorServiceClipboard {

WaylandClipboardSource::WaylandClipboardSource(QObject* parent)
    : IClipboardSource(parent)
{
    // The device's signals carry the same payloads as the seam's, so forward them
    // directly rather than through relay lambdas.
    connect(&m_device, &PhosphorWayland::ClipboardDevice::selectionChanged, this, &IClipboardSource::selectionChanged);
    connect(&m_device, &PhosphorWayland::ClipboardDevice::dataReceived, this, &IClipboardSource::dataReceived);
}

void WaylandClipboardSource::receive(const QString& mimeType)
{
    m_device.receive(mimeType);
}

void WaylandClipboardSource::setSelection(const QMap<QString, QByteArray>& data)
{
    m_device.setSelection(data);
}

} // namespace PhosphorServiceClipboard
