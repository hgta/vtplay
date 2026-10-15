#include "FilmDiagnosis.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QProcess>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QTimer>

#include <algorithm>
#include <cmath>
#include <vector>

namespace vtapp {

namespace {

// ---------------------------------------------------------------- 阈值
//
// 全部分级阈值集中在此，便于按实际素材标定。当前取值来自对随包 ffmpeg 的
// 实测观察（不是文档推断），仍需用真实老片素材做一轮标定后才能定稿。

/// 交错判定：交错帧占比达到此值即认为源是交错的。
/// 取 0.30 而非更低，是因为 3:2 pulldown 的源也会产生约 20% 的"梳状帧"，
/// 若阈值过低会把 pulldown 误判成纯交错（两者的正确处理方式不同）。
constexpr double kInterlacedRatio = 0.30;

/// 重复场占比达到此值认为疑似 3:2 pulldown。
constexpr double kPulldownRatio = 0.15;

/// 高频细节量（gblur 差分残差均值）分级阈值。
/// 实测参照：一段 11.6MB 的 H.264 竖屏素材（已较重压缩）约 4.3。
constexpr double kDetailMild    = 2.0;
constexpr double kDetailObvious = 6.0;

/// 抖动分级阈值，单位为「像素/帧²」（全局位移二阶差分的均方根）。
/// 平滑摇镜的二阶差分接近 0，因此这个指标衡量的是位移的高频分量。
constexpr double kShakeMild    = 0.5;
constexpr double kShakeObvious = 2.0;

/// 黑边判定：抵消掉的边长占比低于此值不算黑边。
/// cropdetect 在干净画面上也会给出几像素的"建议"（实测 1080 宽给了 1072），
/// 不设这个门槛会把正常画面误判成有黑边。
constexpr double kBarsMinRatio = 0.02;

/// vidstabdetect 结果里参与估计全局位移的最小匹配对比度。
/// 低对比度的块容易匹配到错误位置，会把中位数带偏。
constexpr double kTrfMinContrast = 0.30;

/// 单步诊断的超时（毫秒）。超时即该项降级为「未知」，不中断整个诊断。
constexpr int kStepTimeoutMs = 120000;

QString makeTempDirPath() {
    const QString base = QStandardPaths::writableLocation(QStandardPaths::TempLocation);
    return QDir(base.isEmpty() ? QDir::tempPath() : base)
        .filePath(QStringLiteral("vtplay-diag-%1")
                      .arg(QCoreApplication::applicationPid()));
}

} // namespace

// ---------------------------------------------------------------- 名称

QString diagLevelName(DiagLevel v) {
    switch (v) {
        case DiagLevel::None:    return QStringLiteral("无");
        case DiagLevel::Mild:    return QStringLiteral("轻度");
        case DiagLevel::Obvious: return QStringLiteral("明显");
        case DiagLevel::Unknown: break;
    }
    return QStringLiteral("未知");
}

QString scanTypeName(ScanType t) {
    switch (t) {
        case ScanType::Progressive:         return QStringLiteral("逐行");
        case ScanType::InterlacedTff:       return QStringLiteral("交错（上场优先）");
        case ScanType::InterlacedBff:       return QStringLiteral("交错（下场优先）");
        case ScanType::Pulldown32:          return QStringLiteral("3:2 pulldown");
        case ScanType::Unknown:             break;
    }
    return QStringLiteral("未知");
}

bool DiagnosisResult::anyUsable() const {
    return scan != ScanType::Unknown
        || detail != DiagLevel::Unknown
        || shake != DiagLevel::Unknown
        || lumaShift != DiagLevel::Unknown;
}

// ---------------------------------------------------------------- 解析
//
// 以下格式全部是**实测确认过的真实输出**，不是按文档想象的。
// 之所以强调这点：按想象写解析器是这一类工作最经典的失败方式
// （例如 bitplanenoise 文档说"测量位平面噪声"，实测却根本不往 stderr 写数值）。

ScanType FilmDiagnosis::parseIdet(const QString& text, double* interlacedRatio,
                                  double* repeatedRatio) {
    if (interlacedRatio) *interlacedRatio = -1.0;
    if (repeatedRatio)   *repeatedRatio   = -1.0;

    static const QRegularExpression reMulti(
        QStringLiteral("Multi frame detection:\\s*TFF:\\s*(\\d+)\\s*BFF:\\s*(\\d+)\\s*"
                       "Progressive:\\s*(\\d+)\\s*Undetermined:\\s*(\\d+)"));
    static const QRegularExpression reRepeated(
        QStringLiteral("Repeated Fields:\\s*Neither:\\s*(\\d+)\\s*Top:\\s*(\\d+)\\s*"
                       "Bottom:\\s*(\\d+)"));

    // idet 会在每次统计更新时重复打印，取**最后一次**（累计值最全）
    int tff = -1, bff = -1, prog = -1, undet = -1;
    auto it = reMulti.globalMatch(text);
    while (it.hasNext()) {
        const auto m = it.next();
        tff   = m.captured(1).toInt();
        bff   = m.captured(2).toInt();
        prog  = m.captured(3).toInt();
        undet = m.captured(4).toInt();
    }
    int neither = -1, top = -1, bottom = -1;
    auto it2 = reRepeated.globalMatch(text);
    while (it2.hasNext()) {
        const auto m = it2.next();
        neither = m.captured(1).toInt();
        top     = m.captured(2).toInt();
        bottom  = m.captured(3).toInt();
    }

    if (tff < 0) return ScanType::Unknown;   // 完全没拿到统计

    const int total = tff + bff + prog + undet;
    if (total <= 0) return ScanType::Unknown;

    const double ilRatio = static_cast<double>(tff + bff) / static_cast<double>(total);
    if (interlacedRatio) *interlacedRatio = ilRatio;

    double repRatio = -1.0;
    if (neither >= 0) {
        const int repTotal = neither + top + bottom;
        if (repTotal > 0) {
            repRatio = static_cast<double>(top + bottom) / static_cast<double>(repTotal);
        }
    }
    if (repeatedRatio) *repeatedRatio = repRatio;

    // 判据顺序有讲究：3:2 pulldown 的梳状帧同时表现为「交错」与「重复场」，
    // 若先判交错会把 pulldown 当成纯交错——而两者的正确处理方式不同
    // （pulldown 走反胶转还原 24p，纯交错走去隔行），不能混淆。
    if (repRatio >= kPulldownRatio && ilRatio >= 0.10) {
        return ScanType::Pulldown32;
    }
    if (ilRatio >= kInterlacedRatio) {
        return (tff >= bff) ? ScanType::InterlacedTff : ScanType::InterlacedBff;
    }
    return ScanType::Progressive;
}

bool FilmDiagnosis::parseCropdetect(const QString& text, int* w, int* h, int* x, int* y) {
    static const QRegularExpression re(
        QStringLiteral("crop=(\\d+):(\\d+):(\\d+):(\\d+)"));
    bool found = false;
    int cw = 0, ch = 0, cx = 0, cy = 0;
    auto it = re.globalMatch(text);
    while (it.hasNext()) {                    // 取最后一次（末帧的稳定值）
        const auto m = it.next();
        cw = m.captured(1).toInt();
        ch = m.captured(2).toInt();
        cx = m.captured(3).toInt();
        cy = m.captured(4).toInt();
        found = true;
    }
    if (found) {
        if (w) *w = cw; if (h) *h = ch; if (x) *x = cx; if (y) *y = cy;
    }
    return found;
}

bool FilmDiagnosis::parseSignalstats(const QString& text, double* yMin, double* yMax,
                                     double* yAvg, double* satAvg) {
    static const QRegularExpression re(
        QStringLiteral("lavfi\\.signalstats\\.(\\w+)=([0-9.]+)"));
    double sumYMin = 0, sumYMax = 0, sumYAvg = 0, sumSat = 0;
    int nYMin = 0, nYMax = 0, nYAvg = 0, nSat = 0;

    auto it = re.globalMatch(text);
    while (it.hasNext()) {
        const auto m = it.next();
        const QString key = m.captured(1);
        const double  val = m.captured(2).toDouble();
        if (key == QLatin1String("YMIN"))        { sumYMin += val; ++nYMin; }
        else if (key == QLatin1String("YMAX"))   { sumYMax += val; ++nYMax; }
        else if (key == QLatin1String("YAVG"))   { sumYAvg += val; ++nYAvg; }
        else if (key == QLatin1String("SATAVG")) { sumSat  += val; ++nSat;  }
    }
    // 多帧时求均值：单帧的统计抖动很大，均值才代表这段素材的常态
    if (yMin)   *yMin   = nYMin ? sumYMin / nYMin : -1.0;
    if (yMax)   *yMax   = nYMax ? sumYMax / nYMax : -1.0;
    if (yAvg)   *yAvg   = nYAvg ? sumYAvg / nYAvg : -1.0;
    if (satAvg) *satAvg = nSat  ? sumSat  / nSat  : -1.0;
    return nYAvg > 0;
}

double FilmDiagnosis::parseVidstabShake(const QString& trfContent) {
    // .trf 是文本，每帧一行：
    //   Frame 2 (List 161 [(LM dx dy x y size contrast match),(LM ...),...])
    // 每帧给出若干局部运动向量。先按帧取**中位数**位移作为全局位移估计
    // （中位数对个别匹配失败的块不敏感），再算二阶差分。
    static const QRegularExpression reFrame(
        QStringLiteral("Frame\\s+\\d+\\s+\\(List\\s+\\d+\\s+\\[(.*)\\]\\)"));
    static const QRegularExpression reVec(
        QStringLiteral("\\(LM\\s+(-?[0-9.]+)\\s+(-?[0-9.]+)\\s+(-?[0-9.]+)\\s+(-?[0-9.]+)\\s+"
                       "(-?[0-9.]+)\\s+([0-9.]+)\\s+(-?[0-9.]+)\\)"));

    std::vector<double> gx, gy;   // 各帧的全局位移估计
    auto it = reFrame.globalMatch(trfContent);
    while (it.hasNext()) {
        const QString body = it.next().captured(1);
        std::vector<double> dxs, dys;
        auto itv = reVec.globalMatch(body);
        while (itv.hasNext()) {
            const auto m = itv.next();
            const double contrast = m.captured(6).toDouble();
            if (contrast < kTrfMinContrast) continue;   // 低对比度块的匹配不可信
            dxs.push_back(m.captured(1).toDouble());
            dys.push_back(m.captured(2).toDouble());
        }
        if (dxs.empty()) continue;
        auto median = [](std::vector<double>& v) {
            const std::size_t mid = v.size() / 2;
            std::nth_element(v.begin(), v.begin() + mid, v.end());
            return v[mid];
        };
        gx.push_back(median(dxs));
        gy.push_back(median(dys));
    }

    // 至少要有 3 帧才能算二阶差分
    if (gx.size() < 3) return -1.0;

    double sumSq = 0.0;
    int    n = 0;
    for (std::size_t i = 2; i < gx.size(); ++i) {
        const double d2x = gx[i] - 2.0 * gx[i - 1] + gx[i - 2];
        const double d2y = gy[i] - 2.0 * gy[i - 1] + gy[i - 2];
        sumSq += d2x * d2x + d2y * d2y;
        ++n;
    }
    if (n == 0) return -1.0;
    return std::sqrt(sumSq / static_cast<double>(n));
}

// ---------------------------------------------------------------- 分级

DiagLevel FilmDiagnosis::gradeDetail(double v) {
    if (v < 0.0)             return DiagLevel::Unknown;
    if (v < kDetailMild)     return DiagLevel::None;
    if (v < kDetailObvious)  return DiagLevel::Mild;
    return DiagLevel::Obvious;
}

DiagLevel FilmDiagnosis::gradeShake(double v) {
    if (v < 0.0)            return DiagLevel::Unknown;
    if (v < kShakeMild)     return DiagLevel::None;
    if (v < kShakeObvious)  return DiagLevel::Mild;
    return DiagLevel::Obvious;
}

bool FilmDiagnosis::barsWorthCropping(int srcW, int srcH, int cropW, int cropH) {
    if (srcW <= 0 || srcH <= 0 || cropW <= 0 || cropH <= 0) return false;
    const double dw = static_cast<double>(srcW - cropW) / srcW;
    const double dh = static_cast<double>(srcH - cropH) / srcH;
    // 两个方向都只差一点点 → 是 cropdetect 的正常抖动，不是黑边
    return (dw >= kBarsMinRatio) || (dh >= kBarsMinRatio);
}

// ---------------------------------------------------------------- 缓存

QString FilmDiagnosis::fingerprint(const QString& path) {
    const QFileInfo fi(path);
    if (!fi.exists()) return QString();
    return QStringLiteral("%1|%2|%3")
        .arg(fi.absoluteFilePath())
        .arg(fi.size())
        .arg(fi.lastModified().toMSecsSinceEpoch());
}

namespace {
/// 指纹 → 结论。进程内缓存：诊断要起 15 个子进程、耗时数十秒，
/// 同一文件重复诊断显然不该重算。跨进程持久化不在本次范围内。
QHash<QString, DiagnosisResult> g_cache;
} // namespace

bool FilmDiagnosis::cachedResult(const QString& path, DiagnosisResult* out) {
    const QString fp = fingerprint(path);
    if (fp.isEmpty()) return false;
    const auto it = g_cache.constFind(fp);
    if (it == g_cache.constEnd()) return false;
    if (out) *out = it.value();
    return true;
}

void FilmDiagnosis::storeResult(const QString& path, const DiagnosisResult& r) {
    const QString fp = fingerprint(path);
    if (!fp.isEmpty()) g_cache.insert(fp, r);
}

void FilmDiagnosis::clearCache() { g_cache.clear(); }

// ---------------------------------------------------------------- 执行

FilmDiagnosis::FilmDiagnosis(QObject* parent) : QObject(parent) {
    tempDir_ = makeTempDirPath();

    stepTimer_ = std::make_unique<QTimer>(this);
    stepTimer_->setSingleShot(true);
    stepTimer_->setInterval(kStepTimeoutMs);
    connect(stepTimer_.get(), &QTimer::timeout, this, [this]() {
        // 超时只把**当前项**降级为「未知」，整体诊断继续。
        if (!running_ || cancelRequested_) return;
        stepTimedOut_ = true;
        if (proc_ && proc_->state() != QProcess::NotRunning) {
            proc_->kill();
            // 必须等真正退出：否则 QProcess 析构时会报 "Destroyed while process is still running"。
            // 这里会同步触发 onProcessFinished —— 它看到 stepTimedOut_ 会立刻返回，
            // 避免和下面的推进逻辑重复走一步。
            proc_->waitForFinished(3000);
        }
        if (probeIndex_ < static_cast<int>(probes_.size())) {
            result_.notes << QStringLiteral("%1：诊断超时（%2 秒），该项判为未知")
                                 .arg(probes_[static_cast<std::size_t>(probeIndex_)].id)
                                 .arg(kStepTimeoutMs / 1000);
        }
        ++probeIndex_;
        stepTimedOut_ = false;
        runNextProbe();
    });
}

FilmDiagnosis::~FilmDiagnosis() {
    if (proc_ && proc_->state() != QProcess::NotRunning) {
        cancelRequested_ = true;
        proc_->kill();
        proc_->waitForFinished(3000);
    }
    cleanupTempDir();
}

void FilmDiagnosis::setSampling(int segments, double secondsPerSegment) {
    segments_      = std::max(1, segments);
    secondsPerSeg_ = secondsPerSegment > 0.5 ? secondsPerSegment : 20.0;
}

QString FilmDiagnosis::tempDirPath() const { return tempDir_; }

void FilmDiagnosis::cleanupTempDir() {
    if (!tempDir_.isEmpty() && QFileInfo(tempDir_).isDir()) {
        QDir(tempDir_).removeRecursively();
    }
}

void FilmDiagnosis::start(const QString& input, double durationSec, int srcW, int srcH) {
    if (running_) return;

    QString exe = transcoderPath_;
    if (exe.isEmpty() || !QFileInfo::exists(exe)) {
        // 没给转码器路径时同样走应用目录 / PATH 的探测顺序
        const QString appDir = QCoreApplication::applicationDirPath();
        const QStringList cands{ appDir + QStringLiteral("/ffmpeg.exe"),
                                 appDir + QStringLiteral("/bin/ffmpeg.exe") };
        for (const QString& c : cands) {
            if (QFileInfo::exists(c)) { exe = c; break; }
        }
        if (exe.isEmpty()) {
            exe = QStandardPaths::findExecutable(QStringLiteral("ffmpeg"));
        }
    }
    if (exe.isEmpty() || !QFileInfo::exists(exe)) {
        failWith(QStringLiteral("未找到转码器（ffmpeg）"),
                 QStringLiteral("诊断需要一个可用的 ffmpeg。请在设置中指定其路径。"));
        return;
    }
    transcoderPath_ = exe;
    inputPath_      = input;
    durationSec_    = durationSec;
    srcW_           = srcW;
    srcH_           = srcH;

    // 缓存命中就不再起进程：一次诊断是十余个子进程、数十秒的开销。
    // 指纹含大小与修改时间，文件被替换后自动失效。
    DiagnosisResult cached;
    if (cachedResult(inputPath_, &cached)) {
        result_ = cached;
        result_.notes << QStringLiteral("（本次使用缓存的诊断结果）");
        lastNote_ = QStringLiteral("使用缓存的诊断结果");
        running_ = true;
        emit runningChanged();
        // 异步发出：避免调用方在 start() 尚未返回时就收到 finished 而重入
        QTimer::singleShot(0, this, [this]() { emitResult(); });
        return;
    }

    // ---- 构建探针 ----
    // 顺序即执行顺序；每一项独立超时、独立降级。
    probes_.clear();
    probes_.push_back({ QStringLiteral("idet"),
                        { QStringLiteral("-vf"), QStringLiteral("idet") }, false });
    // 高频细节量：gblur 差分残差。用 signalstats 读残差图像的均值。
    // 之所以不用 bitplanenoise：它只写日志通道，实测在 info/verbose/debug/trace
    // 四个等级下都取不到数值（画面则原样透传）。
    probes_.push_back({ QStringLiteral("detail"),
                        { QStringLiteral("-vf"),
                          QStringLiteral("split=2[a][b];[b]gblur=sigma=6[blur];"
                                         "[a][blur]blend=all_mode=difference,"
                                         "signalstats,metadata=print") }, false });
    probes_.push_back({ QStringLiteral("crop"),
                        { QStringLiteral("-vf"),
                          QStringLiteral("cropdetect=limit=24:round=2") }, false });
    probes_.push_back({ QStringLiteral("stats"),
                        { QStringLiteral("-vf"),
                          QStringLiteral("signalstats,metadata=print") }, false });
    // vidstabdetect 必须写一个结果文件。**用相对路径 + 工作目录**：
    // 把 Windows 绝对路径（含 `D:` 的冒号）写进滤镜串会被 ffmpeg 的滤镜语法
    // 解析器炸掉（实测 "Error parsing a filter description"），相对路径可以完全绕开。
    probes_.push_back({ QStringLiteral("shake"),
                        { QStringLiteral("-vf"),
                          QStringLiteral("vidstabdetect=result=phys.trf:shakiness=5") }, true });

    result_ = DiagnosisResult{};
    result_.srcW = srcW_;
    result_.srcH = srcH_;
    shakeTrfFiles_.clear();
    lastNote_.clear();
    cancelRequested_ = false;
    probeIndex_ = 0;
    segmentIndex_ = 0;

    if (!proc_) {
        proc_ = std::make_unique<QProcess>(this);
        connect(proc_.get(), &QProcess::finished, this,
                [this](int code, QProcess::ExitStatus) { onProcessFinished(code); });
    }

    running_ = true;
    emit runningChanged();
    runNextProbe();
}

void FilmDiagnosis::cancel() {
    cancelRequested_ = true;
    if (stepTimer_) stepTimer_->stop();
    if (proc_ && proc_->state() != QProcess::NotRunning) {
        proc_->kill();
        proc_->waitForFinished(3000);   // 必须等真正退出，避免析构时告警
    }
    cleanupTempDir();
    if (running_) {
        running_ = false;
        emit runningChanged();
    }
}

void FilmDiagnosis::failWith(const QString& message, const QString& detail) {
    lastNote_ = message;
    if (running_) { running_ = false; emit runningChanged(); }
    cleanupTempDir();
    emit failed(message, detail);
}

/// 采样偏移：沿时间轴均匀分布。
/// 刻意不只看开头——老片的问题（尤其抖动与颗粒）可能只出现在中段。
static double segmentOffset(double duration, int segments, int index, double segLen) {
    if (segments <= 1 || duration <= 0.0) return 0.0;
    if (duration <= segLen) return 0.0;
    const double last = duration - segLen;
    return last * static_cast<double>(index) / static_cast<double>(segments - 1);
}

void FilmDiagnosis::runNextProbe() {
    if (cancelRequested_) return;
    if (probeIndex_ >= static_cast<int>(probes_.size())) {
        emitResult();
        return;
    }
    segmentIndex_ = 0;
    stepStderr_.clear();
    startNextSegment();
}

void FilmDiagnosis::startNextSegment() {
    if (cancelRequested_) return;

    const Probe& probe = probes_[static_cast<std::size_t>(probeIndex_)];

    // 段数按实际时长收缩：短视频不必重复跑同样的小片段
    int segs = segments_;
    double segLen = secondsPerSeg_;
    if (durationSec_ > 0.0) {
        if (durationSec_ <= segLen) segs = 1;
        else {
            // 保证所有段都落在时长内
            segs = std::min(segs, std::max(1, static_cast<int>(durationSec_ / segLen)));
            segLen = std::min(segLen, durationSec_ / segs);
        }
    } else {
        segs = 1;
    }
    if (segmentIndex_ >= segs) {
        // 本探针所有段跑完 → 解析
        const QString out = stepStderr_;
        if (probe.id == QLatin1String("idet")) {
            double il = -1.0, rep = -1.0;
            const ScanType t = parseIdet(out, &il, &rep);
            if (t == ScanType::Unknown) {
                result_.notes << QStringLiteral("扫描方式：未能判定（idet 无有效统计）");
            } else {
                result_.scan = t;
                result_.interlacedRatio = il;
                result_.repeatedFieldRatio = rep;
            }
        } else if (probe.id == QLatin1String("detail")) {
            double yMin = -1, yMax = -1, yAvg = -1, sat = -1;
            if (parseSignalstats(out, &yMin, &yMax, &yAvg, &sat)) {
                result_.detailValue = yAvg;
                result_.detail = gradeDetail(yAvg);
            } else {
                result_.notes << QStringLiteral("噪点/颗粒：未能判定（残差统计缺失）");
            }
        } else if (probe.id == QLatin1String("crop")) {
            int w = 0, h = 0, x = 0, y = 0;
            if (parseCropdetect(out, &w, &h, &x, &y)) {
                result_.cropW = w; result_.cropH = h;
                result_.cropX = x; result_.cropY = y;
                if (srcW_ <= 0 || srcH_ <= 0) {
                    // 没有源尺寸就无法判断"裁掉的这部分算不算黑边"。
                    // 保持 Unknown 并说明原因，不能默认成「无」。
                    result_.bars = DiagLevel::Unknown;
                    result_.notes << QStringLiteral(
                        "画面边界：未知（未提供源尺寸，无法判断裁剪比例是否有意义）");
                } else {
                    result_.bars = barsWorthCropping(srcW_, srcH_, w, h)
                                       ? DiagLevel::Obvious : DiagLevel::None;
                }
            } else {
                result_.notes << QStringLiteral("画面边界：未能判定（cropdetect 无输出）");
            }
        } else if (probe.id == QLatin1String("stats")) {
            double yMin = -1, yMax = -1, yAvg = -1, sat = -1;
            if (parseSignalstats(out, &yMin, &yMax, &yAvg, &sat)) {
                result_.lumaMin = yMin; result_.lumaMax = yMax;
                result_.lumaAvg = yAvg; result_.satAvg = sat;
                // 亮度动态范围被压缩（最亮也压得很低 / 最暗也抬得很高）→ 疑似褪色
                const double range = yMax - yMin;
                result_.lumaShift = (range < 80.0)  ? DiagLevel::Obvious
                                  : (range < 140.0) ? DiagLevel::Mild
                                                    : DiagLevel::None;
                result_.chromaShift = (sat < 8.0)   ? DiagLevel::Obvious
                                    : (sat < 18.0)  ? DiagLevel::Mild
                                                    : DiagLevel::None;
            } else {
                result_.notes << QStringLiteral("亮度/色彩：未能判定（signalstats 缺失）");
            }
        } else if (probe.id == QLatin1String("shake")) {
            // 逐段解析 .trf 后取**最大值**：抖动只要在任一段出现就该被检出，
            // 求平均会被平稳段落稀释掉。
            double worst = -1.0;
            for (const QString& rel : shakeTrfFiles_) {
                const QString path = QDir(tempDir_).filePath(rel);
                QFile f(path);
                if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) continue;
                const QString content = QString::fromLatin1(f.readAll());
                f.close();
                const double v = parseVidstabShake(content);
                if (v > worst) worst = v;
            }
            if (worst < 0.0) {
                result_.notes << QStringLiteral("抖动：未能判定（运动向量不足）");
            } else {
                result_.shakeValue = worst;
                result_.shake = gradeShake(worst);
            }
        }

        ++probeIndex_;
        runNextProbe();
        return;
    }

    // ---- 启动这一段的 ffmpeg ----
    const double offset = segmentOffset(durationSec_, segs, segmentIndex_, segLen);

    QStringList args;
    args << QStringLiteral("-hide_banner")
         << QStringLiteral("-nostdin")
         << QStringLiteral("-loglevel") << QStringLiteral("info");
    // -ss 放在 -i 之前：诊断不需要帧精确，关键帧定位快得多
    if (offset > 0.0) {
        args << QStringLiteral("-ss") << QString::number(offset, 'f', 3);
    }
    // **无论是否知道总时长都要限时**：早期版本写成"已知时长才加 -t"，
    // 结果不知道时长时每个探针都会把整个文件跑一遍——实测一部 1.5 分钟的素材
    // 诊断耗时 80 秒，换成两小时的老片就是十几分钟。诊断只看采样片段。
    args << QStringLiteral("-t") << QString::number(segLen, 'f', 3);
    args << QStringLiteral("-i") << inputPath_
         << QStringLiteral("-an") << QStringLiteral("-sn");
    args += probe.args;
    args << QStringLiteral("-f") << QStringLiteral("null") << QStringLiteral("-");

    stepStderr_.clear();
    const QString workDir = probe.needsWorkDir ? tempDir_ : QString();
    if (probe.needsWorkDir) {
        QDir().mkpath(tempDir_);
        // 每段一个独立结果文件，避免相互覆盖
        QStringList a2 = args;
        const int vfIdx = a2.indexOf(QStringLiteral("-vf"));
        if (vfIdx >= 0) {
            a2[vfIdx + 1] = QStringLiteral("vidstabdetect=result=phys_%1.trf:shakiness=5")
                                .arg(segmentIndex_);
        }
        shakeTrfFiles_ << QStringLiteral("phys_%1.trf").arg(segmentIndex_);
        proc_->setWorkingDirectory(workDir);
        proc_->setProgram(transcoderPath_);
        proc_->setArguments(a2);
    } else {
        proc_->setWorkingDirectory(QDir::tempPath());
        proc_->setProgram(transcoderPath_);
        proc_->setArguments(args);
    }

    ++segmentIndex_;
    stepTimedOut_ = false;
    if (stepTimer_) stepTimer_->start();   // 单步超时：超时只降级本项，不中断整体诊断
    proc_->start();
    if (!proc_->waitForStarted(5000)) {
        // 起不来只影响本项：记录并跳到下一项
        result_.notes << QStringLiteral("%1：无法启动诊断进程").arg(probe.id);
        ++probeIndex_;
        runNextProbe();
    }
}

void FilmDiagnosis::onProcessFinished(int exitCode) {
    if (stepTimer_) stepTimer_->stop();
    // 超时处理里已经 kill 并推进过了：直接返回，避免"双跳"跳过下一项
    if (stepTimedOut_) return;
    if (cancelRequested_) return;

    stepStderr_ += QString::fromLocal8Bit(proc_->readAllStandardError());
    stepStderr_ += QString::fromLocal8Bit(proc_->readAllStandardOutput());

    // 单项失败只是让该项降级为「未知」，绝不中断整体诊断
    if (exitCode != 0) {
        const Probe& probe = probes_[static_cast<std::size_t>(probeIndex_)];
        result_.notes << QStringLiteral("%1：诊断进程返回 %2，该项判为未知")
                             .arg(probe.id).arg(exitCode);
        ++probeIndex_;
        runNextProbe();
        return;
    }
    startNextSegment();
}

void FilmDiagnosis::emitResult() {
    if (scanOverride_ != ScanType::Unknown) {
        result_.scan = scanOverride_;
        result_.scanOverridden = true;
        result_.notes << QStringLiteral("扫描方式：按用户手动指定（%1），未采用自动检出")
                             .arg(scanTypeName(scanOverride_));
    }
    // 采样信息：段数 × 每段时长（各探针相同，这里只记一份）
    int segs = segments_;
    double segLen = secondsPerSeg_;
    if (durationSec_ > 0.0) {
        if (durationSec_ <= segLen) segs = 1;
        else {
            segs = std::min(segs, std::max(1, static_cast<int>(durationSec_ / segLen)));
            segLen = std::min(segLen, durationSec_ / segs);
        }
    } else { segs = 1; }
    result_.segments = segs;
    result_.analyzedSeconds = segs * segLen;

    storeResult(inputPath_, result_);

    running_ = false;
    emit runningChanged();
    cleanupTempDir();
    emit finished(result_);
}

} // namespace vtapp
