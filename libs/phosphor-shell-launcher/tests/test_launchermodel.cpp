// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// LauncherModel over two fake providers. Pins the ranking contract the
// surface relies on: rows grouped by provider, providers ordered by their
// best row, the empty-query gate, the provider filter and its Tab cycle,
// activation routing (including the alternate-action refusal), and a
// provider dying under the model.

#include <PhosphorShellLauncher/LauncherModel.h>

#include <QSignalSpy>
#include <QtTest/QtTest>

using PhosphorRegistry::ILauncherProvider;
using PhosphorRegistry::LauncherResult;
using PhosphorShellLauncher::LauncherModel;

namespace {

class FakeProvider : public ILauncherProvider
{
public:
    FakeProvider(QString id, bool listsOnEmpty, QObject* parent = nullptr)
        : ILauncherProvider(parent)
        , m_id(std::move(id))
        , m_listsOnEmpty(listsOnEmpty)
    {
    }

    QString id() const override
    {
        return m_id;
    }
    QString displayName() const override
    {
        return m_id.toUpper();
    }
    QString iconName() const override
    {
        return QStringLiteral("icon-") + m_id;
    }
    bool listsOnEmptyQuery() const override
    {
        return m_listsOnEmpty;
    }
    void setQuery(const QString& query) override
    {
        lastQuery = query;
        Q_EMIT resultsChanged();
    }
    QList<LauncherResult> results() const override
    {
        return rows;
    }
    bool activate(const QString& resultId, Activation activation) override
    {
        activated.append(
            resultId + (activation == Activation::Primary ? QStringLiteral(":primary") : QStringLiteral(":alternate")));
        return accept;
    }

    static LauncherResult row(const QString& id, int score, bool alternate = false)
    {
        LauncherResult r;
        r.id = id;
        r.title = id;
        r.score = score;
        r.primaryActionLabel = QStringLiteral("Go");
        if (alternate) {
            r.alternateActionLabel = QStringLiteral("Alt");
        }
        return r;
    }

    QString lastQuery;
    QList<LauncherResult> rows;
    QStringList activated;
    bool accept = true;

private:
    QString m_id;
    bool m_listsOnEmpty;
};

QStringList titles(const LauncherModel& m)
{
    QStringList out;
    for (int i = 0; i < m.rowCount(); ++i) {
        out.append(m.data(m.index(i), LauncherModel::TitleRole).toString());
    }
    return out;
}

} // namespace

class TestLauncherModel : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void queryIsPushedToEveryProvider();
    void rowsAreGroupedAndProvidersOrderedByBestRow();
    void emptyQueryShowsOnlyProvidersThatListOnEmpty();
    void providerFilterNarrowsAndTabCyclesThroughNonEmptyProviders();
    void activateRoutesToTheOwningProviderAndRefusesMissingAlternate();
    void providersPropertyReportsUnfilteredCounts();
    void aDestroyedProviderDropsOut();
    void aFilterOnAProviderThatGoesEmptyIsCleared();
    void countAndProvidersNotifyOnRealChangesOnly();
    void aProviderAddedAfterAQueryIsGivenIt();
    void addingTheSameProviderTwiceIsRefused();
    void roleNamesCoverEveryRoleTheSurfaceBinds();
};

// A filter is the only piece of model state the user cannot see the cause of
// when it goes wrong: the pill for an empty provider hides, so a stale filter
// shows an empty list with nothing explaining it and no way to recover.
void TestLauncherModel::aFilterOnAProviderThatGoesEmptyIsCleared()
{
    LauncherModel model;
    auto* a = new FakeProvider(QStringLiteral("a"), false, &model);
    auto* b = new FakeProvider(QStringLiteral("b"), false, &model);
    a->rows = {FakeProvider::row(QStringLiteral("a1"), 10)};
    b->rows = {FakeProvider::row(QStringLiteral("b1"), 5)};
    model.addProvider(a);
    model.addProvider(b);
    model.setQuery(QStringLiteral("x"));
    model.setProviderFilter(QStringLiteral("b"));
    QCOMPARE(titles(model), QStringList{QStringLiteral("b1")});

    QSignalSpy filterSpy(&model, &LauncherModel::providerFilterChanged);
    // The next keystroke eliminates b's only row.
    b->rows.clear();
    model.setQuery(QStringLiteral("xy"));

    QCOMPARE(model.providerFilter(), QString());
    QCOMPARE(filterSpy.count(), 1);
    QCOMPARE(titles(model), QStringList{QStringLiteral("a1")});
}

// Both are Q_PROPERTY NOTIFY signals the surface binds. Without a spy, an
// emission deleted from rebuild() leaves rowCount() and providers() correct
// while a bound view freezes at its first value, which no other assertion
// in this file would catch.
void TestLauncherModel::countAndProvidersNotifyOnRealChangesOnly()
{
    LauncherModel model;
    auto* a = new FakeProvider(QStringLiteral("a"), false, &model);
    a->rows = {FakeProvider::row(QStringLiteral("a1"), 10)};
    model.addProvider(a);

    QSignalSpy countSpy(&model, &LauncherModel::countChanged);
    QSignalSpy providersSpy(&model, &LauncherModel::providersChanged);

    model.setQuery(QStringLiteral("x"));
    QCOMPARE(model.rowCount(), 1);
    QCOMPARE(countSpy.count(), 1);
    QCOMPARE(providersSpy.count(), 1);

    // Same query, same rows: nothing changed, so neither may fire again.
    model.setQuery(QStringLiteral("x"));
    QCOMPARE(countSpy.count(), 1);
    QCOMPARE(providersSpy.count(), 1);

    a->rows.append(FakeProvider::row(QStringLiteral("a2"), 8));
    model.setQuery(QStringLiteral("y"));
    QCOMPARE(model.rowCount(), 2);
    QCOMPARE(countSpy.count(), 2);
    QCOMPARE(providersSpy.count(), 2);
}

// Every other case adds providers before the first query, so a provider
// registered later never being told the current query was invisible.
void TestLauncherModel::aProviderAddedAfterAQueryIsGivenIt()
{
    LauncherModel model;
    model.setQuery(QStringLiteral("fire"));
    auto* late = new FakeProvider(QStringLiteral("late"), false, &model);
    model.addProvider(late);
    QCOMPARE(late->lastQuery, QStringLiteral("fire"));
}

void TestLauncherModel::addingTheSameProviderTwiceIsRefused()
{
    LauncherModel model;
    auto* a = new FakeProvider(QStringLiteral("a"), false, &model);
    a->rows = {FakeProvider::row(QStringLiteral("a1"), 10)};
    model.addProvider(a);
    model.addProvider(a);
    model.setQuery(QStringLiteral("x"));
    // A second registration would duplicate the row source and install a
    // second resultsChanged connection, doubling every rebuild.
    QCOMPARE(titles(model), QStringList{QStringLiteral("a1")});
}

// The C++ suite reaches every role through the enum, so a wrong or missing
// role NAME breaks every QML binding on the surface while staying green.
void TestLauncherModel::roleNamesCoverEveryRoleTheSurfaceBinds()
{
    LauncherModel model;
    const QHash<int, QByteArray> names = model.roleNames();
    QCOMPARE(names.value(LauncherModel::TitleRole), QByteArrayLiteral("title"));
    QCOMPARE(names.value(LauncherModel::SubtitleRole), QByteArrayLiteral("subtitle"));
    QCOMPARE(names.value(LauncherModel::IconNameRole), QByteArrayLiteral("iconName"));
    QCOMPARE(names.value(LauncherModel::ProviderIdRole), QByteArrayLiteral("providerId"));
    QCOMPARE(names.value(LauncherModel::ProviderNameRole), QByteArrayLiteral("providerName"));
    QCOMPARE(names.value(LauncherModel::ProviderIconRole), QByteArrayLiteral("providerIcon"));
    QCOMPARE(names.value(LauncherModel::ResultIdRole), QByteArrayLiteral("resultId"));
    QCOMPARE(names.value(LauncherModel::PrimaryActionLabelRole), QByteArrayLiteral("primaryActionLabel"));
    QCOMPARE(names.value(LauncherModel::AlternateActionLabelRole), QByteArrayLiteral("alternateActionLabel"));
    QCOMPARE(names.value(LauncherModel::HasAlternateActionRole), QByteArrayLiteral("hasAlternateAction"));
    QCOMPARE(names.value(LauncherModel::ScoreRole), QByteArrayLiteral("score"));
}

void TestLauncherModel::queryIsPushedToEveryProvider()
{
    LauncherModel model;
    auto* a = new FakeProvider(QStringLiteral("a"), false, &model);
    auto* b = new FakeProvider(QStringLiteral("b"), false, &model);
    model.addProvider(a);
    model.addProvider(b);
    QSignalSpy queryChanged(&model, &LauncherModel::queryChanged);

    model.setQuery(QStringLiteral("fire"));
    QCOMPARE(a->lastQuery, QStringLiteral("fire"));
    QCOMPARE(b->lastQuery, QStringLiteral("fire"));
    QCOMPARE(queryChanged.count(), 1);
    // Same query again: no re-push, no notify.
    model.setQuery(QStringLiteral("fire"));
    QCOMPARE(queryChanged.count(), 1);
}

void TestLauncherModel::rowsAreGroupedAndProvidersOrderedByBestRow()
{
    LauncherModel model;
    auto* apps = new FakeProvider(QStringLiteral("apps"), false, &model);
    auto* calc = new FakeProvider(QStringLiteral("calc"), false, &model);
    model.addProvider(apps);
    model.addProvider(calc);

    // apps registered first, but calc's single row scores highest, so
    // calc's section leads; within apps the rows sort by score.
    apps->rows = {FakeProvider::row(QStringLiteral("a-low"), 10), FakeProvider::row(QStringLiteral("a-high"), 50)};
    calc->rows = {FakeProvider::row(QStringLiteral("answer"), 999)};
    model.setQuery(QStringLiteral("x"));

    QCOMPARE(titles(model), (QStringList{QStringLiteral("answer"), QStringLiteral("a-high"), QStringLiteral("a-low")}));
    QCOMPARE(model.data(model.index(0), LauncherModel::ProviderIdRole).toString(), QStringLiteral("calc"));
    QCOMPARE(model.data(model.index(1), LauncherModel::ProviderIdRole).toString(), QStringLiteral("apps"));
    QCOMPARE(model.data(model.index(1), LauncherModel::ProviderNameRole).toString(), QStringLiteral("APPS"));
    // A row with no icon of its own falls back to the provider's glyph.
    QCOMPARE(model.data(model.index(1), LauncherModel::IconNameRole).toString(), QStringLiteral("icon-apps"));

    // Equal best scores: registration order breaks the tie.
    calc->rows = {FakeProvider::row(QStringLiteral("answer"), 50)};
    model.setQuery(QStringLiteral("y"));
    QCOMPARE(model.data(model.index(0), LauncherModel::ProviderIdRole).toString(), QStringLiteral("apps"));
}

void TestLauncherModel::emptyQueryShowsOnlyProvidersThatListOnEmpty()
{
    LauncherModel model;
    auto* apps = new FakeProvider(QStringLiteral("apps"), false, &model);
    auto* windows = new FakeProvider(QStringLiteral("windows"), true, &model);
    model.addProvider(apps);
    model.addProvider(windows);
    apps->rows = {FakeProvider::row(QStringLiteral("app"), 5)};
    windows->rows = {FakeProvider::row(QStringLiteral("win"), 0)};

    model.setQuery(QString());
    // apps has rows but does not list on empty; windows does.
    model.setProviderFilter(QString());
    apps->setQuery(QString()); // re-announce with the empty query in place
    QCOMPARE(titles(model), QStringList{QStringLiteral("win")});

    model.setQuery(QStringLiteral("a"));
    QCOMPARE(titles(model), (QStringList{QStringLiteral("app"), QStringLiteral("win")}));
}

void TestLauncherModel::providerFilterNarrowsAndTabCyclesThroughNonEmptyProviders()
{
    LauncherModel model;
    auto* a = new FakeProvider(QStringLiteral("a"), false, &model);
    auto* b = new FakeProvider(QStringLiteral("b"), false, &model);
    auto* c = new FakeProvider(QStringLiteral("c"), false, &model);
    model.addProvider(a);
    model.addProvider(b);
    model.addProvider(c);
    a->rows = {FakeProvider::row(QStringLiteral("a1"), 10)};
    b->rows = {}; // nothing for this query
    c->rows = {FakeProvider::row(QStringLiteral("c1"), 10)};
    model.setQuery(QStringLiteral("q"));
    QCOMPARE(model.rowCount(), 2);

    model.setProviderFilter(QStringLiteral("c"));
    QCOMPARE(titles(model), QStringList{QStringLiteral("c1")});

    // Tab from "all": a, then c (b is skipped, it has no rows), then all.
    model.setProviderFilter(QString());
    model.cycleProviderFilter(1);
    QCOMPARE(model.providerFilter(), QStringLiteral("a"));
    model.cycleProviderFilter(1);
    QCOMPARE(model.providerFilter(), QStringLiteral("c"));
    model.cycleProviderFilter(1);
    QCOMPARE(model.providerFilter(), QString());
    // Shift+Tab goes the other way.
    model.cycleProviderFilter(-1);
    QCOMPARE(model.providerFilter(), QStringLiteral("c"));
}

void TestLauncherModel::activateRoutesToTheOwningProviderAndRefusesMissingAlternate()
{
    LauncherModel model;
    auto* a = new FakeProvider(QStringLiteral("a"), false, &model);
    auto* b = new FakeProvider(QStringLiteral("b"), false, &model);
    model.addProvider(a);
    model.addProvider(b);
    a->rows = {FakeProvider::row(QStringLiteral("a1"), 20, /*alternate=*/true)};
    b->rows = {FakeProvider::row(QStringLiteral("b1"), 10)};
    model.setQuery(QStringLiteral("q"));

    QVERIFY(model.activate(0));
    QCOMPARE(a->activated, QStringList{QStringLiteral("a1:primary")});
    QVERIFY(model.activate(0, true));
    QCOMPARE(a->activated.last(), QStringLiteral("a1:alternate"));

    // b1 offers no alternate: refused at the model, provider never asked.
    QVERIFY(!model.activate(1, true));
    QVERIFY(b->activated.isEmpty());
    QVERIFY(model.activate(1));
    QCOMPARE(b->activated, QStringList{QStringLiteral("b1:primary")});

    // Out of range, and a provider that refuses.
    QVERIFY(!model.activate(5));
    QVERIFY(!model.activate(-1));
    b->accept = false;
    QVERIFY(!model.activate(1));
}

void TestLauncherModel::providersPropertyReportsUnfilteredCounts()
{
    LauncherModel model;
    auto* a = new FakeProvider(QStringLiteral("a"), false, &model);
    auto* b = new FakeProvider(QStringLiteral("b"), false, &model);
    model.addProvider(a);
    model.addProvider(b);
    a->rows = {FakeProvider::row(QStringLiteral("a1"), 1), FakeProvider::row(QStringLiteral("a2"), 1)};
    b->rows = {FakeProvider::row(QStringLiteral("b1"), 1)};
    model.setQuery(QStringLiteral("q"));
    model.setProviderFilter(QStringLiteral("b"));

    const QVariantList providers = model.providers();
    QCOMPARE(providers.size(), 2);
    const QVariantMap first = providers.at(0).toMap();
    QCOMPARE(first.value(QStringLiteral("id")).toString(), QStringLiteral("a"));
    QCOMPARE(first.value(QStringLiteral("name")).toString(), QStringLiteral("A"));
    // Counts are for the query, not the filter: a still reports 2 while
    // the filter shows only b's row, so the pill can say "A 2".
    QCOMPARE(first.value(QStringLiteral("count")).toInt(), 2);
    QCOMPARE(model.rowCount(), 1);
}

void TestLauncherModel::aDestroyedProviderDropsOut()
{
    LauncherModel model;
    auto* a = new FakeProvider(QStringLiteral("a"), false);
    auto* b = new FakeProvider(QStringLiteral("b"), false, &model);
    model.addProvider(a);
    model.addProvider(b);
    a->rows = {FakeProvider::row(QStringLiteral("a1"), 1)};
    b->rows = {FakeProvider::row(QStringLiteral("b1"), 1)};
    model.setQuery(QStringLiteral("q"));
    QCOMPARE(model.rowCount(), 2);

    delete a;
    QCOMPARE(model.rowCount(), 1);
    QCOMPARE(titles(model), QStringList{QStringLiteral("b1")});
    QCOMPARE(model.providers().size(), 1);
}

QTEST_GUILESS_MAIN(TestLauncherModel)

#include "test_launchermodel.moc"
