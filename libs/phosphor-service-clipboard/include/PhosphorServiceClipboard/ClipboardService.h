// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <PhosphorServiceClipboard/phosphorserviceclipboard_export.h>

#include <QAbstractItemModel>
#include <QObject>

#include <memory>

namespace PhosphorServiceClipboard {

/**
 * @brief Clipboard-history host for Phosphor-based desktop shells.
 *
 * Watches the session clipboard through `phosphor-wayland`'s `ClipboardDevice`
 * (a `wlr-data-control` client), keeps a de-duplicated, capped, on-disk history,
 * and can re-apply any entry. It is the policy / history / persistence layer over
 * the raw device; it composes the device rather than binding the protocol itself,
 * so its public surface is a clean Qt/QML type with no Wayland types leaking out.
 *
 * The history loads from disk on construction and persists on every change. A
 * dialog binds `history` (a model with `preview` / `mimeType` / `offeredTypes` /
 * `timestamp` roles, most-recent first) and calls `copy(index)` to paste an entry
 * back. Sensitive selections (password-manager hints) are never read or kept.
 */
class PHOSPHORSERVICECLIPBOARD_EXPORT ClipboardService : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool supported READ isSupported CONSTANT)
    Q_PROPERTY(QAbstractItemModel* history READ history CONSTANT)
    Q_PROPERTY(int count READ count NOTIFY countChanged)

public:
    explicit ClipboardService(QObject* parent = nullptr);
    ~ClipboardService() override;

    /// Whether the compositor advertises the `wlr-data-control` protocol this
    /// service builds on. When false the service constructs but stays inert
    /// (the persisted history still loads and is readable).
    [[nodiscard]] bool isSupported() const;

    /// The history list model (most-recent first). Owned by the service.
    [[nodiscard]] QAbstractItemModel* history() const;

    /// Number of entries in the history.
    [[nodiscard]] int count() const;

    /// Re-apply the entry at @p index to the clipboard selection. Out-of-range
    /// indices are ignored.
    Q_INVOKABLE void copy(int index);
    /// Remove the entry at @p index from the history (and disk).
    Q_INVOKABLE void remove(int index);
    /// Clear the entire history (and disk).
    Q_INVOKABLE void clear();

Q_SIGNALS:
    void countChanged();

private:
    Q_DISABLE_COPY_MOVE(ClipboardService)

    class Private;
    std::unique_ptr<Private> d;
};

} // namespace PhosphorServiceClipboard
