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
        // The QML side relies on these specific role names — bind names
        // are a public contract (renaming them silently breaks the
        // shell's Repeater delegates).
        QVERIFY(roles.values().contains("itemId"));
        QVERIFY(roles.values().contains("title"));
        QVERIFY(roles.values().contains("iconUrl"));
        QVERIFY(roles.values().contains("iconImage"));
        QVERIFY(roles.values().contains("status"));
        QVERIFY(roles.values().contains("toolTipTitle"));
        QVERIFY(roles.values().contains("toolTipBody"));
        QVERIFY(roles.values().contains("menuPath"));
        QVERIFY(roles.values().contains("dbusService"));
        QVERIFY(roles.values().contains("dbusPath"));
        QVERIFY(roles.values().contains("item"));
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
        // crash — the QML side may fire late mouse events while items
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
