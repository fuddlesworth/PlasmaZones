// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorServiceSni/DBusMenuModel.h>

#include <QtTest/QtTest>

using PhosphorServiceSni::DBusMenuModel;

class TestDBusMenuModel : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void emptyByDefault()
    {
        DBusMenuModel m;
        QCOMPARE(m.rowCount(), 0);
        QVERIFY(!m.valid());
        QCOMPARE(m.rootId(), 0);
        QVERIFY(m.service().isEmpty());
        QVERIFY(m.path().isEmpty());
    }

    void rolesArePresent()
    {
        DBusMenuModel m;
        const auto roles = m.roleNames();
        // Public contract: QML's TrayMenuPopup binds these names.
        // `itemType` / `itemEnabled` / `itemVisible` rather than the
        // plain forms so the QML delegate Item doesn't shadow
        // QQuickItem's FINAL `visible` and Q_PROPERTY `enabled`.
        // Compare per-enum so a swap of names between two roles fails
        // the test instead of passing (which `values().contains` would
        // allow).
        QCOMPARE(roles[DBusMenuModel::IdRole], QByteArrayLiteral("menuId"));
        QCOMPARE(roles[DBusMenuModel::TypeRole], QByteArrayLiteral("itemType"));
        QCOMPARE(roles[DBusMenuModel::LabelRole], QByteArrayLiteral("label"));
        QCOMPARE(roles[DBusMenuModel::EnabledRole], QByteArrayLiteral("itemEnabled"));
        QCOMPARE(roles[DBusMenuModel::VisibleRole], QByteArrayLiteral("itemVisible"));
        QCOMPARE(roles[DBusMenuModel::IconUrlRole], QByteArrayLiteral("iconUrl"));
        QCOMPARE(roles[DBusMenuModel::IconImageRole], QByteArrayLiteral("iconImage"));
        QCOMPARE(roles[DBusMenuModel::ToggleTypeRole], QByteArrayLiteral("toggleType"));
        QCOMPARE(roles[DBusMenuModel::ToggleStateRole], QByteArrayLiteral("toggleState"));
        QCOMPARE(roles[DBusMenuModel::ChildrenDisplayRole], QByteArrayLiteral("childrenDisplay"));
        QCOMPARE(roles[DBusMenuModel::ShortcutRole], QByteArrayLiteral("shortcut"));
    }

    void roleEnumIntegersArePinned()
    {
        // Role enum starts at Qt::UserRole + 1 and is contiguous; QML
        // callers that read role ids out of a Repeater (or any
        // cross-module consumer using qmlRegisterUncreatable metatypes)
        // hard-codes these integers. Reordering the enum would
        // silently shift the mapping; this test makes that a build
        // break.
        QCOMPARE(static_cast<int>(DBusMenuModel::IdRole), int(Qt::UserRole) + 1);
        QCOMPARE(static_cast<int>(DBusMenuModel::TypeRole), int(Qt::UserRole) + 2);
        QCOMPARE(static_cast<int>(DBusMenuModel::LabelRole), int(Qt::UserRole) + 3);
        QCOMPARE(static_cast<int>(DBusMenuModel::EnabledRole), int(Qt::UserRole) + 4);
        QCOMPARE(static_cast<int>(DBusMenuModel::VisibleRole), int(Qt::UserRole) + 5);
        QCOMPARE(static_cast<int>(DBusMenuModel::IconUrlRole), int(Qt::UserRole) + 6);
        QCOMPARE(static_cast<int>(DBusMenuModel::IconImageRole), int(Qt::UserRole) + 7);
        QCOMPARE(static_cast<int>(DBusMenuModel::ToggleTypeRole), int(Qt::UserRole) + 8);
        QCOMPARE(static_cast<int>(DBusMenuModel::ToggleStateRole), int(Qt::UserRole) + 9);
        QCOMPARE(static_cast<int>(DBusMenuModel::ChildrenDisplayRole), int(Qt::UserRole) + 10);
        QCOMPARE(static_cast<int>(DBusMenuModel::ShortcutRole), int(Qt::UserRole) + 11);
    }

    void emptySetterIsIdempotent()
    {
        DBusMenuModel m;
        m.setService(QString());
        m.setPath(QString());
        m.setRootId(0);
        QVERIFY(!m.valid());
    }

    void invalidIndicesAreSafe()
    {
        DBusMenuModel m;
        // Trigger / aboutToShowSubmenu on a model that hasn't loaded
        // yet (no service/path) must be no-ops, not crashes: the QML
        // side can fire these via debounced clicks during teardown.
        m.triggerItem(0);
        QCOMPARE(m.aboutToShowSubmenu(0), -1);
        m.aboutToShow();
        m.aboutToHide();
        QVERIFY(true);
    }
};

QTEST_MAIN(TestDBusMenuModel)
#include "test_dbusmenumodel.moc"
