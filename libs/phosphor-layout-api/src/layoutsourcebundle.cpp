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
    // Single-shot: every factory's create() runs exactly once. A second
    // build() would either re-instantiate sources (if we let it through)
    // or silently skip work the caller might be expecting (the previous
    // behaviour). Assert in debug; no-op in release with a warn so a
    // misbehaving caller is observable. Matches the lifecycle discipline
    // on addFactory and buildFromRegistered.
    Q_ASSERT_X(!m_composite, "LayoutSourceBundle::build",
               "build() is single-shot per bundle; second call is a programmer error");
    if (m_composite) {
        qWarning("LayoutSourceBundle::build: ignoring second call (bundle already built)");
        return;
    }
    m_sources.reserve(m_factories.size());
    m_sourceNames.reserve(m_factories.size());
    QList<ILayoutSource*> raw;
    raw.reserve(static_cast<int>(m_factories.size()));
    for (auto& factory : m_factories) {
        const QString name = factory->name();
        // Duplicate-name detection: source(name) walks m_sourceNames and
        // returns the first match, so a collision silently routes
        // composition-root setAutotileLayoutSource (and similar) at the
        // wrong source. Warn loudly here — a future plugin-loading cycle
        // (XDG path duplication, dlopen + static init re-run) is the
        // realistic trigger. Still register both so the composite carries
        // the entries; the warning is the actionable signal.
        if (std::find(m_sourceNames.begin(), m_sourceNames.end(), name) != m_sourceNames.end()) {
            qWarning(
                "LayoutSourceBundle::build: duplicate factory name '%s' — source(name) lookups will only return "
                "the first match",
                qUtf8Printable(name));
        }
        auto source = factory->create();
        if (!source) {
            // A factory returning null is a programmer error (the contract
            // in ILayoutSourceFactory says create() hands back a fresh
            // source). Skip rather than pushing a null into m_sources —
            // doing so would leave m_sourceNames / m_sources out of
            // index-sync and feed a null into the composite. We already
            // know the bad factory by name; log and move on.
            qWarning("LayoutSourceBundle::build: factory '%s' returned null — skipping", qUtf8Printable(name));
            continue;
        }
        m_sourceNames.push_back(name);
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
    // Single-shot per bundle: every auto-registered factory would otherwise
    // be rejected by addFactory's post-build gate, leaving the caller with a
    // silently-stale bundle. Asserts in debug; no-ops in release (matches the
    // build() idempotency contract on the second call rather than crashing).
    Q_ASSERT_X(!m_composite, "LayoutSourceBundle::buildFromRegistered",
               "buildFromRegistered is single-shot per bundle; second call is a programmer error");
    if (m_composite) {
        qWarning("LayoutSourceBundle::buildFromRegistered: ignoring second call (bundle already built)");
        return;
    }

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
