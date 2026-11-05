// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorLayoutApi/LayoutSourceBundle.h>

#include <PhosphorRegistry/Registry.h>

#include <QDebug>
#include <QMutexLocker>
#include <QtGlobal>

#include <algorithm>
#include <vector>

namespace PhosphorLayout {

LayoutSourceBundle::LayoutSourceBundle()
    : m_factoryRegistry(std::make_unique<PhosphorRegistry::Registry<ILayoutSourceFactory>>())
{
}

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
        m_factoryRegistry = std::move(other.m_factoryRegistry);
        m_sources = std::move(other.m_sources);
        m_sourceIndex = std::move(other.m_sourceIndex);
        m_composite = std::move(other.m_composite);
        // Post-condition: std::move on unique_ptr leaves `other` empty and
        // std::vector's move leaves its storage empty. Assert in debug so a
        // future observer / iterator added to this class that walks a moved-
        // from bundle trips the invariant immediately rather than producing
        // a subtle UAF.
        Q_ASSERT(!other.m_composite);
        Q_ASSERT(other.m_sources.empty());
        Q_ASSERT(!other.m_factoryRegistry);
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
    // Reject empty names up front so source(QString) lookups have a stable
    // identifier and the registry's id-key is non-empty. An empty name can
    // never be looked up via source(QString), and returning a non-null
    // ILayoutSource* for "" would let composition roots paper over a
    // misconfigured factory. Matches FactoryContext::set's fail-safe
    // discipline on programmer errors: assert in debug, warn + drop in release.
    const QString name = factory->name();
    Q_ASSERT_X(!name.isEmpty(), "LayoutSourceBundle::addFactory",
               "factory must supply a non-empty name — source(QString) lookups and the duplicate-name guard "
               "require a stable identifier");
    if (name.isEmpty()) {
        qWarning("LayoutSourceBundle::addFactory: ignoring factory with empty name");
        return;
    }
    // Duplicate-name handling is now the registry's job: registerFactory with
    // the default Reject policy keeps the first registration, drops + warns on
    // a later same-id factory. This is the same first-wins fail-safe build()
    // used to implement by hand (the realistic trigger is a future
    // plugin-loading cycle re-running a provider's static-init). id() == name().
    m_factoryRegistry->registerFactory(std::shared_ptr<ILayoutSourceFactory>(std::move(factory)));
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

    // Iterate the factory catalogue in registration order — Registry::ids()
    // returns insertion order, so the composite iterates sources in the order
    // factories were added (which buildFromRegistered feeds in priority order).
    // This preserves the id-namespace precedence the composite documents. The
    // registry has already applied first-wins de-duplication, so every id maps
    // to exactly one factory.
    const QStringList ids = m_factoryRegistry->ids();

    m_sources.reserve(ids.size());
    m_sourceIndex.reserve(static_cast<int>(ids.size()));
    QList<ILayoutSource*> raw;
    raw.reserve(static_cast<int>(ids.size()));
    for (const QString& name : std::as_const(ids)) {
        const auto factory = m_factoryRegistry->factory(name);
        if (!factory) {
            continue; // single-shot, GUI thread — a racing unregister is impossible
        }
        auto source = factory->create();
        if (!source) {
            // A factory returning null is a programmer error (the contract in
            // ILayoutSourceFactory says create() hands back a fresh source).
            // Skip rather than pushing a null into m_sources — doing so would
            // leave m_sources / m_sourceIndex out of sync and feed a null
            // into the composite. We already know the bad factory by name;
            // log and move on.
            qWarning("LayoutSourceBundle::build: factory '%s' returned null — skipping", qUtf8Printable(name));
            continue;
        }
        const std::size_t idx = m_sources.size();
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
    // would otherwise produce platform- / toolchain-dependent results.
    //
    // The priority sort still decides which provider WINS a duplicate-name
    // collision (registered first → kept by the registry's first-wins
    // policy); build() then iterates the de-duplicated catalogue in id order.
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
    // O(1) hash lookup — build() populates m_sourceIndex with duplicate
    // names already rejected at addFactory time, so a hit maps to exactly
    // one valid index into m_sources. Returns nullptr when no factory with
    // that name has been registered or when build() has not run yet.
    const auto it = m_sourceIndex.constFind(name);
    if (it == m_sourceIndex.constEnd()) {
        return nullptr;
    }
    return m_sources[it.value()].get();
}

} // namespace PhosphorLayout
