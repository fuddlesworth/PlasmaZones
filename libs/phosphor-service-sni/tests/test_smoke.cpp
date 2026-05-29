// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorServiceSni/StatusNotifierItemModel.h>

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

    void modelRolesArePinned()
    {
        using Model = PhosphorServiceSni::StatusNotifierItemModel;
        Model model;
        const auto roles = model.roleNames();

        // Pin enum integer values. The Roles enum starts at Qt::UserRole + 1
        // and is contiguous; QML callers that read role ids out of a
        // Repeater (or any cross-module consumer using qmlRegisterUncreatable
        // metatypes) hard-codes these integers. Reordering the enum would
        // silently shift the mapping; this test makes that a build break.
        QCOMPARE(static_cast<int>(Model::IdRole), int(Qt::UserRole) + 1);
        QCOMPARE(static_cast<int>(Model::TitleRole), int(Qt::UserRole) + 2);
        QCOMPARE(static_cast<int>(Model::CategoryRole), int(Qt::UserRole) + 3);
        QCOMPARE(static_cast<int>(Model::StatusRole), int(Qt::UserRole) + 4);
        QCOMPARE(static_cast<int>(Model::IconUrlRole), int(Qt::UserRole) + 5);
        QCOMPARE(static_cast<int>(Model::OverlayIconUrlRole), int(Qt::UserRole) + 6);
        QCOMPARE(static_cast<int>(Model::AttentionIconUrlRole), int(Qt::UserRole) + 7);
        QCOMPARE(static_cast<int>(Model::IconImageRole), int(Qt::UserRole) + 8);
        QCOMPARE(static_cast<int>(Model::OverlayIconImageRole), int(Qt::UserRole) + 9);
        QCOMPARE(static_cast<int>(Model::AttentionIconImageRole), int(Qt::UserRole) + 10);
        QCOMPARE(static_cast<int>(Model::ToolTipTitleRole), int(Qt::UserRole) + 11);
        QCOMPARE(static_cast<int>(Model::ToolTipBodyRole), int(Qt::UserRole) + 12);
        QCOMPARE(static_cast<int>(Model::MenuPathRole), int(Qt::UserRole) + 13);
        QCOMPARE(static_cast<int>(Model::ItemIsMenuRole), int(Qt::UserRole) + 14);
        QCOMPARE(static_cast<int>(Model::DBusServiceRole), int(Qt::UserRole) + 15);
        QCOMPARE(static_cast<int>(Model::DBusPathRole), int(Qt::UserRole) + 16);
        QCOMPARE(static_cast<int>(Model::ItemObjectRole), int(Qt::UserRole) + 17);

        // Pin the byte-string names mapped to each role enum. QML
        // delegates bind against these literals; renaming a role
        // string silently breaks every shell consumer.
        QCOMPARE(roles[Model::IdRole], QByteArrayLiteral("itemId"));
        QCOMPARE(roles[Model::TitleRole], QByteArrayLiteral("title"));
        QCOMPARE(roles[Model::CategoryRole], QByteArrayLiteral("category"));
        QCOMPARE(roles[Model::StatusRole], QByteArrayLiteral("status"));
        QCOMPARE(roles[Model::IconUrlRole], QByteArrayLiteral("iconUrl"));
        QCOMPARE(roles[Model::OverlayIconUrlRole], QByteArrayLiteral("overlayIconUrl"));
        QCOMPARE(roles[Model::AttentionIconUrlRole], QByteArrayLiteral("attentionIconUrl"));
        QCOMPARE(roles[Model::IconImageRole], QByteArrayLiteral("iconImage"));
        QCOMPARE(roles[Model::OverlayIconImageRole], QByteArrayLiteral("overlayIconImage"));
        QCOMPARE(roles[Model::AttentionIconImageRole], QByteArrayLiteral("attentionIconImage"));
        QCOMPARE(roles[Model::ToolTipTitleRole], QByteArrayLiteral("toolTipTitle"));
        QCOMPARE(roles[Model::ToolTipBodyRole], QByteArrayLiteral("toolTipBody"));
        QCOMPARE(roles[Model::MenuPathRole], QByteArrayLiteral("menuPath"));
        QCOMPARE(roles[Model::ItemIsMenuRole], QByteArrayLiteral("itemIsMenu"));
        QCOMPARE(roles[Model::DBusServiceRole], QByteArrayLiteral("dbusService"));
        QCOMPARE(roles[Model::DBusPathRole], QByteArrayLiteral("dbusPath"));
        QCOMPARE(roles[Model::ItemObjectRole], QByteArrayLiteral("item"));
    }
};

QTEST_MAIN(TestSmoke)
#include "test_smoke.moc"
