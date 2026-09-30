// vtplay-export-check
// headless 自测：验证导出预设、命令行构造、预估体积、进度解析与取消路径（无 UI）。
// 用法：
//   export_check --list                     列出内置预设
//   export_check <input> [presetId] [--dry] 打印参数与完整命令（--dry 不真正导出）
//   export_check <input> <presetId> --cancel-after <sec>   验证取消后无残留

#include "Exporter.h"
#include "MediaInfo.h"
#include "MediaPipeline.h"

#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <QTimer>

#include <cstdio>

using namespace vtapp;

namespace {

QString humanSize(qint64 bytes) {
    return QString::fromStdString(vtcore::formatSize(bytes));
}

void listPresets() {
    std::printf("%-14s %-12s %8s %10s %6s %9s %8s %6s\n",
                "id", "label", "shortSide", "videoKbps", "crf", "audioKbps",
                "maxSec", "copy");
    for (const auto& p : Exporter::builtinPresets()) {
        std::printf("%-14s %-12s %8d %10d %6d %9d %8d %6d\n",
                    qPrintable(p.id), qPrintable(p.label), p.shortSide,
                    p.videoKbps, p.crf, p.audioKbps, p.maxSeconds,
                    static_cast<int>(p.streamCopy));
    }
}

} // namespace

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    const QStringList args = app.arguments();

    if (args.size() < 2) {
        std::fprintf(stderr,
            "usage: export_check <input> [presetId] [--dry]\n"
            "       export_check --list\n");
        return 2;
    }
    if (args.at(1) == QLatin1String("--list")) {
        listPresets();
        return 0;
    }

    const QString input    = args.at(1);
    const QString presetId = (args.size() > 2 && !args.at(2).startsWith(QLatin1String("--")))
                             ? args.at(2) : QStringLiteral("wechat-share");
    const bool dry = args.contains(QStringLiteral("--dry"));

    int cancelAfter = 0;
    const int ci = args.indexOf(QStringLiteral("--cancel-after"));
    if (ci >= 0 && ci + 1 < args.size()) cancelAfter = args.at(ci + 1).toInt();

    // 测试挂钩：覆盖"磁盘空间不足"分支（用一个不可能满足的预估值）
    qint64 forceExpect = -1;
    const int ei = args.indexOf(QStringLiteral("--expect-bytes"));
    if (ei >= 0 && ei + 1 < args.size()) forceExpect = args.at(ei + 1).toLongLong();

    // ---- 源信息：复用核心管线解析，避免在工具里重写探针 ----
    vtcore::MediaPipeline pipeline;
    try {
        pipeline.open(input.toStdString());
    } catch (const std::exception& e) {
        std::fprintf(stderr, "open failed: %s\n", e.what());
        return 1;
    }
    const vtcore::MediaInfo info = pipeline.info();
    pipeline.close();

    // ---- 转码器探测 ----
    const TranscoderInfo tc = Exporter::probeTranscoder();
    std::printf("transcoder : %s\n", tc.available ? qPrintable(tc.path) : "(未找到)");
    if (tc.available) {
        std::printf("version    : %s\n", qPrintable(tc.version));
        std::printf("source     : %s\n", qPrintable(tc.source));
    }

    std::printf("input      : %s  %.2fs  %dx%d  %s\n",
                info.fileName.c_str(), info.durationSec,
                info.videoWidth, info.videoHeight,
                vtcore::formatSpecSummary(info).c_str());

    // ---- 预设与命令行构造 ----
    const ExportPreset preset = Exporter::presetById(presetId);
    std::printf("preset     : %s (%s) — %s\n",
                qPrintable(preset.id), qPrintable(preset.label),
                qPrintable(preset.detail));

    const QString vf = Exporter::scaleFilter(info.videoWidth, info.videoHeight,
                                             preset.shortSide);
    std::printf("scale      : %s\n", vf.isEmpty() ? "(不缩放)" : qPrintable(vf));

    const QString out = Exporter::makeOutputPath(input, preset.suffix);
    std::printf("output     : %s\n", qPrintable(out));

    const qint64 est = Exporter::estimateBytes(preset, info.durationSec, info.fileSizeBytes);
    std::printf("estimate   : %s\n", qPrintable(humanSize(est)));

    const QStringList cmdArgs = Exporter::buildArgs(preset, input, out,
                                                    info.videoWidth, info.videoHeight,
                                                    info.hasAudio);
    std::printf("command    : ffmpeg %s\n", qPrintable(cmdArgs.join(QLatin1Char(' '))));

    if (dry) return 0;
    if (!tc.available) {
        std::fprintf(stderr, "错误：未找到 ffmpeg，无法执行导出\n");
        return 1;
    }

    // 清理同名残留，确保 --dry 与真实运行的输出路径一致
    if (QFile::exists(out)) QFile::remove(out);

    Exporter ex;
    ex.setTranscoderPath(tc.path);

    int rc = 0;
    QObject::connect(&ex, &Exporter::progressChanged,
                     [](double ratio, double speed, qint64 bytes) {
        if (ratio < 0.0) std::printf("\r导出中 速度 %.2fx  %s      ",
                                     speed, qPrintable(humanSize(bytes)));
        else             std::printf("\r导出中 %.1f%%  速度 %.2fx  %s      ",
                                     ratio * 100.0, speed, qPrintable(humanSize(bytes)));
        std::fflush(stdout);
    });
    QObject::connect(&ex, &Exporter::finished, [&rc](const QString& p) {
        std::printf("\n完成：%s\n", qPrintable(p));
        rc = 0;
        QCoreApplication::quit();
    });
    QObject::connect(&ex, &Exporter::failed, [&rc](const QString& m, const QString& d) {
        std::fprintf(stderr, "\n失败：%s\n%s\n", qPrintable(m), qPrintable(d));
        rc = 1;
        QCoreApplication::quit();
    });
    QObject::connect(&ex, &Exporter::cancelled, [&rc]() {
        std::fprintf(stderr, "\n已取消\n");
        rc = 3;
        QCoreApplication::quit();
    });

    if (cancelAfter > 0) {
        QTimer::singleShot(cancelAfter * 1000, &ex, [&ex, &out]() {
            std::printf("\n[测试] 触发取消…\n");
            ex.cancel();
            // 取消后必须无残留文件
            std::printf("残留文件：%s\n", QFile::exists(out) ? "存在（不符合预期）" : "无（符合预期）");
        });
    }

    ex.start(preset, input, out, info.videoWidth, info.videoHeight,
             info.hasAudio, info.durationSec,
             forceExpect >= 0 ? forceExpect : est);

    // start() 可能**同步**失败（空间不足/无转码器）并已发射 failed；
    // 此时若仍进入 exec()，事件循环里已无任何待处理事件会永久挂起。
    if (ex.isRunning()) {
        app.exec();
    }

    if (rc == 0 && QFile::exists(out)) {
        const qint64 actual = QFileInfo(out).size();
        std::printf("实际体积   : %s (%lld 字节)\n",
                    qPrintable(humanSize(actual)), static_cast<long long>(actual));
        if (est > 0) {
            std::printf("预估偏差   : %+.1f%%\n",
                        (static_cast<double>(actual) - static_cast<double>(est))
                        / static_cast<double>(est) * 100.0);
        }
    }
    return rc;
}
