// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorLayoutApi/LayoutSourceBundle.h>

#include <QDebug>
#include <QMutexLocker>
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
//
// Use clearSourcesSilent() rather than clearSources() so the teardown
// path does not fire a contentsChanged() signal into observers whose
// own destructors may already have begun running. Composition roots
// (daemon, editor, settings) rely on Qt's receiver-side auto-disconnect
// for safety, but emitting a signal at destruction time is still a
// surprise that a future subscriber could trip on by holding a queued
// connection. The silent variant avoids that foot-gun class entirely.
LayoutSourceBundle::~LayoutSourceBundle()
{
    if (m_composite) {
        m_composite->clearSourcesSilent();
    }
}

LayoutSourceBundle::LayoutSourceBundle(LayoutSourceBundle&&) noexcept = default;

// Hand-written move-assign — same rationale as the destructor. The
// defaulted move would walk the members in declaration order, leaving
// the *old* LHS composite pointing into the *old* LHS sources for one
// member-move step each. Drop the old composite's child pointers
// first, then perform the moves. clearSourcesSilent() (not
// clearSources()) is used for the same reason as in the destructor —
// move-assign is a teardown of the old value and should not notify
// observers.
LayoutSourceBundle& LayoutSourceBundle::operator=(LayoutSourceBundle&& other) noexcept
{
    if (this != &other) {
        if (m_composite) {
            m_composite->clearSourcesSilent();
        }
        m_factories = std::move(other.m_factories);
        m_sources = std::move(other.m_sources);
        m_sourceNames = std::move(other.m_sourceNames);
        m_sourceIndex = std::move(other.m_sourceIndex);
        m_composite = std::move(other.m_composite);
        // Post-condition: std::move on unique_ptr leaves `other` empty and
        // std::vector's move leaves its storage empty. Assert in debug so a
        // future observer / iterator added to this class that walks a moved-
        // from bundle trips the invariant immediately rather than producing
        // a subtle UAF.
        Q_ASSERT(!other.m_composite);
        Q_ASSERT(other.m_sources.empty());
        Q_ASSERT(other.m_factories.empty());
        Q_ASSERT(other.m_sourceNames.empty());
        Q_ASSERT(other.m_sourceIndex.isEmpty());
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
    // Reject empty names up front so the duplicate-name first-wins pass in
    // build() never has to special-case them. An empty name can never be
    // looked up via source(QString) — the public accessor matches on string
    // equality, and returning a non-null ILayoutSource* for "" would let
    // composition roots paper over a misconfigured factory. Matches
    // FactoryContext::set's fail-safe first-wins discipline on programmer
    // errors: assert in debug, warn + drop in release.
    const QString name = factory->name();
    Q_ASSERT_X(!name.isEmpty(), "LayoutSourceBundle::addFactory",
               "factory must supply a non-empty name — source(QString) lookups and the duplicate-name guard "
               "require a stable identifier");
    if (name.isEmpty()) {
        qWarning("LayoutSourceBundle::addFactory: ignoring factory with empty name");
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
    m_sourceIndex.reserve(static_cast<int>(m_factories.size()));
    QList<ILayoutSource*> raw;
    raw.reserve(static_cast<int>(m_factories.size()));
    for (auto& factory : m_factories) {
        const QString name = factory->name();
        // Duplicate-name detection: source(name) is an O(1) hash lookup
        // over m_sourceIndex, so a collision would silently route
        // composition-root setAutotileLayoutSource (and similar) at the
        // wrong source. availableLayouts() on the composite would also
        // return duplicate previews. Fail-safe policy: first-registration
        // wins, later duplicates warn and skip. Matches
        // FactoryContext::set's first-wins discipline and
        // AlgorithmRegistry's system-script dedup. Realistic trigger is a
        // future plugin-loading cycle (XDG path duplication, dlopen +
        // static-init re-run) — the warning identifies the offending
        // provider by name for diagnostics.
        if (m_sourceIndex.contains(name)) {
            // Include the duplicate's position in m_factories so plugin
            // authors looking at the warning can correlate against the
            // (priority, name) registrar list. The first-wins entry's
            // position is implicit (lower index) — only the dropped one
            // needs identifying.
            qWarning(
                "LayoutSourceBundle::build: duplicate factory name '%s' at registration index %td — "
                "skipping later registration (first wins)",
                qUtf8Printable(name), static_cast<std::ptrdiff_t>(&factory - m_factories.data()));
            continue;
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
        const std::size_t idx = m_sources.size();
        m_sourceNames.push_back(name);
        m_sources.push_back(std::move(source));
        m_sourceIndex.insert(name, idx);
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

    // Sort by priority (lower first); tie-break on name (lexicographic) for
    // stable ordering across translation units. Static-init order across
    // TUs is implementation-defined, so any registrars sharing a priority
    // would otherwise produce platform- / toolchain-dependent composite
    // iteration. Tie-breaking on name makes the output deterministic
    // regardless of which TU's registrar ran first.
    //
    // Snapshot the pending list into a local copy before sorting so
    // concurrent bundles on different threads don't race on the process-
    // global list's underlying storage. The list is append-only at
    // static-init time (so its contents are stable once main() runs),
    // but std::sort mutates the container — two bundles building in
    // parallel would otherwise trip over each other's sort swaps.
    //
    // Hold @c pendingLayoutSourceProvidersMutex across the snapshot loop
    // so a registrar ctor running on another thread (Qt plugin loader's
    // worker, late dlopen) cannot append while we copy. Released before
    // the per-provider builder calls so a builder that reaches back into
    // the registry (e.g. a future plugin metadata helper) cannot
    // deadlock on the same mutex.
    std::vector<PendingLayoutSourceProvider> providers;
    {
        QMutexLocker locker(&pendingLayoutSourceProvidersMutex());
        const auto& pending = pendingLayoutSourceProviders();
        providers.reserve(static_cast<std::size_t>(pending.size()));
        for (const auto& p : pending) {
            providers.push_back(p);
        }
    }
    std::sort(providers.begin(), providers.end(), [](const auto& a, const auto& b) {
        if (a.priority != b.priority) {
            return a.priority < b.priority;
        }
        return a.name < b.name;
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
    // O(1) hash lookup — build() populates m_sourceIndex alongside
    // m_sourceNames with duplicate-name skips already applied, so a
    // hit maps to exactly one valid index into m_sources. Returns
    // nullptr when no factory with that name has been registered or
    // when build() has not run yet.
    const auto it = m_sourceIndex.constFind(name);
    if (it == m_sourceIndex.constEnd()) {
        return nullptr;
    }
    return m_sources[it.value()].get();
}

} // namespace PhosphorLayout
