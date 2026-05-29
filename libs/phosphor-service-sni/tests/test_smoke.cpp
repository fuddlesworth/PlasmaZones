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

    void modelRolesArePresent()
    {
        using Model = PhosphorServiceSni::StatusNotifierItemModel;
        Model model;
        const auto roles = model.roleNames();
        // Every role enum must be present in the QHash returned by
        // roleNames(); the QML side keys against the byte-string
        // names, but enum coverage here catches the case where an
        // enum is added without a matching roleNames entry.
        QVERIFY(roles.contains(Model::IdRole));
        QVERIFY(roles.contains(Model::TitleRole));
        QVERIFY(roles.contains(Model::CategoryRole));
        QVERIFY(roles.contains(Model::StatusRole));
        QVERIFY(roles.contains(Model::IconUrlRole));
        QVERIFY(roles.contains(Model::OverlayIconUrlRole));
        QVERIFY(roles.contains(Model::AttentionIconUrlRole));
        QVERIFY(roles.contains(Model::IconImageRole));
        QVERIFY(roles.contains(Model::OverlayIconImageRole));
        QVERIFY(roles.contains(Model::AttentionIconImageRole));
        QVERIFY(roles.contains(Model::ToolTipTitleRole));
        QVERIFY(roles.contains(Model::ToolTipBodyRole));
        QVERIFY(roles.contains(Model::MenuPathRole));
        QVERIFY(roles.contains(Model::ItemIsMenuRole));
        QVERIFY(roles.contains(Model::DBusServiceRole));
        QVERIFY(roles.contains(Model::DBusPathRole));
        QVERIFY(roles.contains(Model::ItemObjectRole));

        // Pin the byte-string names too; QML delegates bind against
        // these literals.
        QCOMPARE(roles[Model::IdRole], QByteArrayLiteral("itemId"));
        QCOMPARE(roles[Model::IconUrlRole], QByteArrayLiteral("iconUrl"));
        QCOMPARE(roles[Model::OverlayIconUrlRole], QByteArrayLiteral("overlayIconUrl"));
        QCOMPARE(roles[Model::AttentionIconUrlRole], QByteArrayLiteral("attentionIconUrl"));
        QCOMPARE(roles[Model::ItemIsMenuRole], QByteArrayLiteral("itemIsMenu"));
        QCOMPARE(roles[Model::CategoryRole], QByteArrayLiteral("category"));
        QCOMPARE(roles[Model::ItemObjectRole], QByteArrayLiteral("item"));
    }
};

QTEST_MAIN(TestSmoke)
#include "test_smoke.moc"
