// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorLayoutApi/LayoutSourceBundle.h>

#include <QDebug>
#include <QtGlobal>

#include <algorithm>
#include <vector>

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
    // Release-safe guard: in debug the asserts above fire; in release we log
    // and drop the registration rather than silently pushing a factory that
    // will never be turned into a source (build() runs exactly once) or a
    // null pointer that would crash on create().
    if (m_composite) {
        qWarning("LayoutSourceBundle::addFactory: ignoring factory registered after build()");
        return;
    }
    if (!factory) {
        qWarning("LayoutSourceBundle::addFactory: ignoring null factory");
        return;
    }
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
        auto source = factory->create();
        if (!source) {
            // A factory returning null is a programmer error (the contract
            // in ILayoutSourceFactory says create() hands back a fresh
            // source). Skip rather than pushing a null into m_sources —
            // doing so would leave m_sourceNames / m_sources out of
            // index-sync and feed a null into the composite. We already
            // know the bad factory by name; log and move on.
            qWarning("LayoutSourceBundle::build: factory '%s' returned null — skipping",
                     qUtf8Printable(factory->name()));
            continue;
        }
        m_sourceNames.push_back(factory->name());
        m_sources.push_back(std::move(source));
        raw.append(m_sources.back().get());
    }
    m_composite = std::make_unique<CompositeLayoutSource>();
    // setSources fires contentsChanged exactly once; incremental
    // addSource calls would emit N times during initial wiring.
    m_composite->setSources(raw);
}

void LayoutSourceBundle::buildFromRegistered(const FactoryContext& ctx)
{
    // Sort by priority (lower first); stable_sort preserves registration
    // order for ties so the composite walks sources in a deterministic,
    // source-id-prefix-friendly order.
    //
    // Snapshot the pending list into a local copy before sorting so
    // concurrent bundles on different threads don't race on the process-
    // global list's underlying storage. The list is append-only at
    // static-init time (so its contents are stable once main() runs),
    // but std::stable_sort mutates the container — two bundles building
    // in parallel would otherwise trip over each other's sort swaps.
    std::vector<PendingLayoutSourceProvider> providers;
    {
        const auto& pending = pendingLayoutSourceProviders();
        providers.reserve(static_cast<std::size_t>(pending.size()));
        for (const auto& p : pending) {
            providers.push_back(p);
        }
    }
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
        } else {
            // Builder returned null → "this composition root doesn't host
            // this engine" per the registrar contract. Emit a debug log
            // so misconfigurations (forgot to ctx.set<IFoo>(...)) are
            // distinguishable from intentional not-hosted cases during
            // bring-up — the name identifies which provider bailed.
            qDebug("LayoutSourceBundle::buildFromRegistered: provider '%s' not hosted (builder returned null)",
                   qUtf8Printable(provider.name));
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
