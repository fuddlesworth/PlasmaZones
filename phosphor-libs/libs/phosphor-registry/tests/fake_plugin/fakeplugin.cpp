// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

// One source for every bar-widget test plugin. tests/CMakeLists.txt builds
// it once per variant, each with its own fake_plugin_config.h and
// metadata.json in a per-variant include directory, so the variants differ
// only in their IID, their metadata and what their factory does.

#include "fake_plugin_config.h"

#include <PhosphorRegistry/IBarWidgetPlugin.h>

#include <QObject>
#include <QQmlEngine>
#include <QQuickItem>
#include <QtCore/qtclasshelpermacros.h>

#include <memory>

namespace {

class FakeFactory : public PhosphorRegistry::IBarWidgetFactory
{
public:
    FakeFactory() = default;
    ~FakeFactory() override = default;
    Q_DISABLE_COPY_MOVE(FakeFactory)

    [[nodiscard]] QString id() const override
    {
        return QStringLiteral(FAKE_FACTORY_ID);
    }
    [[nodiscard]] QString displayName() const override
    {
        return QStringLiteral("Fake Plugin");
    }
    [[nodiscard]] QStringList capabilities() const override
    {
        return {QStringLiteral("bar.widget")};
    }
    [[nodiscard]] QQuickItem* createWidget(QQmlEngine* /*engine*/, QObject* parent) override
    {
        auto* parentItem = qobject_cast<QQuickItem*>(parent);
        auto* item = new QQuickItem(parentItem);
        if (parent && !parentItem) {
            item->setParent(parent);
        }
        return item;
    }
};

} // namespace

class FakeBarWidgetPlugin : public QObject, public PhosphorRegistry::IBarWidgetPlugin
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID FAKE_PLUGIN_IID FILE "metadata.json")
    Q_INTERFACES(PhosphorRegistry::IBarWidgetPlugin)

public:
    std::unique_ptr<PhosphorRegistry::IBarWidgetFactory> createFactory() override
    {
        if (FAKE_NULL_FACTORY) {
            return nullptr;
        }
        return std::make_unique<FakeFactory>();
    }
};

#include "fakeplugin.moc"
