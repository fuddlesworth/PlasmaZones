// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorServiceSni/QmlRegistration.h>
#include <PhosphorServiceSni/StatusNotifierItemModel.h>
#include <PhosphorServiceSni/TrayItems.h>

#include <QPointer>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QSignalSpy>
#include <QtTest/QtTest>

#include <array>
#include <cstddef>
#include <memory>

using namespace PhosphorServiceSni;

class FakeTraySource : public QAbstractListModel
{
public:
    struct Row
    {
        QVariantMap values;
        QPointer<QObject> object;
    };

    int rowCount(const QModelIndex& parent = {}) const override
    {
        return parent.isValid() ? 0 : rows.size();
    }

    QVariant data(const QModelIndex& index, int role) const override
    {
        if (!index.isValid() || index.row() < 0 || index.row() >= rows.size())
            return {};
        const auto& row = rows.at(index.row());
        if (role == StatusNotifierItemModel::ItemObjectRole)
            return QVariant::fromValue(row.object.data());
        return row.values.value(QString::fromUtf8(roleNames().value(role)));
    }

    QHash<int, QByteArray> roleNames() const override
    {
        return StatusNotifierItemModel().roleNames();
    }

    void add(const QString& id, StatusNotifierItem::Status status = StatusNotifierItem::Status::Active,
             const QString& service = {}, QObject* object = nullptr)
    {
        const int row = rows.size();
        beginInsertRows({}, row, row);
        rows.append(
            {{{QStringLiteral("itemId"), id},
              {QStringLiteral("title"), id},
              {QStringLiteral("status"), QVariant::fromValue(status)},
              {QStringLiteral("dbusService"), service.isEmpty() ? QStringLiteral(":1.%1").arg(++nextService) : service},
              {QStringLiteral("dbusPath"), QStringLiteral("/StatusNotifierItem")},
              {QStringLiteral("iconUrl"), QStringLiteral("image://fixture/%1").arg(id)},
              {QStringLiteral("toolTipBody"), QStringLiteral("A useful status message")}},
             object ? object : new QObject(this)});
        endInsertRows();
    }

    void remove(int row)
    {
        beginRemoveRows({}, row, row);
        rows.removeAt(row);
        endRemoveRows();
    }

    void change(int row, const QString& role, const QVariant& value)
    {
        rows[row].values.insert(role, value);
        Q_EMIT dataChanged(index(row, 0), index(row, 0));
    }

    void swapRows()
    {
        beginResetModel();
        rows.swapItemsAt(0, 1);
        endResetModel();
    }

    void moveFirstToLast()
    {
        beginMoveRows({}, 0, 0, {}, rows.size());
        rows.move(0, rows.size() - 1);
        endMoveRows();
    }

    void invalidateObject(int row)
    {
        rows[row].object = nullptr;
        Q_EMIT dataChanged(index(row, 0), index(row, 0));
    }

    void replaceObject(int row, QObject* object)
    {
        rows[row].object = object;
        Q_EMIT dataChanged(index(row, 0), index(row, 0), {StatusNotifierItemModel::ItemObjectRole});
    }

    QList<Row> rows;
    int nextService = 0;
};

namespace {

QStringList keys(const QVariantList& items, const QString& role = QStringLiteral("itemId"))
{
    QStringList result;
    for (const QVariant& item : items)
        result.append(item.toMap().value(role).toString());
    return result;
}

} // namespace

class TestTrayItems : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void allRowsAndRolePayloadsAreAvailable()
    {
        FakeTraySource source;
        source.add(QStringLiteral("mail"));
        source.add(QStringLiteral("chat"));
        TrayItems tray;
        tray.setSource(&source);
        QCOMPARE(tray.items().size(), 2);
        QVERIFY(tray.barItems().isEmpty());
        QCOMPARE(tray.overflowItems().size(), 2);
        const auto row = tray.items().first().toMap();
        QCOMPARE(row.value(QLatin1String("preferenceKey")).toString(), QStringLiteral("id:mail"));
        QCOMPARE(row.value(QLatin1String("instanceKey")).toString(), QStringLiteral(":1.1|/StatusNotifierItem"));
        QCOMPARE(row.value(QLatin1String("visibility")).toString(), QStringLiteral("auto"));
        QCOMPARE(row.value(QLatin1String("iconUrl")).toString(), QStringLiteral("image://fixture/mail"));
        QCOMPARE(row.value(QLatin1String("toolTipBody")).toString(), QStringLiteral("A useful status message"));
        QCOMPARE(row.value(QLatin1String("item")).value<QObject*>(), source.rows.first().object.data());
        QCOMPARE(QQmlEngine::objectOwnership(source.rows.first().object), QQmlEngine::CppOwnership);
        QCOMPARE(tray.itemByInstanceKey(QStringLiteral(":1.1|/StatusNotifierItem")), row);
        QVERIFY(tray.itemByInstanceKey(QStringLiteral("missing")).isEmpty());
    }

    void preferenceOrderDoesNotLoseNewOrHiddenApps()
    {
        FakeTraySource source;
        source.add(QStringLiteral("mail"));
        source.add(QStringLiteral("chat"));
        source.add(QStringLiteral("music"));
        TrayItems tray;
        tray.setOrder({QStringLiteral("id:music"), QStringLiteral("id:missing"), QStringLiteral("id:mail"),
                       QStringLiteral("id:music"), QString()});
        tray.setVisibility({{QStringLiteral("id:music"), QStringLiteral("hidden")}});
        tray.setSource(&source);
        QCOMPARE(keys(tray.items()),
                 QStringList({QStringLiteral("music"), QStringLiteral("mail"), QStringLiteral("chat")}));
        QCOMPARE(keys(tray.overflowItems()), QStringList({QStringLiteral("mail"), QStringLiteral("chat")}));
        QCOMPARE(tray.order().size(), 3);
        source.add(QStringLiteral("drive"));
        QCOMPARE(keys(tray.items()).last(), QStringLiteral("drive"));
        tray.setOrder({});
        source.moveFirstToLast();
        QCOMPARE(keys(tray.items()).last(), QStringLiteral("mail"));
        source.swapRows();
        QCOMPARE(keys(tray.items()).first(), QStringLiteral("music"));
    }

    void attentionPromotesWithoutOverridingExplicitVisibility()
    {
        FakeTraySource source;
        source.add(QStringLiteral("pinned"));
        source.add(QStringLiteral("alert"), StatusNotifierItem::Status::NeedsAttention);
        source.add(QStringLiteral("hidden"), StatusNotifierItem::Status::NeedsAttention);
        source.add(QStringLiteral("overflow"), StatusNotifierItem::Status::NeedsAttention);
        TrayItems tray;
        tray.setVisibility({{QStringLiteral("id:pinned"), QStringLiteral("pinned")},
                            {QStringLiteral("id:hidden"), QStringLiteral("hidden")},
                            {QStringLiteral("id:overflow"), QStringLiteral("overflow")}});
        tray.setSource(&source);
        QCOMPARE(keys(tray.barItems()), QStringList({QStringLiteral("alert"), QStringLiteral("pinned")}));
        QCOMPARE(keys(tray.overflowItems()), QStringList({QStringLiteral("overflow")}));
        QCOMPARE(tray.items().size(), 4);
        tray.setAttentionPromotion(false);
        QCOMPARE(keys(tray.barItems()), QStringList({QStringLiteral("pinned")}));
        QCOMPARE(keys(tray.overflowItems()), QStringList({QStringLiteral("alert"), QStringLiteral("overflow")}));
        tray.setAttentionPromotion(true);
        source.change(1, QStringLiteral("status"), QVariant::fromValue(StatusNotifierItem::Status::Passive));
        QCOMPARE(keys(tray.barItems()), QStringList({QStringLiteral("pinned")}));
        QVERIFY(!tray.items().at(1).toMap().value(QLatin1String("attention")).toBool());
    }

    void capacityIsBoundedAndRemainderStaysAccessible()
    {
        FakeTraySource source;
        QVariantMap visibility;
        for (int i = 0; i < 10; ++i) {
            const QString id = QStringLiteral("app%1").arg(i);
            source.add(id);
            visibility.insert(QStringLiteral("id:") + id, QStringLiteral("pinned"));
        }
        TrayItems tray;
        tray.setSource(&source);
        tray.setVisibility(visibility);
        QCOMPARE(tray.barItems().size(), 2);
        QCOMPARE(tray.overflowItems().size(), 8);
        tray.setMaximumBarIcons(100);
        QCOMPARE(tray.maximumBarIcons(), 4);
        QCOMPARE(tray.barItems().size(), 4);
        QCOMPARE(tray.overflowItems().size(), 6);
        tray.setMaximumBarIcons(-5);
        QCOMPARE(tray.maximumBarIcons(), 0);
        QCOMPARE(tray.barItems().size(), 0);
        QCOMPARE(tray.overflowItems().size(), 10);
    }

    void duplicateApplicationsHaveSeparateInstanceActions()
    {
        FakeTraySource source;
        source.add(QStringLiteral("chat"));
        source.add(QStringLiteral("chat"));
        TrayItems tray;
        tray.setSource(&source);
        tray.setVisibility({{QStringLiteral("id:chat"), QStringLiteral("pinned")}});
        QCOMPARE(tray.barItems().size(), 2);
        QCOMPARE(keys(tray.items(), QStringLiteral("preferenceKey")),
                 QStringList({QStringLiteral("id:chat"), QStringLiteral("id:chat")}));
        const auto instanceKeys = keys(tray.items(), QStringLiteral("instanceKey"));
        QVERIFY(instanceKeys.at(0) != instanceKeys.at(1));
        QCOMPARE(tray.itemByInstanceKey(instanceKeys.at(1)).value(QLatin1String("item")).value<QObject*>(),
                 source.rows.at(1).object.data());
        source.remove(0);
        QCOMPARE(tray.barItems().size(), 1);
        QVERIFY(tray.itemByInstanceKey(instanceKeys.at(0)).isEmpty());
        QVERIFY(!tray.itemByInstanceKey(instanceKeys.at(1)).isEmpty());
    }

    void fallbackPreferenceSurvivesDifferentBusOwner()
    {
        FakeTraySource source;
        source.add(QString(), StatusNotifierItem::Status::Active, QStringLiteral(":1.35"));
        source.change(0, QStringLiteral("title"), QStringLiteral("  Sync Client  "));
        TrayItems tray;
        tray.setSource(&source);
        const QString key = keys(tray.items(), QStringLiteral("preferenceKey")).first();
        QCOMPARE(key, QStringLiteral("fallback:sync client|/StatusNotifierItem"));
        tray.setVisibility({{key, QStringLiteral("pinned")}});
        source.change(0, QStringLiteral("dbusService"), QStringLiteral(":1.782"));
        QCOMPARE(tray.barItems().size(), 1);
        QCOMPARE(keys(tray.items(), QStringLiteral("preferenceKey")).first(), key);
        QCOMPARE(keys(tray.items(), QStringLiteral("instanceKey")).first(),
                 QStringLiteral(":1.782|/StatusNotifierItem"));
        source.change(0, QStringLiteral("itemId"), QStringLiteral("stable-app-id"));
        QCOMPARE(keys(tray.items(), QStringLiteral("preferenceKey")).first(), QStringLiteral("id:stable-app-id"));
    }

    void notificationsReflectOnlyActualChanges()
    {
        FakeTraySource source;
        source.add(QStringLiteral("mail"));
        TrayItems tray;
        tray.setSource(&source);
        QSignalSpy items(&tray, &TrayItems::itemsChanged);
        QSignalSpy bar(&tray, &TrayItems::barItemsChanged);
        QSignalSpy overflow(&tray, &TrayItems::overflowItemsChanged);
        QSignalSpy sourceChanged(&tray, &TrayItems::sourceChanged);
        QSignalSpy limitChanged(&tray, &TrayItems::maximumBarIconsChanged);
        tray.setSource(&source);
        tray.setOrder({});
        tray.setVisibility({});
        tray.setAttentionPromotion(true);
        tray.setMaximumBarIcons(2);
        source.change(0, QStringLiteral("title"), QStringLiteral("mail"));
        QCOMPARE(items.count(), 0);
        QCOMPARE(bar.count(), 0);
        QCOMPARE(overflow.count(), 0);
        QCOMPARE(sourceChanged.count(), 0);
        QCOMPARE(limitChanged.count(), 0);
        source.change(0, QStringLiteral("title"), QStringLiteral("Unread mail"));
        QCOMPARE(items.count(), 1);
        QCOMPARE(bar.count(), 0);
        QCOMPARE(overflow.count(), 1);
        tray.setMaximumBarIcons(3);
        QCOMPARE(limitChanged.count(), 1);
        QCOMPARE(items.count(), 1);
        QCOMPARE(overflow.count(), 1);
        tray.setVisibility(
            {{QStringLiteral("id:mail"), QStringLiteral("invalid")}, {QString(), QStringLiteral("hidden")}});
        QVERIFY(tray.visibility().isEmpty());
        QCOMPARE(items.count(), 1);
    }

    void objectAndSourceLifetimesDoNotLeaveActionTargets()
    {
        auto source = std::make_unique<FakeTraySource>();
        auto item = std::make_unique<QObject>();
        source->add(QStringLiteral("mail"), StatusNotifierItem::Status::Active, {}, item.get());
        TrayItems tray;
        tray.setSource(source.get());
        tray.setVisibility({{QStringLiteral("id:mail"), QStringLiteral("pinned")}});
        const QString instance = keys(tray.items(), QStringLiteral("instanceKey")).first();
        item.reset();
        QVERIFY(tray.items().isEmpty());
        QVERIFY(tray.barItems().isEmpty());
        QVERIFY(tray.itemByInstanceKey(instance).isEmpty());
        source->add(QStringLiteral("chat"));
        QCOMPARE(tray.items().size(), 1);
        source->invalidateObject(1);
        QVERIFY(tray.items().isEmpty());
        source->add(QStringLiteral("drive"));
        QSignalSpy destroyed(&tray, &TrayItems::sourceChanged);
        source.reset();
        QCOMPARE(destroyed.count(), 1);
        QVERIFY(!tray.source());
        QVERIFY(tray.items().isEmpty());
        QVERIFY(tray.overflowItems().isEmpty());
    }

    void replacingSourceDisconnectsOldRows()
    {
        FakeTraySource first;
        FakeTraySource second;
        first.add(QStringLiteral("first"));
        second.add(QStringLiteral("second"));
        TrayItems tray;
        tray.setSource(&first);
        tray.setSource(&second);
        QSignalSpy changes(&tray, &TrayItems::itemsChanged);
        first.add(QStringLiteral("ignored"));
        QCOMPARE(changes.count(), 0);
        QCOMPARE(keys(tray.items()), QStringList({QStringLiteral("second")}));
        tray.setSource(nullptr);
        QCOMPARE(changes.count(), 1);
        QVERIFY(tray.items().isEmpty());
    }

    void aNewObjectCanReuseADestroyedObjectsAddress()
    {
        alignas(QObject) std::array<std::byte, sizeof(QObject)> storage;
        auto destroy = [](QObject* object) {
            std::destroy_at(object);
        };
        const auto construct = [&storage, &destroy] {
            return std::unique_ptr<QObject, decltype(destroy)>(
                std::construct_at(reinterpret_cast<QObject*>(storage.data())), destroy);
        };
        FakeTraySource source;
        TrayItems tray;
        tray.setSource(&source);
        auto first = construct();
        source.add(QStringLiteral("first"), StatusNotifierItem::Status::Active, {}, first.get());
        auto* address = first.get();
        first.reset();
        QVERIFY(tray.items().isEmpty());
        auto second = construct();
        QCOMPARE(second.get(), address);
        source.add(QStringLiteral("second"), StatusNotifierItem::Status::Active, {}, second.get());
        QCOMPARE(keys(tray.items()), QStringList({QStringLiteral("second")}));
        second.reset();
        QVERIFY(tray.items().isEmpty());
        auto replacement = construct();
        QCOMPARE(replacement.get(), address);
        source.replaceObject(1, replacement.get());
        QCOMPARE(keys(tray.items()), QStringList({QStringLiteral("second")}));
        QCOMPARE(tray.items().first().toMap().value(QLatin1String("item")).value<QObject*>(), replacement.get());
    }

    void qmlRegistrationExposesPresentationType()
    {
        registerQmlTypes();
        QQmlEngine engine;
        QQmlComponent component(&engine);
        component.setData("import Phosphor.Service.Sni 1.0\nTrayItems { maximumBarIcons: 3 }", QUrl());
        std::unique_ptr<QObject> object(component.create());
        QVERIFY2(object, qPrintable(component.errorString()));
        auto* tray = qobject_cast<TrayItems*>(object.get());
        QVERIFY(tray);
        QCOMPARE(tray->maximumBarIcons(), 3);
    }
};

QTEST_GUILESS_MAIN(TestTrayItems)
#include "test_trayitems.moc"
