// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// WindowsProvider over a fake toplevel model.
//
// Like the clipboard provider, this one reaches its source by name: a model
// whose rows carry a "toplevel" role, each of which is an object with
// `title` and `appId` properties, `titleChanged` / `appIdChanged`
// notifications, and an `activate()` method. This library does not link the
// Wayland toplevel type, so that duck-typed contract is the real interface
// and had no test at all.

#include <PhosphorRegistry/ILauncherProvider.h>
#include <PhosphorShellLauncher/WindowsProvider.h>

#include <QAbstractListModel>
#include <QSignalSpy>
#include <QtTest/QtTest>

using PhosphorShellLauncher::WindowsProvider;
using Activation = PhosphorRegistry::ILauncherProvider::Activation;

namespace {

class FakeToplevel : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString title READ title NOTIFY titleChanged)
    Q_PROPERTY(QString appId READ appId NOTIFY appIdChanged)

public:
    FakeToplevel(QString title, QString appId, QObject* parent = nullptr)
        : QObject(parent)
        , m_title(std::move(title))
        , m_appId(std::move(appId))
    {
    }

    QString title() const
    {
        return m_title;
    }
    QString appId() const
    {
        return m_appId;
    }

    void retitle(const QString& t)
    {
        m_title = t;
        Q_EMIT titleChanged();
    }

    Q_INVOKABLE void activate()
    {
        activations++;
    }

    int activations = 0;

Q_SIGNALS:
    void titleChanged();
    void appIdChanged();

private:
    QString m_title;
    QString m_appId;
};

class FakeToplevelModel : public QAbstractListModel
{
    Q_OBJECT

public:
    enum Roles {
        ToplevelRole = Qt::UserRole + 1
    };

    explicit FakeToplevelModel(QObject* parent = nullptr)
        : QAbstractListModel(parent)
    {
    }

    int rowCount(const QModelIndex& parent = QModelIndex()) const override
    {
        return parent.isValid() ? 0 : static_cast<int>(m_rows.size());
    }

    QVariant data(const QModelIndex& index, int role) const override
    {
        if (!index.isValid() || role != ToplevelRole || index.row() >= m_rows.size()) {
            return {};
        }
        return QVariant::fromValue(static_cast<QObject*>(m_rows.at(index.row())));
    }

    QHash<int, QByteArray> roleNames() const override
    {
        return {{ToplevelRole, "toplevel"}};
    }

    FakeToplevel* append(const QString& title, const QString& appId)
    {
        auto* t = new FakeToplevel(title, appId, this);
        beginInsertRows(QModelIndex(), static_cast<int>(m_rows.size()), static_cast<int>(m_rows.size()));
        m_rows.append(t);
        endInsertRows();
        return t;
    }

private:
    QList<FakeToplevel*> m_rows;
};

QStringList titles(const WindowsProvider& p)
{
    QStringList out;
    for (const auto& r : p.results()) {
        out.append(r.title);
    }
    return out;
}

} // namespace

class TestWindowsProvider : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void aNullModelIsInertRatherThanFatal();
    void aModelWithoutTheRoleIsRefused();
    void anEmptyQueryListsEveryWindow();
    void aQueryMatchesTitleAndAppId();
    void aWindowThatRenamesItselfUpdatesItsRow();
    void anAppIdShapedLikeAPathIsNotUsedAsAnIconName();
    void activationSwitchesToTheWindowTheIdNames();
};

void TestWindowsProvider::aNullModelIsInertRatherThanFatal()
{
    WindowsProvider provider(nullptr);
    provider.setQuery(QString());
    QVERIFY(provider.results().isEmpty());
    QVERIFY(!provider.activate(QStringLiteral("1"), Activation::Primary));
}

void TestWindowsProvider::aModelWithoutTheRoleIsRefused()
{
    // A plain string list model: no "toplevel" role, so there is nothing the
    // provider can read. It goes inert rather than reading row 0 blindly.
    QStringListModel wrong({QStringLiteral("a")});
    WindowsProvider provider(&wrong);
    provider.setQuery(QString());
    QVERIFY(provider.results().isEmpty());
}

void TestWindowsProvider::anEmptyQueryListsEveryWindow()
{
    FakeToplevelModel model;
    model.append(QStringLiteral("Inbox"), QStringLiteral("org.kde.kmail"));
    model.append(QStringLiteral("draft.md"), QStringLiteral("org.kde.kate"));

    WindowsProvider provider(&model);
    QVERIFY(provider.listsOnEmptyQuery());
    provider.setQuery(QString());
    QCOMPARE(titles(provider), (QStringList{QStringLiteral("Inbox"), QStringLiteral("draft.md")}));
}

void TestWindowsProvider::aQueryMatchesTitleAndAppId()
{
    FakeToplevelModel model;
    model.append(QStringLiteral("Inbox"), QStringLiteral("org.kde.kmail"));
    model.append(QStringLiteral("draft.md"), QStringLiteral("org.kde.kate"));

    WindowsProvider provider(&model);
    provider.setQuery(QStringLiteral("inbox"));
    QCOMPARE(titles(provider), QStringList{QStringLiteral("Inbox")});

    // The app id is matched too, so a user who thinks in application names
    // finds the window whose title says nothing about it.
    provider.setQuery(QStringLiteral("kate"));
    QCOMPARE(titles(provider), QStringList{QStringLiteral("draft.md")});

    provider.setQuery(QStringLiteral("nothing matches this"));
    QVERIFY(provider.results().isEmpty());
}

void TestWindowsProvider::aWindowThatRenamesItselfUpdatesItsRow()
{
    FakeToplevelModel model;
    FakeToplevel* window = model.append(QStringLiteral("Inbox"), QStringLiteral("org.kde.kmail"));

    WindowsProvider provider(&model);
    provider.setQuery(QString());
    QCOMPARE(titles(provider), QStringList{QStringLiteral("Inbox")});

    // The list model announces rows arriving and leaving, not a window
    // retitling itself, and a browser or an editor retitles constantly. The
    // provider subscribes to each toplevel's own notification; without that
    // the launcher showed the title a window had when it opened for the rest
    // of the session.
    QSignalSpy spy(&provider, &WindowsProvider::resultsChanged);
    window->retitle(QStringLiteral("Sent"));
    QCOMPARE(spy.count(), 1);
    QCOMPARE(titles(provider), QStringList{QStringLiteral("Sent")});

    // And the row is findable under the new title.
    provider.setQuery(QStringLiteral("sent"));
    QCOMPARE(titles(provider), QStringList{QStringLiteral("Sent")});
}

void TestWindowsProvider::anAppIdShapedLikeAPathIsNotUsedAsAnIconName()
{
    FakeToplevelModel model;
    model.append(QStringLiteral("Sketchy"), QStringLiteral("/tmp/evil.png"));

    WindowsProvider provider(&model);
    provider.setQuery(QString());
    QCOMPARE(provider.results().size(), 1);
    // An icon source that looks like a path is loaded as a file, and the app
    // id is client-controlled, so only theme-name shaped values pass through.
    QVERIFY(provider.results().first().iconName.isEmpty());
}

void TestWindowsProvider::activationSwitchesToTheWindowTheIdNames()
{
    FakeToplevelModel model;
    model.append(QStringLiteral("Inbox"), QStringLiteral("org.kde.kmail"));
    FakeToplevel* second = model.append(QStringLiteral("draft.md"), QStringLiteral("org.kde.kate"));

    WindowsProvider provider(&model);
    provider.setQuery(QString());
    const QString id = provider.results().at(1).id;

    QVERIFY(provider.activate(id, Activation::Primary));
    QCOMPARE(second->activations, 1);

    // There is no alternate action, and an id nobody issued is refused.
    QVERIFY(!provider.activate(id, Activation::Alternate));
    QTest::ignoreMessage(QtWarningMsg, QRegularExpression(QStringLiteral("window is gone")));
    QVERIFY(!provider.activate(QStringLiteral("999"), Activation::Primary));
    QCOMPARE(second->activations, 1);
}

QTEST_GUILESS_MAIN(TestWindowsProvider)
#include "test_windowsprovider.moc"
