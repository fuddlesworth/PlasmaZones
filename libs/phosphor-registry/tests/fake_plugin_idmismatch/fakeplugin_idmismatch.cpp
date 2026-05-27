// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

// Test fixture for PluginLoader's factory-id-vs-manifest-id mismatch
// detection path. The factory returned here claims id "fake-other";
// the accompanying manifest.json claims id "id-mismatch-plugin". The
// loader must reject the load on mismatch.

#include <PhosphorRegistry/IBarWidgetFactory.h>

#include <QQuickItem>
#include <QtCore/qtclasshelpermacros.h>

namespace {

class FakePluginIdMismatchFactory : public PhosphorRegistry::IBarWidgetFactory
{
public:
    FakePluginIdMismatchFactory() = default;
    ~FakePluginIdMismatchFactory() override = default;
    Q_DISABLE_COPY_MOVE(FakePluginIdMismatchFactory)

    [[nodiscard]] QString id() const override
    {
        return QStringLiteral("fake-other");
    }
    [[nodiscard]] QString displayName() const override
    {
        return QStringLiteral("Fake Plugin Id Mismatch");
    }
    [[nodiscard]] QStringList capabilities() const override
    {
        return {QStringLiteral("bar.widget")};
    }
    [[nodiscard]] QQuickItem* createWidget(QQmlEngine* /*engine*/, QObject* parent) override
    {
        return new QQuickItem(qobject_cast<QQuickItem*>(parent));
    }
};

} // namespace

extern "C" Q_DECL_EXPORT PhosphorRegistry::IBarWidgetFactory* phosphor_registry_create_factory()
{
    return new FakePluginIdMismatchFactory();
}
