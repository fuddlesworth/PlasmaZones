// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
//
// phosphor-service-clipboard-cli: headless driver + worked example for the
// phosphor-service-clipboard library.
//
// Phase 2.8 milestone-2 skeleton: it constructs the ClipboardService and reports
// whether clipboard support is available, then exits. The live demo (watch the
// clipboard, list the history, copy an entry back) lands with the later
// milestones, where it runs under the shell's Wayland platform so the
// data-control protocol is actually advertised. Under a plain QCoreApplication
// there is no Wayland platform integration, so `supported` reads false here by
// construction.

#include <PhosphorServiceClipboard/ClipboardService.h>

#include <QCoreApplication>
#include <QTextStream>

using namespace PhosphorServiceClipboard;

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);

    ClipboardService service;

    QTextStream out(stdout);
    out << "phosphor-service-clipboard skeleton\n"
        << "  clipboard support (this process): " << (service.isSupported() ? "yes" : "no") << "\n"
        << "  (the live watch / list / copy demo arrives with the later Phase 2.8 milestones;\n"
        << "   it runs under the shell's Wayland platform where the data-control protocol is live.)\n";
    out.flush();

    return 0;
}
