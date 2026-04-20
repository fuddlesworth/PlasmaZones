// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "plasmazones_export.h"
#include <QLatin1String>
#include <QRect>
#include <QString>
#include <QVector>

namespace Phosphor::Screens {
class ScreenManager;
}

namespace PhosphorZones {
class LayoutRegistry;
}

namespace PlasmaZones {

class AutotileEngine;

/**
 * @brief Generates a redacted support report for bug reports and discussions
 *
 * Collects config, screen topology, autotile state, layout info, and recent
 * journal logs into a Markdown-formatted report suitable for pasting into
 * GitHub Discussions or Issues.
 *
 * Privacy:
 * - Home directory paths replaced with ~
 * - No network calls — report is generated locally
 */
class PLASMAZONES_EXPORT SupportReport
{
public:
    /**
     * @brief Thread-safe snapshot of QObject state for async report generation
     *
     * Collected on the main thread; the report is then assembled off-thread
     * so that blocking I/O (file reads, journalctl) does not stall the event loop.
     */
    struct Snapshot
    {
        struct ScreenInfo
        {
            QString name;
            QRect geometry;
            QRect available;
            qreal refreshRate = 0;
            qreal devicePixelRatio = 1;
        };
        struct LayoutInfo
        {
            QString name;
            QString id;
            int zoneCount = 0;
            bool isActive = false;
        };

        QVector<ScreenInfo> screens;
        bool hasScreenManager = false;

        QVector<LayoutInfo> layouts;
        bool hasLayoutManager = false;

        bool autotileEnabled = false;
        QStringList autotileScreens;
        bool hasAutotileEngine = false;
    };

    /**
     * @brief Collect a thread-safe snapshot from QObject pointers (main thread only)
     */
    static Snapshot collectSnapshot(Phosphor::Screens::ScreenManager* screenManager,
                                    PhosphorZones::LayoutRegistry* layoutManager, AutotileEngine* autotileEngine);

    /**
     * @brief Generate a report from a pre-collected snapshot (thread-safe)
     *
     * Safe to call from any thread — only accesses plain data, files, and QProcess.
     */
    static QString generateFromSnapshot(const Snapshot& snapshot, int sinceMinutes = 30);

    /**
     * @brief Generate a complete support report (convenience, blocks calling thread)
     *
     * @warning This performs blocking I/O (file reads + journalctl subprocess). Do not
     * call from the main/GUI thread. Use collectSnapshot() + generateFromSnapshot()
     * with QtConcurrent::run instead (see ControlAdaptor for the async pattern).
     *
     * @note Config and session data are read from disk, not from in-memory Settings,
     * so the report reflects the persisted state (which is what matters for diagnostics).
     *
     * @param screenManager Phosphor::Screens::ScreenManager instance (nullable)
     * @param layoutManager PhosphorZones::LayoutRegistry instance (nullable)
     * @param autotileEngine AutotileEngine instance (nullable)
     * @param sinceMinutes How many minutes of journal logs to include (default 30, capped at 120)
     * @return Markdown-formatted support report
     */
    static QString generate(Phosphor::Screens::ScreenManager* screenManager,
                            PhosphorZones::LayoutRegistry* layoutManager, AutotileEngine* autotileEngine,
                            int sinceMinutes = 30);

    /**
     * @brief Redact home directory paths from a string
     * @param input String that may contain the user's home path
     * @return String with home path replaced by ~
     */
    static QString redactHomePath(const QString& input);

    /**
     * @brief Read a file, redact home paths, and wrap in a Markdown code fence
     * @param path File path to read
     * @param label Human-readable label for error messages
     * @param lang Code fence language tag (default: "json")
     * @return Markdown code block with redacted content, or error message if unreadable
     */
    static QString readAndRedactFile(const QString& path, const QString& label,
                                     const QString& lang = QLatin1String("json"));

private:
    SupportReport() = delete;

    static QString sectionVersion();
    static QString sectionEnvironment();
    static QString sectionScreens(const Snapshot& snapshot);
    static QString sectionConfig();
    static QString sectionLayouts(const Snapshot& snapshot);
    static QString sectionAutotile(const Snapshot& snapshot);
    static QString sectionSession();
    static QString sectionLogs(int sinceMinutes);
};

} // namespace PlasmaZones
