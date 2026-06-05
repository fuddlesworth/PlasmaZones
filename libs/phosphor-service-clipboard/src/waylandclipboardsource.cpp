// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include "waylandclipboardsource.h"

namespace PhosphorServiceClipboard {

WaylandClipboardSource::WaylandClipboardSource(QObject* parent)
    : IClipboardSource(parent)
{
    connect(&m_device, &PhosphorWayland::ClipboardDevice::selectionChanged, this, [this](const QStringList& mimeTypes) {
        Q_EMIT selectionChanged(mimeTypes);
    });
    connect(&m_device, &PhosphorWayland::ClipboardDevice::dataReceived, this,
            [this](const QString& mimeType, const QByteArray& data) {
                Q_EMIT dataReceived(mimeType, data);
            });
}

QStringList WaylandClipboardSource::mimeTypes() const
{
    return m_device.mimeTypes();
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
