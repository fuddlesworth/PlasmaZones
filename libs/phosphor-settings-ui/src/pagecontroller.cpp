// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include "PhosphorSettingsUi/PageController.h"

namespace PhosphorSettingsUi {

PageController::PageController(QString id, QObject* parent)
    : StagingDomain(parent)
    , m_id(std::move(id))
{
}

PageController::~PageController() = default;

QString PageController::id() const
{
    return m_id;
}

} // namespace PhosphorSettingsUi
