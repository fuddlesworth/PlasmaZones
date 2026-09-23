// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorShell/ScreenModel.h>

#include <PhosphorLayer/IScreenProvider.h>

#include <QLoggingCategory>
#include <QQmlEngine>
#include <QScreen>

Q_LOGGING_CATEGORY(lcScreenModel, "phosphorshell.screenmodel")

namespace PhosphorShell {

ScreenModel::ScreenModel(PhosphorLayer::IScreenProvider* provider, QObject* parent)
    : QAbstractListModel(parent)
    , m_provider(provider)
{
    // qFatal, not Q_ASSERT_X: the two lines below dereference m_provider
    // unconditionally, and data() / onScreensChanged() do the same for the
    // model's whole lifetime — m_provider is never reset. Q_ASSERT_X compiles
    // out under NDEBUG, so a null provider was a debug abort and a release
    // segfault. Matches LayoutRegistry's ctor, which qFatals for the same
    // reason (initCommon() dereferences its store unconditionally).
    if (m_provider == nullptr) {
        qFatal("ScreenModel: IScreenProvider is required — the model dereferences it on every screen query");
    }
    m_screens = m_provider->screens();

    // Resolved once and guarded, matching ShellEngine::load: the notifier is
    // optional — a screen provider need not expose one — and connecting to a
    // null sender would silently do nothing but print two QObject::connect
    // warnings per model. A null notifier means the model is a one-shot
    // snapshot of the screens at construction: it still renders, it just never
    // refreshes. Hoisted into a local rather than calling notifier() twice,
    // since the interface does not promise a stable pointer across calls.
    auto* notifier = m_provider->notifier();
    if (notifier == nullptr) {
        qCWarning(lcScreenModel)
            << "screenProvider exposes no notifier — the screen list will not update on topology or primary changes";
        return;
    }

    connect(notifier, &PhosphorLayer::ScreenProviderNotifier::screensChanged, this, &ScreenModel::onScreensChanged);
    // The provider's screensChanged signal fires for set / geometry changes
    // but not for a primary-screen swap on the same set. KDE allows changing
    // primary at runtime. We listen to the provider's primaryChanged
    // signal (rather than qGuiApp directly) so a custom IScreenProvider
    // implementation whose `primary()` diverges from qGuiApp — e.g. a
    // virtual-screen provider with focused-monitor primary policy — gets
    // its bindings refreshed via its own state machine. The default
    // provider re-emits primaryChanged when qGuiApp does.
    connect(notifier, &PhosphorLayer::ScreenProviderNotifier::primaryChanged, this,
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
        // data() is a Q_INVOKABLE, and a parentless QObject returned from
        // one takes JavaScript ownership: the engine's collector would then
        // delete the QScreen (a hot reload tears the old engine down, and
        // the next engine's delegates read a dead screen). The screens are
        // the application's.
        QQmlEngine::setObjectOwnership(screen, QQmlEngine::CppOwnership);
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
