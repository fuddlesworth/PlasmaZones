// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include "VirtualScreen.h"
#include "phosphorscreens_export.h"

#include <QHash>
#include <QObject>
#include <QString>

namespace Phosphor::Screens {

/**
 * @brief Pluggable persistence for virtual-screen configurations.
 *
 * The library never reads JSON, never touches `~/.config`, and never knows
 * the schema-version migration rules. The host (daemon, KCM, test harness)
 * implements this interface and ScreenManager treats it as the single
 * source of truth: writes go through `save` / `remove`, the @ref changed
 * signal triggers a `refreshVirtualConfigs` cycle, reads come back via
 * `loadAll`.
 *
 * Implementations are owned by the consumer; lifetime must outlive the
 * ScreenManager that holds the pointer.
 *
 * Threading: ScreenManager calls every method on the GUI thread; the
 * @ref changed signal must be emitted on the GUI thread.
 *
 * Validation: implementations should accept whatever
 * `VirtualScreenConfig::isValid` accepts and reject the rest. Returning
 * false from `save` is an explicit "rejected"; ScreenManager logs and
 * leaves its cache untouched.
 */
class PHOSPHORSCREENS_EXPORT IConfigStore : public QObject
{
    Q_OBJECT
public:
    explicit IConfigStore(QObject* parent = nullptr)
        : QObject(parent)
    {
    }
    ~IConfigStore() override = default;

    /// Snapshot every persisted virtual-screen config, keyed by physical
    /// screen ID. Empty configs are NOT included — absence means "no
    /// subdivision". Callers should treat the returned map as
    /// authoritative and fully replace any prior cache.
    virtual QHash<QString, VirtualScreenConfig> loadAll() const = 0;

    /// Single-key read for callers that only need one entry (e.g. the
    /// VirtualScreenSwapper, which mutates one physical screen at a time).
    /// Returns an empty config if no entry exists for @p physicalScreenId —
    /// callers can distinguish via `isEmpty()`.
    virtual VirtualScreenConfig get(const QString& physicalScreenId) const = 0;

    /// Persist @p config for @p physicalScreenId. An empty config is a
    /// removal request; implementations should drop the entry rather than
    /// store an empty marker.
    /// @return true if accepted (and persisted, if applicable). false if
    ///         the config failed validation or could not be written.
    virtual bool save(const QString& physicalScreenId, const VirtualScreenConfig& config) = 0;

    /// Drop the entry for @p physicalScreenId. No-op + true if no entry
    /// exists; a removal of a missing entry is not an error.
    ///
    /// Contract: `remove(id)` MUST be functionally equivalent to
    /// `save(id, VirtualScreenConfig{})` — i.e. both forms drop the
    /// entry and emit `changed()`. The empty-config-as-removal sentinel
    /// exists because some hosts (Settings-backed adapters) have a
    /// single unified setter on their underlying store; exposing both
    /// forms lets callers use whichever reads more cleanly at the call
    /// site without forcing implementers to duplicate the mutation path.
    /// @return true if the entry was dropped OR did not exist. false is
    ///         reserved for backing-store write failure.
    virtual bool remove(const QString& physicalScreenId) = 0;

Q_SIGNALS:
    /// Some entry in the store has changed (via this interface or by an
    /// external writer — KCM, settings UI, hand-edited file). Listeners
    /// should re-read via `loadAll` and apply the delta. The signal is
    /// deliberately coarse-grained: it does not say which entry changed
    /// because external writers can't always pinpoint that.
    void changed();
};

} // namespace Phosphor::Screens
