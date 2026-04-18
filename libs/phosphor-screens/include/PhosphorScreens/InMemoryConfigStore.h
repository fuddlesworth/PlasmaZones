// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include "IConfigStore.h"

namespace Phosphor::Screens {

/**
 * @brief Trivial @ref IConfigStore that holds its state in process memory.
 *
 * Use in unit tests, in hosts that synthesise topology programmatically,
 * and as a placeholder during ScreenManager bring-up before the host's
 * persistent store is wired. No serialisation, no schema migration —
 * configs disappear with the process.
 *
 * Header-only and inline.
 */
class InMemoryConfigStore final : public IConfigStore
{
    Q_OBJECT
public:
    explicit InMemoryConfigStore(QObject* parent = nullptr)
        : IConfigStore(parent)
    {
    }

    QHash<QString, VirtualScreenConfig> loadAll() const override
    {
        return m_configs;
    }

    VirtualScreenConfig get(const QString& physicalScreenId) const override
    {
        return m_configs.value(physicalScreenId);
    }

    bool save(const QString& physicalScreenId, const VirtualScreenConfig& config) override
    {
        if (config.isEmpty()) {
            return remove(physicalScreenId);
        }
        const auto it = m_configs.constFind(physicalScreenId);
        if (it != m_configs.constEnd() && it.value() == config) {
            return true; // No-op write — don't fire changed().
        }
        m_configs.insert(physicalScreenId, config);
        Q_EMIT changed();
        return true;
    }

    bool remove(const QString& physicalScreenId) override
    {
        if (m_configs.remove(physicalScreenId) > 0) {
            Q_EMIT changed();
        }
        return true;
    }

private:
    QHash<QString, VirtualScreenConfig> m_configs;
};

} // namespace Phosphor::Screens
