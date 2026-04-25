// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "encoder.h"

#include <QFileInfo>
#include <QProcess>
#include <QStandardPaths>
#include <QStringList>

#include <iostream>

namespace PlasmaZones::ShaderRender {
namespace {

/// Writes each frame to <stem>_NNNN.png in the directory of outPath.
class PngSequenceSink : public FrameSink
{
public:
    PngSequenceSink(const QString& outPath)
        : m_stem(QFileInfo(outPath).absoluteFilePath().chopped(4))  // drop ".png"
    {
    }

    bool writeFrame(const QImage& frame) override
    {
        const QString name = QStringLiteral("%1_%2.png")
            .arg(m_stem)
            .arg(m_index, 4, 10, QLatin1Char('0'));
        if (!frame.save(name, "PNG")) {
            std::cerr << "PngSequenceSink: failed to save " << name.toStdString() << "\n";
            return false;
        }
        ++m_index;
        return true;
    }

    bool finalize() override { return true; }

private:
    QString m_stem;
    int m_index = 1;
};

/// Pipes raw RGBA frames into an ffmpeg subprocess.  Uses VP9 for
/// .webm output and H.264 for .mp4; both keep the file under ~500KB
/// for a 5-second 480x270 clip.
class FfmpegPipeSink : public FrameSink
{
public:
    FfmpegPipeSink(const QString& outPath, const QSize& size, int fps)
        : m_outPath(outPath), m_size(size), m_fps(fps)
    {
    }

    ~FfmpegPipeSink() override
    {
        if (m_proc.state() != QProcess::NotRunning) {
            m_proc.closeWriteChannel();
            m_proc.waitForFinished(5000);
        }
    }

    bool start()
    {
        const QString ffmpeg = QStandardPaths::findExecutable(QStringLiteral("ffmpeg"));
        if (ffmpeg.isEmpty()) {
            std::cerr << "FfmpegPipeSink: ffmpeg not on PATH\n";
            return false;
        }
        const QString ext = QFileInfo(m_outPath).suffix().toLower();

        QStringList args = {
            QStringLiteral("-y"),
            QStringLiteral("-f"), QStringLiteral("rawvideo"),
            QStringLiteral("-pix_fmt"), QStringLiteral("rgba"),
            QStringLiteral("-s"), QStringLiteral("%1x%2").arg(m_size.width()).arg(m_size.height()),
            QStringLiteral("-r"), QString::number(m_fps),
            QStringLiteral("-i"), QStringLiteral("pipe:0"),
            QStringLiteral("-an"),
            QStringLiteral("-pix_fmt"), QStringLiteral("yuv420p"),
        };
        if (ext == QLatin1String("webm")) {
            // CRF picked to keep grain at bay on the noisy/high-frequency
            // shaders without bloating the simpler ones — VP9 with
            // -b:v 0 makes CRF the sole quality target.  Tile/row
            // multithreading speeds up encoding without affecting bits.
            args << QStringLiteral("-c:v") << QStringLiteral("libvpx-vp9")
                 << QStringLiteral("-b:v") << QStringLiteral("0")
                 << QStringLiteral("-crf") << QStringLiteral("28")
                 << QStringLiteral("-row-mt") << QStringLiteral("1")
                 << QStringLiteral("-deadline") << QStringLiteral("good");
        } else if (ext == QLatin1String("mp4")) {
            args << QStringLiteral("-c:v") << QStringLiteral("libx264")
                 << QStringLiteral("-crf") << QStringLiteral("23")
                 << QStringLiteral("-preset") << QStringLiteral("medium");
        } else {
            std::cerr << "FfmpegPipeSink: unsupported extension ." << ext.toStdString() << "\n";
            return false;
        }
        args << m_outPath;

        m_proc.setProcessChannelMode(QProcess::ForwardedErrorChannel);
        m_proc.start(ffmpeg, args);
        return m_proc.waitForStarted(5000);
    }

    bool writeFrame(const QImage& frame) override
    {
        QImage rgba = frame;
        if (rgba.format() != QImage::Format_RGBA8888) {
            rgba = rgba.convertToFormat(QImage::Format_RGBA8888);
        }
        if (rgba.size() != m_size) {
            rgba = rgba.scaled(m_size, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
        }
        const qsizetype expected = static_cast<qsizetype>(m_size.width()) * m_size.height() * 4;
        // QImage rows can be padded; copy line-by-line to a flat buffer.
        QByteArray buf;
        buf.reserve(expected);
        for (int y = 0; y < rgba.height(); ++y) {
            buf.append(reinterpret_cast<const char*>(rgba.constScanLine(y)),
                       rgba.width() * 4);
        }
        const qint64 written = m_proc.write(buf);
        if (written != buf.size()) {
            std::cerr << "FfmpegPipeSink: short write (" << written << " of "
                      << buf.size() << ")\n";
            return false;
        }
        // Don't let the pipe back up: wait for ffmpeg to drain when
        // its buffer fills.  ffmpeg reading slower than we render
        // means we block briefly here, which is fine.
        m_proc.waitForBytesWritten(5000);
        return true;
    }

    bool finalize() override
    {
        m_proc.closeWriteChannel();
        if (!m_proc.waitForFinished(30000)) {
            std::cerr << "FfmpegPipeSink: ffmpeg didn't exit cleanly\n";
            m_proc.kill();
            return false;
        }
        return m_proc.exitCode() == 0;
    }

private:
    QString  m_outPath;
    QSize    m_size;
    int      m_fps;
    QProcess m_proc;
};

} // namespace

FrameSink* makeFrameSink(const QString& outPath, const QSize& size, int fps)
{
    const QString ext = QFileInfo(outPath).suffix().toLower();
    if (ext == QLatin1String("png")) {
        return new PngSequenceSink(outPath);
    }
    if (ext == QLatin1String("webm") || ext == QLatin1String("mp4")) {
        auto* sink = new FfmpegPipeSink(outPath, size, fps);
        if (!sink->start()) {
            delete sink;
            return nullptr;
        }
        return sink;
    }
    std::cerr << "makeFrameSink: unknown output extension ." << ext.toStdString() << "\n";
    return nullptr;
}

} // namespace PlasmaZones::ShaderRender
