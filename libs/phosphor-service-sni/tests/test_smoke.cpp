// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorServiceSni/StatusNotifierItemModel.h>

#include <QGuiApplication>
#include <QtTest/QtTest>

class TestSmoke : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void modelWithoutHostIsEmpty()
    {
        PhosphorServiceSni::StatusNotifierItemModel model;
        QCOMPARE(model.rowCount(), 0);
        QVERIFY(!model.host());
    }

    void modelRolesArePresent()
    {
        PhosphorServiceSni::StatusNotifierItemModel model;
        const auto roles = model.roleNames();
        QVERIFY(roles.contains(PhosphorServiceSni::StatusNotifierItemModel::IdRole));
        QVERIFY(roles.contains(PhosphorServiceSni::StatusNotifierItemModel::TitleRole));
        QVERIFY(roles.contains(PhosphorServiceSni::StatusNotifierItemModel::IconImageRole));
        QVERIFY(roles.contains(PhosphorServiceSni::StatusNotifierItemModel::ItemObjectRole));
    }
};

QTEST_MAIN(TestSmoke)
#include "test_smoke.moc"
