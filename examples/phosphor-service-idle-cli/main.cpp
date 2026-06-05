// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
//
// phosphor-service-idle-cli: headless driver + worked example for the
// phosphor-service-idle library.
//
// Phase 2.7 milestone-1 skeleton: it constructs the IdleService and reports
// whether idle support is available, then exits. The live demo (configure
// timeouts, log each stage fire with a wall-clock timestamp, toggle an
// inhibitor) lands with the later milestones, where it runs under the shell's
// Wayland platform so the idle protocol is actually advertised. Under a plain
// QCoreApplication there is no Wayland platform integration, so `supported`
// reads false here by construction.

#include <PhosphorServiceIdle/IdleService.h>

#include <QCoreApplication>
#include <QTextStream>

using namespace PhosphorServiceIdle;

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);

    IdleService service;

    QTextStream out(stdout);
    out << "phosphor-service-idle skeleton\n"
        << "  idle support (this process): " << (service.isSupported() ? "yes" : "no") << "\n"
        << "  (the live timeout / inhibit demo arrives with the later Phase 2.7 milestones;\n"
        << "   it runs under the shell's Wayland platform where the idle protocol is live.)\n";
    out.flush();

    return 0;
}
