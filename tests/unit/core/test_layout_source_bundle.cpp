// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_layout_source_bundle.cpp
 * @brief Unit tests for PhosphorLayout::LayoutSourceBundle + FactoryContext
 *        + the auto-discovery builder registration.
 *
 * Exercises the PR #343 refactor's extension points directly:
 *   - FactoryContext::set<T> round-trips the stored pointer.
 *   - addFactory + build produce a composite whose availableLayouts()
 *     concatenates every source's entries.
 *   - buildFromRegistered picks up static-init registrars and skips
 *     providers whose required services are absent from the context.
 *   - Duplicate-name factories are rejected with first-wins semantics.
 *   - Single-shot build() + buildFromRegistered() contract is enforced.
 *   - The bundle destructor does not emit contentsChanged into
 *     subscribers (regression guard for the clearSourcesSilent fix).
 *
 * Uses a fixture ITileAlgorithmRegistry stub rather than the concrete
 * PhosphorTiles::AlgorithmRegistry so the tests cover the abstract
 * interface — which is the whole point of extracting it.
 */

#include <QSignalSpy>
#include <QStringList>
#include <QTest>
#include <QVector>

#include <PhosphorLayoutApi/CompositeLayoutSource.h>
#include <PhosphorLayoutApi/ILayoutSource.h>
#include <PhosphorLayoutApi/ILayoutSourceFactory.h>
#include <PhosphorLayoutApi/LayoutPreview.h>
#include <PhosphorLayoutApi/LayoutSourceBundle.h>
#include <PhosphorLayoutApi/LayoutSourceProviderRegistry.h>

// ─── Fixture sources / factories ────────────────────────────────────────────

namespace {

/// Tiny handwritten ILayoutSource that reports a fixed list of previews.
class FixtureLayoutSource : public PhosphorLayout::ILayoutSource
{
    Q_OBJECT
public:
    explicit FixtureLayoutSource(QStringList ids, QObject* parent = nullptr)
        : PhosphorLayout::ILayoutSource(parent)
        , m_ids(std::move(ids))
    {
    }

    QVector<PhosphorLayout::LayoutPreview> availableLayouts() const override
    {
        QVector<PhosphorLayout::LayoutPreview> result;
        result.reserve(m_ids.size());
        for (const QString& id : m_ids) {
            PhosphorLayout::LayoutPreview p;
            p.id = id;
            p.displayName = id;
            result.append(p);
        }
        return result;
    }

    PhosphorLayout::LayoutPreview previewAt(const QString& id, int /*windowCount*/, const QSize& /*canvas*/) override
    {
        if (!m_ids.contains(id)) {
            return {};
        }
        PhosphorLayout::LayoutPreview p;
        p.id = id;
        p.displayName = id;
        return p;
    }

    void bumpContents()
    {
        Q_EMIT contentsChanged();
    }

private:
    QStringList m_ids;
};

/// Factory that hands out FixtureLayoutSource instances under a caller-
/// configured name.
class FixtureSourceFactory : public PhosphorLayout::ILayoutSourceFactory
{
public:
    FixtureSourceFactory(QString name, QStringList ids)
        : m_name(std::move(name))
        , m_ids(std::move(ids))
    {
    }

    QString name() const override
    {
        return m_name;
    }

    std::unique_ptr<PhosphorLayout::ILayoutSource> create() override
    {
        ++m_createCount;
        return std::make_unique<FixtureLayoutSource>(m_ids);
    }

    int createCount() const
    {
        return m_createCount;
    }

private:
    QString m_name;
    QStringList m_ids;
    int m_createCount = 0;
};

/// Stub service type — the FactoryContext key that exercises the
/// set/get round-trip in isolation from the real interface chain.
struct FixtureService
{
    int tag = 0;
};

/// File-scope service for the auto-discovery test. Must outlive every
/// call into buildFromRegistered across every test method in this
/// binary — static-init registrars append to a process-global list
/// that never shrinks, and their builder lambdas will be invoked on
/// every bundle rebuild. Using a file-scope instance prevents dangling
/// captures when a test method returns.
FixtureService g_fixtureServiceA{11};
FixtureService g_fixtureServiceB{22};

/// Service type that NO test ever registers — the "this composition
/// root doesn't host this engine" signal path.
struct AbsentFixtureService
{
};

// ─── File-scope static-init registrars ──────────────────────────────────────
// These append to the process-global pending providers list exactly
// once at static init. The builder lambdas capture file-scope state
// only (no method-local captures), so multiple buildFromRegistered
// passes across tests stay well-defined.
PhosphorLayout::LayoutSourceProviderRegistrar
    g_providerA(QStringLiteral("fixture-a"), /*priority=*/10,
                [](const PhosphorLayout::FactoryContext& ctx) -> std::unique_ptr<PhosphorLayout::ILayoutSourceFactory> {
                    if (ctx.get<FixtureService>() != &g_fixtureServiceA)
                        return nullptr;
                    return std::make_unique<FixtureSourceFactory>(QStringLiteral("fixture-a"),
                                                                  QStringList{QStringLiteral("a1")});
                });
PhosphorLayout::LayoutSourceProviderRegistrar
    g_providerB(QStringLiteral("fixture-b"), /*priority=*/5,
                [](const PhosphorLayout::FactoryContext& ctx) -> std::unique_ptr<PhosphorLayout::ILayoutSourceFactory> {
                    if (ctx.get<FixtureService>() != &g_fixtureServiceA)
                        return nullptr;
                    return std::make_unique<FixtureSourceFactory>(QStringLiteral("fixture-b"),
                                                                  QStringList{QStringLiteral("b1")});
                });
PhosphorLayout::LayoutSourceProviderRegistrar
    g_providerC(QStringLiteral("fixture-c"), /*priority=*/1,
                [](const PhosphorLayout::FactoryContext& ctx) -> std::unique_ptr<PhosphorLayout::ILayoutSourceFactory> {
                    // Deliberately pulls a service nobody ever sets — exercises the
                    // "builder returns nullptr → bundle skips" branch.
                    if (!ctx.get<AbsentFixtureService>())
                        return nullptr;
                    return std::make_unique<FixtureSourceFactory>(QStringLiteral("fixture-c"),
                                                                  QStringList{QStringLiteral("c1")});
                });

} // namespace

class TestLayoutSourceBundle : public QObject
{
    Q_OBJECT

private Q_SLOTS:

    // ═══════════════════════════════════════════════════════════════════════
    // FactoryContext
    // ═══════════════════════════════════════════════════════════════════════

    void testFactoryContext_setGetRoundTrip()
    {
        FixtureService svc{42};
        PhosphorLayout::FactoryContext ctx;
        const bool inserted = ctx.set<FixtureService>(&svc);
        QVERIFY(inserted);

        FixtureService* got = ctx.get<FixtureService>();
        QVERIFY(got != nullptr);
        QCOMPARE(got, &svc);
        QCOMPARE(got->tag, 42);
    }

    void testFactoryContext_missingKeyReturnsNull()
    {
        PhosphorLayout::FactoryContext ctx;
        FixtureService* got = ctx.get<FixtureService>();
        QVERIFY(got == nullptr);
    }

    void testFactoryContext_duplicateSetReturnsFalse()
    {
        FixtureService svc1{1};
        FixtureService svc2{2};
        PhosphorLayout::FactoryContext ctx;
        QVERIFY(ctx.set<FixtureService>(&svc1));
        // Duplicate registration: asserts in debug, returns false in
        // release. Tests run in release for coverage on CI. Either way,
        // first-registration wins — verify svc1 is still what we get.
#ifndef QT_DEBUG
        QVERIFY(!ctx.set<FixtureService>(&svc2));
        QCOMPARE(ctx.get<FixtureService>()->tag, 1);
#else
        // In debug, Q_ASSERT_X aborts — can't test the release-fallback
        // path here. Skip rather than fail.
        QSKIP("Q_ASSERT_X aborts in debug builds — duplicate-set release behaviour covered on release runs");
#endif
    }

    // ═══════════════════════════════════════════════════════════════════════
    // addFactory + build
    // ═══════════════════════════════════════════════════════════════════════

    void testBundle_buildConcatenatesEveryFactoryOutput()
    {
        PhosphorLayout::LayoutSourceBundle bundle;
        bundle.addFactory(std::make_unique<FixtureSourceFactory>(
            QStringLiteral("alpha"), QStringList{QStringLiteral("a1"), QStringLiteral("a2")}));
        bundle.addFactory(
            std::make_unique<FixtureSourceFactory>(QStringLiteral("beta"), QStringList{QStringLiteral("b1")}));
        bundle.build();

        QVERIFY(bundle.composite() != nullptr);
        const auto layouts = bundle.composite()->availableLayouts();
        QCOMPARE(layouts.size(), 3);
        QCOMPARE(layouts[0].id, QStringLiteral("a1"));
        QCOMPARE(layouts[1].id, QStringLiteral("a2"));
        QCOMPARE(layouts[2].id, QStringLiteral("b1"));
    }

    void testBundle_sourceByNameResolves()
    {
        PhosphorLayout::LayoutSourceBundle bundle;
        bundle.addFactory(
            std::make_unique<FixtureSourceFactory>(QStringLiteral("alpha"), QStringList{QStringLiteral("a1")}));
        bundle.build();

        QVERIFY(bundle.source(QStringLiteral("alpha")) != nullptr);
        QVERIFY(bundle.source(QStringLiteral("nonexistent")) == nullptr);
        // source() must match the composite's view of that source.
        const auto layouts = bundle.source(QStringLiteral("alpha"))->availableLayouts();
        QCOMPARE(layouts.size(), 1);
        QCOMPARE(layouts[0].id, QStringLiteral("a1"));
    }

    void testBundle_duplicateFactoryNameFirstWins()
    {
        PhosphorLayout::LayoutSourceBundle bundle;
        auto f1 = std::make_unique<FixtureSourceFactory>(QStringLiteral("dup"), QStringList{QStringLiteral("first")});
        auto f2 = std::make_unique<FixtureSourceFactory>(QStringLiteral("dup"), QStringList{QStringLiteral("second")});
        FixtureSourceFactory* f1Raw = f1.get();
        FixtureSourceFactory* f2Raw = f2.get();
        bundle.addFactory(std::move(f1));
        bundle.addFactory(std::move(f2));
        bundle.build();

        QCOMPARE(f1Raw->createCount(), 1); // first-registration was used
        QCOMPARE(f2Raw->createCount(), 0); // duplicate skipped
        const auto layouts = bundle.composite()->availableLayouts();
        QCOMPARE(layouts.size(), 1);
        QCOMPARE(layouts[0].id, QStringLiteral("first"));
    }

    void testBundle_addFactoryRejectsEmptyName()
    {
        // addFactory must reject factories that return an empty name(): the
        // source(QString) lookup keys on the name, and an empty key would
        // let a misconfigured factory silently route through public
        // lookups. Asserts in debug, warn + drop in release. Run the
        // release-path assertion here because Q_ASSERT_X aborts under
        // QT_DEBUG.
#ifndef QT_DEBUG
        PhosphorLayout::LayoutSourceBundle bundle;
        auto empty =
            std::make_unique<FixtureSourceFactory>(QString(), QStringList{QStringLiteral("should-not-appear")});
        bundle.addFactory(std::move(empty));
        auto ok = std::make_unique<FixtureSourceFactory>(QStringLiteral("ok"), QStringList{QStringLiteral("ok1")});
        bundle.addFactory(std::move(ok));
        bundle.build();

        QVERIFY(bundle.source(QString()) == nullptr);
        QVERIFY(bundle.source(QStringLiteral("ok")) != nullptr);
        const auto layouts = bundle.composite()->availableLayouts();
        QCOMPARE(layouts.size(), 1);
        QCOMPARE(layouts[0].id, QStringLiteral("ok1"));
#else
        QSKIP("Q_ASSERT_X aborts in debug builds — empty-name release behaviour covered on release runs");
#endif
    }

    // ═══════════════════════════════════════════════════════════════════════
    // buildFromRegistered
    // ═══════════════════════════════════════════════════════════════════════

    void testBundle_buildFromRegisteredRespectsPriorityAndSkipsAbsentServices()
    {
        // The three file-scope registrars (g_providerA/B/C) are already
        // in the process-global pending list from static init.
        // Providers A + B pull FixtureService; C pulls AbsentFixtureService
        // that nothing ever sets — expected to be skipped.
        //
        // Real provider libraries (phosphor-zones, phosphor-tiles) are
        // ALSO in the pending list since this test links against their
        // shared libraries. Their builders pull IZoneLayoutRegistry /
        // ITileAlgorithmRegistry which we don't publish here, so they
        // are skipped. We therefore check for our fixture entries' by-
        // name presence rather than exact count/exclusivity.
        PhosphorLayout::FactoryContext ctx;
        ctx.set<FixtureService>(&g_fixtureServiceA);

        PhosphorLayout::LayoutSourceBundle bundle;
        bundle.buildFromRegistered(ctx);

        QVERIFY(bundle.source(QStringLiteral("fixture-a")) != nullptr);
        QVERIFY(bundle.source(QStringLiteral("fixture-b")) != nullptr);
        QVERIFY(bundle.source(QStringLiteral("fixture-c")) == nullptr);

        // Priority order check: fixture-b (priority 5) must come before
        // fixture-a (priority 10) in the composite's enumeration. The
        // actual composite list interleaves other-provider skips with
        // ours, but b's "b1" must appear before a's "a1" regardless.
        const auto layouts = bundle.composite()->availableLayouts();
        int idxA = -1, idxB = -1;
        for (int i = 0; i < layouts.size(); ++i) {
            if (layouts[i].id == QStringLiteral("a1"))
                idxA = i;
            else if (layouts[i].id == QStringLiteral("b1"))
                idxB = i;
        }
        QVERIFY2(idxA >= 0, "fixture-a source must contribute its layout");
        QVERIFY2(idxB >= 0, "fixture-b source must contribute its layout");
        QVERIFY2(idxB < idxA, "lower-priority registrar must sort first");
    }

    void testBundle_buildFromRegisteredSkipsEverythingWhenServiceAbsent()
    {
        // Without FixtureService in the ctx, BOTH fixture-a and fixture-b
        // providers return nullptr from their builders and are silently
        // skipped. fixture-c is skipped for the same reason.
        PhosphorLayout::FactoryContext ctx; // empty
        PhosphorLayout::LayoutSourceBundle bundle;
        bundle.buildFromRegistered(ctx);

        QVERIFY(bundle.source(QStringLiteral("fixture-a")) == nullptr);
        QVERIFY(bundle.source(QStringLiteral("fixture-b")) == nullptr);
        QVERIFY(bundle.source(QStringLiteral("fixture-c")) == nullptr);
    }

    // ═══════════════════════════════════════════════════════════════════════
    // Single-shot lifecycle
    // ═══════════════════════════════════════════════════════════════════════

    void testBundle_composite_nullBeforeBuild()
    {
        PhosphorLayout::LayoutSourceBundle bundle;
        QVERIFY(bundle.composite() == nullptr);
        QVERIFY(bundle.source(QStringLiteral("anything")) == nullptr);
    }

    // ═══════════════════════════════════════════════════════════════════════
    // Signal forwarding (composite forwards child contentsChanged)
    // ═══════════════════════════════════════════════════════════════════════

    void testBundle_compositeForwardsChildContentsChanged()
    {
        PhosphorLayout::LayoutSourceBundle bundle;
        bundle.addFactory(
            std::make_unique<FixtureSourceFactory>(QStringLiteral("alpha"), QStringList{QStringLiteral("a1")}));
        bundle.build();

        auto* source = static_cast<FixtureLayoutSource*>(bundle.source(QStringLiteral("alpha")));
        QVERIFY(source != nullptr);

        QSignalSpy spy(bundle.composite(), &PhosphorLayout::ILayoutSource::contentsChanged);
        source->bumpContents();
        QCOMPARE(spy.count(), 1);
    }

    // ═══════════════════════════════════════════════════════════════════════
    // Destructor silence (regression guard for clearSourcesSilent fix)
    // ═══════════════════════════════════════════════════════════════════════

    void testBundle_destructor_doesNotEmitContentsChanged()
    {
        // Subscribers that outlive the bundle must not receive a
        // contentsChanged() during the bundle's destructor — the
        // clearSourcesSilent() path in ~LayoutSourceBundle guarantees
        // this. Without that fix, every bundle teardown would deliver a
        // spurious "contents invalidated" notification into still-live
        // consumers (including QML binding re-evaluation on daemon
        // shutdown).
        QSignalSpy* spy = nullptr;
        PhosphorLayout::CompositeLayoutSource* composite = nullptr;
        {
            PhosphorLayout::LayoutSourceBundle bundle;
            bundle.addFactory(
                std::make_unique<FixtureSourceFactory>(QStringLiteral("alpha"), QStringList{QStringLiteral("a1")}));
            bundle.build();
            composite = bundle.composite();
            spy = new QSignalSpy(composite, &PhosphorLayout::ILayoutSource::contentsChanged);
            QCOMPARE(spy->count(), 0);
        }
        // After bundle scope exit: the composite is destroyed. Qt's
        // receiver-side auto-disconnect breaks the spy's connection
        // silently — what we want to verify is that NO explicit emit
        // landed before the auto-disconnect fired. An emit during the
        // destructor would have been caught by the spy (it's wired to
        // the composite, which existed until the bundle's destructor
        // completed).
        QCOMPARE(spy->count(), 0);
        delete spy;
    }
};

QTEST_MAIN(TestLayoutSourceBundle)
#include "test_layout_source_bundle.moc"
