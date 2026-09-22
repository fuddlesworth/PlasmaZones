// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "cli_io.h"

#include <QTextStream>

#include <cstdio>

namespace PhosphorPipeWireCli {

QTextStream& out()
{
    static QTextStream stream(stdout);
    return stream;
}

QTextStream& err()
{
    static QTextStream stream(stderr);
    return stream;
}

} // namespace PhosphorPipeWireCli
