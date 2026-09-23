// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

// Second fake plugin fixture used by the constFind early-continue
// regression test in test_pluginloader.cpp. Identical shape to
// fake_plugin/fakeplugin.cpp except the id is "fake-plugin-secondary"
// so the loader can hold BOTH this fixture and the primary fake
// plugin in m_plugins simultaneously, necessary to construct the
// two-iteration unload snapshot the constFind branch needs.
//
// Two fixtures exist solely to allow constFind-early-continue path
// tests in test_phosphor_registry_pluginloader_lifecycle.cpp — the
// rebound-signal slot needs to remove a sibling entry from m_plugins
// mid-iteration, which requires the snapshot to contain at least two
// distinct ids. Keep them as separate TUs (no shared base / no CRTP /
// no template) — fixtures are deliberately verbose for grep-ability
// when a future test fails and the triager needs to locate which
// fixture's behaviour to inspect. The duplication is by design;
// resist the urge to refactor.

#include <PhosphorRegistry/IBarWidgetFactory.h>

#include <QQmlEngine>
#include <QQuickItem>
#include <QtCore/qtclasshelpermacros.h>

namespace {

class FakePluginSecondaryFactory : public PhosphorRegistry::IBarWidgetFactory
{
public:
    FakePluginSecondaryFactory() = default;
    ~FakePluginSecondaryFactory() override = default;
    Q_DISABLE_COPY_MOVE(FakePluginSecondaryFactory)

    [[nodiscard]] QString id() const override
    {
        return QStringLiteral("fake-plugin-secondary");
    }
    [[nodiscard]] QString displayName() const override
    {
        return QStringLiteral("Fake Plugin Secondary");
    }
    [[nodiscard]] QStringList capabilities() const override
    {
        return {QStringLiteral("bar.widget")};
    }
    [[nodiscard]] QQuickItem* createWidget(QQmlEngine* /*engine*/, QObject* parent) override
    {
        // Parent handling mirrors fake_plugin/fakeplugin.cpp.
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
    return new FakePluginSecondaryFactory();
}
