// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "pageadapter.h"

namespace PlasmaZones {

PageAdapter::PageAdapter(QString id, QObject* parent)
    : PhosphorControl::PageController(std::move(id), parent)
{
}

PageAdapter::~PageAdapter() = default;

bool PageAdapter::isDirty() const
{
    // Dirty tracking is global in PlasmaZones — SettingsController.needsSave
    // flows through SettingsStagingDomain (see settingsstagingdomain.h),
    // which is registered alongside these adapters on the same
    // ApplicationController. Adapter pages stay clean so the global dirty
    // flag is the sole signal driving the footer.
    return false;
}

void PageAdapter::apply()
{
}

void PageAdapter::discard()
{
}

} // namespace PlasmaZones
