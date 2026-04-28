// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "encoder.h"

#include <QFileInfo>
#include <QLoggingCategory>
#include <QProcess>
#include <QStandardPaths>
#include <QStringList>

namespace PlasmaZones::ShaderRender {

Q_LOGGING_CATEGORY(lcEncoder, "plasmazones.shader-render.encoder")

namespace {

/// Width of the zero-padded frame index in the PNG sequence filename. Six
/// digits cover up to a million frames (~9 hours at 30 fps), more than any
/// preview will ever need, and shell-glob alphabetical sort matches numeric.
constexpr int kPngIndexWidth = 6;

/// Resize @p frame to @p target with high-quality bilinear, no-op if already
/// matching. Centralised so both sinks honour the renderer's --output-size in
/// the same way.
QImage scaleToOutput(QImage frame, const QSize& target)
{
    if (frame.size() == target)
        return frame;
    return frame.scaled(target, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
}

/// Writes each frame to <stem>_NNNNNN.png in the directory of outPath.
class PngSequenceSink : public FrameSink
{
public:
    PngSequenceSink(const QString& outPath, const QSize& size)
        : m_size(size)
    {
        // Use completeBaseName/absoluteDir rather than chopping a fixed
        // suffix length: works regardless of extension length and survives
        // case differences (foo.PNG, foo.png).
        const QFileInfo fi(outPath);
        m_stem = fi.absoluteDir().filePath(fi.completeBaseName());
    }

    bool writeFrame(const QImage& frame) override
    {
        const QString name = QStringLiteral("%1_%2.png").arg(m_stem).arg(m_index, kPngIndexWidth, 10, QLatin1Char('0'));
        const QImage scaled = scaleToOutput(frame, m_size);
        if (!scaled.save(name, "PNG")) {
            qCWarning(lcEncoder) << "PngSequenceSink: failed to save" << name;
            return false;
        }
        ++m_index;
        return true;
    }

    bool finalize() override
    {
        return true;
    }

private:
    QString m_stem;
    QSize m_size;
    int m_index = 1;
};

/// Pipes raw RGBA frames into an ffmpeg subprocess. Codec choice is driven by
/// the resolved OutputFormat — this class does not re-inspect the path
/// extension.
class FfmpegPipeSink : public FrameSink
{
public:
    FfmpegPipeSink(const QString& outPath, OutputFormat fmt, const QSize& size, int fps)
        : m_outPath(outPath)
        , m_format(fmt)
        , m_size(size)
        , m_fps(fps)
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
            qCWarning(lcEncoder) << "FfmpegPipeSink: ffmpeg not on PATH";
            return false;
        }

        QStringList args = {
            QStringLiteral("-y"),
            QStringLiteral("-f"),
            QStringLiteral("rawvideo"),
            QStringLiteral("-pix_fmt"),
            QStringLiteral("rgba"),
            QStringLiteral("-s"),
            QStringLiteral("%1x%2").arg(m_size.width()).arg(m_size.height()),
            QStringLiteral("-r"),
            QString::number(m_fps),
            QStringLiteral("-i"),
            QStringLiteral("pipe:0"),
            QStringLiteral("-an"),
            QStringLiteral("-pix_fmt"),
            QStringLiteral("yuv420p"),
        };
        switch (m_format) {
        case OutputFormat::Webm:
            // CRF picked to keep grain at bay on the noisy/high-frequency
            // shaders without bloating the simpler ones — VP9 with -b:v 0
            // makes CRF the sole quality target. Tile/row multithreading
            // speeds up encoding without affecting bits.
            args << QStringLiteral("-c:v") << QStringLiteral("libvpx-vp9") << QStringLiteral("-b:v")
                 << QStringLiteral("0") << QStringLiteral("-crf") << QStringLiteral("28") << QStringLiteral("-row-mt")
                 << QStringLiteral("1") << QStringLiteral("-deadline") << QStringLiteral("good");
            break;
        case OutputFormat::Mp4:
            args << QStringLiteral("-c:v") << QStringLiteral("libx264") << QStringLiteral("-crf")
                 << QStringLiteral("23") << QStringLiteral("-preset") << QStringLiteral("medium");
            break;
        case OutputFormat::PngSequence:
        case OutputFormat::Unknown:
            qCWarning(lcEncoder) << "FfmpegPipeSink: unsupported format for ffmpeg pipe";
            return false;
        }
        args << m_outPath;

        m_proc.setProcessChannelMode(QProcess::ForwardedErrorChannel);
        m_proc.start(ffmpeg, args);
        return m_proc.waitForStarted(5000);
    }

    bool writeFrame(const QImage& frame) override
    {
        QImage rgba = scaleToOutput(frame, m_size);
        if (rgba.format() != QImage::Format_RGBA8888) {
            rgba = rgba.convertToFormat(QImage::Format_RGBA8888);
        }
        const qsizetype rowBytes = static_cast<qsizetype>(rgba.width()) * 4;
        const qsizetype expected = rowBytes * rgba.height();

        // Fast-path: if the QImage is tightly packed (no row padding), pipe
        // the whole buffer in one go. The padded path copies row-by-row to a
        // contiguous scratch buffer.
        qint64 written = 0;
        if (rgba.bytesPerLine() == rowBytes) {
            written = m_proc.write(reinterpret_cast<const char*>(rgba.constBits()), expected);
        } else {
            QByteArray buf;
            buf.reserve(expected);
            for (int y = 0; y < rgba.height(); ++y) {
                buf.append(reinterpret_cast<const char*>(rgba.constScanLine(y)), rowBytes);
            }
            written = m_proc.write(buf);
            if (written == buf.size()) {
                return true;
            }
        }
        if (written != expected) {
            qCWarning(lcEncoder) << "FfmpegPipeSink: short write (" << written << "of" << expected << ")";
            return false;
        }
        return true;
    }

    bool finalize() override
    {
        m_proc.closeWriteChannel();
        if (!m_proc.waitForFinished(30000)) {
            qCWarning(lcEncoder) << "FfmpegPipeSink: ffmpeg didn't exit cleanly";
            m_proc.kill();
            return false;
        }
        return m_proc.exitCode() == 0;
    }

private:
    QString m_outPath;
    OutputFormat m_format;
    QSize m_size;
    int m_fps;
    QProcess m_proc;
};

} // namespace

OutputFormat formatFromExtension(const QString& path)
{
    const QString ext = QFileInfo(path).suffix().toLower();
    if (ext == QLatin1String("png"))
        return OutputFormat::PngSequence;
    if (ext == QLatin1String("webm"))
        return OutputFormat::Webm;
    if (ext == QLatin1String("mp4"))
        return OutputFormat::Mp4;
    return OutputFormat::Unknown;
}

std::unique_ptr<FrameSink> makeFrameSink(const QString& outPath, const QSize& size, int fps)
{
    const OutputFormat fmt = formatFromExtension(outPath);
    switch (fmt) {
    case OutputFormat::PngSequence:
        return std::make_unique<PngSequenceSink>(outPath, size);
    case OutputFormat::Webm:
    case OutputFormat::Mp4: {
        auto sink = std::make_unique<FfmpegPipeSink>(outPath, fmt, size, fps);
        if (!sink->start()) {
            return nullptr;
        }
        return sink;
    }
    case OutputFormat::Unknown:
        qCWarning(lcEncoder) << "makeFrameSink: unknown output extension for" << outPath;
        return nullptr;
    }
    return nullptr;
}

} // namespace PlasmaZones::ShaderRender
