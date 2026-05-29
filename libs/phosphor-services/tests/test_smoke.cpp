// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorServices/StatusNotifierItemModel.h>

#include <QGuiApplication>
#include <QtTest/QtTest>

class TestSmoke : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void modelWithoutHostIsEmpty()
    {
        PhosphorServices::StatusNotifierItemModel model;
        QCOMPARE(model.rowCount(), 0);
        QVERIFY(!model.host());
    }

    void modelRolesArePresent()
    {
        PhosphorServices::StatusNotifierItemModel model;
        const auto roles = model.roleNames();
        QVERIFY(roles.contains(PhosphorServices::StatusNotifierItemModel::IdRole));
        QVERIFY(roles.contains(PhosphorServices::StatusNotifierItemModel::TitleRole));
        QVERIFY(roles.contains(PhosphorServices::StatusNotifierItemModel::IconImageRole));
        QVERIFY(roles.contains(PhosphorServices::StatusNotifierItemModel::ItemObjectRole));
    }
};

QTEST_MAIN(TestSmoke)
#include "test_smoke.moc"
