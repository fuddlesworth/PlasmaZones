// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
#include "PickerController.h"
#include <PhosphorLayer/IScreenProvider.h>
#include <PhosphorTheme/AppearanceStore.h>
#include <PhosphorShellPicker/AppearanceLibrary.h>
#include <QScreen>
namespace PhosphorShellApp {
namespace {
PhosphorTheme::AppearanceStore* appearance()
{
    return PhosphorTheme::AppearanceStore::create(nullptr, nullptr);
}
}
PickerController::PickerController(PhosphorLayer::IScreenProvider* screens, QObject* parent)
    : QObject(parent)
    , m_screens(screens)
{
    if (m_screens && m_screens->notifier()) {
        connect(m_screens->notifier(), &PhosphorLayer::ScreenProviderNotifier::screensChanged, this, [this] {
            Q_EMIT screensChanged();
            bool targetExists = false, selectionExists = false;
            for (const auto* screen : m_screens->screens()) {
                targetExists |= screen->name() == m_openScreen;
                selectionExists |= screen->name() == m_selectedScreen;
            }
            if (!selectionExists) {
                m_selectedScreen = resolveScreen(QString());
                Q_EMIT selectedScreenChanged();
            }
            if (!m_openScreen.isEmpty() && !targetExists)
                setOpenScreen(resolveScreen(QString()));
        });
    }
}
PickerController::~PickerController() = default;
QVariantList PickerController::screens() const
{
    QVariantList result;
    if (!m_screens)
        return result;
    int index = 0;
    for (const auto* screen : m_screens->screens()) {
        result.append(QVariantMap{{QStringLiteral("value"), screen->name()},
                                  {QStringLiteral("label"), QStringLiteral("%1 · %2").arg(++index).arg(screen->name())},
                                  {QStringLiteral("width"), screen->size().width()},
                                  {QStringLiteral("height"), screen->size().height()}});
    }
    return result;
}
void PickerController::show(const QString& screenName)
{
    const auto target = resolveScreen(screenName);
    if (target.isEmpty())
        return;
    PhosphorShellPicker::AppearanceLibrary::create(nullptr, nullptr)->rescan();
    if (!appearance()->beginPreview())
        return;
    if (m_selectedScreen.isEmpty()) {
        m_selectedScreen = target;
        Q_EMIT selectedScreenChanged();
    }
    setOpenScreen(target);
}
void PickerController::toggle(const QString& screenName)
{
    const auto target = resolveScreen(screenName);
    if (m_openScreen == target && !target.isEmpty())
        hide();
    else
        show(target);
}
void PickerController::hide()
{
    if (m_openScreen.isEmpty())
        return;
    if (m_desktopPreview) {
        m_desktopPreview = false;
        Q_EMIT desktopPreviewChanged();
        return;
    }
    if (appearance()->dirty()) {
        m_closePending = true;
        Q_EMIT closePendingChanged();
        return;
    }
    discard();
}
void PickerController::discard()
{
    appearance()->endPreview();
    m_desktopPreview = false;
    Q_EMIT desktopPreviewChanged();
    keepEditing();
    setOpenScreen(QString());
}
bool PickerController::apply(bool close)
{
    if (!appearance()->applyPreview())
        return false;
    if (close)
        discard();
    return true;
}
void PickerController::keepEditing()
{
    if (m_closePending) {
        m_closePending = false;
        Q_EMIT closePendingChanged();
    }
}
QString PickerController::resolveScreen(const QString& screenName) const
{
    if (!m_screens)
        return {};
    for (const auto* screen : m_screens->screens()) {
        if (screen->name() == screenName)
            return screenName;
    }
    const auto* screen = m_screens->focused() ? m_screens->focused() : m_screens->primary();
    return screen ? screen->name() : QString();
}
void PickerController::setOpenScreen(const QString& screenName)
{
    if (m_openScreen != screenName) {
        m_openScreen = screenName;
        Q_EMIT openScreenChanged();
    }
}
}
