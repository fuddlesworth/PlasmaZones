// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once

#include "PhosphorControl/StagingDomain.h"

#include <QString>

namespace PhosphorControl {

/**
 * Base class for a settings page: a staging domain backed by a QML page
 * that appears in the sidebar.
 *
 * Subclasses provide the page identity (id) and the staged-state methods
 * inherited from StagingDomain. Title and qmlSource are wired at
 * registration time by ApplicationController::registerPage().
 *
 * The id must be globally unique within the application and stable across
 * runs — it is used by PageRegistry for lookup, by ApplicationController
 * to address the current page, and by QML for state restoration.
 */
class PHOSPHORCONTROL_EXPORT PageController : public StagingDomain
{
    Q_OBJECT
    Q_PROPERTY(QString id READ id CONSTANT)
    QML_NAMED_ELEMENT(PageController)
    QML_UNCREATABLE("PageController is an abstract base; subclass in C++.")

public:
    explicit PageController(QString id, QObject* parent = nullptr);
    ~PageController() override;

    QString id() const;

private:
    QString m_id;
};

} // namespace PhosphorControl
