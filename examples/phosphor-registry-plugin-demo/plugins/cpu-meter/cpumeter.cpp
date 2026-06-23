// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
//
// CPU meter plugin for phosphor-registry-plugin-demo. Demonstrates
// the plugin ABI shape: a single .so exporting
// `phosphor_registry_create_factory` which returns an
// IBarWidgetFactory.
//
// The widget itself is inline QML (no separate file) because the
// plugin needs to be a self-contained .so without a QML module
// registrar — keeps the example minimal. A real plugin would bundle
// its QML via qt_add_qml_module and load via a qrc URL; for the
// demo, inline QML is the simplest proof-of-shape.

#include <PhosphorRegistry/IBarWidgetFactory.h>

#include <QEventLoop>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QString>
#include <QStringList>
#include <QtCore/qtclasshelpermacros.h>

#include <new> // std::nothrow

namespace {

constexpr const char* CpuMeterQml = R"(
import QtQuick

Rectangle {
    id: root
    implicitWidth: 120
    implicitHeight: 32
    color: "#1f2228"
    border.color: "#3b4048"
    border.width: 1
    radius: 4
    property real reading: 0
    Timer {
        interval: 200
        repeat: true
        running: true
        onTriggered: root.reading = 0.5 + 0.5 * Math.sin(Date.now() / 1000)
    }
    Rectangle {
        anchors.left: parent.left
        anchors.leftMargin: 4
        anchors.verticalCenter: parent.verticalCenter
        width: (parent.width - 8) * root.reading
        height: parent.height - 8
        color: "#4ea1ff"
        radius: 2
    }
    Text {
        anchors.centerIn: parent
        text: "CPU " + Math.round(root.reading * 100) + "%"
        color: "#e8eaee"
        font.pixelSize: 12
        font.family: "monospace"
    }
}
)";

class CpuMeterFactory : public PhosphorRegistry::IBarWidgetFactory
{
public:
    CpuMeterFactory() = default;
    ~CpuMeterFactory() override = default;
    Q_DISABLE_COPY_MOVE(CpuMeterFactory)

    QString id() const override
    {
        return QStringLiteral("cpu-meter");
    }
    QString displayName() const override
    {
        return QStringLiteral("CPU Meter");
    }
    QStringList capabilities() const override
    {
        return {QStringLiteral("bar.widget"), QStringLiteral("system.read")};
    }
    QQuickItem* createWidget(QQmlEngine* engine, QObject* parent) override
    {
        if (!engine) {
            qWarning("CpuMeterFactory: null engine");
            return nullptr;
        }
        // Qt 6.5+ farms QML compilation out to a worker thread pool;
        // QQmlComponent::setData returns while the component is
        // still in Loading state, and a subsequent create() warns
        // "Component is not ready" + returns nullptr.
        //
        // PreferSynchronous would be the canonical fix but every
        // QQmlComponent ctor that accepts a CompilationMode requires
        // a URL or filename — there is no setData-with-mode overload
        // — so we cannot supply PreferSynchronous to the inline-QML
        // path. The pragmatic workaround is to wait on the Loading→
        // Ready transition via a nested QEventLoop.
        //
        // Re-entry hazard: any queued event delivered while the
        // event loop spins runs before this function returns. The
        // reload-button in the plugin demo can queue a SECOND
        // reloadPlugins() while a first createWidget is mid-compile,
        // re-entering this function and stacking nested loops.
        // QEventLoop::ExcludeUserInputEvents blocks mouse/keyboard
        // delivery during the spin so the Reload button cannot
        // re-fire mid-compile. Non-user events (timers, deferred
        // deletes) still flow, which is required for the QML
        // compilation worker thread to post its statusChanged
        // signal back to us.
        QQmlComponent component(engine);
        component.setData(QByteArray(CpuMeterQml), QUrl(QStringLiteral("inline:cpu-meter")));
        if (component.isLoading()) {
            QEventLoop loop;
            QObject::connect(&component, &QQmlComponent::statusChanged, &loop, &QEventLoop::quit);
            loop.exec(QEventLoop::ExcludeUserInputEvents);
        }
        if (component.isError()) {
            qWarning("CpuMeterFactory: component error %s", qPrintable(component.errorString()));
            return nullptr;
        }
        if (!component.isReady()) {
            qWarning("CpuMeterFactory: component status=%d (expected Ready)", static_cast<int>(component.status()));
            return nullptr;
        }
        QObject* obj = component.create(engine->rootContext());
        auto* item = qobject_cast<QQuickItem*>(obj);
        if (!item) {
            qWarning("CpuMeterFactory: component is not a QQuickItem");
            if (obj) {
                obj->deleteLater();
            }
            return nullptr;
        }
        item->setParent(parent);
        if (auto* parentItem = qobject_cast<QQuickItem*>(parent)) {
            item->setParentItem(parentItem);
        } else {
            qWarning("CpuMeterFactory: parent is not a QQuickItem — widget will be invisible");
        }
        // Default CppOwnership matches the QObject parent-cascade
        // we just established. Mixing JavaScriptOwnership in here
        // would create dual ownership with the parent and is the
        // wrong default for a factory-created child.
        return item;
    }
};

} // namespace

extern "C" Q_DECL_EXPORT PhosphorRegistry::IBarWidgetFactory* phosphor_registry_create_factory()
{
    // The PluginLoader ABI contract (PluginLoader.h) says the entry
    // point must not throw — exceptions crossing the extern "C"
    // boundary are UB. Throwing-new could violate that contract on
    // allocation failure; nothrow-new returns nullptr instead, which
    // the loader treats as a recoverable "plugin construction
    // failed" failure mode. This also keeps the plugin buildable
    // with -fno-exceptions.
    return new (std::nothrow) CpuMeterFactory();
}
