// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
#include "shell/PickerController.h"

#include <PhosphorLayer/IScreenProvider.h>
#include <PhosphorShell/PanelWindow.h>
#include <PhosphorShell/QmlRegistration.h>
#include <PhosphorShell/ScreenModel.h>
#include <PhosphorTheme/AppearanceStore.h>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QLockFile>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickWindow>
#include <QScreen>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>

#include <memory>

using PhosphorShellApp::PickerController;
using PhosphorTheme::AppearanceStore;

class EntryScreens : public PhosphorLayer::IScreenProvider
{
public:
    QList<QScreen*> outputs = QGuiApplication::screens();
    mutable PhosphorLayer::ScreenProviderNotifier changes;
    QList<QScreen*> screens() const override
    {
        return outputs;
    }
    QScreen* primary() const override
    {
        return outputs.isEmpty() ? nullptr : outputs.first();
    }
    QScreen* focused() const override
    {
        return primary();
    }
    PhosphorLayer::ScreenProviderNotifier* notifier() const override
    {
        return &changes;
    }
};

class EntryShell : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QAbstractListModel* screens MEMBER screens CONSTANT)
public:
    QAbstractListModel* screens = nullptr;
};

class RecordedEffects : public QObject
{
    Q_OBJECT
public:
    QHash<QQuickItem*, QRect> regions;
    QHash<QQuickItem*, qreal> radii;
    Q_INVOKABLE bool setBlurBehind(QQuickItem* item, const QRect& region, const QRect&, qreal radius)
    {
        regions[item] = region;
        radii[item] = radius;
        return true;
    }
};

class TestShellAppearanceEntry : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void initTestCase()
    {
        PhosphorShell::registerQmlTypes();
        QVERIFY(QGuiApplication::screens().size() >= 2);
        for (const auto* screen : QGuiApplication::screens())
            QVERIFY(!screen->name().isEmpty());
    }
    void cleanup()
    {
        auto* store = AppearanceStore::create(nullptr, nullptr);
        store->endPreview();
        QVERIFY(store->setValues(AppearanceStore::defaults()));
    }
    void unavailableOutputReportsFailure()
    {
        EntryScreens screens;
        screens.outputs.clear();
        PickerController controller(&screens);
        QSignalSpy failures(&controller, &PickerController::openingFailed);
        QVERIFY(!controller.show());
        QVERIFY(!controller.toggle());
        QCOMPARE(failures.size(), 2);
        QVERIFY(!failures.first().first().toString().isEmpty());
        QVERIFY(controller.openScreen().isEmpty());
        QVERIFY(!AppearanceStore::create(nullptr, nullptr)->editing());
    }
    void lockedPreviewReportsFailureThenRecovers()
    {
        const auto path = QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation)
            + QStringLiteral("/phosphor-shell/appearance.json.preview.lock");
        QVERIFY(QDir().mkpath(QFileInfo(path).absolutePath()));
        QLockFile competitor(path);
        QVERIFY(competitor.tryLock());
        EntryScreens screens;
        PickerController controller(&screens);
        QSignalSpy failures(&controller, &PickerController::openingFailed);
        QVERIFY(!controller.show(screens.primary()->name()));
        QCOMPARE(failures.size(), 1);
        QCOMPARE(failures.first().first().toString(), AppearanceStore::create(nullptr, nullptr)->error());
        QVERIFY(!failures.first().first().toString().isEmpty());
        QVERIFY(controller.openScreen().isEmpty());
        QVERIFY(!AppearanceStore::create(nullptr, nullptr)->editing());
        competitor.unlock();
        QVERIFY(controller.show(screens.primary()->name()));
        QCOMPARE(controller.openScreen(), screens.primary()->name());
        QVERIFY(AppearanceStore::create(nullptr, nullptr)->editing());
        controller.discard();
        QVERIFY(controller.openScreen().isEmpty());
    }
    void repeatedOpenAndOutputTransferKeepThePreviewTransaction()
    {
        EntryScreens screens;
        PickerController controller(&screens);
        auto* store = AppearanceStore::create(nullptr, nullptr);
        QVERIFY(controller.show(screens.outputs.first()->name()));
        const auto saved = store->values();
        const int changedRadius = saved.value(QStringLiteral("radius")).toInt() == 23 ? 24 : 23;
        QVERIFY(store->setValue(QStringLiteral("radius"), changedRadius));
        QVERIFY(controller.show(screens.outputs.first()->name()));
        QVERIFY(controller.toggle(screens.outputs.last()->name()));
        QCOMPARE(controller.openScreen(), screens.outputs.last()->name());
        QCOMPARE(store->values().value(QStringLiteral("radius")).toInt(), changedRadius);
        QVERIFY(controller.toggle(screens.outputs.last()->name()));
        QVERIFY(controller.closePending());
        controller.keepEditing();
        controller.discard();
        QCOMPARE(store->values(), saved);
        QVERIFY(!store->editing());
        QVERIFY(controller.openScreen().isEmpty());
    }
    void overlayReleasesInputAndTracksTheVisibleMaterial()
    {
        EntryScreens screens;
        PhosphorShell::ScreenModel model(&screens);
        EntryShell shell;
        shell.screens = &model;
        PickerController controller(&screens);
        RecordedEffects effects;
        QQmlEngine engine;
        engine.rootContext()->setContextProperty(QStringLiteral("PhosphorShell"), &shell);
        engine.rootContext()->setContextProperty(QStringLiteral("PickerRegistry"), &controller);
        engine.rootContext()->setContextProperty(QStringLiteral("ShellEffects"), &effects);
        engine.rootContext()->setContextProperty(QStringLiteral("BarRegistry"),
                                                 QVariantMap{{QStringLiteral("factoryIds"), QStringList{}}});
        engine.rootContext()->setContextProperty(
            QStringLiteral("ShellChrome"),
            QVariantMap{{QStringLiteral("decorationComponent"), QVariant::fromValue<QObject*>(nullptr)}});
        QQmlComponent component(&engine,
                                QUrl(QStringLiteral("qrc:/qt/qml/Phosphor/Shell/Example/AppearanceSurfaces.qml")));
        std::unique_ptr<QObject> object(component.create());
        QVERIFY2(object, qPrintable(component.errorString()));
        const auto panels = object->findChildren<PhosphorShell::PanelWindow*>();
        QCOMPARE(panels.size(), 2);
        auto* surface = panels.first();
        for (auto* panel : panels)
            QCOMPARE(panel->keyboardFocus(), PhosphorShell::PanelWindow::None);
        QQuickWindow window;
        window.resize(1000, 800);
        surface->setParentItem(window.contentItem());
        surface->setPosition(QPointF(20, 30));
        surface->setSize(QSizeF(800, 600));
        window.show();
        QVERIFY(controller.show(surface->screen()->name()));
        QCOMPARE(surface->keyboardFocus(), PhosphorShell::PanelWindow::OnDemand);
        auto* workspace = surface->findChild<QQuickItem*>(QStringLiteral("appearanceWorkspaceHost"));
        auto* actions = surface->findChild<QQuickItem*>(QStringLiteral("appearancePreviewActions"));
        QVERIFY(workspace);
        QVERIFY(actions);
        const auto materialRect = [](QQuickItem* item) {
            return item->mapRectToItem(nullptr, QRectF(0, 0, item->width(), item->height())).toRect();
        };
        QTRY_COMPARE(effects.regions.value(surface), materialRect(workspace));
        QVERIFY(effects.regions.value(surface).width() < surface->width());
        auto* store = AppearanceStore::create(nullptr, nullptr);
        QVERIFY(store->setValue(QStringLiteral("radius"), 27));
        QTRY_COMPARE(effects.radii.value(surface), 27.0);
        controller.setProperty("desktopPreview", true);
        QTRY_COMPARE(effects.regions.value(surface), materialRect(actions));
        QVERIFY(effects.regions.value(surface).height() < 100);
        controller.setProperty("desktopPreview", false);
        surface->setWidth(900);
        QTRY_COMPARE(effects.regions.value(surface), materialRect(workspace));
        QVERIFY(store->setValue(QStringLiteral("material"), QStringLiteral("solid")));
        QTRY_VERIFY(effects.regions.value(surface).isEmpty());
        QVERIFY(store->setValue(QStringLiteral("material"), QStringLiteral("glass")));
        QTRY_COMPARE(effects.regions.value(surface), materialRect(workspace));
        object->setProperty("locked", true);
        QCOMPARE(surface->keyboardFocus(), PhosphorShell::PanelWindow::None);
        QTRY_VERIFY(effects.regions.value(surface).isEmpty());
        object->setProperty("locked", false);
        QCOMPARE(surface->keyboardFocus(), PhosphorShell::PanelWindow::OnDemand);
        controller.discard();
        QCOMPARE(surface->keyboardFocus(), PhosphorShell::PanelWindow::None);
        QTRY_VERIFY(effects.regions.value(surface).isEmpty());
        // A queued activation from show() must not revive a just-closed overlay.
        QVERIFY(controller.show(surface->screen()->name()));
        controller.hide();
        QCoreApplication::processEvents();
        QCOMPARE(surface->keyboardFocus(), PhosphorShell::PanelWindow::None);
        QVERIFY(effects.regions.value(surface).isEmpty());
        surface->setParentItem(nullptr);
    }
};

int main(int argc, char** argv)
{
    QTemporaryDir sandbox;
    if (!sandbox.isValid())
        return 1;
    qputenv("XDG_CONFIG_HOME", QFile::encodeName(sandbox.filePath(QStringLiteral("config"))));
    qputenv("XDG_DATA_HOME", QFile::encodeName(sandbox.filePath(QStringLiteral("data"))));
    QGuiApplication app(argc, argv);
    TestShellAppearanceEntry test;
    return QTest::qExec(&test, argc, argv);
}

#include "test_shell_appearance_entry.moc"
