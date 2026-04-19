// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorLayoutApi/LayoutSourceBundle.h>

#include <QtGlobal>

#include <algorithm>

namespace PhosphorLayout {

LayoutSourceBundle::LayoutSourceBundle() = default;

// Hand-written destructor — we drop the composite's borrowed pointers
// explicitly before m_sources tears down. C++'s reverse-declaration
// order would do this for us today, but a future reorder of the data
// members would silently invert the order and cause use-after-free
// during teardown.
LayoutSourceBundle::~LayoutSourceBundle()
{
    if (m_composite) {
        m_composite->clearSources();
    }
}

LayoutSourceBundle::LayoutSourceBundle(LayoutSourceBundle&&) noexcept = default;

// Hand-written move-assign — same rationale as the destructor. The
// defaulted move would walk the members in declaration order, leaving
// the *old* LHS composite pointing into the *old* LHS sources for one
// member-move step each. Drop the old composite's child pointers
// first, then perform the moves.
LayoutSourceBundle& LayoutSourceBundle::operator=(LayoutSourceBundle&& other) noexcept
{
    if (this != &other) {
        if (m_composite) {
            m_composite->clearSources();
        }
        m_factories = std::move(other.m_factories);
        m_sources = std::move(other.m_sources);
        m_sourceNames = std::move(other.m_sourceNames);
        m_composite = std::move(other.m_composite);
    }
    return *this;
}

void LayoutSourceBundle::addFactory(std::unique_ptr<ILayoutSourceFactory> factory)
{
    Q_ASSERT_X(!m_composite, "LayoutSourceBundle::addFactory", "must not register factories after build()");
    Q_ASSERT(factory);
    m_factories.push_back(std::move(factory));
}

void LayoutSourceBundle::build()
{
    if (m_composite) {
        return;
    }
    m_sources.reserve(m_factories.size());
    m_sourceNames.reserve(m_factories.size());
    QList<ILayoutSource*> raw;
    raw.reserve(static_cast<int>(m_factories.size()));
    for (auto& factory : m_factories) {
        m_sourceNames.push_back(factory->name());
        m_sources.push_back(factory->create());
        raw.append(m_sources.back().get());
    }
    m_composite = std::make_unique<CompositeLayoutSource>();
    // setSources fires contentsChanged exactly once; incremental
    // addSource calls would emit N times during initial wiring.
    m_composite->setSources(raw);
}

void LayoutSourceBundle::buildFromRegistered(const FactoryContext& ctx)
{
    // Sort the pending list by priority (lower first); stable_sort
    // preserves registration order for ties so the composite walks
    // sources in a deterministic, source-id-prefix-friendly order.
    // Sort the global list in place — every consumer wants the same
    // order, so paying for the sort once per process is fine.
    auto& providers = pendingLayoutSourceProviders();
    std::stable_sort(providers.begin(), providers.end(), [](const auto& a, const auto& b) {
        return a.priority < b.priority;
    });
    for (const auto& provider : providers) {
        if (!provider.builder) {
            continue;
        }
        auto factory = provider.builder(ctx);
        if (factory) {
            addFactory(std::move(factory));
        }
    }
    build();
}

ILayoutSource* LayoutSourceBundle::source(const QString& name) const
{
    for (std::size_t i = 0; i < m_sourceNames.size(); ++i) {
        if (m_sourceNames[i] == name) {
            return m_sources[i].get();
        }
    }
    return nullptr;
}

} // namespace PhosphorLayout
