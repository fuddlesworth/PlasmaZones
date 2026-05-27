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

#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QString>
#include <QStringList>

namespace {

constexpr const char* kCpuMeterQml = R"(
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
            return nullptr;
        }
        QQmlComponent component(engine);
        component.setData(QByteArray(kCpuMeterQml), QUrl(QStringLiteral("inline:cpu-meter")));
        if (component.isError()) {
            qWarning("CpuMeterFactory: component error %s", qPrintable(component.errorString()));
            return nullptr;
        }
        QObject* obj = component.create(engine->rootContext());
        auto* item = qobject_cast<QQuickItem*>(obj);
        if (!item) {
            if (obj) {
                obj->deleteLater();
            }
            return nullptr;
        }
        item->setParent(parent);
        if (auto* parentItem = qobject_cast<QQuickItem*>(parent)) {
            item->setParentItem(parentItem);
        }
        return item;
    }
};

} // namespace

extern "C" Q_DECL_EXPORT PhosphorRegistry::IBarWidgetFactory* phosphor_registry_create_factory()
{
    return new CpuMeterFactory();
}
