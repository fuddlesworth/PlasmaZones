// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once

#include <PhosphorShell/phosphorshell_export.h>
#include <QByteArray>
#include <QVariantMap>
#include <memory>

namespace PhosphorShell {
// A synchronous Linux collector. Run on a worker thread; paths may point to a
// proc/sys snapshot. Unavailable numeric fields use -1, never a fabricated zero.
class PHOSPHORSHELL_EXPORT SystemStatsReader
{
public:
    explicit SystemStatsReader(const QString& procRoot = QStringLiteral("/proc"),
                               const QString& sysRoot = QStringLiteral("/sys"));
    ~SystemStatsReader();
    QVariantMap sample();
    void resetRates();
    static QVariantMap memoryFrom(const QByteArray& content);
    static double counterRate(quint64 previous, quint64 current, double seconds);

private:
    struct Private;
    std::unique_ptr<Private> d;
};
}
