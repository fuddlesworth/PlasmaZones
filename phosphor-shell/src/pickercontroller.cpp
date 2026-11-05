// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "PickerController.h"

#include <PhosphorLayer/IScreenProvider.h>

#include <QScreen>

namespace PhosphorShellApp {

PickerController::PickerController(PhosphorLayer::IScreenProvider* screens, QObject* parent)
    : QObject(parent)
    , m_screens(screens)
{
}

PickerController::~PickerController() = default;

QString PickerController::openScreen() const
{
    return m_openScreen;
}

int PickerController::targetCount() const
{
    const int screenCount = m_screens ? static_cast<int>(m_screens->screens().size()) : 0;
    return screenCount * kSurfacesPerScreen + kSharedSurfaces;
}

void PickerController::show(const QString& screenName)
{
    setOpenScreen(resolveScreen(screenName));
}

void PickerController::toggle(const QString& screenName)
{
    const QString target = resolveScreen(screenName);
    if (m_openScreen == target && !target.isEmpty()) {
        hide();
        return;
    }
    setOpenScreen(target);
}

void PickerController::hide()
{
    setOpenScreen({});
}

QString PickerController::resolveScreen(const QString& screenName) const
{
    if (!screenName.isEmpty()) {
        return screenName;
    }
    if (!m_screens) {
        return {};
    }
    // Focused, not primary: the strip belongs on the output the user is
    // working on, which is where the retint is looked at.
    QScreen* screen = m_screens->focused();
    if (!screen) {
        screen = m_screens->primary();
    }
    return screen ? screen->name() : QString();
}

void PickerController::setOpenScreen(const QString& screenName)
{
    if (m_openScreen == screenName) {
        return;
    }
    m_openScreen = screenName;
    Q_EMIT openScreenChanged();
}

} // namespace PhosphorShellApp
