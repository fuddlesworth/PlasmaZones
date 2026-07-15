// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_layoutregistry_transfer.cpp
 * @brief Unit tests for LayoutRegistry::exportLayout / importLayout reporting.
 *
 * Both used to be void, so every failure was a warning in the journal and
 * nothing else: the D-Bus adaptor above them inferred an import failure from
 * the layout count and could not see an export failure at all. These pin the
 * return values the callers now report to the user, and the atomic write that
 * keeps a failed export from destroying whatever the user picked.
 */

#include <QTest>
#include <QDir>
#include <QFile>
#include <memory>

#include <PhosphorZones/Layout.h>
#include <PhosphorZones/LayoutRegistry.h>
#include <PhosphorZones/Zone.h>

#include "../helpers/IsolatedConfigGuard.h"
#include "../helpers/LayoutRegistryTestHelpers.h"

using PlasmaZones::TestHelpers::IsolatedConfigGuard;

class TestLayoutRegistryTransfer : public QObject
{
    Q_OBJECT

private:
    PhosphorZones::Layout* createTestLayout(const QString& name, QObject* parent = nullptr)
    {
        auto* layout = new PhosphorZones::Layout(name, parent);
        auto* zone = new PhosphorZones::Zone();
        zone->setRelativeGeometry(QRectF(0, 0, 1, 1));
        layout->addZone(zone);
        return layout;
    }

    PhosphorZones::LayoutRegistry* createRegistry(QObject* parent = nullptr)
    {
        m_guards.emplace_back(std::make_unique<IsolatedConfigGuard>());
        auto* mgr = PlasmaZones::TestHelpers::makeLayoutRegistry(QStringLiteral("plasmazones/layouts"), parent);
        const QString layoutDir = m_guards.back()->dataPath() + QStringLiteral("/plasmazones/layouts");
        QDir().mkpath(layoutDir);
        mgr->setLayoutDirectory(layoutDir);
        return mgr;
    }

    QString tempPath(const QString& fileName) const
    {
        return m_guards.back()->dataPath() + QLatin1Char('/') + fileName;
    }

    std::vector<std::unique_ptr<IsolatedConfigGuard>> m_guards;

private Q_SLOTS:

    void cleanup()
    {
        m_guards.clear();
    }

    // A round trip reports true on both legs, which is the whole basis for the
    // callers' success paths.
    void exportThenImportReportsSuccess()
    {
        QObject owner;
        auto* registry = createRegistry(&owner);
        auto* layout = createTestLayout(QStringLiteral("Exported"), &owner);

        const QString dest = tempPath(QStringLiteral("out.json"));
        QVERIFY(registry->exportLayout(layout, dest));
        QVERIFY(QFile::exists(dest));

        QVERIFY(registry->importLayout(dest) != nullptr);
    }

    // Export answers false rather than reporting a success the caller then
    // toasts. A directory is a destination QSaveFile cannot open.
    void exportReportsFailureOnUnwritableDestination()
    {
        QObject owner;
        auto* registry = createRegistry(&owner);
        auto* layout = createTestLayout(QStringLiteral("Exported"), &owner);

        const QString dir = tempPath(QStringLiteral("a-directory"));
        QVERIFY(QDir().mkpath(dir));
        QVERIFY(!registry->exportLayout(layout, dir));
    }

    // The staged write has to actually land. QSaveFile discards its temporary
    // unless commit() is called, so an export that skipped the commit would
    // report whatever it liked while the destination kept its old contents and
    // nobody noticed. Pin that the destination really is replaced.
    void exportReplacesExistingDestination()
    {
        QObject owner;
        auto* registry = createRegistry(&owner);
        auto* layout = createTestLayout(QStringLiteral("Replacement"), &owner);

        const QString dest = tempPath(QStringLiteral("existing.json"));
        {
            QFile f(dest);
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write("{\"stale\":true}");
        }

        QVERIFY(registry->exportLayout(layout, dest));

        QFile f(dest);
        QVERIFY(f.open(QIODevice::ReadOnly));
        const QByteArray written = f.readAll();
        QVERIFY(!written.contains("stale"));
        QVERIFY(written.contains("Replacement"));
    }

    // Every rejection the registry can name answers nullptr. Each of these was
    // a bare `return` that the adaptor could only see as "the count did not
    // grow". One row per scenario, so an early failure cannot hide the rest.
    void importReportsFailureForEveryRejection_data()
    {
        QTest::addColumn<QString>("fileName");
        QTest::addColumn<bool>("createFile");
        QTest::addColumn<QByteArray>("content");

        QTest::newRow("empty path") << QString() << false << QByteArray();
        QTest::newRow("missing file") << QStringLiteral("does-not-exist.json") << false << QByteArray();
        QTest::newRow("empty file") << QStringLiteral("empty.json") << true << QByteArray();
        QTest::newRow("non-JSON") << QStringLiteral("not.json") << true << QByteArrayLiteral("this is not json at all");
        // Valid JSON that is not a layout: parses, then fails schema validation.
        QTest::newRow("non-layout JSON") << QStringLiteral("notlayout.json") << true
                                         << QByteArrayLiteral("{\"unrelated\":1}");
    }

    void importReportsFailureForEveryRejection()
    {
        QFETCH(QString, fileName);
        QFETCH(bool, createFile);
        QFETCH(QByteArray, content);

        QObject owner;
        auto* registry = createRegistry(&owner);

        const QString path = fileName.isEmpty() ? QString() : tempPath(fileName);
        if (createFile) {
            QFile f(path);
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write(content);
        }

        QVERIFY(registry->importLayout(path) == nullptr);
    }
};

QTEST_MAIN(TestLayoutRegistryTransfer)
#include "test_layoutregistry_transfer.moc"
