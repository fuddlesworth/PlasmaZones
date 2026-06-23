// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// MetadataPackLoader<T> end-to-end: scans content-pack directories
// (each a subdir with a metadata.json), parses each into a factory, and
// reconciles a Registry<T> — registering new packs, re-registering
// content-changed ones, and unregistering removed ones, with minimal
// per-entry notifier signals. Uses LiveReload::Off + explicit refresh()
// so every assertion runs against a deterministic synchronous scan.

#include <PhosphorRegistry/IFactoryBase.h>
#include <PhosphorRegistry/MetadataPackLoader.h>
#include <PhosphorRegistry/Registry.h>

#include <PhosphorFsLoader/WatchedDirectorySet.h>

#include <QtCore/QCryptographicHash>
#include <QtCore/QDir>
#include <QtCore/QJsonObject>
#include <QtCore/QLoggingCategory>
#include <QtCore/QTemporaryDir>
#include <QtTest/QSignalSpy>
#include <QtTest/QtTest>

#include <memory>

namespace {
Q_LOGGING_CATEGORY(lcTest, "test.metadatapackloader")

// A content pack: id + display name + an integer payload so a content
// edit (same id, new value) is observable and drives a re-register.
class FakePack : public PhosphorRegistry::IFactoryBase
{
public:
    FakePack(QString id, QString name, int value, bool isUser)
        : m_id(std::move(id))
        , m_name(std::move(name))
        , m_value(value)
        , m_isUser(isUser)
    {
    }
    [[nodiscard]] QString id() const override
    {
        return m_id;
    }
    [[nodiscard]] QString displayName() const override
    {
        return m_name;
    }
    [[nodiscard]] int value() const
    {
        return m_value;
    }
    [[nodiscard]] bool isUser() const
    {
        return m_isUser;
    }

private:
    QString m_id;
    QString m_name;
    int m_value;
    bool m_isUser;
};

// Parser: metadata.json is { "id": "...", "name": "...", "value": N }.
std::shared_ptr<FakePack> parsePack(const QString& /*subdir*/, const QJsonObject& root, bool isUser)
{
    const QString id = root.value(QStringLiteral("id")).toString();
    if (id.isEmpty()) {
        return nullptr; // decline
    }
    return std::make_shared<FakePack>(id, root.value(QStringLiteral("name")).toString(),
                                      root.value(QStringLiteral("value")).toInt(), isUser);
}

// const char* params so the call sites can pass bare literals without
// QStringLiteral noise; the conversion is confined here.
void writePack(const QString& root, const char* subdir, const char* id, const char* name, int value)
{
    const QString subdirS = QLatin1String(subdir);
    QDir().mkpath(root + QLatin1Char('/') + subdirS);
    QFile f(root + QLatin1Char('/') + subdirS + QStringLiteral("/metadata.json"));
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write(QStringLiteral("{\"id\":\"%1\",\"name\":\"%2\",\"value\":%3}")
                .arg(QLatin1String(id), QLatin1String(name))
                .arg(value)
                .toUtf8());
}
} // namespace

class TestMetadataPackLoader : public QObject
{
    Q_OBJECT

    using Loader = PhosphorRegistry::MetadataPackLoader<FakePack>;
    using Reg = PhosphorRegistry::Registry<FakePack>;

    static std::unique_ptr<Loader> makeLoader(Reg* reg)
    {
        auto loader = std::make_unique<Loader>(reg, parsePack, lcTest());
        // Content fingerprint = the value field, so a same-id edit is seen.
        loader->setSignatureContrib([](QCryptographicHash& h, const FakePack& p) {
            h.addData(QByteArray::number(p.value()));
        });
        return loader;
    }

private Q_SLOTS:

    // Initial scan registers every discovered pack into the registry.
    void testInitialScanRegisters()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        writePack(dir.path(), "alpha", "alpha", "Alpha", 1);
        writePack(dir.path(), "beta", "beta", "Beta", 2);

        Reg reg;
        auto loader = makeLoader(&reg);
        QSignalSpy added(reg.notifier(), &PhosphorRegistry::RegistryNotifier::factoryRegistered);

        loader->addSearchPaths({dir.path()}, PhosphorFsLoader::LiveReload::Off);

        QCOMPARE(reg.size(), 2);
        QVERIFY(reg.factory(QStringLiteral("alpha")) != nullptr);
        QCOMPARE(reg.factory(QStringLiteral("beta"))->value(), 2);
        QCOMPARE(added.count(), 2);
    }

    // A pack added on disk after the initial scan registers on refresh;
    // a removed pack unregisters; unchanged packs stay put (no churn).
    void testAddAndRemoveOnRefresh()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        writePack(dir.path(), "alpha", "alpha", "Alpha", 1);

        Reg reg;
        auto loader = makeLoader(&reg);
        loader->addSearchPaths({dir.path()}, PhosphorFsLoader::LiveReload::Off);
        QCOMPARE(reg.size(), 1);

        QSignalSpy added(reg.notifier(), &PhosphorRegistry::RegistryNotifier::factoryRegistered);
        QSignalSpy removed(reg.notifier(), &PhosphorRegistry::RegistryNotifier::factoryUnregistered);

        // Add beta, refresh → only beta registers (alpha untouched).
        writePack(dir.path(), "beta", "beta", "Beta", 2);
        loader->refresh();
        QCOMPARE(reg.size(), 2);
        QCOMPARE(added.count(), 1);
        QCOMPARE(removed.count(), 0);

        // Remove alpha, refresh → only alpha unregisters (beta untouched).
        QVERIFY(QDir(dir.path() + QStringLiteral("/alpha")).removeRecursively());
        loader->refresh();
        QCOMPARE(reg.size(), 1);
        QVERIFY(reg.factory(QStringLiteral("alpha")) == nullptr);
        QVERIFY(reg.factory(QStringLiteral("beta")) != nullptr);
        QCOMPARE(removed.count(), 1);
        QCOMPARE(added.count(), 1); // unchanged from the add above — beta did not re-register
    }

    // Editing a pack's content (same id, new value) re-registers it with
    // the fresh factory; a sibling pack left alone does not churn.
    void testContentEditReregisters()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        writePack(dir.path(), "alpha", "alpha", "Alpha", 1);
        writePack(dir.path(), "beta", "beta", "Beta", 2);

        Reg reg;
        auto loader = makeLoader(&reg);
        loader->addSearchPaths({dir.path()}, PhosphorFsLoader::LiveReload::Off);
        QCOMPARE(reg.factory(QStringLiteral("alpha"))->value(), 1);

        QSignalSpy added(reg.notifier(), &PhosphorRegistry::RegistryNotifier::factoryRegistered);
        QSignalSpy removed(reg.notifier(), &PhosphorRegistry::RegistryNotifier::factoryUnregistered);

        // Capture registration order before the edit so we can prove the edit
        // keeps the pack's position rather than shuffling it to the end.
        const QList<QString> orderBefore = reg.ids();

        writePack(dir.path(), "alpha", "alpha", "Alpha", 99); // same id, new value
        loader->refresh();

        QCOMPARE(reg.factory(QStringLiteral("alpha"))->value(), 99);
        QCOMPARE(removed.count(), 1); // alpha replaced
        QCOMPARE(added.count(), 1);
        QCOMPARE(reg.factory(QStringLiteral("beta"))->value(), 2); // sibling untouched
        // The content edit reconciles via Registry Replace, which keeps the
        // pack's insertion-order position — a hot-reload must not reorder the
        // catalogue. (A plain unregister+register would move alpha to the end.)
        QCOMPARE(reg.ids(), orderBefore);
    }

    // Removing several packs in ONE rescan emits factoryUnregistered in
    // registration order (reconcile walks the registry's ordered ids(), not the
    // hash-ordered fingerprint map).
    void testMultiPackRemovalEmitsInRegistrationOrder()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        writePack(dir.path(), "alpha", "alpha", "Alpha", 1);
        writePack(dir.path(), "beta", "beta", "Beta", 2);
        writePack(dir.path(), "gamma", "gamma", "Gamma", 3);

        Reg reg;
        auto loader = makeLoader(&reg);
        loader->addSearchPaths({dir.path()}, PhosphorFsLoader::LiveReload::Off);
        QCOMPARE(reg.ids(), (QList<QString>{QStringLiteral("alpha"), QStringLiteral("beta"), QStringLiteral("gamma")}));

        QSignalSpy removed(reg.notifier(), &PhosphorRegistry::RegistryNotifier::factoryUnregistered);

        // Drop alpha + gamma; refresh reconciles both removals in one pass.
        QVERIFY(QDir(dir.path() + QStringLiteral("/alpha")).removeRecursively());
        QVERIFY(QDir(dir.path() + QStringLiteral("/gamma")).removeRecursively());
        loader->refresh();

        QCOMPARE(reg.size(), 1);
        QVERIFY(reg.factory(QStringLiteral("beta")) != nullptr);
        QCOMPARE(removed.count(), 2);
        // Registration order (alpha before gamma), not QHash order.
        QCOMPARE(removed.at(0).at(0).toString(), QStringLiteral("alpha"));
        QCOMPARE(removed.at(1).at(0).toString(), QStringLiteral("gamma"));
    }

    // User-path packs win over system-path packs on id collision, and
    // carry the isUser flag.
    void testUserOverridesSystem()
    {
        QTemporaryDir sys;
        QTemporaryDir user;
        QVERIFY(sys.isValid() && user.isValid());
        writePack(sys.path(), "shared", "shared", "System", 1);
        writePack(user.path(), "shared", "shared", "User", 2);

        Reg reg;
        auto loader = makeLoader(&reg);
        loader->setUserPath(user.path());
        // [sys, user] in LowestPriorityFirst order: user (last) wins.
        loader->addSearchPaths({sys.path(), user.path()}, PhosphorFsLoader::LiveReload::Off);

        QCOMPARE(reg.size(), 1);
        const auto f = reg.factory(QStringLiteral("shared"));
        QVERIFY(f != nullptr);
        QCOMPARE(f->value(), 2);
        QVERIFY(f->isUser());
    }

    // The coarse onCommitted hook fires once per committed rescan (not on
    // a no-change refresh); a skipped subdir name is never registered.
    void testOnCommittedAndSubdirSkip()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        writePack(dir.path(), "alpha", "alpha", "Alpha", 1);
        writePack(dir.path(), "shared", "shared", "Shared", 9); // should be skipped

        Reg reg;
        auto loader = makeLoader(&reg);
        int committed = 0;
        loader->setOnCommitted([&] {
            ++committed;
        });
        loader->setPerSubdirSkip([](const QString& name) {
            return name == QLatin1String("shared");
        });

        loader->addSearchPaths({dir.path()}, PhosphorFsLoader::LiveReload::Off);
        QVERIFY(committed >= 1); // initial commit observed
        QVERIFY(reg.factory(QStringLiteral("alpha")) != nullptr);
        QVERIFY(reg.factory(QStringLiteral("shared")) == nullptr); // subdir skipped

        const int before = committed;
        loader->refresh(); // no on-disk change → no commit
        QCOMPARE(committed, before);

        writePack(dir.path(), "beta", "beta", "Beta", 2);
        loader->refresh(); // change → commit fires
        QVERIFY(committed > before);
    }

    // setPerEntryWatchPaths feeds extra per-pack files into the pack signature:
    // editing a watched sidecar (NOT the metadata.json) still changes the
    // pack's signature and fires a committed rescan.
    void testPerEntryWatchPathFeedsSignature()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        writePack(dir.path(), "alpha", "alpha", "Alpha", 1);
        const QString sidecar = dir.path() + QStringLiteral("/alpha/sidecar.txt");
        const auto writeSidecar = [&](const QByteArray& bytes) {
            QFile f(sidecar);
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write(bytes);
        };
        writeSidecar("v1");

        Reg reg;
        auto loader = makeLoader(&reg);
        int committed = 0;
        loader->setOnCommitted([&] {
            ++committed;
        });
        // Each pack watches its own sidecar (keyed by id under the temp root).
        const QString root = dir.path();
        loader->setPerEntryWatchPaths([root](const FakePack& p) -> QStringList {
            return {root + QLatin1Char('/') + p.id() + QStringLiteral("/sidecar.txt")};
        });

        loader->addSearchPaths({root}, PhosphorFsLoader::LiveReload::Off);
        QVERIFY(committed >= 1);
        QVERIFY(reg.factory(QStringLiteral("alpha")) != nullptr);
        const int before = committed;

        // Edit ONLY the watched sidecar, changing its SIZE so the size+mtime
        // signature shifts deterministically (no mtime-tick flakiness). The
        // metadata.json is untouched, so the pack is not re-registered, but the
        // watched-file change must still trigger a committed rescan.
        writeSidecar("v2-substantially-longer-payload");
        loader->refresh();
        QVERIFY(committed > before);
    }
};

QTEST_MAIN(TestMetadataPackLoader)
#include "test_phosphor_registry_metadatapack_loader.moc"
