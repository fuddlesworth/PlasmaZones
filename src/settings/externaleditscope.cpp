// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "externaleditscope.h"

#include "settingscontroller.h"

namespace PlasmaZones {

ExternalEditScope::ExternalEditScope(SettingsController& owner, const QString& page)
    : m_owner(owner)
{
    m_owner.beginExternalEdit(page);
}

ExternalEditScope::~ExternalEditScope()
{
    m_owner.endExternalEdit();
}

} // namespace PlasmaZones
