// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Direct coverage for `resolveWithinDirectory`, the shared containment guard
// four pack parsers in three other libraries depend on.
//
// It had none when it landed, and the case that slipped through was the one a
// table like this catches on the first run: a symlink out of the directory whose
// LEAF does not exist yet. Canonicalisation returns empty for a missing leaf, so
// the original implementation fell back to a lexical compare, which cannot see a
// symlinked intermediate component — and a not-yet-existing leaf is the
// live-reload case (the watcher arms on the path, the file appears later), not
// an edge case.

#include <PhosphorFsLoader/PackPathGuard.h>

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>

using PhosphorFsLoader::AbsolutePathPolicy;
using PhosphorFsLoader::resolveWithinDirectory;

class TestPackPathGuard : public QObject
{
    Q_OBJECT

private:
    /// The directory a pack is scanned from.
    std::unique_ptr<QTemporaryDir> m_pack;
    /// Somewhere for an escape to point AT, so a passing escape is provably an
    /// escape rather than a path that happens not to resolve.
    std::unique_ptr<QTemporaryDir> m_outside;

    [[nodiscard]] bool touch(const QString& path) const
    {
        QFile f(path);
        return f.open(QIODevice::WriteOnly | QIODevice::Truncate) && f.write(QByteArrayLiteral("x")) == 1;
    }

private Q_SLOTS:

    void init()
    {
        m_pack.reset(new QTemporaryDir);
        m_outside.reset(new QTemporaryDir);
        QVERIFY(m_pack->isValid());
        QVERIFY(m_outside->isValid());
    }

    // ─── Accepted ─────────────────────────────────────────────────────────

    /// A plain name in the directory.
    void acceptsAPlainNameInTheDirectory()
    {
        QVERIFY(touch(m_pack->filePath(QStringLiteral("effect.frag"))));
        const auto resolved =
            resolveWithinDirectory(QStringLiteral("effect.frag"), m_pack->path(), AbsolutePathPolicy::Reject);
        QVERIFY(resolved.has_value());
        QCOMPARE(*resolved, QDir::cleanPath(m_pack->filePath(QStringLiteral("effect.frag"))));
    }

    /// A SUBDIRECTORY inside the pack. This is the row that stops an
    /// over-tightening "fix" that just refuses path separators.
    void acceptsASubdirectoryInsideTheDirectory()
    {
        QVERIFY(QDir(m_pack->path()).mkpath(QStringLiteral("shaders")));
        QVERIFY(touch(m_pack->filePath(QStringLiteral("shaders/effect.frag"))));
        const auto resolved =
            resolveWithinDirectory(QStringLiteral("shaders/effect.frag"), m_pack->path(), AbsolutePathPolicy::Reject);
        QVERIFY(resolved.has_value());
        QVERIFY(resolved->endsWith(QStringLiteral("shaders/effect.frag")));
    }

    /// A file that does not exist yet is ACCEPTED when it is contained.
    /// Existence is the caller's diagnostic, not this guard's — and refusing it
    /// would break live reload, where the watcher arms before the file lands.
    void acceptsAContainedPathThatDoesNotExistYet()
    {
        const auto resolved =
            resolveWithinDirectory(QStringLiteral("future.frag"), m_pack->path(), AbsolutePathPolicy::Reject);
        QVERIFY(resolved.has_value());
    }

    /// The returned path is CLEANED, so two spellings of one file cannot reach
    /// watch keys, content signatures or path-keyed caches as distinct strings.
    void returnsACleanedPath_data()
    {
        QTest::addColumn<QString>("declared");
        QTest::newRow("dot prefix") << QStringLiteral("./effect.frag");
        QTest::newRow("double slash") << QStringLiteral("shaders//effect.frag");
        QTest::newRow("dot segment") << QStringLiteral("shaders/./effect.frag");
    }

    void returnsACleanedPath()
    {
        QFETCH(QString, declared);
        QVERIFY(QDir(m_pack->path()).mkpath(QStringLiteral("shaders")));
        const auto resolved = resolveWithinDirectory(declared, m_pack->path(), AbsolutePathPolicy::Reject);
        QVERIFY(resolved.has_value());
        QVERIFY2(!resolved->contains(QStringLiteral("//")) && !resolved->contains(QStringLiteral("/./")),
                 qPrintable(*resolved));
    }

    // ─── Refused ──────────────────────────────────────────────────────────

    void refusesAnAbsolutePathUnderReject()
    {
        const QString outside = m_outside->filePath(QStringLiteral("evil.frag"));
        QVERIFY(touch(outside));
        QVERIFY(!resolveWithinDirectory(outside, m_pack->path(), AbsolutePathPolicy::Reject).has_value());
    }

    void refusesParentTraversal_data()
    {
        QTest::addColumn<QString>("declared");
        QTest::newRow("leading") << QStringLiteral("../evil.frag");
        QTest::newRow("deep") << QStringLiteral("../../../evil.frag");
        // Lexically it starts inside the pack. Only a resolved comparison
        // refuses it, which is why a leading-`..` test is not enough.
        QTest::newRow("through a subdir") << QStringLiteral("shaders/../../evil.frag");
    }

    void refusesParentTraversal()
    {
        QFETCH(QString, declared);
        QVERIFY(!resolveWithinDirectory(declared, m_pack->path(), AbsolutePathPolicy::Reject).has_value());
    }

    /// The directory ITSELF is refused: a directory is not a file any consumer
    /// of this guard can use.
    void refusesTheDirectoryItself_data()
    {
        QTest::addColumn<QString>("declared");
        QTest::newRow("dot") << QStringLiteral(".");
        QTest::newRow("subdir then up") << QStringLiteral("shaders/..");
    }

    void refusesTheDirectoryItself()
    {
        QFETCH(QString, declared);
        QVERIFY(QDir(m_pack->path()).mkpath(QStringLiteral("shaders")));
        QVERIFY(!resolveWithinDirectory(declared, m_pack->path(), AbsolutePathPolicy::Reject).has_value());
    }

    /// A symlinked FILE inside the pack pointing outside it. Lexically
    /// contained, canonically not.
    void refusesASymlinkedFilePointingOutside()
    {
        const QString outside = m_outside->filePath(QStringLiteral("evil.frag"));
        QVERIFY(touch(outside));
        QVERIFY(QFile::link(outside, m_pack->filePath(QStringLiteral("effect.frag"))));
        QVERIFY(!resolveWithinDirectory(QStringLiteral("effect.frag"), m_pack->path(), AbsolutePathPolicy::Reject)
                     .has_value());
    }

    /// A symlinked DIRECTORY inside the pack, with an EXISTING leaf beyond it.
    void refusesASymlinkedDirectoryWithAnExistingLeaf()
    {
        QVERIFY(touch(m_outside->filePath(QStringLiteral("evil.frag"))));
        QVERIFY(QFile::link(m_outside->path(), m_pack->filePath(QStringLiteral("esc"))));
        QVERIFY(!resolveWithinDirectory(QStringLiteral("esc/evil.frag"), m_pack->path(), AbsolutePathPolicy::Reject)
                     .has_value());
    }

    /// THE ONE THAT FAILED OPEN. A symlinked directory out of the pack, with a
    /// leaf that does not exist YET.
    ///
    /// `QFileInfo::canonicalFilePath()` returns empty for a missing leaf, so an
    /// implementation that falls back to a purely lexical compare here accepts
    /// the path — and this is the live-reload shape, so the accepted path is
    /// stored, watched, and compiled when the file materialises OUTSIDE the pack.
    /// Reducing the guard to lexical-only makes this slot, and only this slot,
    /// fail.
    void refusesASymlinkedDirectoryWithAMissingLeaf_data()
    {
        QTest::addColumn<QString>("declared");
        // One missing component. The `cdUp()` walk handled this by luck: cd("..")
        // from `pack/esc/future.frag` lands on `pack/esc`, which DOES exist.
        QTest::newRow("missing leaf") << QStringLiteral("esc/future.frag");
        // TWO missing components — the shape that actually failed OPEN. cd("..")
        // from `pack/esc/sub/future.frag` targets `pack/esc/sub`, which does NOT
        // exist, so `QDir::cd` returned false, the walk bailed out to a purely
        // lexical path, and that lexical path was then compared against a
        // CANONICAL root. Verified accepted-in-error before the fix.
        QTest::newRow("missing intermediate") << QStringLiteral("esc/sub/future.frag");
        QTest::newRow("deep missing intermediate") << QStringLiteral("esc/a/b/c/future.frag");
    }

    void refusesASymlinkedDirectoryWithAMissingLeaf()
    {
        QFETCH(QString, declared);
        QVERIFY(QFile::link(m_outside->path(), m_pack->filePath(QStringLiteral("esc"))));
        QVERIFY2(!resolveWithinDirectory(declared, m_pack->path(), AbsolutePathPolicy::Reject).has_value(),
                 qPrintable(QStringLiteral("a symlink out of the directory was accepted: ") + declared));
    }

    /// The climb must not be fooled by a missing intermediate WITHOUT a symlink
    /// either: a deep not-yet-existing path inside the pack stays ACCEPTED, so
    /// the fix above cannot have been a blanket "refuse anything deep".
    void acceptsADeepContainedPathThatDoesNotExistYet()
    {
        const auto resolved =
            resolveWithinDirectory(QStringLiteral("a/b/c/future.frag"), m_pack->path(), AbsolutePathPolicy::Reject);
        QVERIFY2(resolved.has_value(), "a contained path with several missing components was refused");
        QVERIFY(resolved->endsWith(QStringLiteral("a/b/c/future.frag")));
    }

    void refusesAnEmptyDeclaredPathOrDirectory()
    {
        QVERIFY(!resolveWithinDirectory(QString(), m_pack->path(), AbsolutePathPolicy::Reject).has_value());
        QVERIFY(!resolveWithinDirectory(QStringLiteral("effect.frag"), QString(), AbsolutePathPolicy::Reject)
                     .has_value());
    }

    // ─── Policy ───────────────────────────────────────────────────────────

    /// `Trust` is for a RUNTIME override the user picked, where a path outside
    /// any pack is the point. It must return the absolute path unchanged — and
    /// it must NOT weaken the relative case.
    void trustAcceptsAnAbsolutePathButStillRefusesRelativeEscapes()
    {
        const QString outside = m_outside->filePath(QStringLiteral("chosen.png"));
        QVERIFY(touch(outside));

        const auto trusted = resolveWithinDirectory(outside, m_pack->path(), AbsolutePathPolicy::Trust);
        QVERIFY(trusted.has_value());
        QCOMPARE(*trusted, outside);

        // Trusted, but still CLEANED, like every other accepted return — two
        // spellings of one trusted override must not reach watch keys and
        // path-keyed caches as distinct strings.
        const QString messy = m_outside->path() + QStringLiteral("/./chosen.png");
        const auto cleaned = resolveWithinDirectory(messy, m_pack->path(), AbsolutePathPolicy::Trust);
        QVERIFY(cleaned.has_value());
        QCOMPARE(*cleaned, outside);

        // The two policies must differ on exactly this input and nothing else.
        QVERIFY(!resolveWithinDirectory(outside, m_pack->path(), AbsolutePathPolicy::Reject).has_value());
        QVERIFY(!resolveWithinDirectory(QStringLiteral("../chosen.png"), m_pack->path(), AbsolutePathPolicy::Trust)
                     .has_value());
    }

    /// A root that cannot be canonicalised (it does not exist) falls back to a
    /// lexical comparison on BOTH sides rather than mixing domains — which would
    /// accept everything.
    void aNonExistentDirectoryStillRefusesTraversal()
    {
        const QString missing = m_pack->filePath(QStringLiteral("not-created-yet"));
        QVERIFY(!resolveWithinDirectory(QStringLiteral("../../evil.frag"), missing, AbsolutePathPolicy::Reject)
                     .has_value());
        QVERIFY(resolveWithinDirectory(QStringLiteral("effect.frag"), missing, AbsolutePathPolicy::Reject)
                    .has_value());
    }
};

QTEST_MAIN(TestPackPathGuard)
#include "test_packpathguard.moc"
