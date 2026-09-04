// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// ClipboardProvider, against a fake service standing in for
// phosphor-service-clipboard.
//
// The provider reaches its service entirely by name: a `history`
// QAbstractItemModel with "preview", "mimeType" and "timestamp" roles, and
// `bool copy(int)` / `bool remove(int)` slots. That duck-typed contract is
// what this fake implements, and it is the contract worth pinning, since
// nothing else in the build checks it.
//
// The activation cases matter beyond this one provider. Every launcher
// provider had its refusal legs covered and not one had a call that
// returned true, so a provider that resolved a row and then did nothing
// would have passed the suite.

#include <PhosphorRegistry/ILauncherProvider.h>
#include <PhosphorShellLauncher/ClipboardProvider.h>

#include <QAbstractListModel>
#include <QHash>
#include <QList>
#include <QObject>
#include <QString>
#include <QTest>

using PhosphorShellLauncher::ClipboardProvider;
using Activation = PhosphorRegistry::ILauncherProvider::Activation;

namespace {

struct Entry
{
    QString preview;
    QString mimeType;
    QString timestamp;
};

// The history model. Roles are named exactly as the provider's documented
// contract requires; renaming one here is the same as the service renaming
// it, and the provider should then find nothing rather than misread a row.
class FakeHistory : public QAbstractListModel
{
    Q_OBJECT

public:
    enum Roles {
        PreviewRole = Qt::UserRole + 1,
        MimeRole,
        TimestampRole
    };

    explicit FakeHistory(QList<Entry> entries, QObject* parent = nullptr)
        : QAbstractListModel(parent)
        , m_entries(std::move(entries))
    {
    }

    int rowCount(const QModelIndex& parent = QModelIndex()) const override
    {
        return parent.isValid() ? 0 : static_cast<int>(m_entries.size());
    }

    QVariant data(const QModelIndex& index, int role) const override
    {
        if (!index.isValid() || index.row() < 0 || index.row() >= m_entries.size()) {
            return {};
        }
        const Entry& e = m_entries.at(index.row());
        switch (role) {
        case PreviewRole:
            return e.preview;
        case MimeRole:
            return e.mimeType;
        case TimestampRole:
            return e.timestamp;
        default:
            return {};
        }
    }

    QHash<int, QByteArray> roleNames() const override
    {
        return {{PreviewRole, "preview"}, {MimeRole, "mimeType"}, {TimestampRole, "timestamp"}};
    }

    void removeAt(int row)
    {
        beginRemoveRows(QModelIndex(), row, row);
        m_entries.removeAt(row);
        endRemoveRows();
    }

private:
    QList<Entry> m_entries;
};

// The service facade. `history`, `copy` and `remove` are all the provider
// knows about.
class FakeService : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QAbstractItemModel* history READ history CONSTANT)

public:
    explicit FakeService(QList<Entry> entries, QObject* parent = nullptr)
        : QObject(parent)
        , m_history(new FakeHistory(std::move(entries), this))
    {
    }

    QAbstractItemModel* history() const
    {
        return m_history;
    }

    Q_INVOKABLE bool copy(int row)
    {
        copiedRows.append(row);
        return !refuse;
    }

    Q_INVOKABLE bool remove(int row)
    {
        removedRows.append(row);
        if (refuse) {
            return false;
        }
        m_history->removeAt(row);
        return true;
    }

    FakeHistory* model() const
    {
        return m_history;
    }

    QList<int> copiedRows;
    QList<int> removedRows;
    // Drives the leg where the service resolves the row and then declines
    // to act on it: an entry with empty content, say.
    bool refuse = false;

private:
    FakeHistory* m_history;
};

QList<Entry> sampleEntries()
{
    return {{QStringLiteral("https://example.invalid/kittens"), QStringLiteral("text/plain"), QStringLiteral("300")},
            {QStringLiteral("git rebase --interactive"), QStringLiteral("text/plain"), QStringLiteral("200")},
            {QStringLiteral("a quiet paragraph of prose"), QStringLiteral("text/plain"), QStringLiteral("100")}};
}

} // namespace

class TestClipboardProvider : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void aNullServiceIsInertRatherThanFatal();
    void anEmptyQueryListsTheWholeHistoryInOrder();
    void aQueryFuzzyMatchesThePreviewText();
    void theCapAppliesAfterRankingNotBeforeIt();
    void primaryActivationCopiesTheRowTheIdNames();
    void alternateActivationRemovesTheRow();
    void activationRefusesWhenTheEntryHasLeftHistory();
    void activationReportsTheServicesOwnRefusal();
};

void TestClipboardProvider::aNullServiceIsInertRatherThanFatal()
{
    ClipboardProvider provider(nullptr);
    provider.setQuery(QString());
    QVERIFY(provider.results().isEmpty());
    QVERIFY(!provider.activate(QStringLiteral("300"), Activation::Primary));
}

void TestClipboardProvider::anEmptyQueryListsTheWholeHistoryInOrder()
{
    FakeService service(sampleEntries());
    ClipboardProvider provider(&service);
    QVERIFY(provider.listsOnEmptyQuery());

    provider.setQuery(QString());
    const auto results = provider.results();
    QCOMPARE(results.size(), 3);
    // Model order, most recent first, unranked: with no query there is
    // nothing to rank by.
    QCOMPARE(results.at(0).id, QStringLiteral("300"));
    QCOMPARE(results.at(2).id, QStringLiteral("100"));
    QCOMPARE(results.at(1).title, QStringLiteral("git rebase --interactive"));
    QCOMPARE(results.at(1).subtitle, QStringLiteral("text/plain"));
}

void TestClipboardProvider::aQueryFuzzyMatchesThePreviewText()
{
    FakeService service(sampleEntries());
    ClipboardProvider provider(&service);

    provider.setQuery(QStringLiteral("rebase"));
    const auto results = provider.results();
    QCOMPARE(results.size(), 1);
    QCOMPARE(results.first().id, QStringLiteral("200"));

    provider.setQuery(QStringLiteral("nothing here matches this"));
    QVERIFY(provider.results().isEmpty());
}

void TestClipboardProvider::theCapAppliesAfterRankingNotBeforeIt()
{
    FakeService service(sampleEntries());
    ClipboardProvider provider(&service);
    provider.setMaximumResults(1);

    // "prose" only matches the LAST entry in history order. A cap applied
    // during the scan would have kept an earlier, worse row and dropped
    // this one.
    provider.setQuery(QStringLiteral("prose"));
    const auto results = provider.results();
    QCOMPARE(results.size(), 1);
    QCOMPARE(results.first().id, QStringLiteral("100"));
}

void TestClipboardProvider::primaryActivationCopiesTheRowTheIdNames()
{
    FakeService service(sampleEntries());
    ClipboardProvider provider(&service);
    provider.setQuery(QString());

    QVERIFY2(provider.activate(QStringLiteral("200"), Activation::Primary), "the copy really happened");
    // Re-resolved by timestamp at activation time, not by a captured index.
    QCOMPARE(service.copiedRows, QList<int>{1});
    QVERIFY(service.removedRows.isEmpty());
}

void TestClipboardProvider::alternateActivationRemovesTheRow()
{
    FakeService service(sampleEntries());
    ClipboardProvider provider(&service);
    provider.setQuery(QString());

    QVERIFY(provider.activate(QStringLiteral("300"), Activation::Alternate));
    QCOMPARE(service.removedRows, QList<int>{0});
    QVERIFY(service.copiedRows.isEmpty());
    QCOMPARE(service.model()->rowCount(), 2);
}

void TestClipboardProvider::activationRefusesWhenTheEntryHasLeftHistory()
{
    FakeService service(sampleEntries());
    ClipboardProvider provider(&service);
    provider.setQuery(QString());

    // History shifts under the user between typing and Enter, which is the
    // whole reason rows are addressed by timestamp.
    service.model()->removeAt(1);
    QVERIFY(!provider.activate(QStringLiteral("200"), Activation::Primary));
    QVERIFY(service.copiedRows.isEmpty());
}

void TestClipboardProvider::activationReportsTheServicesOwnRefusal()
{
    FakeService service(sampleEntries());
    service.refuse = true;
    ClipboardProvider provider(&service);
    provider.setQuery(QString());

    // The service was asked and declined. Reporting success here closed the
    // launcher on a copy that never happened.
    QVERIFY(!provider.activate(QStringLiteral("300"), Activation::Primary));
    QCOMPARE(service.copiedRows, QList<int>{0});
}

QTEST_GUILESS_MAIN(TestClipboardProvider)
#include "test_clipboardprovider.moc"
