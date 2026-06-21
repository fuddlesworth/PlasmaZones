// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <QSignalSpy>
#include <QTest>
#include <QUrl>

#include "PhosphorControl/ApplicationController.h"
#include "PhosphorControl/ISearchProvider.h"
#include "PhosphorControl/PageController.h"
#include "PhosphorControl/SearchController.h"
#include "PhosphorControl/SearchEntry.h"

using PhosphorControl::ApplicationController;
using PhosphorControl::ISearchProvider;
using PhosphorControl::PageController;
using PhosphorControl::SearchController;
using PhosphorControl::SearchEntry;

namespace {

class StubPage : public PageController
{
    Q_OBJECT
public:
    explicit StubPage(QString id, QObject* parent = nullptr)
        : PageController(std::move(id), parent)
    {
    }
    bool isDirty() const override
    {
        return false;
    }
    void apply() override
    {
    }
    void discard() override
    {
    }
    void resetToDefaults() override
    {
    }
};

class StubProvider : public ISearchProvider
{
public:
    QVector<SearchEntry> searchEntries() const override
    {
        SearchEntry e;
        e.kind = SearchEntry::Kind::Entity;
        e.pageId = QStringLiteral("window-rules");
        e.anchor = QStringLiteral("rule:abc");
        e.title = QStringLiteral("Steam");
        e.subtitle = QStringLiteral("Window Rules");
        return {e};
    }
};

QStringList resultPageIds(const SearchController& sc)
{
    QStringList ids;
    const QVariantList r = sc.results();
    for (const QVariant& v : r) {
        ids << v.toMap().value(QStringLiteral("pageId")).toString();
    }
    return ids;
}

} // namespace

class TestSearchController : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void init()
    {
        m_app = new ApplicationController(this);
        // A non-navigable category (empty qmlSource) + two navigable leaves.
        m_app->registerPage(new StubPage(QStringLiteral("snapping")), {}, QStringLiteral("Snapping"), QUrl());
        m_app->registerPage(new StubPage(QStringLiteral("general")), {}, QStringLiteral("General"),
                            QUrl(QStringLiteral("qrc:/General.qml")));
        m_app->registerPage(new StubPage(QStringLiteral("snapping-appearance")), QStringLiteral("snapping"),
                            QStringLiteral("Appearance"), QUrl(QStringLiteral("qrc:/Appearance.qml")));
        // Two-deep leaf so the breadcrumb actually joins multiple ancestors.
        m_app->registerPage(new StubPage(QStringLiteral("snapping-appearance-colors")),
                            QStringLiteral("snapping-appearance"), QStringLiteral("Colors"),
                            QUrl(QStringLiteral("qrc:/Colors.qml")));
    }

    void cleanup()
    {
        delete m_app;
        m_app = nullptr;
    }

    void findsPageByTitle()
    {
        SearchController sc(m_app);
        sc.setQuery(QStringLiteral("general"));
        QVERIFY(resultPageIds(sc).contains(QStringLiteral("general")));
    }

    void categoryWithoutQmlIsNotATarget()
    {
        SearchController sc(m_app);
        // "snapping" matches the category title AND the leaf's breadcrumb, but
        // only the navigable leaf (not the category id) is ever a result.
        sc.setQuery(QStringLiteral("snapping"));
        const QStringList ids = resultPageIds(sc);
        QVERIFY(!ids.contains(QStringLiteral("snapping")));
        QVERIFY(ids.contains(QStringLiteral("snapping-appearance")));
    }

    void breadcrumbFromParentChain()
    {
        SearchController sc(m_app);
        sc.setQuery(QStringLiteral("appearance"));
        const QVariantList r = sc.results();
        QVERIFY(!r.isEmpty());
        QCOMPARE(r.first().toMap().value(QStringLiteral("subtitle")).toString(), QStringLiteral("Snapping"));
    }

    void breadcrumbJoinsAncestorsWithSeparator()
    {
        SearchController sc(m_app);
        sc.setQuery(QStringLiteral("colors"));
        const QVariantList r = sc.results();
        QVERIFY(!r.isEmpty());
        // Two ancestors joined by U+203A — guards against a QLatin1String
        // separator mangling the multibyte char into mojibake.
        QCOMPARE(r.first().toMap().value(QStringLiteral("subtitle")).toString(),
                 QStringLiteral("Snapping › Appearance"));
    }

    void keywordEnablesSynonymSearch()
    {
        SearchController sc(m_app);
        sc.setPageKeywords(QStringLiteral("general"), {QStringLiteral("color"), QStringLiteral("theme")});
        sc.setQuery(QStringLiteral("color"));
        QVERIFY(resultPageIds(sc).contains(QStringLiteral("general")));
    }

    void staticAnchorEntryIsSearchable()
    {
        SearchController sc(m_app);
        SearchEntry anchor;
        anchor.kind = SearchEntry::Kind::Setting;
        anchor.pageId = QStringLiteral("general");
        anchor.anchor = QStringLiteral("renderingBackend");
        anchor.title = QStringLiteral("Rendering backend");
        sc.addEntry(anchor);

        sc.setQuery(QStringLiteral("rendering"));
        // address() composes pageId#anchor for navigateTo. Assert the matching
        // result is actually present (a bare `contains` + conditional check would
        // pass even if the entry vanished — the regression we'd most want caught).
        bool found = false;
        for (const QVariant& v : sc.results()) {
            const QVariantMap m = v.toMap();
            if (m.value(QStringLiteral("title")).toString() == QStringLiteral("Rendering backend")) {
                found = true;
                QCOMPARE(m.value(QStringLiteral("address")).toString(), QStringLiteral("general#renderingBackend"));
            }
        }
        QVERIFY(found);
    }

    void providerEntitiesAreSearchable()
    {
        SearchController sc(m_app);
        StubProvider provider;
        sc.registerProvider(&provider);
        sc.setQuery(QStringLiteral("steam"));
        // Find the entity by address rather than assuming it ranks first.
        bool found = false;
        for (const QVariant& v : sc.results()) {
            if (v.toMap().value(QStringLiteral("address")).toString() == QStringLiteral("window-rules#rule:abc")) {
                found = true;
            }
        }
        QVERIFY(found);
    }

    void limitCapsResultCount()
    {
        SearchController sc(m_app);
        SearchEntry a;
        a.kind = SearchEntry::Kind::Setting;
        a.pageId = QStringLiteral("general");
        a.title = QStringLiteral("Zoom alpha");
        SearchEntry b = a;
        b.title = QStringLiteral("Zoom beta");
        sc.addEntry(a);
        sc.addEntry(b);

        sc.setQuery(QStringLiteral("zoom"));
        QVERIFY(sc.resultCount() >= 2);
        sc.setLimit(1);
        QCOMPARE(sc.resultCount(), 1);
    }

    void invalidateRefreshesActiveQuery()
    {
        SearchController sc(m_app);
        sc.setQuery(QStringLiteral("steam"));
        QCOMPARE(sc.resultCount(), 0); // no provider yet

        StubProvider provider;
        sc.registerProvider(&provider); // marks index dirty, does NOT recompute
        sc.invalidate(); // active query → rebuild + re-rank
        QVERIFY(sc.resultCount() >= 1);
    }

    void lazyRebuildSeesAddedEntry()
    {
        SearchController sc(m_app);
        sc.setQuery(QStringLiteral("alpha"));
        QCOMPARE(sc.resultCount(), 0);

        SearchEntry e;
        e.kind = SearchEntry::Kind::Setting;
        e.pageId = QStringLiteral("general");
        e.anchor = QStringLiteral("alphaThing");
        e.title = QStringLiteral("Alpha thing");
        sc.addEntry(e); // marks dirty; rebuild happens on next query

        sc.setQuery(QStringLiteral("alph")); // different query → recompute sees it
        QVERIFY(sc.resultCount() >= 1);
    }

    void emptyQueryYieldsNoResults()
    {
        SearchController sc(m_app);
        sc.setQuery(QStringLiteral("   "));
        QCOMPARE(sc.resultCount(), 0);
        QVERIFY(sc.suggestion().isEmpty());
    }

    void suggestionOnZeroResultsTypo()
    {
        SearchController sc(m_app);
        sc.setQuery(QStringLiteral("appearence")); // misspelled, no match
        QCOMPARE(sc.resultCount(), 0);
        QCOMPARE(sc.suggestion(), QStringLiteral("Appearance"));
    }

    void resultsChangedEmitted()
    {
        SearchController sc(m_app);
        QSignalSpy spy(&sc, &SearchController::resultsChanged);
        sc.setQuery(QStringLiteral("general"));
        QCOMPARE(spy.count(), 1);
    }

private:
    ApplicationController* m_app = nullptr;
};

QTEST_MAIN(TestSearchController)
#include "test_search_controller.moc"
