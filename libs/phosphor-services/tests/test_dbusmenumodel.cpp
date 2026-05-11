// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorServices/DBusMenuModel.h>

#include <QtTest/QtTest>

using PhosphorServices::DBusMenuModel;

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
        // Public contract — QML's TrayMenuPopup binds these names.
        QVERIFY(roles.values().contains("menuId"));
        QVERIFY(roles.values().contains("type"));
        QVERIFY(roles.values().contains("label"));
        QVERIFY(roles.values().contains("enabled"));
        QVERIFY(roles.values().contains("visible"));
        QVERIFY(roles.values().contains("iconImage"));
        QVERIFY(roles.values().contains("toggleType"));
        QVERIFY(roles.values().contains("toggleState"));
        QVERIFY(roles.values().contains("childrenDisplay"));
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
        // yet (no service/path) must be no-ops, not crashes — the QML
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
