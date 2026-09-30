// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
#include "StatusIconsController.h"
#include "StatusIconsSource.h"

#include <PhosphorTheme/AppearanceStore.h>

#include <QDir>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

#include <memory>

using namespace PhosphorShellApp;
namespace {
class FixtureSource final : public StatusIconsSource
{
public:
    StatusIconsSnapshot value;
    QStringList actions;
    FixtureSource()
    {
        for (const auto& id : {QStringLiteral("wifi"), QStringLiteral("audio"), QStringLiteral("bluetooth"),
                               QStringLiteral("battery"), QStringLiteral("microphone"), QStringLiteral("nightlight"),
                               QStringLiteral("focus"), QStringLiteral("airplane"), QStringLiteral("power")}) {
            StatusIconState state;
            state.available = true;
            state.summary = id;
            value.states.insert(id, state);
        }
        value.states[QStringLiteral("wifi")].active = true;
        value.states[QStringLiteral("battery")].active = true;
        value.states[QStringLiteral("battery")].percent = 82;
        value.audioAvailable = true;
        value.volume = 64;
        value.powerProfile = QStringLiteral("balanced");
        value.powerProfiles = {QStringLiteral("power-saver"), QStringLiteral("balanced")};
        value.outputs = {QVariantMap{{QStringLiteral("id"), QStringLiteral("speakers")},
                                     {QStringLiteral("name"), QStringLiteral("Speakers")},
                                     {QStringLiteral("selected"), true}},
                         QVariantMap{{QStringLiteral("id"), QStringLiteral("headphones")},
                                     {QStringLiteral("name"), QStringLiteral("Headphones")},
                                     {QStringLiteral("selected"), false}}};
    }
    StatusIconsSnapshot snapshot() const override
    {
        return value;
    }
    void publish()
    {
        Q_EMIT changed();
    }
    bool toggle(const QString& id) override
    {
        actions.append(QStringLiteral("toggle:") + id);
        return true;
    }
    bool setVolume(int percent) override
    {
        if (!value.audioAvailable)
            return false;
        actions.append(QStringLiteral("volume:%1").arg(percent));
        return true;
    }
    bool selectOutput(const QString& id) override
    {
        for (const auto& output : value.outputs) {
            if (output.toMap().value(QStringLiteral("id")).toString() == id) {
                actions.append(QStringLiteral("output:") + id);
                return true;
            }
        }
        return false;
    }
    bool setPowerProfile(const QString& id) override
    {
        actions.append(QStringLiteral("power:") + id);
        return true;
    }
    void setAudioHost(PhosphorServicePipeWire::PipeWireHost*) override
    {
    }
};
QStringList names(const QVariantList& rows)
{
    QStringList result;
    for (const auto& row : rows)
        result.append(row.toMap().value(QStringLiteral("id")).toString());
    return result;
}
QVariantMap row(const StatusIconsController& controller, const QString& id)
{
    for (const auto& item : controller.icons()) {
        if (item.toMap().value(QStringLiteral("id")).toString() == id)
            return item.toMap();
    }
    return {};
}
}

class TestStatusIconsController : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void defaultsAndCatalogRemainComplete()
    {
        QTemporaryDir dir;
        PhosphorTheme::AppearanceStore store(dir.filePath(QStringLiteral("appearance.json")));
        FixtureSource source;
        StatusIconsController controller(&store, &source);
        QCOMPARE(controller.icons().size(), 9);
        QCOMPARE(names(controller.shownIcons()),
                 QStringList({QStringLiteral("wifi"), QStringLiteral("audio"), QStringLiteral("battery")}));
        QVERIFY(controller.overflowIcons().isEmpty());
        QCOMPARE(controller.maxVisible(), 4);
        QVERIFY(controller.showBatteryPercentage());
        QCOMPARE(controller.volume(), 64);
        QCOMPARE(controller.outputs().size(), 2);
        QCOMPARE(row(controller, QStringLiteral("wifi")).value(QStringLiteral("detailId")).toString(),
                 QStringLiteral("network"));
        QCOMPARE(row(controller, QStringLiteral("focus")).value(QStringLiteral("detailId")).toString(),
                 QStringLiteral("notification"));
        QCOMPARE(row(controller, QStringLiteral("battery")).value(QStringLiteral("percent")).toInt(), 82);
        QCOMPARE(row(controller, QStringLiteral("microphone")).value(QStringLiteral("percent")).toInt(), -1);
        QVERIFY(source.actions.isEmpty());
    }
    void filteringOverflowAndServiceLossPreservePreferences()
    {
        QTemporaryDir dir;
        PhosphorTheme::AppearanceStore store(dir.filePath(QStringLiteral("appearance.json")));
        FixtureSource source;
        StatusIconsController controller(&store, &source);
        QVERIFY(controller.setVisibility(QStringLiteral("bluetooth"), QStringLiteral("always")));
        QVERIFY(controller.setVisibility(QStringLiteral("nightlight"), QStringLiteral("auto")));
        QVERIFY(controller.setMaxVisible(2));
        source.value.states[QStringLiteral("nightlight")].active = true;
        source.value.states[QStringLiteral("microphone")].active = true;
        source.value.states[QStringLiteral("microphone")].attention = true;
        source.publish();
        QCOMPARE(names(controller.shownIcons()), QStringList({QStringLiteral("wifi"), QStringLiteral("audio")}));
        QCOMPARE(names(controller.overflowIcons()),
                 QStringList({QStringLiteral("bluetooth"), QStringLiteral("battery"), QStringLiteral("microphone"),
                              QStringLiteral("nightlight")}));
        const auto preferences = store.values();
        source.value.states[QStringLiteral("bluetooth")].available = false;
        source.value.states[QStringLiteral("battery")].active = false;
        source.value.states[QStringLiteral("battery")].available = false;
        source.value.states[QStringLiteral("battery")].percent = -1;
        source.publish();
        QCOMPARE(
            names(controller.overflowIcons()),
            QStringList({QStringLiteral("bluetooth"), QStringLiteral("microphone"), QStringLiteral("nightlight")}));
        QVERIFY(!row(controller, QStringLiteral("bluetooth")).value(QStringLiteral("available")).toBool());
        QCOMPARE(store.values(), preferences);
        QCOMPARE(controller.icons().size(), 9);
    }
    void reordersPersistAndResetKeepsUnrelatedAppearance()
    {
        QTemporaryDir dir;
        const auto path = dir.filePath(QStringLiteral("appearance.json"));
        PhosphorTheme::AppearanceStore store(path);
        FixtureSource source;
        StatusIconsController controller(&store, &source);
        QVERIFY(store.setValue(QStringLiteral("radius"), 22));
        QVERIFY(controller.moveBefore(QStringLiteral("battery"), QStringLiteral("wifi")));
        QCOMPARE(names(controller.shownIcons()).constFirst(), QStringLiteral("battery"));
        QVERIFY(controller.moveIcon(QStringLiteral("battery"), 1));
        QCOMPARE(names(controller.icons()).at(1), QStringLiteral("battery"));
        QVERIFY(controller.moveBefore(QStringLiteral("wifi"), QString{}));
        QCOMPARE(names(controller.icons()).constLast(), QStringLiteral("wifi"));
        PhosphorTheme::AppearanceStore restored(path);
        QCOMPARE(restored.values().value(QStringLiteral("statusOrder")),
                 store.values().value(QStringLiteral("statusOrder")));
        QVERIFY(controller.setMaxVisible(6));
        QVERIFY(controller.setShowBatteryPercentage(false));
        QVERIFY(controller.resetIcons());
        QCOMPARE(controller.maxVisible(), 4);
        QVERIFY(controller.showBatteryPercentage());
        QCOMPARE(names(controller.icons()).constFirst(), QStringLiteral("wifi"));
        QCOMPARE(store.values().value(QStringLiteral("radius")).toInt(), 22);
    }
    void invalidRequestsCannotCorruptPreferences()
    {
        QTemporaryDir dir;
        PhosphorTheme::AppearanceStore store(dir.filePath(QStringLiteral("appearance.json")));
        FixtureSource source;
        StatusIconsController controller(&store, &source);
        const auto original = store.values();
        QVERIFY(!controller.setVisibility(QStringLiteral("unknown"), QStringLiteral("always")));
        QVERIFY(!controller.setVisibility(QStringLiteral("wifi"), QStringLiteral("sometimes")));
        QVERIFY(!controller.setMaxVisible(1));
        QVERIFY(!controller.setMaxVisible(7));
        QVERIFY(!controller.moveIcon(QStringLiteral("wifi"), -1));
        QVERIFY(!controller.moveIcon(QStringLiteral("wifi"), 100));
        QVERIFY(!controller.moveBefore(QStringLiteral("wifi"), QStringLiteral("unknown")));
        QVERIFY(!controller.moveBefore(QStringLiteral("unknown"), QStringLiteral("wifi")));
        QCOMPARE(store.values(), original);
    }
    void serviceTransitionsAndNoOpRefreshSignals()
    {
        QTemporaryDir dir;
        PhosphorTheme::AppearanceStore store(dir.filePath(QStringLiteral("appearance.json")));
        FixtureSource source;
        StatusIconsController controller(&store, &source);
        QSignalSpy changed(&controller, &StatusIconsController::changed);
        source.publish();
        QCOMPARE(changed.count(), 0);
        QVERIFY(controller.setMaxVisible(4));
        QCOMPARE(changed.count(), 0);
        source.value.states[QStringLiteral("airplane")].active = true;
        source.value.states[QStringLiteral("battery")].attention = true;
        source.value.states[QStringLiteral("battery")].percent = 8;
        source.publish();
        QCOMPARE(changed.count(), 1);
        QVERIFY(names(controller.shownIcons()).contains(QStringLiteral("airplane")));
        QVERIFY(row(controller, QStringLiteral("battery")).value(QStringLiteral("attention")).toBool());
        source.value.audioAvailable = false;
        source.value.outputs.clear();
        source.value.states[QStringLiteral("audio")].available = false;
        source.value.states[QStringLiteral("audio")].error = QStringLiteral("Service disconnected");
        source.publish();
        QCOMPARE(changed.count(), 2);
        QVERIFY(!controller.audioAvailable());
        QVERIFY(controller.outputs().isEmpty());
        QVERIFY(names(controller.shownIcons()).contains(QStringLiteral("audio")));
        source.publish();
        QCOMPARE(changed.count(), 2);
    }
    void explicitActionsUseInjectedServicesWithoutOptimisticState()
    {
        QTemporaryDir dir;
        PhosphorTheme::AppearanceStore store(dir.filePath(QStringLiteral("appearance.json")));
        FixtureSource source;
        StatusIconsController controller(&store, &source);
        QVERIFY(controller.toggle(QStringLiteral("audio")));
        QVERIFY(!controller.muted());
        QVERIFY(controller.setVolume(37));
        QCOMPARE(controller.volume(), 64);
        QVERIFY(controller.selectOutput(QStringLiteral("headphones")));
        QVERIFY(!controller.selectOutput(QStringLiteral("removed-device")));
        QVERIFY(controller.setPowerProfile(QStringLiteral("power-saver")));
        QCOMPARE(controller.powerProfile(), QStringLiteral("balanced"));
        QVERIFY(!controller.setPowerProfile(QStringLiteral("performance")));
        QVERIFY(!controller.setVolume(-1));
        QVERIFY(!controller.setVolume(101));
        QVERIFY(!controller.toggle(QStringLiteral("unknown")));
        source.value.states[QStringLiteral("wifi")].pending = true;
        QVERIFY(!controller.toggle(QStringLiteral("wifi")));
        source.value.states[QStringLiteral("bluetooth")].available = false;
        QVERIFY(!controller.toggle(QStringLiteral("bluetooth")));
        QCOMPARE(source.actions,
                 QStringList({QStringLiteral("toggle:audio"), QStringLiteral("volume:37"),
                              QStringLiteral("output:headphones"), QStringLiteral("power:power-saver")}));
        source.value.volume = 37;
        source.value.muted = true;
        source.publish();
        QCOMPARE(controller.volume(), 37);
        QVERIFY(controller.muted());
    }
    void hidingAllIconsKeepsTheCompleteArrangementCatalog()
    {
        QTemporaryDir dir;
        PhosphorTheme::AppearanceStore store(dir.filePath(QStringLiteral("appearance.json")));
        FixtureSource source;
        StatusIconsController controller(&store, &source);
        for (const auto& id : names(controller.icons()))
            QVERIFY(controller.setVisibility(id, QStringLiteral("hidden")));
        QVERIFY(controller.shownIcons().isEmpty());
        QVERIFY(controller.overflowIcons().isEmpty());
        QCOMPARE(controller.icons().size(), 9);
    }
    void writeFailuresAreReportedAndSourceDestructionIsSafe()
    {
        QTemporaryDir dir;
        const auto path = dir.filePath(QStringLiteral("directory"));
        QVERIFY(QDir().mkpath(path));
        PhosphorTheme::AppearanceStore store(path);
        auto source = std::make_unique<FixtureSource>();
        StatusIconsController controller(&store, source.get());
        QVERIFY(!controller.setMaxVisible(5));
        QVERIFY(!controller.error().isEmpty());
        QCOMPARE(controller.maxVisible(), 4);
        source.reset();
        QVERIFY(!controller.audioAvailable());
        QVERIFY(!controller.toggle(QStringLiteral("audio")));
        QCOMPARE(controller.icons().size(), 9);
    }
};
QTEST_GUILESS_MAIN(TestStatusIconsController)
#include "test_status_icons_controller.moc"
