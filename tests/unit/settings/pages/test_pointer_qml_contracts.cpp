// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_pointer_qml_contracts.cpp
 * @brief Contracts between the pointer preview QML and PointerPreviewController,
 *        pinned by parsing the QML source itself.
 *
 * QML resolves names at runtime and the settings app has no QML test harness,
 * so a renamed or mistyped invokable is a silent TypeError and a dead control
 * in the running app rather than a build failure. The animation route carries
 * this guard for its own pane and the decoration route for its own, and each
 * deliberately excludes the other routes because their controller is a
 * different class. The pointer route is excluded from the decoration sweep for
 * exactly that reason, which is what makes this file its counterpart rather
 * than a duplicate.
 *
 * Source-scrape, with the usual limitation: it checks that a call CAN resolve,
 * not what it does.
 */

#include "pages/pointerpreviewcontroller.h"

#include <QFile>
#include <QIODevice>
#include <QMetaMethod>
#include <QMetaObject>
#include <QRegularExpression>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QtTest/QtTest>

using namespace PlasmaZones;

namespace {

QString readFile(const QString& path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return {};
    }
    return QString::fromUtf8(f.readAll());
}

/// Strip comments before scraping, matching the sibling guards: a call that
/// survives only in prose is not a caller, and a `//` inside a string must not
/// swallow the rest of a line that carries one.
QString stripComments(QString src)
{
    static const QRegularExpression blockCommentRe(QStringLiteral("/\\*.*?\\*/"),
                                                   QRegularExpression::DotMatchesEverythingOption);
    static const QRegularExpression lineCommentRe(QStringLiteral("(?<![:\"'])//[^\n]*"));
    src.remove(blockCommentRe);
    src.remove(lineCommentRe);
    return src;
}

/// Every `<receiver>.<name>` used across @p paths.
QSet<QString> scrapeCalls(const QStringList& paths, const QString& receiver, QString* readError)
{
    QSet<QString> used;
    const QRegularExpression callRe(QStringLiteral("\\b%1\\.([A-Za-z_][A-Za-z0-9_]*)").arg(receiver));
    for (const QString& path : paths) {
        const QString raw = readFile(path);
        if (raw.isEmpty()) {
            *readError = path;
            return used;
        }
        const QString src = stripComments(raw);
        auto it = callRe.globalMatch(src);
        while (it.hasNext()) {
            used.insert(it.next().captured(1));
        }
    }
    return used;
}

/// Names in @p used that are neither a property nor a method on @p meta.
QStringList unreachableOn(const QMetaObject* meta, const QSet<QString>& used)
{
    QStringList unreachable;
    for (const QString& name : used) {
        const QByteArray raw = name.toUtf8();
        if (meta->indexOfProperty(raw.constData()) >= 0) {
            continue;
        }
        bool found = false;
        for (int i = 0; i < meta->methodCount() && !found; ++i) {
            found = meta->method(i).name() == raw;
        }
        if (!found) {
            unreachable.append(name);
        }
    }
    unreachable.sort();
    return unreachable;
}

const QString kShadersQml = QStringLiteral(P_SOURCE_DIR "/src/settings/qml/pages/shaders");

} // namespace

class TestPointerQmlContracts : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void everyPreviewControllerCallFromThePointerQmlIsReachable();
    void theBrowserDialogRoutesThePointerPreviewKind();
};

void TestPointerQmlContracts::everyPreviewControllerCallFromThePointerQmlIsReachable()
{
    // Both files, not just the pane: the canvas is where the simulated pointer
    // is driven, so it carries the calls most likely to be renamed.
    const QStringList paths{kShadersQml + QStringLiteral("/PointerPreviewPane.qml"),
                            kShadersQml + QStringLiteral("/PointerPreviewCanvas.qml")};

    QString readError;
    const QSet<QString> used = scrapeCalls(paths, QStringLiteral("previewController"), &readError);
    QVERIFY2(readError.isEmpty(), qPrintable(QStringLiteral("cannot read ") + readError));
    QVERIFY2(!used.isEmpty(), "scraped no previewController.* names — a pointer preview file or the receiver moved");

    PointerPreviewController controller;
    const QStringList unreachable = unreachableOn(controller.metaObject(), used);
    QVERIFY2(unreachable.isEmpty(),
             qPrintable(QStringLiteral("the pointer preview QML calls these on previewController, but "
                                       "PointerPreviewController lacks them: %1")
                            .arg(unreachable.join(QStringLiteral(", ")))));
}

void TestPointerQmlContracts::theBrowserDialogRoutesThePointerPreviewKind()
{
    // The browser dialog picks a preview pane by the page's previewKind, so it
    // must actually route the pointer token somewhere or the pane is dead code
    // whatever the controller answers. Which controller answers "pointer" is
    // checked on the decoration route that now owns the pointer surface.
    const QString dialog = readFile(kShadersQml + QStringLiteral("/ShaderBrowserDetailDialog.qml"));
    QVERIFY2(!dialog.isEmpty(), "cannot read ShaderBrowserDetailDialog.qml");
    QVERIFY2(dialog.contains(QLatin1String("\"pointer\"")),
             "ShaderBrowserDetailDialog does not route the pointer previewKind");
    QVERIFY2(dialog.contains(QLatin1String("PointerPreviewPane")),
             "ShaderBrowserDetailDialog never instantiates PointerPreviewPane");
}

QTEST_MAIN(TestPointerQmlContracts)
#include "test_pointer_qml_contracts.moc"
