// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_control_shortcuts.cpp
 * @brief ControlAdaptor's shortcut catalog read (controladaptor_shortcuts.cpp).
 *
 *  1. getShortcutsJson answers "[]" without a provider, and after detach().
 *  2. With a provider it serialises every row's keys as a JSON array, the
 *     triggers as a string array, in the provider's order.
 *  3. notifyShortcutsChanged emits shortcutsChanged once per call.
 */

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTest>

#include "dbus/controladaptor.h"

using namespace PlasmaZones;

class TestControlShortcuts : public QObject
{
    Q_OBJECT

private:
    static QVariantMap row(const QString& id, const QStringList& triggers, const QString& mode)
    {
        QVariantMap r;
        r.insert(QStringLiteral("id"), id);
        r.insert(QStringLiteral("label"), id);
        r.insert(QStringLiteral("description"), QString());
        r.insert(QStringLiteral("category"), QStringLiteral("Navigation"));
        r.insert(QStringLiteral("categoryOrder"), 1);
        r.insert(QStringLiteral("rowOrder"), 0);
        r.insert(QStringLiteral("triggers"), triggers);
        r.insert(QStringLiteral("assigned"), !triggers.isEmpty());
        r.insert(QStringLiteral("mode"), mode);
        return r;
    }

private Q_SLOTS:
    void emptyWithoutAProvider()
    {
        QObject parent;
        auto* control = new ControlAdaptor(nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
                                           nullptr, &parent);
        QCOMPARE(control->getShortcutsJson(), QStringLiteral("[]"));
    }

    void serialisesTheProvidersRows()
    {
        QObject parent;
        auto* control = new ControlAdaptor(nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
                                           nullptr, &parent);
        int reads = 0;
        control->setShortcutCatalogProvider([&reads]() -> QVariantList {
            ++reads;
            return {row(QStringLiteral("move_window_left"), {QStringLiteral("Meta+Left")}, QStringLiteral("all")),
                    row(QStringLiteral("snap_to_zone_3"), {}, QStringLiteral("snapping"))};
        });

        const QJsonDocument doc = QJsonDocument::fromJson(control->getShortcutsJson().toUtf8());
        QCOMPARE(reads, 1);
        QVERIFY(doc.isArray());
        const QJsonArray rows = doc.array();
        QCOMPARE(rows.size(), 2);

        const QJsonObject first = rows.at(0).toObject();
        QCOMPARE(first.value(QLatin1String("id")).toString(), QStringLiteral("move_window_left"));
        QCOMPARE(first.value(QLatin1String("mode")).toString(), QStringLiteral("all"));
        QVERIFY(first.value(QLatin1String("assigned")).toBool());
        const QJsonArray triggers = first.value(QLatin1String("triggers")).toArray();
        QCOMPARE(triggers.size(), 1);
        QCOMPARE(triggers.at(0).toString(), QStringLiteral("Meta+Left"));
        QCOMPARE(first.value(QLatin1String("categoryOrder")).toInt(), 1);

        const QJsonObject second = rows.at(1).toObject();
        QCOMPARE(second.value(QLatin1String("id")).toString(), QStringLiteral("snap_to_zone_3"));
        QVERIFY(!second.value(QLatin1String("assigned")).toBool());
        QVERIFY(second.value(QLatin1String("triggers")).toArray().isEmpty());

        // Each read goes back to the provider: nothing is cached.
        control->getShortcutsJson();
        QCOMPARE(reads, 2);

        // detach() drops the provider with the other borrowed pointers.
        control->detach();
        QCOMPARE(control->getShortcutsJson(), QStringLiteral("[]"));
        QCOMPARE(reads, 2);
    }

    void notifyRelaysTheSignal()
    {
        QObject parent;
        auto* control = new ControlAdaptor(nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
                                           nullptr, &parent);
        QSignalSpy spy(control, &ControlAdaptor::shortcutsChanged);
        control->notifyShortcutsChanged();
        control->notifyShortcutsChanged();
        QCOMPARE(spy.count(), 2);
    }
};

QTEST_MAIN(TestControlShortcuts)
#include "test_control_shortcuts.moc"
