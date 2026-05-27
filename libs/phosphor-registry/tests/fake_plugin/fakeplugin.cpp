// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

// Fake bar widget plugin used by test_pluginloader.cpp. Exports the
// canonical Phase-1.3 entry point and returns a trivial factory.
// Not a real widget — createWidget returns a default-constructed
// QQuickItem; the test only checks load / register / unload paths.

#include <PhosphorRegistry/IBarWidgetFactory.h>

#include <QQuickItem>

namespace {

class FakePluginFactory : public PhosphorRegistry::IBarWidgetFactory
{
public:
    QString id() const override
    {
        return QStringLiteral("fake-plugin");
    }
    QString displayName() const override
    {
        return QStringLiteral("Fake Plugin");
    }
    QStringList capabilities() const override
    {
        return {QStringLiteral("bar.widget")};
    }
    QQuickItem* createWidget(QQmlEngine* /*engine*/, QObject* parent) override
    {
        return new QQuickItem(qobject_cast<QQuickItem*>(parent));
    }
};

} // namespace

extern "C" Q_DECL_EXPORT PhosphorRegistry::IBarWidgetFactory* phosphor_registry_create_factory()
{
    return new FakePluginFactory();
}
