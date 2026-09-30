#include "Exporter.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QStandardPaths>
#include <QStorageInfo>

#include "MediaInfo.h"   // formatSize：失败提示中展示可读体积

#include <algorithm>

namespace vtapp {

namespace {

/// 短边取偶数：H.264 要求宽高为偶数，奇数会直接编码失败。
int evenSide(int v) { return v > 1 ? (v & ~1) : 2; }

} // namespace

// ---------------------------------------------------------------- 预设

QList<ExportPreset> Exporter::builtinPresets() {
    // 说明：预设只描述"参数"，具体预估体积由 estimateBytes() 按源时长计算，
    // 避免把体积数字写死在这里而随参数变化失真。
    return {
        { QStringLiteral("wechat-share"), QStringLiteral("微信分享"),
          QStringLiteral("720p · 2.5 Mbps · 适合聊天发送"),
          720, 2500, 23, 128, 0, false, QStringLiteral("微信分享") },

        { QStringLiteral("wechat-hd"), QStringLiteral("微信高清"),
          QStringLiteral("1080p · 5 Mbps · 画质更好"),
          1080, 5000, 22, 128, 0, false, QStringLiteral("微信高清") },

        { QStringLiteral("moments"), QStringLiteral("朋友圈片段"),
          QStringLiteral("720p · 仅前 15 秒"),
          720, 2000, 24, 96, 15, false, QStringLiteral("朋友圈") },

        { QStringLiteral("remux"), QStringLiteral("仅换格式"),
          QStringLiteral("不重编码 · 保持原画质 · 极快"),
          0, 0, 0, 0, 0, true, QStringLiteral("兼容格式") },

        // 参数由界面传入（start 时按用户选择覆盖），此处仅提供默认值
        { QStringLiteral("custom"), QStringLiteral("自定义"),
          QStringLiteral("手动指定分辨率与码率"),
          720, 2500, 23, 128, 0, false, QStringLiteral("自定义") },
    };
}

ExportPreset Exporter::presetById(const QString& id) {
    const auto list = builtinPresets();
    for (const auto& p : list) {
        if (p.id == id) return p;
    }
    return list.first();   // 未知 id 回退到第一个预设
}

// ---------------------------------------------------------------- 纯逻辑

QString Exporter::scaleFilter(int srcW, int srcH, int shortSide) {
    if (shortSide <= 0 || srcW <= 0 || srcH <= 0) return QString();
    const bool portrait = srcH > srcW;
    const int  srcShort = portrait ? srcW : srcH;
    if (srcShort <= shortSide) return QString();   // 已经够小：不放大
    const int s = evenSide(shortSide);
    return portrait ? QStringLiteral("scale=%1:-2:flags=lanczos").arg(s)
                    : QStringLiteral("scale=-2:%1:flags=lanczos").arg(s);
}

QStringList Exporter::buildArgs(const ExportPreset& p, const QString& input,
                                const QString& output, int srcW, int srcH,
                                bool hasAudio, int threads) {
    QStringList a;
    a << QStringLiteral("-hide_banner")
      << QStringLiteral("-nostdin")
      << QStringLiteral("-y")
      << QStringLiteral("-i") << input;

    if (p.streamCopy) {
        a << QStringLiteral("-c") << QStringLiteral("copy");
    } else {
        const QString vf = scaleFilter(srcW, srcH, p.shortSide);
        if (!vf.isEmpty()) a << QStringLiteral("-vf") << vf;

        if (threads > 0) a << QStringLiteral("-threads") << QString::number(threads);

        a << QStringLiteral("-c:v")     << QStringLiteral("libx264")
          << QStringLiteral("-preset")  << QStringLiteral("veryfast")
          << QStringLiteral("-crf")     << QString::number(p.crf)
          << QStringLiteral("-maxrate") << QStringLiteral("%1k").arg(p.videoKbps)
          << QStringLiteral("-bufsize") << QStringLiteral("%1k").arg(p.videoKbps * 2);
    }

    if (p.maxSeconds > 0) a << QStringLiteral("-t") << QString::number(p.maxSeconds);

    if (!hasAudio) {
        a << QStringLiteral("-an");
    } else if (p.streamCopy) {
        a << QStringLiteral("-c:a") << QStringLiteral("copy");
    } else {
        a << QStringLiteral("-c:a") << QStringLiteral("aac")
          << QStringLiteral("-b:a") << QStringLiteral("%1k").arg(p.audioKbps);
    }

    // moov 前置：微信/网页播放器可边下边播（design 决策 D4）
    a << QStringLiteral("-movflags") << QStringLiteral("+faststart")
      << QStringLiteral("-progress")  << QStringLiteral("pipe:1")
      << QStringLiteral("-nostats")
      << output;
    return a;
}

qint64 Exporter::estimateBytes(const ExportPreset& p, double durationSec,
                               qint64 sourceBytes) {
    if (p.streamCopy) return sourceBytes;   // 体积基本不变
    double eff = durationSec;
    if (p.maxSeconds > 0) eff = std::min(eff, static_cast<double>(p.maxSeconds));
    if (eff <= 0.0) return 0;
    const double bitsPerSec = static_cast<double>(p.videoKbps + p.audioKbps) * 1000.0;
    return static_cast<qint64>(eff * bitsPerSec / 8.0);
}

bool Exporter::hasEnoughSpace(const QString& dirPath, qint64 requiredBytes) {
    if (requiredBytes <= 0) return true;
    const QStorageInfo si(dirPath);
    if (!si.isValid() || si.bytesAvailable() < 0) return true;   // 无法判断时不阻止
    // 留 20% 余量：编码过程还可能有临时写入与容器开销
    return si.bytesAvailable() >= static_cast<qint64>(requiredBytes * 1.2);
}

QString Exporter::makeOutputPath(const QString& inputPath, const QString& suffix) {
    const QFileInfo fi(inputPath);
    const QDir      dir(fi.absolutePath());
    const QString   base = fi.completeBaseName();          // 去掉扩展名
    const QString   tag  = suffix.isEmpty() ? QStringLiteral("导出") : suffix;

    QString candidate = dir.filePath(QStringLiteral("%1_%2.mp4").arg(base, tag));
    for (int n = 1; QFile::exists(candidate); ++n) {
        candidate = dir.filePath(QStringLiteral("%1_%2_%3.mp4").arg(base, tag).arg(n));
    }
    return candidate;
}

// ---------------------------------------------------------------- 探测

TranscoderInfo Exporter::probeTranscoder(const QString& configuredPath) {
    TranscoderInfo info;

    // 三级探测顺序（design 决策 D9）
    QStringList paths, sources;
    if (!configuredPath.isEmpty()) {
        paths << configuredPath;
        sources << QStringLiteral("configured");
    }
    const QString appDir = QCoreApplication::applicationDirPath();
    paths << appDir + QStringLiteral("/ffmpeg.exe")
          << appDir + QStringLiteral("/bin/ffmpeg.exe");
    sources << QStringLiteral("appdir") << QStringLiteral("appdir");

    const QString fromPath = QStandardPaths::findExecutable(QStringLiteral("ffmpeg"));
    if (!fromPath.isEmpty()) {
        paths << fromPath;
        sources << QStringLiteral("PATH");
    }

    for (int i = 0; i < paths.size(); ++i) {
        const QFileInfo fi(paths.at(i));
        if (!fi.exists() || !fi.isFile()) continue;

        // 用 -version 验证确实可执行（存在但依赖缺失的文件会在此失败）
        QProcess probe;
        probe.start(fi.absoluteFilePath(),
                    { QStringLiteral("-hide_banner"), QStringLiteral("-version") });
        if (!probe.waitForFinished(10000)) {
            // 收尾必须等进程真正退出：否则 QProcess 析构时会打印
            // "Destroyed while process is still running"（磁盘繁忙时冷启动可能超时）
            probe.kill();
            probe.waitForFinished(5000);
            continue;
        }
        if (probe.exitStatus() != QProcess::NormalExit || probe.exitCode() != 0) continue;

        const QString out = QString::fromLocal8Bit(probe.readAllStandardOutput());
        info.available = true;
        info.path      = fi.absoluteFilePath();
        info.version   = out.section(QLatin1Char('\n'), 0, 0).trimmed();
        info.source    = sources.at(i);
        break;
    }
    return info;
}

// ---------------------------------------------------------------- 执行

Exporter::Exporter(QObject* parent) : QObject(parent) {}

Exporter::~Exporter() {
    if (proc_ && proc_->state() != QProcess::NotRunning) {
        cancelRequested_ = true;
        proc_->kill();
        proc_->waitForFinished(3000);
    }
    // 兜底：若 finished 未及触发，仍要清掉半成品（应用退出不留下残缺文件）
    if (cancelRequested_) discardPartialOutput();
}

void Exporter::start(const ExportPreset& preset, const QString& input, const QString& output,
                     int srcW, int srcH, bool hasAudio, double durationSec,
                     qint64 expectedBytes) {
    if (running_) return;

    // 空间校验前置：不足时直接失败，避免跑到一半才因写入失败中断
    if (!hasEnoughSpace(QFileInfo(output).absolutePath(), expectedBytes)) {
        const QStorageInfo si(QFileInfo(output).absolutePath());
        emit failed(QStringLiteral("目标磁盘空间不足"),
                    QStringLiteral("可用 %1，预计需要约 %2。请更换输出位置或清理磁盘。")
                        .arg(QString::fromStdString(
                                 vtcore::formatSize(si.bytesAvailable())),
                             QString::fromStdString(
                                 vtcore::formatSize(expectedBytes))));
        return;
    }

    QString exe = transcoderPath_;
    if (exe.isEmpty()) {
        const TranscoderInfo tc = probeTranscoder();
        if (!tc.available) {
            emit failed(QStringLiteral("未找到转码器（ffmpeg）"),
                        QStringLiteral("请安装 ffmpeg 并加入 PATH，或在设置中指定其路径。"));
            return;
        }
        exe = tc.path;
    }
    transcoderPath_ = exe;

    outputPath_      = output;
    durationSec_     = durationSec;
    cancelRequested_ = false;
    outTimeSec_      = 0.0;
    speed_           = 0.0;
    writtenBytes_    = 0;
    errorDetail_.clear();
    errTail_.clear();
    outBuf_.clear();

    if (!proc_) {
        proc_ = std::make_unique<QProcess>(this);
        connect(proc_.get(), &QProcess::readyReadStandardOutput, this, &Exporter::onStdout);
        connect(proc_.get(), &QProcess::readyReadStandardError,  this, &Exporter::onStderr);
        connect(proc_.get(), &QProcess::finished,
                this, [this](int code, QProcess::ExitStatus) { onFinished(code); });
    }

    proc_->setProgram(exe);
    proc_->setArguments(buildArgs(preset, input, output, srcW, srcH, hasAudio,
                                  encoderThreads_));
    running_ = true;
    emit runningChanged();
    proc_->start();
}

void Exporter::cancel() {
    if (!proc_ || proc_->state() == QProcess::NotRunning) {
        // 进程尚未起来（或已结束）：只需保证不残留文件
        if (running_) { running_ = false; emit runningChanged(); emit cancelled(); }
        return;
    }
    cancelRequested_ = true;
    proc_->kill();
    // finished 会在等待期间同步触发 onFinished，由其完成清理与信号发送
    proc_->waitForFinished(3000);
}

void Exporter::discardPartialOutput() {
    if (!outputPath_.isEmpty() && QFile::exists(outputPath_)) {
        QFile::remove(outputPath_);
    }
}

void Exporter::onStdout() {
    outBuf_ += QString::fromLocal8Bit(proc_->readAllStandardOutput());

    int idx;
    while ((idx = outBuf_.indexOf(QLatin1Char('\n'))) >= 0) {
        const QString line = outBuf_.left(idx).trimmed();
        outBuf_.remove(0, idx + 1);
        if (line.isEmpty()) continue;

        const int eq = line.indexOf(QLatin1Char('='));
        if (eq <= 0) continue;
        const QString key = line.left(eq);
        const QString val = line.mid(eq + 1).trimmed();

        if (key == QLatin1String("out_time_us") || key == QLatin1String("out_time_ms")) {
            bool ok = false;
            const qint64 us = val.toLongLong(&ok);
            if (ok) outTimeSec_ = static_cast<double>(us) / 1e6;
        } else if (key == QLatin1String("speed")) {
            const QString v = val.endsWith(QLatin1Char('x')) ? val.left(val.size() - 1) : val;
            speed_ = v.toDouble();
        } else if (key == QLatin1String("total_size")) {
            writtenBytes_ = val.toLongLong();
        } else if (key == QLatin1String("progress")) {
            // 每个进度块以 progress= 结束：此刻统一上报一次
            const double ratio = (durationSec_ > 0.0)
                ? std::clamp(outTimeSec_ / durationSec_, 0.0, 1.0)
                : -1.0;
            emit progressChanged(ratio, speed_, writtenBytes_);
        }
    }
}

void Exporter::onStderr() {
    const QString chunk = QString::fromLocal8Bit(proc_->readAllStandardError());
    const auto lines = chunk.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    for (const QString& l : lines) {
        const QString t = l.trimmed();
        if (t.isEmpty()) continue;
        errTail_.append(t);
        while (errTail_.size() > 8) errTail_.removeFirst();
    }
}

void Exporter::onFinished(int exitCode) {
    const bool wasCancelled = cancelRequested_;
    running_         = false;
    cancelRequested_ = false;
    errorDetail_     = errTail_.join(QLatin1Char('\n'));

    if (wasCancelled) {
        discardPartialOutput();
        emit runningChanged();
        emit cancelled();
        return;
    }

    if (exitCode == 0) {
        emit runningChanged();
        emit finished(outputPath_);
        return;
    }

    // 失败也不能留下半成品
    discardPartialOutput();
    emit runningChanged();
    emit failed(QStringLiteral("导出失败（转码器返回 %1）").arg(exitCode), errorDetail_);
}

} // namespace vtapp
