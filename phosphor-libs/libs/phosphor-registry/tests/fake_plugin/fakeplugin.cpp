// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

// Fake bar widget plugin used by test_pluginloader.cpp. Exports the
// canonical Phase-1.3 entry point and returns a trivial factory.
// Not a real widget — createWidget returns a default-constructed
// QQuickItem; the test only checks load / register / unload paths.

#include <PhosphorRegistry/IBarWidgetFactory.h>

#include <QQmlEngine>
#include <QQuickItem>
#include <QtCore/qtclasshelpermacros.h>

namespace {

class FakePluginFactory : public PhosphorRegistry::IBarWidgetFactory
{
public:
    FakePluginFactory() = default;
    ~FakePluginFactory() override = default;
    Q_DISABLE_COPY_MOVE(FakePluginFactory)

    [[nodiscard]] QString id() const override
    {
        return QStringLiteral("fake-plugin");
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
        // The real bar host always passes a QQuickItem* parent. If a
        // caller passes a non-QQuickItem QObject parent, we still
        // return a valid item but with no scene-graph parent — the
        // test for that path lives in test_registry.cpp.
        auto* parentItem = qobject_cast<QQuickItem*>(parent);
        auto* item = new QQuickItem(parentItem);
        if (parent && !parentItem) {
            item->setParent(parent);
        }
        return item;
    }
};

} // namespace

extern "C" Q_DECL_EXPORT PhosphorRegistry::IBarWidgetFactory* phosphor_registry_create_factory()
{
    return new FakePluginFactory();
}
