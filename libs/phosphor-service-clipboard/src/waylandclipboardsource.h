// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

// Internal (not installed) IClipboardSource backed by the foundation
// wlr-data-control client. The production facade wires this to the history
// model; it forwards PhosphorWayland::ClipboardDevice's selectionChanged /
// dataReceived signals into the seam, and adds the write path (setSelection)
// the copy action uses.

#include "iclipboardsource.h"

#include <PhosphorWayland/ClipboardDevice.h>

#include <QByteArray>
#include <QMap>

namespace PhosphorServiceClipboard {

class WaylandClipboardSource : public IClipboardSource
{
    Q_OBJECT
    Q_DISABLE_COPY_MOVE(WaylandClipboardSource)

public:
    explicit WaylandClipboardSource(QObject* parent = nullptr);

    void receive(const QString& mimeType) override;

    /// Take ownership of the clipboard selection, offering @p data keyed by MIME
    /// type (the copy-an-entry-back path). Beyond the read-only seam.
    void setSelection(const QMap<QString, QByteArray>& data);

private:
    PhosphorWayland::ClipboardDevice m_device;
};

} // namespace PhosphorServiceClipboard
