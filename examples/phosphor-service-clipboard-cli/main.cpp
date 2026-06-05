// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
//
// phosphor-service-clipboard-cli: headless driver + worked example for the
// phosphor-service-clipboard library.
//
//   watch        watch the clipboard and log each new entry (Ctrl+C to stop)
//   list         print the persisted history and exit
//   copy <n>     re-apply history entry <n> to the clipboard, then serve it
//                until Ctrl+C (taking the selection makes this process the
//                clipboard owner, which must stay alive to answer pastes)
//
// It runs under the phosphorwayland QPA integration (registered before
// QGuiApplication) so wlr-data-control is live. `list` reads only the persisted
// history and needs no compositor; `watch` / `copy` need one and report when it
// is unavailable.

#include <PhosphorServiceClipboard/ClipboardService.h>

#include <PhosphorWayland/LayerShellPluginLoader.h>

#include <QAbstractItemModel>
#include <QGuiApplication>
#include <QObject>
#include <QStringList>
#include <QTextStream>

using namespace PhosphorServiceClipboard;

namespace {
QTextStream& out()
{
    static QTextStream s(stdout);
    return s;
}
QTextStream& err()
{
    static QTextStream s(stderr);
    return s;
}

constexpr int kOk = 0;
constexpr int kUsage = 64;
constexpr int kRuntime = 1;

int usage()
{
    err() << "usage: phosphor-service-clipboard-cli <command>\n\n"
          << "  watch        watch the clipboard and log each new entry (Ctrl+C to stop)\n"
          << "  list         print the persisted history and exit\n"
          << "  copy <n>     re-apply history entry <n> to the clipboard, then serve it\n"
          << "               until Ctrl+C\n";
    err().flush();
    return kUsage;
}

int previewRole(const QAbstractItemModel* model)
{
    const int role = model->roleNames().key(QByteArrayLiteral("preview"), -1);
    return role >= 0 ? role : Qt::DisplayRole;
}

void printList(const QAbstractItemModel* model)
{
    const int role = previewRole(model);
    const int rows = model->rowCount();
    if (rows == 0) {
        out() << "(history is empty)\n";
        out().flush();
        return;
    }
    for (int i = 0; i < rows; ++i) {
        out() << "[" << i << "] " << model->data(model->index(i, 0), role).toString() << "\n";
    }
    out().flush();
}
} // namespace

int main(int argc, char** argv)
{
    // MUST run before QGuiApplication: selects the phosphorwayland Wayland
    // shell-integration plugin so the wlr-data-control global is reachable.
    PhosphorWayland::registerLayerShellPlugin();
    QGuiApplication app(argc, argv);
    const QStringList args = app.arguments();

    if (args.size() < 2 || args.at(1) == QLatin1String("--help") || args.at(1) == QLatin1String("-h")) {
        usage();
        return args.size() < 2 ? kUsage : kOk;
    }

    const QString command = args.at(1);
    ClipboardService service;
    QAbstractItemModel* model = service.history();

    if (command == QLatin1String("list")) {
        // The persisted history is loaded at construction; print it and exit.
        printList(model);
        return kOk;
    }

    if (command == QLatin1String("watch")) {
        if (!service.isSupported()) {
            err() << "the compositor does not advertise wlr-data-control; cannot watch the clipboard.\n"
                  << "run this inside a Phosphor / wlroots-style Wayland session.\n";
            err().flush();
            return kRuntime;
        }
        const int role = previewRole(model);
        QObject::connect(model, &QAbstractItemModel::rowsInserted, &app,
                         [model, role](const QModelIndex&, int first, int) {
                             out() << "+ captured: " << model->data(model->index(first, 0), role).toString() << "\n";
                             out().flush();
                         });
        out() << "watching the clipboard (" << service.count() << " entr" << (service.count() == 1 ? "y" : "ies")
              << " in history). Ctrl+C to stop.\n";
        out().flush();
        return app.exec();
    }

    if (command == QLatin1String("copy")) {
        if (args.size() < 3) {
            err() << "copy needs a history index\n";
            return usage();
        }
        bool ok = false;
        const int index = args.at(2).toInt(&ok);
        if (!ok || index < 0 || index >= service.count()) {
            err() << "no such history entry: " << args.at(2) << " (history has " << service.count() << " entries)\n";
            err().flush();
            return kUsage;
        }
        if (!service.isSupported()) {
            err() << "the compositor does not advertise wlr-data-control; cannot set the clipboard.\n";
            err().flush();
            return kRuntime;
        }
        service.copy(index);
        out() << "copied entry " << index << " to the clipboard; serving the selection (Ctrl+C to stop).\n";
        out().flush();
        return app.exec();
    }

    return usage();
}
