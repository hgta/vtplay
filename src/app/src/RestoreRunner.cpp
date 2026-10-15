#include "RestoreRunner.h"

#include "Exporter.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QStandardPaths>
#include <QStorageInfo>

#include <algorithm>

namespace vtapp {

namespace {

/// 临时目录前缀。启动清扫按此前缀识别"自己人"，避免误删别人的临时文件。
const char* kTempPrefix = "vtplay-restore-";

QString makeTempDirPath() {
    const QString base = QStandardPaths::writableLocation(QStandardPaths::TempLocation);
    return QDir(base.isEmpty() ? QDir::tempPath() : base)
        .filePath(QStringLiteral("%1%2").arg(QLatin1String(kTempPrefix))
                      .arg(QCoreApplication::applicationPid()));
}

} // namespace

struct RestoreRunner::Impl {
    std::unique_ptr<QProcess> proc;
    QString     transcoder;

    // 本次导出的输入
    RestorePlan plan;
    QString     input, output;
    int         srcW = 0, srcH = 0;
    bool        hasAudio = true;
    double      durationSec = 0.0;
    TimeRange   range;
    bool        needsTemp = false;
    QString     tempDir;

    // 运行态
    bool    running = false;
    bool    dryRun = false;
    bool    cancelRequested = false;
    bool    detectDone = false;
    int     stageIndex = 0;
    int     encoderThreads = 0;
    QList<double> weights;

    // 进度解析
    QString outBuf;
    double  outTimeSec = 0.0;
    double  speed = 0.0;
    qint64  writtenBytes = 0;

    // 失败诊断
    QStringList errTail;
    QString     errorDetail;
    QString     expectedLabel;
};

RestoreRunner::RestoreRunner(QObject* parent) : QObject(parent), d_(new Impl) {}

void RestoreRunner::setTranscoderPath(const QString& path) { d_->transcoder = path; }
QString RestoreRunner::transcoderPath() const { return d_->transcoder; }
void RestoreRunner::setEncoderThreads(int n) { d_->encoderThreads = n; }
bool RestoreRunner::isRunning() const { return d_->running; }
void RestoreRunner::setDryRun(bool dry) { d_->dryRun = dry; }
QString RestoreRunner::lastErrorDetail() const { return d_->errorDetail; }
int RestoreRunner::currentStageIndex() const { return d_->stageIndex; }

RestoreRunner::~RestoreRunner() {
    if (d_->proc && d_->proc->state() != QProcess::NotRunning) {
        d_->cancelRequested = true;
        d_->proc->kill();
        d_->proc->waitForFinished(3000);   // 必须等真正退出，否则析构时告警
    }
    cleanupTempDir();
}

void RestoreRunner::cleanupTempDir() {
    if (!d_->tempDir.isEmpty() && QFileInfo(d_->tempDir).isDir()) {
        QDir(d_->tempDir).removeRecursively();
    }
    d_->tempDir.clear();
}

void RestoreRunner::sweepStaleTempDirs(int olderThanHours) {
    const QString base = QStandardPaths::writableLocation(QStandardPaths::TempLocation);
    QDir dir(base.isEmpty() ? QDir::tempPath() : base);
    const QDateTime cutoff = QDateTime::currentDateTime().addSecs(
        -static_cast<qint64>(olderThanHours) * 3600);
    const QFileInfoList list = dir.entryInfoList(
        { QStringLiteral("%1*").arg(QLatin1String(kTempPrefix)) },
        QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QFileInfo& fi : list) {
        // 按时间阈值判断而不是"全删"：可能有另一个实例正在导出，
        // 它的临时目录同样匹配前缀。
        if (fi.lastModified() < cutoff) {
            QDir(fi.absoluteFilePath()).removeRecursively();
        }
    }
}

void RestoreRunner::failWith(const QString& message, const QString& detail) {
    d_->running = false;
    d_->errorDetail = detail;
    emit runningChanged();
    cleanupTempDir();
    emit failed(message, detail);
}

void RestoreRunner::start(const RestorePlan& plan, const QString& input, const QString& output,
                          int srcW, int srcH, bool hasAudio, double durationSec,
                          const TimeRange& range, qint64 expectedBytes) {
    if (d_->running) {
        // 只有**真的还有子进程在跑**才算忙；否则 running 只是个残留标志。
        //
        // 实测踩过：预览之后 running 未复位，于是后续导出在这里被静默丢掉——
        // 用户点了导出，什么也没发生，也没有任何错误提示。
        // 这是最难排查的一类失败：日志干净、界面正常、就是不动。
        const bool alive = d_->proc && d_->proc->state() != QProcess::NotRunning;
        if (alive) {
            failWith(QStringLiteral("已有导出在进行中"),
                     QStringLiteral("请等它结束，或先取消当前导出。"));
            return;
        }
        d_->running = false;      // 复位残留标志，让本次导出能正常开始
        emit runningChanged();
    }

    if (!plan.valid) {
        failWith(QStringLiteral("阶段清单非法，拒绝执行"), plan.invalidReason);
        return;
    }
    if (!QFileInfo::exists(transcoderPath())) {
        failWith(QStringLiteral("未找到转码器（ffmpeg）"),
                 QStringLiteral("请在设置中指定 ffmpeg 路径。"));
        return;
    }
    const QString rangeErr = validateTimeRange(range, durationSec);
    if (!rangeErr.isEmpty()) {
        failWith(QStringLiteral("导出时间段不合法"), rangeErr);
        return;
    }

    d_->plan        = plan;
    d_->input       = input;
    d_->output      = output;
    d_->srcW        = srcW;
    d_->srcH        = srcH;
    d_->hasAudio    = hasAudio;
    d_->durationSec = durationSec;
    d_->range       = range;
    d_->cancelRequested = false;
    d_->detectDone  = false;
    d_->stageIndex  = 0;
    d_->outTimeSec  = 0.0;
    d_->speed       = 0.0;
    d_->writtenBytes = 0;
    d_->outBuf.clear();
    d_->errTail.clear();
    d_->errorDetail.clear();
    d_->weights = initialStageWeights(plan);

    for (const Stage& s : plan.stages) {
        if (s.needsTempFiles) { d_->needsTemp = true; break; }
    }

    // ---- 空间校验（含临时产物）----
    // 放在开始前：写到一半才发现空间不足，代价是用户白等一场。
    const QString outDir = QFileInfo(output).absolutePath();
    if (!Exporter::hasEnoughSpace(outDir, expectedBytes)) {
        const QStorageInfo si(outDir);
        failWith(QStringLiteral("目标磁盘空间不足"),
                 QStringLiteral("可用 %1，预计需要 %2。")
                     .arg(si.bytesAvailable() / (1024 * 1024))
                     .arg(expectedBytes / (1024 * 1024)));
        return;
    }
    if (d_->needsTemp) {
        d_->tempDir = makeTempDirPath();
        if (!QDir().mkpath(d_->tempDir)) {
            failWith(QStringLiteral("无法创建临时目录"), d_->tempDir);
            return;
        }
        // 帧数估算：时长 × 30（保守取 30fps；只用于空间估算）
        const double secs = d_->range.length > 0 ? d_->range.length : d_->durationSec;
        const int frames = secs > 0 ? static_cast<int>(secs * 30.0) : 0;
        const qint64 tempBytes = estimateTempBytes(plan, frames);
        if (tempBytes > 0) {
            const QStorageInfo si(d_->tempDir);
            if (si.isValid() && si.bytesAvailable() >= 0
                && si.bytesAvailable() < tempBytes) {
                failWith(QStringLiteral("临时目录空间不足"),
                         QStringLiteral("需要约 %1 MB，可用 %2 MB。")
                             .arg(tempBytes / (1024 * 1024))
                             .arg(si.bytesAvailable() / (1024 * 1024)));
                return;
            }
        }
    }

    if (!d_->proc) {
        d_->proc = std::make_unique<QProcess>(this);
        connect(d_->proc.get(), &QProcess::readyReadStandardOutput, this, &RestoreRunner::onStdout);
        connect(d_->proc.get(), &QProcess::readyReadStandardError, this, [this]() {
            const QString chunk = QString::fromLocal8Bit(d_->proc->readAllStandardError());
            for (const QString& l : chunk.split(QLatin1Char('\n'), Qt::SkipEmptyParts)) {
                const QString t = l.trimmed();
                if (t.isEmpty()) continue;
                d_->errTail.append(t);
                while (d_->errTail.size() > 8) d_->errTail.removeFirst();
            }
        });
        connect(d_->proc.get(), &QProcess::finished, this,
                [this](int code, QProcess::ExitStatus) { onProcessFinished(code); });
    }

    d_->running = true;
    emit runningChanged();

    if (d_->needsTemp) runDetectPass();
    else               runMainPass();
}

void RestoreRunner::runDetectPass() {
    // 稳定需要先探测出变换序列。探测同样带预热段，否则片段开头的运动估计不可靠。
    const Stage* stab = nullptr;
    for (const Stage& s : d_->plan.stages) {
        if (s.kind == StageKind::Stabilize) { stab = &s; break; }
    }
    if (!stab) { runMainPass(); return; }

    d_->stageIndex = 0;

    const double pre = prerollSeconds(d_->plan);
    const double start = std::max(0.0, d_->range.start - pre);
    const double want  = (d_->range.full() ? 0.0 : d_->range.length) + pre + 1.0;

    QStringList a;
    a << QStringLiteral("-hide_banner") << QStringLiteral("-nostdin") << QStringLiteral("-y");
    if (start > 0.0) a << QStringLiteral("-ss") << QString::number(start, 'f', 3);
    if (want > 0.0)  a << QStringLiteral("-t")  << QString::number(want, 'f', 3);
    a << QStringLiteral("-i") << d_->input
      << QStringLiteral("-an") << QStringLiteral("-sn")
      // 相对路径 + 工作目录：绝对路径里的 `D:` 冒号会被滤镜语法解析器炸掉
      << QStringLiteral("-vf")
      << QStringLiteral("vidstabdetect=result=transforms.trf:shakiness=5")
      << QStringLiteral("-f") << QStringLiteral("null") << QStringLiteral("-");

    emit commandBuilt(QStringLiteral("detect"), a.join(QLatin1Char(' ')));
    if (d_->dryRun) { runMainPass(); return; }   // 只报告命令

    d_->proc->setWorkingDirectory(d_->tempDir);
    d_->proc->setProgram(transcoderPath());
    d_->proc->setArguments(a);
    d_->proc->start();
}

QString RestoreRunner::buildMainChain() const {
    QString chain = Exporter::buildFilterChain(d_->plan, d_->srcW, d_->srcH);

    // 预热 + 裁剪的正确顺序：**裁剪必须排在整条链之后**。
    // 若把 trim 放到链前面，时域滤波（去隔行/降噪/稳定）就看不到预热帧，
    // 预热也就白做了——而"预热"正是为了让这些滤波器在开头已经收敛。
    const double pre = prerollSeconds(d_->plan);
    if (!d_->range.full() && pre >= 0.0) {
        QString trim = QStringLiteral("trim=start=%1").arg(pre, 0, 'f', 3);
        if (d_->range.length > 0.0) {
            trim += QStringLiteral(":end=%1").arg(pre + d_->range.length, 0, 'f', 3);
        }
        // 用**帧序号**重建时间戳，而不是 PTS-STARTPTS。
        //
        // 踩过的坑：`-ss` 放在 `-i` 之前做输入定位时，裁剪出的帧虽然数量正确，
        // 但它们的原始 PTS 仍横跨整段（实测 120 帧的 PTS 从 4.5 铺到 19.67），
        // 而 `setpts=PTS-STARTPTS` 没有把它们归零 —— 结果是内容 2 秒、
        // 容器却声明 15 秒，播放器把它当成 15 秒的视频 ✗
        // （只查 Duration 会误判成"裁剪没生效"；必须同时看帧数才能发现真相）
        //
        // `setpts=N/FRAME_RATE/TB` 直接由帧序号与帧率算出 PTS，
        // 完全不依赖输入时间戳，因此对输入定位的偏移免疫。
        chain += (chain.isEmpty() ? QString() : QStringLiteral(","))
                 + trim + QStringLiteral(",setpts=N/FRAME_RATE/TB");
    }
    return chain;
}

void RestoreRunner::runMainPass() {
    d_->stageIndex = d_->needsTemp ? 1 : 0;

    const double pre = prerollSeconds(d_->plan);
    const double start = d_->range.full() ? 0.0 : std::max(0.0, d_->range.start - pre);

    // 编码参数沿用组 3 的构造（不设 -maxrate、10bit、tune grain），
    // 这里只替换 -i / -vf 与加时间段的定位。
    QStringList a = Exporter::buildRestoreArgs(d_->plan, d_->input, d_->output,
                                               d_->srcW, d_->srcH, d_->hasAudio,
                                               d_->encoderThreads);
    if (a.isEmpty()) {   // 非法清单（start 里已拦，这里是双保险）
        failWith(QStringLiteral("无法构造转码参数"), d_->plan.invalidReason);
        return;
    }

    const QString chain = buildMainChain();
    // 音频必须与视频**对称**地应用同一个时间范围。
    //
    // 踩过的坑（排查花了多轮）：`-vf` 只作用于视频流，音频**原样穿过**。
    // 于是产物是「2 秒的视频 + 15 秒的音频」，容器 Duration 取两者最大值 → 15.17 秒 ✗
    // 而**帧数是对的**（视频确实被裁了），所以只看帧数完全看不出来。
    // `-ss` 放在 `-i` 之前对**所有流**生效，因此两边用同一个预热偏移是对的。
    QString audioChain;
    if (!d_->range.full()) {
        const double pre = prerollSeconds(d_->plan);
        QString at = QStringLiteral("atrim=start=%1").arg(pre, 0, 'f', 3);
        if (d_->range.length > 0.0) {
            at += QStringLiteral(":end=%1").arg(pre + d_->range.length, 0, 'f', 3);
        }
        // asetpts=N/SR/TB 与视频侧的 setpts=N/FRAME_RATE/TB 一一对应：
        // 都按序号重建时间戳，避免"一边归零一边不归零"造成两流错位
        audioChain = at + QStringLiteral(",asetpts=N/SR/TB");
    }

    // 在 -i 之前插入 -ss（快定位）；-vf 的值替换为带裁剪的链
    const int iIdx = a.indexOf(QStringLiteral("-i"));
    if (start > 0.0 && iIdx >= 0) {
        a.insert(iIdx, QString::number(start, 'f', 3));
        a.insert(iIdx, QStringLiteral("-ss"));
    }
    const int vIdx = a.indexOf(QStringLiteral("-vf"));
    if (!chain.isEmpty()) {
        if (vIdx >= 0) a[vIdx + 1] = chain;
        else {                       // 原本没有 -vf（只有编码）时补上，插在 -i 之后
            const int after = a.indexOf(QStringLiteral("-i")) + 2;
            a.insert(after, chain);
            a.insert(after, QStringLiteral("-vf"));
        }
    }

    // 音频裁剪链插在视频之后（两者都在 -i 之后、输出之前）
    if (!audioChain.isEmpty()) {
        const int vIdx2 = a.indexOf(QStringLiteral("-vf"));
        int at = (vIdx2 >= 0) ? vIdx2 + 2 : a.indexOf(QStringLiteral("-i")) + 2;
        a.insert(at, audioChain);
        a.insert(at, QStringLiteral("-af"));
    }

    emit commandBuilt(QStringLiteral("main"), a.join(QLatin1Char(' ')));
    if (d_->dryRun) {   // 只报告命令，不执行
        d_->running = false;
        emit runningChanged();
        cleanupTempDir();
        return;
    }

    // 稳定阶段用相对文件名读取 transforms.trf，所以工作目录必须是临时目录
    d_->proc->setWorkingDirectory(d_->needsTemp ? d_->tempDir : QDir::tempPath());
    d_->proc->setProgram(transcoderPath());
    d_->proc->setArguments(a);
    d_->proc->start();
}

void RestoreRunner::onStdout() {
    d_->outBuf += QString::fromLocal8Bit(d_->proc->readAllStandardOutput());
    int idx;
    while ((idx = d_->outBuf.indexOf(QLatin1Char('\n'))) >= 0) {
        const QString line = d_->outBuf.left(idx).trimmed();
        d_->outBuf.remove(0, idx + 1);
        const int eq = line.indexOf(QLatin1Char('='));
        if (eq <= 0) continue;
        const QString key = line.left(eq);
        const QString val = line.mid(eq + 1).trimmed();
        if (key == QLatin1String("out_time_us") || key == QLatin1String("out_time_ms")) {
            bool ok = false;
            const qint64 us = val.toLongLong(&ok);
            if (ok) d_->outTimeSec = static_cast<double>(us) / 1e6;
        } else if (key == QLatin1String("speed")) {
            const QString v = val.endsWith(QLatin1Char('x')) ? val.left(val.size() - 1) : val;
            d_->speed = v.toDouble();
        } else if (key == QLatin1String("total_size")) {
            d_->writtenBytes = val.toLongLong();
        } else if (key == QLatin1String("progress")) {
            // 期望输出时长：指定了时间段就是该时长，否则是整片时长
            const double want = (!d_->range.full() && d_->range.length > 0.0)
                                    ? d_->range.length : d_->durationSec;
            const double ratio = want > 0.0
                                     ? std::clamp(d_->outTimeSec / want, 0.0, 1.0) : -1.0;
            // 探测遍没有输出画面，进度按"已耗时"无法精确，用 -1 表示不定
            // si 是**遍数**索引（0=探测遍，1=主遍），不是阶段索引——两者在多阶段方案里
            // 并不对应。这里不求精确对应，只求"给一个不误导人的阶段名"：
            // 夹进合法范围后取该阶段名，**宁可近似也不返回空串**。
            // 返回空串的代价不小：界面那行完全不显示，用户就失去了唯一的过程信息
            // （修复动辄几十分钟，看不出"卡住了"还是"在跑慢阶段"）。
            const int size = int(d_->plan.stages.size());
            const int si = size > 0 ? std::clamp(d_->stageIndex, 0, size - 1) : -1;
            const QString name = si >= 0 ? stageKindName(d_->plan.stages[si].kind) : QString();
            emit progressChanged(d_->detectDone ? ratio : -1.0, d_->speed,
                                 d_->writtenBytes, si, name);
        }
    }
}

void RestoreRunner::onProcessFinished(int exitCode) {
    const bool wasCancelled = d_->cancelRequested;
    if (wasCancelled) {
        d_->running = false;
        d_->cancelRequested = false;
        QFile::remove(d_->output);          // 半成品必须删掉
        emit runningChanged();
        cleanupTempDir();
        emit cancelled();
        return;
    }

    d_->proc->readAllStandardError();       // 收尾（errTail 已在信号里累积）
    d_->errorDetail = d_->errTail.join(QLatin1Char('\n'));

    if (exitCode != 0) {
        QFile::remove(d_->output);
        failWith(QStringLiteral("修复导出失败（转码器返回 %1）").arg(exitCode),
                 d_->errorDetail);
        return;
    }

    if (d_->needsTemp && !d_->detectDone) {
        d_->detectDone = true;
        runMainPass();
        return;
    }

    d_->running = false;
    emit runningChanged();
    cleanupTempDir();
    emit finished(d_->output);
}

void RestoreRunner::cancel() {
    d_->cancelRequested = true;
    if (d_->proc && d_->proc->state() != QProcess::NotRunning) {
        d_->proc->kill();
        d_->proc->waitForFinished(3000);    // 由 onProcessFinished 完成清理
        return;
    }
    if (d_->running) {
        d_->running = false;
        emit runningChanged();
        cleanupTempDir();
        emit cancelled();
    }
}

QString RestoreRunner::renderPreview(const RestorePlan& plan, const QString& input, double atSec,
                                     int srcW, int srcH, const QString& outPng) {
    const RestorePlan pv = previewPlan(plan);   // 跳过稳定（两遍全片，单帧做不了）
    if (!pv.valid) return QStringLiteral("阶段清单非法：%1").arg(pv.invalidReason);
    if (!QFileInfo::exists(transcoderPath())) return QStringLiteral("未找到转码器");

    const QString chain = Exporter::buildFilterChain(pv, srcW, srcH);

    QStringList a;
    a << QStringLiteral("-hide_banner") << QStringLiteral("-nostdin") << QStringLiteral("-y")
      << QStringLiteral("-ss") << QString::number(atSec, 'f', 3)
      << QStringLiteral("-i") << input;
    if (!chain.isEmpty()) a << QStringLiteral("-vf") << chain;
    a << QStringLiteral("-frames:v") << QStringLiteral("1")
      << QStringLiteral("-an") << QStringLiteral("-sn")
      // 单张图片输出必须加 -update 1，否则 ffmpeg 要求文件名带 %03d 序列模式
      << QStringLiteral("-update") << QStringLiteral("1")
      << outPng;

    QProcess p;
    p.setWorkingDirectory(QDir::tempPath());
    p.start(transcoderPath(), a);
    if (!p.waitForStarted(5000)) return QStringLiteral("无法启动转码器");
    if (!p.waitForFinished(60000)) {
        p.kill();
        p.waitForFinished(3000);
        return QStringLiteral("预览超时");
    }
    if (p.exitStatus() != QProcess::NormalExit || p.exitCode() != 0) {
        return QStringLiteral("预览渲染失败：%1")
            .arg(QString::fromLocal8Bit(p.readAllStandardError()).right(400));
    }
    if (QFileInfo::exists(outPng)) return QString();

    // 退出码为 0 但没产出文件，最常见的原因是 `-ss` 落在片尾之后：
    // 此时 ffmpeg 会明确说 "Output file is empty, nothing was encoded"。
    // 而上面只在 exitCode != 0 时才读 stderr——恰恰把唯一能解释原因的那句话丢掉了，
    // 界面只剩一句"未生成文件"，对判断毫无帮助（实测踩过）。
    const QString err = QString::fromLocal8Bit(p.readAllStandardError());
    if (err.contains(QStringLiteral("nothing was encoded"))) {
        return QStringLiteral("该时间点没有画面（可能已超出片长）");
    }
    return QStringLiteral("预览未生成文件");
}

} // namespace vtapp
