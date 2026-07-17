// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include <QGuiApplication>
#include <QQmlComponent>
#include <QCoreApplication>
#include <QQmlEngine>
#include <QTest>

#include <QtPlugin>

#include "daemon/rendering/zoneshaderitem.h"

Q_IMPORT_PLUGIN(org_plasmazones_commonPlugin)

/**
 * @brief Every type in org.plasmazones.common must instantiate.
 *
 * "X is not a type" at runtime hides the nested cause (a bad property in the
 * file, a broken singleton it references, a missing dependent import). This
 * pins component CREATION for each public type in the shared module, so a
 * load regression fails here with the real nested error message instead of
 * silently breaking every settings page / popup that consumes the type.
 */
class TestSharedModuleLoad : public QObject
{
    Q_OBJECT

private:
    QQmlEngine m_engine;

    void loadType(const QString& name, const QString& extraProps = QString())
    {
        QQmlComponent comp(&m_engine);
        comp.setData(
            QStringLiteral("import QtQuick\nimport org.plasmazones.common\n%1 { %2 }\n").arg(name, extraProps).toUtf8(),
            QUrl(QStringLiteral("inline://%1.qml").arg(name)));
        // Module types with singleton/animation dependencies compile
        // ASYNCHRONOUSLY — creation before Ready returns null with no error.
        // Spin the event loop until the loader settles.
        QTRY_VERIFY_WITH_TIMEOUT(comp.status() != QQmlComponent::Loading, 5000);
        if (comp.status() != QQmlComponent::Ready) {
            qWarning() << name << "status:" << comp.status() << "errors:" << comp.errorString();
        }
        QVERIFY2(!comp.isError(), qPrintable(name));
        std::unique_ptr<QObject> obj(comp.create());
        if (!obj) {
            // Creation-time errors (broken bindings to missing context,
            // failed sub-component instantiation) only surface on the
            // component AFTER create() — print them so the canary names
            // the real nested cause instead of a bare null.
            qWarning() << name << "creation errors:" << comp.errorString();
        }
        QVERIFY2(obj != nullptr, qPrintable(name));
    }

private Q_SLOTS:
    void initTestCase()
    {
        // The apps resolve dynamic QML modules (org.phosphor.animation) via
        // their deployed import path; the test binary lives in build/bin, so
        // ../qml is the build tree's QML module root.
        m_engine.addImportPath(QCoreApplication::applicationDirPath() + QStringLiteral("/../qml"));
        // ZoneShaderRenderer wraps ZoneShaderItem, which the daemon/editor
        // composition roots register imperatively — mirror that here so the
        // shared component's `import PlasmaZones` resolves.
        qmlRegisterType<PlasmaZones::ZoneShaderItem>("PlasmaZones", 1, 0, "ZoneShaderItem");
    }
    void loadsLayoutCard()
    {
        loadType(QStringLiteral("LayoutCard"), QStringLiteral("previewWidth: 160; previewHeight: 90"));
    }
    void loadsZonePreview()
    {
        loadType(QStringLiteral("ZonePreview"), QStringLiteral("zones: []"));
    }
    void loadsPopupFrame()
    {
        loadType(QStringLiteral("PopupFrame"));
    }
    void loadsCategoryBadge()
    {
        loadType(QStringLiteral("CategoryBadge"));
    }
    void loadsAspectRatioBadge()
    {
        loadType(QStringLiteral("AspectRatioBadge"));
    }
    void loadsCapabilityBadgeRow()
    {
        loadType(QStringLiteral("CapabilityBadgeRow"));
    }
    void loadsShaderCompileErrorBanner()
    {
        loadType(QStringLiteral("ShaderCompileErrorBanner"));
    }
    void loadsParameterEditor()
    {
        loadType(QStringLiteral("ParameterEditor"), QStringLiteral("parameters: []; currentValues: ({})"));
    }
    void loadsShaderParamsEditor()
    {
        loadType(QStringLiteral("ShaderParamsEditor"), QStringLiteral("parameters: []; currentValues: ({})"));
    }
    void loadsParameterRow()
    {
        loadType(QStringLiteral("ParameterRow"), QStringLiteral("paramData: ({}); currentValues: ({})"));
    }
    void loadsParameterSection()
    {
        loadType(QStringLiteral("ParameterSection"), QStringLiteral("title: \"section\""));
    }
    void loadsCategoryMenuButton()
    {
        loadType(QStringLiteral("CategoryMenuButton"), QStringLiteral("items: []"));
    }
    void loadsZoneShaderRenderer()
    {
        // config is nullable by design (safeConfig falls back to {}).
        loadType(QStringLiteral("ZoneShaderRenderer"), QStringLiteral("config: null"));
    }
    void singletonResolves()
    {
        QQmlComponent comp(&m_engine);
        comp.setData(QByteArrayLiteral("import QtQuick\nimport org.plasmazones.common\n"
                                       "Item { property color c: ZoneColorDefaults.previewActiveZoneColor }\n"),
                     QUrl(QStringLiteral("inline://singleton.qml")));
        QTRY_VERIFY_WITH_TIMEOUT(comp.status() != QQmlComponent::Loading, 5000);
        if (comp.status() != QQmlComponent::Ready) {
            qWarning() << "singleton status:" << comp.status() << "errors:" << comp.errorString();
        }
        QVERIFY(!comp.isError());
        std::unique_ptr<QObject> obj(comp.create());
        QVERIFY(obj != nullptr);
        QVERIFY(obj->property("c").isValid());
    }
};

QTEST_MAIN(TestSharedModuleLoad)
#include "test_shared_module_load.moc"
