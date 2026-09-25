// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

// Internal (not installed) seam for the clipboard the history model watches.
// Production wraps PhosphorWayland::ClipboardDevice (a wlr-data-control client);
// tests substitute a fake that drives the selectionChanged / dataReceived edges
// directly. Keeping the model behind this interface is what lets the dedup / cap
// / preview logic be unit-tested without a live compositor.

#include <QByteArray>
#include <QObject>
#include <QStringList>

namespace PhosphorServiceClipboard {

/// The read surface of a clipboard the history model consumes: it announces the
/// offered MIME types via `selectionChanged` and reads a chosen type on demand
/// (asynchronously, so a large producer never blocks the loop).
class IClipboardSource : public QObject
{
    Q_OBJECT

public:
    explicit IClipboardSource(QObject* parent = nullptr)
        : QObject(parent)
    {
    }
    // Out-of-line (defined in iclipboardsource.cpp) so it anchors the vtable and
    // gives AUTOMOC a translation unit for the Q_OBJECT metaobject.
    ~IClipboardSource() override;

    /// Asynchronously read the current selection as @p mimeType; the result
    /// arrives via `dataReceived`.
    ///
    /// Contract: every receive() MUST eventually emit exactly one `dataReceived`
    /// carrying the SAME @p mimeType (with empty data on any failure). The model
    /// serializes reads and waits for that matching reply, so a source that drops
    /// a reply or echoes a different type would stall further captures. The
    /// production `WaylandClipboardSource` honours this on every path
    /// (no-offer / pipe-failure / EOF / read-error all emit the requested type).
    virtual void receive(const QString& mimeType) = 0;

Q_SIGNALS:
    /// The clipboard selection changed; @p mimeTypes lists the offered types
    /// (empty when cleared).
    void selectionChanged(const QStringList& mimeTypes);
    /// Asynchronous result of `receive()`. @p data is empty on failure;
    /// @p mimeType is always the type that was requested.
    void dataReceived(const QString& mimeType, const QByteArray& data);
};

} // namespace PhosphorServiceClipboard
