// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include "PhosphorControl/PageController.h"

#include <QDebug>

namespace PhosphorControl {

PageController::PageController(QString id, QObject* parent)
    : StagingDomain(parent)
    , m_id(std::move(id))
{
    // PageRegistry::registerPage() rejects empty ids, but by that
    // point the controller has already been constructed and parented.
    // Warn here so the construction-site error surfaces before
    // registration time.
    if (m_id.isEmpty()) {
        qWarning() << "PhosphorControl::PageController: constructed with empty id — registration will fail";
    }
}

PageController::~PageController() = default;

QString PageController::id() const
{
    return m_id;
}

} // namespace PhosphorControl
