// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorServiceSni/StatusNotifierHost.h>
#include <PhosphorServiceSni/StatusNotifierItem.h>
#include <PhosphorServiceSni/StatusNotifierItemModel.h>

#include <QSignalSpy>
#include <QtTest/QtTest>

using namespace PhosphorServiceSni;

class TestItemModel : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void emptyWithoutHost()
    {
        StatusNotifierItemModel m;
        QCOMPARE(m.rowCount(), 0);
        QVERIFY(!m.host());
    }

    void rolesCoverEverythingWeNeed()
    {
        StatusNotifierItemModel m;
        const auto roles = m.roleNames();
        // The QML side relies on these specific role names; bind names
        // are a public contract (renaming them silently breaks the
        // shell's Repeater delegates). Compare per-enum so a swap of
        // names between two roles fails the test instead of passing
        // (which `values().contains` would allow).
        QCOMPARE(roles[StatusNotifierItemModel::IdRole], QByteArrayLiteral("itemId"));
        QCOMPARE(roles[StatusNotifierItemModel::TitleRole], QByteArrayLiteral("title"));
        QCOMPARE(roles[StatusNotifierItemModel::CategoryRole], QByteArrayLiteral("category"));
        QCOMPARE(roles[StatusNotifierItemModel::StatusRole], QByteArrayLiteral("status"));
        QCOMPARE(roles[StatusNotifierItemModel::IconUrlRole], QByteArrayLiteral("iconUrl"));
        QCOMPARE(roles[StatusNotifierItemModel::OverlayIconUrlRole], QByteArrayLiteral("overlayIconUrl"));
        QCOMPARE(roles[StatusNotifierItemModel::AttentionIconUrlRole], QByteArrayLiteral("attentionIconUrl"));
        QCOMPARE(roles[StatusNotifierItemModel::IconImageRole], QByteArrayLiteral("iconImage"));
        QCOMPARE(roles[StatusNotifierItemModel::OverlayIconImageRole], QByteArrayLiteral("overlayIconImage"));
        QCOMPARE(roles[StatusNotifierItemModel::AttentionIconImageRole], QByteArrayLiteral("attentionIconImage"));
        QCOMPARE(roles[StatusNotifierItemModel::ToolTipTitleRole], QByteArrayLiteral("toolTipTitle"));
        QCOMPARE(roles[StatusNotifierItemModel::ToolTipBodyRole], QByteArrayLiteral("toolTipBody"));
        QCOMPARE(roles[StatusNotifierItemModel::MenuPathRole], QByteArrayLiteral("menuPath"));
        QCOMPARE(roles[StatusNotifierItemModel::ItemIsMenuRole], QByteArrayLiteral("itemIsMenu"));
        QCOMPARE(roles[StatusNotifierItemModel::DBusServiceRole], QByteArrayLiteral("dbusService"));
        QCOMPARE(roles[StatusNotifierItemModel::DBusPathRole], QByteArrayLiteral("dbusPath"));
        QCOMPARE(roles[StatusNotifierItemModel::ItemObjectRole], QByteArrayLiteral("item"));
    }

    void hostChangeFiresSignal()
    {
        StatusNotifierItemModel m;
        QSignalSpy spy(&m, &StatusNotifierItemModel::hostChanged);

        StatusNotifierHost host;
        m.setHost(&host);
        QCOMPARE(spy.count(), 1);
        QCOMPARE(m.host(), &host);

        // Re-assigning the same host is a no-op.
        m.setHost(&host);
        QCOMPARE(spy.count(), 1);
    }

    void detachingHostClearsCount()
    {
        StatusNotifierItemModel m;
        StatusNotifierHost host;
        m.setHost(&host);
        m.setHost(nullptr);
        QCOMPARE(m.rowCount(), 0);
        QVERIFY(!m.host());
    }

    void invalidRowActionsAreSafe()
    {
        StatusNotifierItemModel m;
        // Calling activate/contextMenu/etc on an empty model must not
        // crash: the QML side may fire late mouse events while items
        // are being torn down.
        m.activate(-1, 0, 0);
        m.activate(0, 0, 0);
        m.activate(1000, 0, 0);
        m.contextMenu(-1, 0, 0);
        m.secondaryActivate(0, 0, 0);
        m.scroll(0, 1, QStringLiteral("vertical"));
        QVERIFY(true); // reached without crashing
    }
};

QTEST_MAIN(TestItemModel)
#include "test_itemmodel.moc"
