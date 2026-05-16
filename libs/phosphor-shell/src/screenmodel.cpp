// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorShell/ScreenModel.h>

#include <PhosphorLayer/IScreenProvider.h>

#include <QScreen>

namespace PhosphorShell {

ScreenModel::ScreenModel(PhosphorLayer::IScreenProvider* provider, QObject* parent)
    : QAbstractListModel(parent)
    , m_provider(provider)
{
    Q_ASSERT_X(m_provider, "ScreenModel", "IScreenProvider must not be null");
    m_screens = m_provider->screens();

    connect(m_provider->notifier(), &PhosphorLayer::ScreenProviderNotifier::screensChanged, this,
            &ScreenModel::onScreensChanged);
    // The provider's screensChanged signal fires for set / geometry changes
    // but not for a primary-screen swap on the same set. KDE allows changing
    // primary at runtime. We listen to the provider's primaryChanged
    // signal (rather than qGuiApp directly) so a custom IScreenProvider
    // implementation whose `primary()` diverges from qGuiApp — e.g. a
    // virtual-screen provider with focused-monitor primary policy — gets
    // its bindings refreshed via its own state machine. The default
    // provider re-emits primaryChanged when qGuiApp does.
    connect(m_provider->notifier(), &PhosphorLayer::ScreenProviderNotifier::primaryChanged, this,
            &ScreenModel::onPrimaryScreenChanged);
}

ScreenModel::~ScreenModel() = default;

int ScreenModel::rowCount(const QModelIndex& parent) const
{
    if (parent.isValid()) {
        return 0;
    }
    return m_screens.size();
}

QVariant ScreenModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_screens.size()) {
        return {};
    }

    QScreen* screen = m_screens.at(index.row());
    if (!screen) {
        return {};
    }

    switch (role) {
    case ScreenRole:
        return QVariant::fromValue(screen);
    case NameRole:
        return screen->name();
    case WidthRole:
        return screen->size().width();
    case HeightRole:
        return screen->size().height();
    case IsPrimaryRole:
        return screen == m_provider->primary();
    default:
        return {};
    }
}

QHash<int, QByteArray> ScreenModel::roleNames() const
{
    return {
        {ScreenRole, "screen"}, {NameRole, "name"},           {WidthRole, "width"},
        {HeightRole, "height"}, {IsPrimaryRole, "isPrimary"},
    };
}

void ScreenModel::onScreensChanged()
{
    const QList<QScreen*> newScreens = m_provider->screens();

    if (newScreens == m_screens) {
        return;
    }

    beginResetModel();
    m_screens = newScreens;
    endResetModel();
}

void ScreenModel::onPrimaryScreenChanged()
{
    // Emit dataChanged for IsPrimaryRole across the whole model. Cheap —
    // primary changes are rare (user-initiated KCM action), and a targeted
    // scan to find old/new primary indices would still re-evaluate the
    // same N bindings.
    if (m_screens.isEmpty()) {
        return;
    }
    Q_EMIT dataChanged(index(0), index(m_screens.size() - 1), {IsPrimaryRole});
}

} // namespace PhosphorShell
