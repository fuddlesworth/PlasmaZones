// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include <QGuiApplication>
#include <QQmlComponent>
#include <QCoreApplication>
#include <QQmlEngine>
#include <QTest>

#include <QtPlugin>

#include "daemon/rendering/surfaceshaderitem.h"
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
        // SurfaceDecoration instantiates one SurfaceShaderItem per chain stage
        // and reaches it through the same imperative `import PlasmaZones`, so
        // without this the decoration types below fail with "SurfaceShaderItem
        // is not a type" rather than telling us anything about the module.
        qmlRegisterType<PlasmaZones::SurfaceShaderItem>("PlasmaZones", 1, 0, "SurfaceShaderItem");
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
    void loadsShaderPreviewPlaceholder()
    {
        loadType(QStringLiteral("ShaderPreviewPlaceholder"));
    }
    void loadsStripEmptyState()
    {
        loadType(QStringLiteral("StripEmptyState"), QStringLiteral("caption: \"no windows\""));
    }
    void loadsAxisChevron()
    {
        // Every property is required, so an omission here fails creation
        // rather than silently defaulting.
        loadType(QStringLiteral("AxisChevron"),
                 QStringLiteral("direction: 0; arm: 6; thickness: 1.4; strokeColor: \"white\""));
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
    void loadsSurfaceDecoration()
    {
        // The daemon overlays and the settings decoration preview both reach
        // this through the module rather than through a same-directory import,
        // which is exactly what broke when the file moved here: the hosts kept
        // instantiating it unqualified under a namespaced import. An undecorated
        // instance is the interesting case — every host starts here and only
        // then writes a chain.
        loadType(QStringLiteral("SurfaceDecoration"), QStringLiteral("decorationChain: []; decorationOuterPadding: 0"));
    }
    void loadsDecorationPreviewCard()
    {
        loadType(QStringLiteral("DecorationPreviewCard"));
    }
    /// SurfaceDecoration's readiness gate compares a stage against
    /// `SurfaceShaderItem.Ready` and `SurfaceShaderItem.Error`, enums it
    /// reaches through the registered type rather than declaring itself.
    ///
    /// Worth pinning because the failure is silent and inverted: an
    /// unresolvable enum yields `undefined`, every `status === undefined`
    /// comparison is false, so `chainReady` never goes true and a host that
    /// covers its preview until then covers it forever. A permanent "Preview
    /// unavailable" over a preview that is in fact rendering perfectly.
    void surfaceShaderItemStatusEnumResolves()
    {
        QQmlComponent comp(&m_engine);
        comp.setData(QByteArrayLiteral("import QtQuick\nimport PlasmaZones 1.0\n"
                                       "Item {\n"
                                       "    property int ready: SurfaceShaderItem.Ready\n"
                                       "    property int loading: SurfaceShaderItem.Loading\n"
                                       "    property int failed: SurfaceShaderItem.Error\n"
                                       "}\n"),
                     QUrl(QStringLiteral("inline://surfacestatus.qml")));
        QTRY_VERIFY_WITH_TIMEOUT(comp.status() != QQmlComponent::Loading, 5000);
        if (comp.status() != QQmlComponent::Ready) {
            qWarning() << "status enum:" << comp.status() << "errors:" << comp.errorString();
        }
        QVERIFY(!comp.isError());
        std::unique_ptr<QObject> obj(comp.create());
        QVERIFY(obj != nullptr);
        // Distinct and non-negative: proves they resolved to the real
        // enumerators rather than both collapsing to a default-constructed int.
        QVERIFY(obj->property("ready").toInt() >= 0);
        QVERIFY(obj->property("ready").toInt() != obj->property("loading").toInt());
        // Error too: it is what settles a chain whose shader will not compile,
        // so an unresolvable one leaves that chain permanently unsettled — the
        // same silent inversion, reached by a different route.
        QVERIFY(obj->property("failed").toInt() >= 0);
        QVERIFY(obj->property("failed").toInt() != obj->property("ready").toInt());
        QVERIFY(obj->property("failed").toInt() != obj->property("loading").toInt());
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
