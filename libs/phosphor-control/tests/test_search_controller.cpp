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

QStringList resultTitles(const SearchController& sc)
{
    QStringList t;
    const QVariantList r = sc.results();
    for (const QVariant& v : r) {
        t << v.toMap().value(QStringLiteral("title")).toString();
    }
    return t;
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
        QVERIFY(resultTitles(sc).contains(QStringLiteral("Rendering backend")));
        // address() composes pageId#anchor for navigateTo.
        for (const QVariant& v : sc.results()) {
            const QVariantMap m = v.toMap();
            if (m.value(QStringLiteral("title")).toString() == QStringLiteral("Rendering backend")) {
                QCOMPARE(m.value(QStringLiteral("address")).toString(), QStringLiteral("general#renderingBackend"));
            }
        }
    }

    void providerEntitiesAreSearchable()
    {
        SearchController sc(m_app);
        StubProvider provider;
        sc.registerProvider(&provider);
        sc.setQuery(QStringLiteral("steam"));
        const QVariantList r = sc.results();
        QVERIFY(!r.isEmpty());
        QCOMPARE(r.first().toMap().value(QStringLiteral("address")).toString(),
                 QStringLiteral("window-rules#rule:abc"));
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
        QVERIFY(spy.count() >= 1);
    }

private:
    ApplicationController* m_app = nullptr;
};

QTEST_MAIN(TestSearchController)
#include "test_search_controller.moc"
