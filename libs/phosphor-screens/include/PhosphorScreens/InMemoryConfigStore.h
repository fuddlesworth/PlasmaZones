// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include "IConfigStore.h"
#include "phosphorscreens_export.h"

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
class PHOSPHORSCREENS_EXPORT InMemoryConfigStore final : public IConfigStore
{
    Q_OBJECT
public:
    /// Production-matching default cap. Mirrors
    /// @ref ScreenManagerConfig::maxVirtualScreensPerPhysical so tests that
    /// spin up a store without specifying a cap still exercise the same
    /// admission ceiling the daemon enforces. A lower default (or 0 = "no
    /// cap") silently accepted over-limit configs the real consumer would
    /// have rejected, which masked regressions at the layer boundary.
    /// Tests that deliberately want no cap pass 0 explicitly.
    static constexpr int DefaultMaxScreensPerPhysical = 8;

    explicit InMemoryConfigStore(QObject* parent = nullptr)
        : IConfigStore(parent)
        , m_maxScreensPerPhysical(DefaultMaxScreensPerPhysical)
    {
    }

    /// Construct with an explicit maximum-virtual-screens-per-physical cap.
    /// Pass 0 for "no cap" (test-only; production parity requires matching
    /// the daemon's cap).
    explicit InMemoryConfigStore(int maxScreensPerPhysical, QObject* parent = nullptr)
        : IConfigStore(parent)
        , m_maxScreensPerPhysical(maxScreensPerPhysical)
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
        // Honour the IConfigStore contract: reject what
        // VirtualScreenConfig::isValid rejects. Mirrors SettingsConfigStore's
        // behaviour so tests exercise the same acceptance surface real
        // consumers see. A 0 cap means "no cap" — the default — but callers
        // can pass a production-matching cap via the constructor to catch
        // over-limit regressions the lightweight default would otherwise hide.
        if (!VirtualScreenConfig::isValid(config, physicalScreenId, m_maxScreensPerPhysical)) {
            return false;
        }
        const auto it = m_configs.constFind(physicalScreenId);
        // Exact operator== (not approxEqual). In-memory storage has no JSON
        // round-trip to round off floats, so there's nothing for the
        // tolerance window to absorb — but approxEqual is non-transitive,
        // and using a non-transitive predicate as a dedup key can collapse
        // a sequence of sub-tolerance drifts into a single equivalence class
        // the caller didn't intend. Settings-backed stores keep approxEqual
        // because they DO serialise through JSON; this store doesn't need it.
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
    int m_maxScreensPerPhysical; ///< 0 means "no cap". Default is DefaultMaxScreensPerPhysical.
};

} // namespace Phosphor::Screens
