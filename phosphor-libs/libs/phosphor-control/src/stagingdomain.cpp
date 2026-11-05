// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include "PhosphorControl/StagingDomain.h"

namespace PhosphorControl {

StagingDomain::StagingDomain(QObject* parent)
    : QObject(parent)
{
}

StagingDomain::~StagingDomain() = default;

void StagingDomain::resetToDefaults()
{
    // Domains that do not expose factory defaults need not override this.
}

} // namespace PhosphorControl
