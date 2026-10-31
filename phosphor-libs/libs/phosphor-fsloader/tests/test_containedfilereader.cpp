// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Coverage for readFileWithinDirectory, the descriptor-checked reader that
// sits beside resolveWithinDirectory. Each refusal row names the property that
// would be lost without it: containment (`..`, a symlink out, an absolute path
// a pack declared), a regular file only (FIFO, directory), and the size cap.
// The FIFO row is the one that matters most to a GUI caller: without
// O_NONBLOCK the open would wait for a writer forever and the test would hang.

#include <PhosphorFsLoader/ContainedFileReader.h>

#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>

#include <memory>

#include <sys/stat.h>

using PhosphorFsLoader::AbsolutePathPolicy;
using PhosphorFsLoader::readFileWithinDirectory;

class TestContainedFileReader : public QObject
{
    Q_OBJECT

private:
    /// The directory reads are contained in.
    std::unique_ptr<QTemporaryDir> m_pack;
    /// A sibling directory an escape points AT, holding a readable file, so
    /// an accepted escape would be a real one rather than a failed open.
    std::unique_ptr<QTemporaryDir> m_outside;

    [[nodiscard]] static bool write(const QString& path, const QByteArray& content)
    {
        QFile f(path);
        return f.open(QIODevice::WriteOnly | QIODevice::Truncate) && f.write(content) == content.size();
    }

    [[nodiscard]] QString packPath(const QString& relative) const
    {
        return QDir::cleanPath(m_pack->filePath(relative));
    }

    [[nodiscard]] QString secretPath() const
    {
        return QDir::cleanPath(m_outside->filePath(QStringLiteral("secret.txt")));
    }

private Q_SLOTS:

    void init()
    {
        m_pack = std::make_unique<QTemporaryDir>();
        m_outside = std::make_unique<QTemporaryDir>();
        QVERIFY(m_pack->isValid());
        QVERIFY(m_outside->isValid());
        QVERIFY(write(secretPath(), "outside"));
    }

    void cleanup()
    {
        m_pack.reset();
        m_outside.reset();
    }

    // ─── Accepted ─────────────────────────────────────────────────────────

    /// A file inside the directory, at the top level and in a subdirectory.
    /// The returned path is the cleaned absolute one.
    void readsAFileInsideTheDirectory()
    {
        QVERIFY(write(packPath(QStringLiteral("mesh.obj")), "v 0 0 0\n"));
        QVERIFY(QDir(m_pack->path()).mkpath(QStringLiteral("assets")));
        QVERIFY(write(packPath(QStringLiteral("assets/tex.png")), "png"));

        QString error;
        const auto top = readFileWithinDirectory(QStringLiteral("mesh.obj"), m_pack->path(), AbsolutePathPolicy::Reject,
                                                 1024, &error);
        QVERIFY2(top.has_value(), qPrintable(error));
        QCOMPARE(top->bytes, QByteArrayLiteral("v 0 0 0\n"));
        QCOMPARE(top->path, packPath(QStringLiteral("mesh.obj")));

        const auto nested = readFileWithinDirectory(QStringLiteral("./assets//tex.png"), m_pack->path(),
                                                    AbsolutePathPolicy::Reject, 1024, &error);
        QVERIFY2(nested.has_value(), qPrintable(error));
        QCOMPARE(nested->bytes, QByteArrayLiteral("png"));
        QCOMPARE(nested->path, packPath(QStringLiteral("assets/tex.png")));
    }

    /// A symlink whose target stays inside the directory is allowed, and the
    /// returned path is the link's own spelling.
    void allowsASymlinkThatStaysInside()
    {
        QVERIFY(write(packPath(QStringLiteral("real.glsl")), "shared"));
        QVERIFY(QFile::link(packPath(QStringLiteral("real.glsl")), packPath(QStringLiteral("alias.glsl"))));

        QString error;
        const auto read = readFileWithinDirectory(QStringLiteral("alias.glsl"), m_pack->path(),
                                                  AbsolutePathPolicy::Reject, 1024, &error);
        QVERIFY2(read.has_value(), qPrintable(error));
        QCOMPARE(read->bytes, QByteArrayLiteral("shared"));
        QCOMPARE(read->path, packPath(QStringLiteral("alias.glsl")));
    }

    /// A file of exactly the limit is accepted, and an empty file reads empty.
    void acceptsAFileOfExactlyTheLimit()
    {
        QVERIFY(write(packPath(QStringLiteral("ten.bin")), QByteArray(10, 'x')));
        QVERIFY(write(packPath(QStringLiteral("empty.bin")), QByteArray()));

        const auto ten =
            readFileWithinDirectory(QStringLiteral("ten.bin"), m_pack->path(), AbsolutePathPolicy::Reject, 10);
        QVERIFY(ten.has_value());
        QCOMPARE(ten->bytes.size(), 10);

        const auto empty =
            readFileWithinDirectory(QStringLiteral("empty.bin"), m_pack->path(), AbsolutePathPolicy::Reject, 10);
        QVERIFY(empty.has_value());
        QVERIFY(empty->bytes.isEmpty());
    }

    // ─── Containment ──────────────────────────────────────────────────────

    /// `..` out of the directory is refused, under either policy: the policy
    /// only governs ABSOLUTE paths.
    void refusesADotDotEscape()
    {
        const QString escape =
            QStringLiteral("../") + QDir(m_outside->path()).dirName() + QStringLiteral("/secret.txt");
        for (const auto policy : {AbsolutePathPolicy::Reject, AbsolutePathPolicy::Trust}) {
            QString error;
            QVERIFY(!readFileWithinDirectory(escape, m_pack->path(), policy, 1024, &error).has_value());
            QVERIFY(error.contains(QStringLiteral("outside")));
        }
        // A `..` that comes back inside is fine.
        QVERIFY(write(packPath(QStringLiteral("a.txt")), "a"));
        QVERIFY(QDir(m_pack->path()).mkpath(QStringLiteral("sub")));
        QVERIFY(readFileWithinDirectory(QStringLiteral("sub/../a.txt"), m_pack->path(), AbsolutePathPolicy::Reject, 16)
                    .has_value());
    }

    /// A symlink out of the directory is refused, whether it is the leaf or
    /// an intermediate directory component.
    void refusesASymlinkToOutside()
    {
        QVERIFY(QFile::link(secretPath(), packPath(QStringLiteral("leak.txt"))));
        QVERIFY(QFile::link(m_outside->path(), packPath(QStringLiteral("leakdir"))));

        QString error;
        QVERIFY(!readFileWithinDirectory(QStringLiteral("leak.txt"), m_pack->path(), AbsolutePathPolicy::Reject, 1024,
                                         &error)
                     .has_value());
        QVERIFY(error.contains(QStringLiteral("outside")));
        QVERIFY(!readFileWithinDirectory(QStringLiteral("leakdir/secret.txt"), m_pack->path(),
                                         AbsolutePathPolicy::Reject, 1024)
                     .has_value());
    }

    /// An absolute path under Reject is held to containment: inside reads,
    /// outside is refused.
    void absolutePath_rejectPolicyRequiresContainment()
    {
        QVERIFY(write(packPath(QStringLiteral("inside.txt")), "in"));

        const auto inside = readFileWithinDirectory(packPath(QStringLiteral("inside.txt")), m_pack->path(),
                                                    AbsolutePathPolicy::Reject, 1024);
        QVERIFY(inside.has_value());
        QCOMPARE(inside->bytes, QByteArrayLiteral("in"));

        QString error;
        QVERIFY(!readFileWithinDirectory(secretPath(), m_pack->path(), AbsolutePathPolicy::Reject, 1024, &error)
                     .has_value());
        QVERIFY(error.contains(QStringLiteral("outside")));
    }

    /// An absolute path under Trust skips containment, but not the other
    /// checks: it must still be a regular file within the limit.
    void absolutePath_trustPolicySkipsContainmentOnly()
    {
        QString error;
        const auto trusted = readFileWithinDirectory(m_outside->path() + QStringLiteral("//secret.txt"), m_pack->path(),
                                                     AbsolutePathPolicy::Trust, 1024, &error);
        QVERIFY2(trusted.has_value(), qPrintable(error));
        QCOMPARE(trusted->bytes, QByteArrayLiteral("outside"));
        QCOMPARE(trusted->path, secretPath());

        QVERIFY(!readFileWithinDirectory(secretPath(), m_pack->path(), AbsolutePathPolicy::Trust, 3).has_value());
        QVERIFY(
            !readFileWithinDirectory(m_outside->path(), m_pack->path(), AbsolutePathPolicy::Trust, 1024).has_value());
    }

    // ─── File kind and size ───────────────────────────────────────────────

    /// A FIFO is refused at once instead of blocking the caller in open()
    /// waiting for a writer.
    void refusesAFifoWithoutBlocking()
    {
        const QString fifo = packPath(QStringLiteral("pipe"));
        QCOMPARE(::mkfifo(QFile::encodeName(fifo).constData(), 0600), 0);

        QElapsedTimer timer;
        timer.start();
        QString error;
        QVERIFY(
            !readFileWithinDirectory(QStringLiteral("pipe"), m_pack->path(), AbsolutePathPolicy::Reject, 1024, &error)
                 .has_value());
        QVERIFY(timer.elapsed() < 1000);
        QVERIFY(error.contains(QStringLiteral("not a regular file")));
    }

    /// A directory inside the directory is not a file.
    void refusesADirectory()
    {
        QVERIFY(QDir(m_pack->path()).mkpath(QStringLiteral("sub")));
        QString error;
        QVERIFY(
            !readFileWithinDirectory(QStringLiteral("sub"), m_pack->path(), AbsolutePathPolicy::Reject, 1024, &error)
                 .has_value());
        QVERIFY(error.contains(QStringLiteral("not a regular file")));
    }

    /// One byte over the limit is refused.
    void refusesAnOversizedFile()
    {
        QVERIFY(write(packPath(QStringLiteral("big.bin")), QByteArray(11, 'x')));
        QString error;
        QVERIFY(
            !readFileWithinDirectory(QStringLiteral("big.bin"), m_pack->path(), AbsolutePathPolicy::Reject, 10, &error)
                 .has_value());
        QVERIFY(error.contains(QStringLiteral("larger than 10 bytes")));
    }

    // ─── Malformed input ──────────────────────────────────────────────────

    /// Empty, NUL-carrying and over-long declared paths are refused before
    /// the filesystem is touched, as is a missing file.
    void refusesMalformedOrMissingPaths()
    {
        const auto refused = [this](const QString& declared) {
            QString error;
            const bool ok =
                readFileWithinDirectory(declared, m_pack->path(), AbsolutePathPolicy::Reject, 1024, &error).has_value();
            return !ok && !error.isEmpty();
        };
        QVERIFY(refused(QString()));
        QVERIFY(refused(QStringLiteral("a") + QChar::Null + QStringLiteral("b")));
        QVERIFY(refused(QString(PhosphorFsLoader::kMaxDeclaredPathLength + 1, QLatin1Char('a'))));
        QVERIFY(refused(QStringLiteral("absent.txt")));
        // The error out-parameter is optional.
        QVERIFY(!readFileWithinDirectory(QStringLiteral("absent.txt"), m_pack->path(), AbsolutePathPolicy::Reject, 1024)
                     .has_value());
    }
};

QTEST_MAIN(TestContainedFileReader)
#include "test_containedfilereader.moc"
