// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
//
// phosphor-service-lock-cli: headless driver + worked example for the
// phosphor-service-lock library.
//
// Phase 2.9 milestone-2 skeleton: it constructs the LockService and reports
// whether session-lock support is available, then exits. The live demo
// (authenticate the user and print success/failure; lock/unlock the session)
// lands with the later milestones. The authenticate path needs no compositor;
// the lock path runs under the shell's Wayland platform where the
// ext-session-lock-v1 protocol is advertised. Under a plain QCoreApplication
// there is no Wayland platform integration, so `supported` reads false here by
// construction.

#include <PhosphorServiceLock/LockService.h>

#include <QCoreApplication>
#include <QTextStream>

using namespace PhosphorServiceLock;

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);

    LockService service;

    QTextStream out(stdout);
    out << "phosphor-service-lock skeleton\n"
        << "  session-lock support (this process): " << (service.isSupported() ? "yes" : "no") << "\n"
        << "  (the live authenticate / lock demo arrives with the later Phase 2.9 milestones;\n"
        << "   authenticate runs anywhere, lock runs under the shell's Wayland platform.)\n";
    out.flush();

    return 0;
}
