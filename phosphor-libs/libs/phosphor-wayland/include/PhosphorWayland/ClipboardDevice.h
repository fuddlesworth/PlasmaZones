// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <phosphorwayland_export.h>

#include <QByteArray>
#include <QMap>
#include <QObject>
#include <QStringList>

#include <memory>

namespace PhosphorWayland {

/**
 * @brief Client-side wrapper around `zwlr_data_control_device_v1`.
 *
 * Watches the session's clipboard selection (independent of keyboard focus, the
 * point of `wlr-data-control`), reports which MIME types the current selection
 * offers, reads the selection content on demand, and can take ownership of the
 * selection to re-offer data (paste a history entry back). This is the
 * foundation primitive a clipboard-history service composes; it carries no
 * policy, persistence, or model of its own.
 *
 * Construct one per process. The device binds the protocol global on the first
 * instance and Wayland keeps a per-binding event stream, so a second device
 * would receive a duplicate stream of selections.
 *
 * Threading: every method MUST be called from the GUI thread.
 *
 * Reads are asynchronous: `receive()` returns immediately and the bytes arrive
 * later via `dataReceived()`, read off a pipe on the event loop so a large or
 * slow producer never blocks the UI.
 */
class PHOSPHORWAYLAND_EXPORT ClipboardDevice : public QObject
{
    Q_OBJECT
    Q_DISABLE_COPY_MOVE(ClipboardDevice)

public:
    explicit ClipboardDevice(QObject* parent = nullptr);
    ~ClipboardDevice() override;

    /// True iff the compositor advertises `zwlr_data_control_manager_v1`. The
    /// constructor still succeeds when unsupported, but the device signals
    /// nothing.
    static bool isSupported();

    /// MIME types offered by the current clipboard selection, or an empty list
    /// when the selection is cleared (or the protocol is unsupported).
    [[nodiscard]] QStringList mimeTypes() const;

    /// Asynchronously read the current clipboard selection as @p mimeType. The
    /// result arrives via `dataReceived(mimeType, data)` (with empty @p data on
    /// failure or when @p mimeType is not offered). A no-op with no current
    /// selection.
    void receive(const QString& mimeType);

    /// Take ownership of the clipboard selection, offering @p data keyed by MIME
    /// type; when a client pastes, the bytes for the requested type are written.
    /// Replaces any previous selection this device set. Pass at least one type.
    void setSelection(const QMap<QString, QByteArray>& data);

Q_SIGNALS:
    /// The clipboard selection changed; @p mimeTypes lists the offered types
    /// (empty when the selection was cleared).
    void selectionChanged(const QStringList& mimeTypes);

    /// Asynchronous result of `receive()`. @p data is empty on failure.
    void dataReceived(const QString& mimeType, const QByteArray& data);

private:
    class Private;
    std::unique_ptr<Private> d;
};

} // namespace PhosphorWayland
