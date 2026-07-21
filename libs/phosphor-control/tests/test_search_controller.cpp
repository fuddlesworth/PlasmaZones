// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <QSignalSpy>
#include <QTest>
#include <QUrl>

#include "PhosphorControl/ApplicationController.h"
#include "PhosphorControl/ISearchProvider.h"
#include "PhosphorControl/PageController.h"
#include "PhosphorControl/SearchController.h"
#include "PhosphorControl/PageRegistry.h"
#include "PhosphorControl/SearchEntry.h"
#include "PhosphorControl/SearchRanker.h"

using PhosphorControl::ApplicationController;
using PhosphorControl::ISearchProvider;
using PhosphorControl::PageController;
using PhosphorControl::PageRegistry;
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
        e.pageId = QStringLiteral("rules");
        e.anchor = QStringLiteral("rule:abc");
        e.title = QStringLiteral("Steam");
        e.subtitle = QStringLiteral("Rules");
        return {e};
    }
};

// Returns an entity with NO subtitle, on a registered page, so the controller's
// auto-derive path (subtitle from the page hierarchy) is exercised.
class StubProviderNoSubtitle : public ISearchProvider
{
public:
    QVector<SearchEntry> searchEntries() const override
    {
        SearchEntry e;
        e.kind = SearchEntry::Kind::Entity;
        e.pageId = QStringLiteral("snapping-appearance-colors");
        e.title = QStringLiteral("Floaty");
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

// Subtitle of the result whose title matches, or empty if no such result —
// robust against ordering (don't assume the match ranks first).
QString subtitleForTitle(const SearchController& sc, const QString& title)
{
    for (const QVariant& v : sc.results()) {
        const QVariantMap m = v.toMap();
        if (m.value(QStringLiteral("title")).toString() == title) {
            return m.value(QStringLiteral("subtitle")).toString();
        }
    }
    return QString();
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
        // StubProvider's entities claim pageId "rules", and buildIndex now
        // drops entries whose pageId is not a registered page (they navigate
        // nowhere). Register it so the provider cases exercise the provider
        // path rather than the unregistered-id drop.
        m_app->registerPage(new StubPage(QStringLiteral("rules")), {}, QStringLiteral("Rules"),
                            QUrl(QStringLiteral("qrc:/Rules.qml")));
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

    void settingSubtitleAutoDerivedFromPagePath()
    {
        // A Setting entry with no subtitle gets the full path to its page
        // (ancestors + the page title), so it reads consistently with page
        // results instead of an author-invented breadcrumb.
        SearchController sc(m_app);
        SearchEntry e;
        e.kind = SearchEntry::Kind::Setting;
        e.pageId = QStringLiteral("snapping-appearance-colors");
        e.anchor = QStringLiteral("hue");
        e.title = QStringLiteral("Hue");
        sc.addEntry(e);

        sc.setQuery(QStringLiteral("hue"));
        const QVariantList r = sc.results();
        QVERIFY(!r.isEmpty());
        QCOMPARE(r.first().toMap().value(QStringLiteral("subtitle")).toString(),
                 QStringLiteral("Snapping › Appearance › Colors"));
    }

    void actionEntrySurfacesActionIdWithoutAddress()
    {
        // Kind::Action carries an app command id instead of a navigable
        // address: the result map must expose `actionId` (the QML dispatch
        // key) and an EMPTY `address` (so the navigate branch can never
        // swallow an action), and the producer's subtitle must survive the
        // breadcrumb autofill despite the empty pageId.
        SearchController sc(m_app);
        SearchEntry e;
        e.kind = SearchEntry::Kind::Action;
        e.actionId = QStringLiteral("show-shortcut-overlay");
        e.title = QStringLiteral("Keyboard Shortcuts");
        e.subtitle = QStringLiteral("Show the shortcut reference");
        e.keywords = {QStringLiteral("hotkey")};
        sc.addEntry(e);

        sc.setQuery(QStringLiteral("hotkey"));
        bool found = false;
        for (const QVariant& v : sc.results()) {
            const QVariantMap m = v.toMap();
            if (m.value(QStringLiteral("title")).toString() == QStringLiteral("Keyboard Shortcuts")) {
                found = true;
                QCOMPARE(m.value(QStringLiteral("actionId")).toString(), QStringLiteral("show-shortcut-overlay"));
                QVERIFY(m.value(QStringLiteral("address")).toString().isEmpty());
                QCOMPARE(m.value(QStringLiteral("kind")).toInt(), static_cast<int>(SearchEntry::Kind::Action));
                QCOMPARE(m.value(QStringLiteral("subtitle")).toString(), QStringLiteral("Show the shortcut reference"));
            }
        }
        QVERIFY(found);
    }

    void actionEntryWithEmptySubtitleSkipsBreadcrumbAutofill()
    {
        // An Action with no producer subtitle must stay subtitle-less:
        // actions are commands, not page-resident targets, so the breadcrumb
        // autofill (which would run breadcrumbFor on the empty pageId) is
        // skipped for them entirely.
        SearchController sc(m_app);
        SearchEntry e;
        e.kind = SearchEntry::Kind::Action;
        e.actionId = QStringLiteral("some-command");
        e.title = QStringLiteral("Bare Command");
        e.keywords = {QStringLiteral("barecmd")};
        sc.addEntry(e);

        sc.setQuery(QStringLiteral("barecmd"));
        bool found = false;
        for (const QVariant& v : sc.results()) {
            const QVariantMap m = v.toMap();
            if (m.value(QStringLiteral("title")).toString() == QStringLiteral("Bare Command")) {
                found = true;
                QVERIFY(m.value(QStringLiteral("subtitle")).toString().isEmpty());
            }
        }
        QVERIFY(found);
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

    void entryWithUnregisteredPageIdIsDropped()
    {
        // pageAllowedInCurrentMode returns true for an unknown id on purpose
        // (tier filter, not an existence check), so a typo'd pageId would
        // otherwise reach the results and navigate nowhere — the app's mode
        // gate rejects the id and bounces the user elsewhere.
        SearchController sc(m_app);
        SearchEntry typo;
        typo.kind = SearchEntry::Kind::Setting;
        typo.pageId = QStringLiteral("exclusions"); // folded out long ago
        typo.anchor = QStringLiteral("whatever");
        typo.title = QStringLiteral("Ghost setting");
        sc.addEntry(typo);

        QTest::ignoreMessage(
            QtWarningMsg,
            QRegularExpression(QStringLiteral("pageId .*exclusions.* is not a registered, navigable page")));
        sc.setQuery(QStringLiteral("ghost"));
        for (const QVariant& v : sc.results()) {
            QVERIFY(v.toMap().value(QStringLiteral("title")).toString() != QStringLiteral("Ghost setting"));
        }
    }

    void advancedOnlyEntryIsHiddenInSimpleMode()
    {
        // SearchEntry::advancedOnly is the ONLY thing keeping an advanced row
        // off the simple-mode result list when its owning page is visible in
        // BOTH modes (the page-tier gate cannot help there). Roughly 25
        // catalogue rows depend on it, so without this test deleting the
        // `!e.advancedOnly ||` clause in SearchController::tierAllows passes
        // the whole suite.
        SearchController sc(m_app);
        SearchEntry advanced;
        advanced.kind = SearchEntry::Kind::Setting;
        advanced.pageId = QStringLiteral("general"); // Always tier — visible in both modes
        advanced.anchor = QStringLiteral("borderScope");
        advanced.title = QStringLiteral("Apply borders to");
        advanced.advancedOnly = true;
        sc.addEntry(advanced);

        const auto titles = [&sc]() {
            QStringList out;
            for (const QVariant& v : sc.results()) {
                out << v.toMap().value(QStringLiteral("title")).toString();
            }
            return out;
        };

        m_app->registry()->setShowAdvanced(true);
        sc.setQuery(QStringLiteral("borders"));
        QVERIFY(titles().contains(QStringLiteral("Apply borders to")));

        m_app->registry()->setShowAdvanced(false);
        sc.setQuery(QStringLiteral("borders"));
        QVERIFY(!titles().contains(QStringLiteral("Apply borders to")));
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
            if (v.toMap().value(QStringLiteral("address")).toString() == QStringLiteral("rules#rule:abc")) {
                found = true;
            }
        }
        QVERIFY(found);
    }

    void providerSubtitleAutoDerivedWhenEmpty()
    {
        // A provider entity with no subtitle gets the full page path, exactly
        // like a static Setting entry.
        SearchController sc(m_app);
        StubProviderNoSubtitle provider;
        sc.registerProvider(&provider);
        sc.setQuery(QStringLiteral("floaty"));
        QCOMPARE(subtitleForTitle(sc, QStringLiteral("Floaty")), QStringLiteral("Snapping › Appearance › Colors"));
    }

    void providerSubtitleRespectedWhenSet()
    {
        // A provider-supplied subtitle (e.g. a rule's match summary) is
        // never overwritten by the auto-derive.
        SearchController sc(m_app);
        StubProvider provider;
        sc.registerProvider(&provider);
        sc.setQuery(QStringLiteral("steam"));
        QCOMPARE(subtitleForTitle(sc, QStringLiteral("Steam")), QStringLiteral("Rules"));
    }

    void registerProviderDedupes()
    {
        // Registering the same provider twice must not double its entries.
        SearchController sc(m_app);
        StubProvider provider;
        sc.registerProvider(&provider);
        sc.registerProvider(&provider);
        sc.setQuery(QStringLiteral("steam"));
        int count = 0;
        for (const QVariant& v : sc.results()) {
            if (v.toMap().value(QStringLiteral("address")).toString() == QStringLiteral("rules#rule:abc")) {
                ++count;
            }
        }
        QCOMPARE(count, 1);
    }

    void pageKindEntryKeepsEmptySubtitle()
    {
        // The auto-derive is gated on kind != Page: a Page-kind static entry
        // with no subtitle is left as-is (not given a self-including path).
        SearchController sc(m_app);
        SearchEntry e;
        e.kind = SearchEntry::Kind::Page;
        e.pageId = QStringLiteral("snapping-appearance-colors");
        e.title = QStringLiteral("Custom Page Entry");
        sc.addEntry(e);
        sc.setQuery(QStringLiteral("custom page entry"));
        QCOMPARE(subtitleForTitle(sc, QStringLiteral("Custom Page Entry")), QString());
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

    void searchFoldsAccentsAndFullCase()
    {
        // Plain toLower() leaves "Café" unmatched by "cafe". foldForSearch
        // decomposes, drops combining marks and case-folds, with ß mapped
        // explicitly (Qt's toCaseFolded is a SIMPLE fold and leaves it alone).
        // Both the global index and the sidebar rail route through it, so the
        // two surfaces cannot disagree about what matches.
        QCOMPARE(PhosphorControl::SearchRanker::foldForSearch(QStringLiteral("Café")), QStringLiteral("cafe"));
        QCOMPARE(PhosphorControl::SearchRanker::foldForSearch(QStringLiteral("Größe")), QStringLiteral("grosse"));
        // Already-folded input is a fixed point.
        QCOMPARE(PhosphorControl::SearchRanker::foldForSearch(QStringLiteral("cafe")), QStringLiteral("cafe"));

        // End to end through the controller.
        SearchController sc(m_app);
        SearchEntry accented;
        accented.kind = SearchEntry::Kind::Setting;
        accented.pageId = QStringLiteral("general");
        accented.anchor = QStringLiteral("cafeSetting");
        accented.title = QStringLiteral("Café Mode");
        sc.addEntry(accented);

        sc.setQuery(QStringLiteral("cafe"));
        bool found = false;
        for (const QVariant& v : sc.results()) {
            if (v.toMap().value(QStringLiteral("title")).toString() == QStringLiteral("Café Mode")) {
                found = true;
            }
        }
        QVERIFY(found);
    }

    void resultsChangedEmitted()
    {
        SearchController sc(m_app);
        QSignalSpy spy(&sc, &SearchController::resultsChanged);
        sc.setQuery(QStringLiteral("general"));
        QCOMPARE(spy.count(), 1);
    }

    void hidesEntriesForPagesTheCurrentModeFilters()
    {
        // A result the active tier hides is unreachable: activating it lets
        // the app's mode gate send the user somewhere else entirely. Both
        // page entries and page-resident static entries must drop out, and
        // the index must REBUILD on the flip rather than serving the mode's
        // set from the session's first query onwards.
        ApplicationController app;
        app.registerPage(new StubPage(QStringLiteral("always")), {}, QStringLiteral("Always"),
                         QUrl(QStringLiteral("qrc:/always.qml")));
        app.registerPage(new StubPage(QStringLiteral("advanced")), {}, QStringLiteral("Advanced"),
                         QUrl(QStringLiteral("qrc:/advanced.qml")), QString(), false, false,
                         PageRegistry::PageVisibility::AdvancedOnly);

        SearchController sc(&app);
        SearchEntry setting;
        setting.kind = SearchEntry::Kind::Setting;
        setting.pageId = QStringLiteral("advanced");
        setting.title = QStringLiteral("Advanced knob");
        sc.addEntry(setting);

        // Advanced mode (the registry default): both are reachable.
        sc.setQuery(QStringLiteral("advanced"));
        QCOMPARE(sc.resultCount(), 2);

        // Flip to simple WITHOUT touching the query: the live results must
        // refresh, which only happens if the flip invalidated the index.
        app.registry()->setShowAdvanced(false);
        QCOMPARE(sc.resultCount(), 0);

        // And back again.
        app.registry()->setShowAdvanced(true);
        QCOMPARE(sc.resultCount(), 2);
        // Assert WHICH entries survive, not just how many — a future entry
        // matching "advanced" would otherwise keep the count assertion
        // passing for the wrong reason.
        QVERIFY(resultPageIds(sc).contains(QStringLiteral("advanced")));

        // The flip must re-emit so QML's resultCount/results bindings
        // refresh; a rebuild without the signal leaves the UI stale.
        QSignalSpy spy(&sc, &SearchController::resultsChanged);
        app.registry()->setShowAdvanced(false);
        QVERIFY(spy.count() > 0);
    }

    void hidesProviderEntriesForHiddenPagesToo()
    {
        // The provider loop applies the same tier gate as the static loop.
        // Pinned separately because a regression reverting only the provider
        // call site would otherwise ship green — and providers are the
        // highest-volume entry source in the real app.
        ApplicationController app;
        app.registerPage(new StubPage(QStringLiteral("hidden")), {}, QStringLiteral("Hidden"),
                         QUrl(QStringLiteral("qrc:/hidden.qml")), QString(), false, false,
                         PageRegistry::PageVisibility::AdvancedOnly);

        class HiddenPageProvider : public ISearchProvider
        {
        public:
            QVector<SearchEntry> searchEntries() const override
            {
                SearchEntry e;
                e.kind = SearchEntry::Kind::Entity;
                e.pageId = QStringLiteral("hidden");
                e.title = QStringLiteral("Zebra");
                return {e};
            }
        };
        HiddenPageProvider provider;

        SearchController sc(&app);
        sc.registerProvider(&provider);

        sc.setQuery(QStringLiteral("zebra"));
        QCOMPARE(sc.resultCount(), 1);

        app.registry()->setShowAdvanced(false);
        QCOMPARE(sc.resultCount(), 0);
    }

    void reclassifyingAPageRefreshesTheIndex()
    {
        // setPageVisibility is the late-reclassification path; the cached
        // index must learn about it.
        ApplicationController app;
        app.registerPage(new StubPage(QStringLiteral("later")), {}, QStringLiteral("Later"),
                         QUrl(QStringLiteral("qrc:/later.qml")));

        SearchController sc(&app);
        sc.setQuery(QStringLiteral("later"));
        QCOMPARE(sc.resultCount(), 1);

        app.registry()->setShowAdvanced(false);
        app.registry()->setPageVisibility(QStringLiteral("later"), PageRegistry::PageVisibility::AdvancedOnly);
        QCOMPARE(sc.resultCount(), 0);
    }

private:
    ApplicationController* m_app = nullptr;
};

QTEST_MAIN(TestSearchController)
#include "test_search_controller.moc"
