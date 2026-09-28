// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once
#include <QVariantList>
#include <QString>
#include <memory>
namespace PhosphorShell {
class SystemStatsGpu
{
public:
    explicit SystemStatsGpu(const QString& sysRoot);
    ~SystemStatsGpu();
    QVariantList sample();

private:
    struct Private;
    std::unique_ptr<Private> d;
};
}
