// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "cli_env.h"

#include <QByteArray>
#include <QString>

#include <cmath>

namespace PhosphorPipeWireCli {

int envOverrideMs(const char* name, int fallbackMs)
{
    const QByteArray overrideEnv = qgetenv(name);
    if (overrideEnv.isEmpty())
        return fallbackMs;
    bool ok = false;
    const int v = overrideEnv.toInt(&ok);
    return (ok && v > 0) ? v : fallbackMs;
}

qreal envOverrideDouble(const char* name, qreal fallback)
{
    const QByteArray overrideEnv = qgetenv(name);
    if (overrideEnv.isEmpty())
        return fallback;
    bool ok = false;
    const double v = QString::fromUtf8(overrideEnv).toDouble(&ok);
    return (ok && std::isfinite(v) && v > 0.0) ? v : fallback;
}

int connectTimeoutMs()
{
    return envOverrideMs("PHOSPHOR_PW_CONNECT_TIMEOUT_MS", kDefaultConnectTimeoutMs);
}

qreal volumeEchoEpsilon()
{
    return envOverrideDouble("PHOSPHOR_PW_VOLUME_ECHO_EPSILON", kDefaultVolumeEchoEpsilon);
}

int postWriteSettleMs()
{
    return envOverrideMs("PHOSPHOR_PW_POST_WRITE_SETTLE_MS", kDefaultPostWriteSettleMs);
}

int postMetadataSettleMs()
{
    return envOverrideMs("PHOSPHOR_PW_POST_METADATA_SETTLE_MS", kDefaultPostMetadataSettleMs);
}

} // namespace PhosphorPipeWireCli
