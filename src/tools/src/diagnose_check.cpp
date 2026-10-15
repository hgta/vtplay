// vtplay-diagnose-check —— 源诊断的 headless 自测
//
// 用法：
//   vtplay-diagnose-check --selftest                 纯函数断言（不起进程，秒级）
//   vtplay-diagnose-check <media> [--segments N]     对真实文件跑一次诊断
//
// 分两层验证的原因：
//   · 纯函数层（解析 / 分级 / 判定）不需要 GPU、不需要真实素材，
//     可以用**实测抓到的真实输出格式**做断言 —— 这是最便宜也最可靠的一层。
//   · 真机层验证整条链路（子进程、采样、降级）能跑通。

#include "Exporter.h"
#include "FilmDiagnosis.h"
#include "RestorePlan.h"
#include "RestoreRunner.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>
#include <QTextStream>
#include <QTimer>

#include <algorithm>
#include <cmath>
#include <utility>
#include <vector>

using namespace vtapp;

namespace {

int g_pass = 0;
int g_fail = 0;
QTextStream out(stdout);

void check(bool ok, const QString& what) {
    if (ok) { ++g_pass; out << "  [OK]   " << what << "\n"; }
    else    { ++g_fail; out << "  [FAIL] " << what << "\n"; }
    out.flush();
}

void checkNear(double got, double want, double tol, const QString& what) {
    const bool ok = (got >= 0 && want >= 0)
                    ? std::fabs(got - want) <= tol
                    : (got == want);
    if (ok) {
        ++g_pass;
        out << QStringLiteral("  [OK]   %1 (got %2)").arg(what).arg(got) << "\n";
    } else {
        ++g_fail;
        out << QStringLiteral("  [FAIL] %1 (got %2, want %3)").arg(what).arg(got).arg(want)
            << "\n";
    }
    out.flush();
}

// ---- 实测抓到的真实格式样例 ----
// 逐行素材（真实抓取，来自一段 1080×1920 的 H.264 文件）
const char* kIdetProgressive =
    "[Parsed_idet_0 @ 0000029859975300] Repeated Fields: Neither:   201 Top:     0 Bottom:     0\n"
    "[Parsed_idet_0 @ 0000029859975300] Single frame detection: TFF:     0 BFF:     0 Progressive:   201 Undetermined:     0\n"
    "[Parsed_idet_0 @ 0000029859975300] Multi frame detection: TFF:     0 BFF:     0 Progressive:   201 Undetermined:     0\n";

// 交错素材（真实的 idet 输出形态：TFF 占多数）
const char* kIdetInterlacedTff =
    "[Parsed_idet_0 @ 0x1] Repeated Fields: Neither:   200 Top:     0 Bottom:     0\n"
    "[Parsed_idet_0 @ 0x1] Multi frame detection: TFF:   200 BFF:     0 Progressive:     0 Undetermined:     0\n";

// 交错（下场优先）
const char* kIdetInterlacedBff =
    "[Parsed_idet_0 @ 0x1] Repeated Fields: Neither:   200 Top:     0 Bottom:     0\n"
    "[Parsed_idet_0 @ 0x1] Multi frame detection: TFF:     0 BFF:   200 Progressive:     0 Undetermined:     0\n";

// 3:2 pulldown：约 20% 帧有重复场、同时约 20% 帧被 idet 看成"交错"。
// 这正是**必须把 pulldown 与纯交错区分开**的原因——两者处理方式不同
// （pulldown 走反胶转还原 24p，纯交错走去隔行），混淆会导致错误输出。
const char* kIdetPulldown =
    "[Parsed_idet_0 @ 0x1] Repeated Fields: Neither:   160 Top:    40 Bottom:     0\n"
    "[Parsed_idet_0 @ 0x1] Multi frame detection: TFF:    40 BFF:     0 Progressive:   150 Undetermined:    10\n";

// cropdetect 真实抓取：干净画面也会给出 8 像素的"建议"（1080 → 1072）
const char* kCropRealistic =
    "[Parsed_cropdetect_0 @ 0x1] x1:0 x2:1079 y1:0 y2:1919 w:1072 h:1920 x:4 y:0 "
    "pts:15360 t:1.000000 limit:0.094118 crop=1072:1920:4:0\n";

// signalstats 真实抓取（metadata=print 形态）
const char* kStatsRealistic =
    "[Parsed_metadata_1 @ 0x1] lavfi.signalstats.YMIN=7\n"
    "[Parsed_metadata_1 @ 0x1] lavfi.signalstats.YMAX=241\n"
    "[Parsed_metadata_1 @ 0x1] lavfi.signalstats.YAVG=123.491\n"
    "[Parsed_metadata_1 @ 0x1] lavfi.signalstats.SATAVG=12.657\n"
    "[Parsed_metadata_1 @ 0x1] lavfi.signalstats.YMIN=9\n"
    "[Parsed_metadata_1 @ 0x1] lavfi.signalstats.YMAX=243\n"
    "[Parsed_metadata_1 @ 0x1] lavfi.signalstats.YAVG=125.509\n"
    "[Parsed_metadata_1 @ 0x1] lavfi.signalstats.SATAVG=12.343\n";

/// 按给定的逐帧全局位移构造一份 .trf 内容（格式与 vidstabdetect 实际写出的一致）。
QString makeTrf(const std::vector<std::pair<double, double>>& tl) {
    QString s = QStringLiteral("VID.STAB 1\n#      accuracy = 15\n");
    for (std::size_t i = 0; i < tl.size(); ++i) {
        s += QStringLiteral("Frame %1 (List 1 [(LM %2 %3 540 1332 112 0.900000 1.000000)])\n")
                 .arg(i + 1).arg(tl[i].first).arg(tl[i].second);
    }
    return s;
}

void selfTest() {
    out << "=== 纯函数断言（无进程、无需素材）===\n";

    // ---- idet 解析 ----
    {
        double il = -1, rep = -1;
        const ScanType t = FilmDiagnosis::parseIdet(QString::fromLatin1(kIdetProgressive), &il, &rep);
        check(t == ScanType::Progressive,
              QStringLiteral("逐行素材判为逐行（实际 %1）").arg(scanTypeName(t)));
        checkNear(il, 0.0, 0.001, QStringLiteral("逐行：交错帧占比"));
        checkNear(rep, 0.0, 0.001, QStringLiteral("逐行：重复场占比"));

        const ScanType t2 = FilmDiagnosis::parseIdet(QString::fromLatin1(kIdetInterlacedTff), nullptr, nullptr);
        check(t2 == ScanType::InterlacedTff,
              QStringLiteral("交错素材判为交错-上场优先（实际 %1）").arg(scanTypeName(t2)));

        const ScanType t3 = FilmDiagnosis::parseIdet(QString::fromLatin1(kIdetInterlacedBff), nullptr, nullptr);
        check(t3 == ScanType::InterlacedBff,
              QStringLiteral("交错素材判为交错-下场优先（实际 %1）").arg(scanTypeName(t3)));

        const ScanType t4 = FilmDiagnosis::parseIdet(QString::fromLatin1(kIdetPulldown), &il, &rep);
        check(t4 == ScanType::Pulldown32,
              QStringLiteral("3:2 pulldown 判为 pulldown 而非纯交错（实际 %1）")
                  .arg(scanTypeName(t4)));

        // 空输入必须判为「未知」，不能滑到「逐行」——
        // 把未知当正常会让上层不做去隔行，梳状纹会被后续放大固化
        const ScanType t5 = FilmDiagnosis::parseIdet(QStringLiteral("no stats here"), nullptr, nullptr);
        check(t5 == ScanType::Unknown,
              QStringLiteral("无统计输出时判为未知（实际 %1）").arg(scanTypeName(t5)));
    }

    // ---- cropdetect 解析 ----
    {
        int w = 0, h = 0, x = 0, y = 0;
        const bool ok = FilmDiagnosis::parseCropdetect(QString::fromLatin1(kCropRealistic), &w, &h, &x, &y);
        check(ok && w == 1072 && h == 1920 && x == 4 && y == 0,
              QStringLiteral("cropdetect 解析（%1×%2 @ %3,%4）").arg(w).arg(h).arg(x).arg(y));

        // 关键：干净画面上的 8 像素"建议"不能被当成黑边
        check(!FilmDiagnosis::barsWorthCropping(1080, 1920, 1072, 1920),
              QStringLiteral("1080→1072 的微小差异不算黑边"));
        check(FilmDiagnosis::barsWorthCropping(1920, 1080, 1920, 800),
              QStringLiteral("1080→800（上下各 140）算黑边"));
        check(FilmDiagnosis::barsWorthCropping(1920, 1080, 1440, 1080),
              QStringLiteral("左右各 240 算黑边"));
    }

    // ---- signalstats 解析（多帧取均值）----
    {
        double yMin = -1, yMax = -1, yAvg = -1, sat = -1;
        const bool ok = FilmDiagnosis::parseSignalstats(QString::fromLatin1(kStatsRealistic),
                                                        &yMin, &yMax, &yAvg, &sat);
        check(ok, QStringLiteral("signalstats 解析成功"));
        checkNear(yMin, 8.0,   0.01, QStringLiteral("YMIN 取均值"));
        checkNear(yMax, 242.0, 0.01, QStringLiteral("YMAX 取均值"));
        checkNear(yAvg, 124.5, 0.01, QStringLiteral("YAVG 取均值"));
        checkNear(sat,  12.5,  0.01, QStringLiteral("SATAVG 取均值"));
    }

    // ---- 抖动量：必须能区分"平滑摇镜"与"抖动" ----
    // 这是这个指标的全部意义所在：只看位移大小会把正常摇镜误判成抖动。
    {
        // 平滑摇镜：位移线性增长 → 二阶差分为 0
        std::vector<std::pair<double, double>> pan;
        for (int i = 0; i < 12; ++i) pan.push_back({ i * 5.0, 0.0 });
        const double panShake = FilmDiagnosis::parseVidstabShake(makeTrf(pan));
        checkNear(panShake, 0.0, 0.001, QStringLiteral("平滑摇镜：抖动量≈0"));
        check(FilmDiagnosis::gradeShake(panShake) == DiagLevel::None,
              QStringLiteral("平滑摇镜判为无抖动"));

        // 大小相同但来回抖动：位移在 0/10 之间跳 → 二阶差分很大
        std::vector<std::pair<double, double>> jitter;
        for (int i = 0; i < 12; ++i) jitter.push_back({ (i % 2) ? 10.0 : 0.0, 0.0 });
        const double jitShake = FilmDiagnosis::parseVidstabShake(makeTrf(jitter));
        check(jitShake > 15.0,
              QStringLiteral("来回抖动：抖动量显著（%1）").arg(jitShake));
        check(FilmDiagnosis::gradeShake(jitShake) == DiagLevel::Obvious,
              QStringLiteral("来回抖动判为明显"));

        // 平移总量相同（都走了 55 像素）却得到完全不同的抖动结论 —— 证明指标
        // 衡量的是位移的高频分量，而不是位移大小
        check(jitShake > panShake + 10.0,
              QStringLiteral("同样位移量下，抖动与摇镜被区分开"));

        // 向量太少 → 判为无法判定
        std::vector<std::pair<double, double>> tiny;
        tiny.push_back({ 0.0, 0.0 });
        tiny.push_back({ 1.0, 0.0 });
        checkNear(FilmDiagnosis::parseVidstabShake(makeTrf(tiny)), -1.0, 0.001,
                  QStringLiteral("帧数不足时返回无法判定"));
    }

    // ---- 分级阈值 ----
    {
        check(FilmDiagnosis::gradeDetail(1.0)  == DiagLevel::None,    QStringLiteral("细节 1.0 → 无"));
        check(FilmDiagnosis::gradeDetail(4.3)  == DiagLevel::Mild,    QStringLiteral("细节 4.3 → 轻度"));
        check(FilmDiagnosis::gradeDetail(10.0) == DiagLevel::Obvious, QStringLiteral("细节 10.0 → 明显"));
        check(FilmDiagnosis::gradeDetail(-1.0) == DiagLevel::Unknown, QStringLiteral("细节 -1 → 未知"));
        check(FilmDiagnosis::gradeShake(0.1)   == DiagLevel::None,    QStringLiteral("抖动 0.1 → 无"));
        check(FilmDiagnosis::gradeShake(1.0)   == DiagLevel::Mild,    QStringLiteral("抖动 1.0 → 轻度"));
        check(FilmDiagnosis::gradeShake(5.0)   == DiagLevel::Obvious, QStringLiteral("抖动 5.0 → 明显"));
    }

    out << QStringLiteral("\n--- 阶段决策（诊断 + 意图 → 阶段清单）---\n");

    // 2.8：默认路径**不得**包含任何会改变画面的阶段。
    // 对高质量源做修复就是破坏，所以「不修复」必须是一条干净路径。
    {
        RestoreIntent in;
        in.strength = RestoreStrength::None;
        DiagnosisResult d;   // 全 Unknown
        const RestorePlan p = buildPlan(d, in, 1920, 1080);
        check(p.onlyEncode(), QStringLiteral("不修复时阶段清单只有编码"));
        check(p.valid, QStringLiteral("不修复时清单合法"));
        check(p.stages.size() == 1, QStringLiteral("不修复时阶段数为 1（实际 %1）").arg(p.stages.size()));
    }

    // 2.6：逐行源不加去隔行；未知不加去隔行但必须给出说明
    {
        RestoreIntent in;
        in.strength = RestoreStrength::Medium;
        DiagnosisResult d;
        d.scan = ScanType::Progressive;
        const RestorePlan p = buildPlan(d, in, 1920, 1080);
        check(p.indexOf(StageKind::Deinterlace) < 0 && p.indexOf(StageKind::InverseTelecine) < 0,
              QStringLiteral("逐行源不含去隔行/反胶转"));

        DiagnosisResult u;
        u.scan = ScanType::Unknown;
        const RestorePlan pu = buildPlan(u, in, 1920, 1080);
        check(pu.indexOf(StageKind::Deinterlace) < 0,
              QStringLiteral("扫描方式未知时不猜、不加去隔行"));
        bool explained = false;
        for (const QString& n : pu.notes) if (n.contains(QStringLiteral("手动指定"))) explained = true;
        check(explained, QStringLiteral("扫描方式未知时给出可执行的说明"));
    }

    // pulldown 走反胶转（帧数会变），而不是走去隔行
    {
        RestoreIntent in;
        in.strength = RestoreStrength::Medium;
        DiagnosisResult d;
        d.scan = ScanType::Pulldown32;
        const RestorePlan p = buildPlan(d, in, 720, 576);
        check(p.indexOf(StageKind::InverseTelecine) >= 0 && p.indexOf(StageKind::Deinterlace) < 0,
              QStringLiteral("pulldown 源走反胶转而非去隔行"));
    }

    // 2.5：保颗粒时降噪强度必须被封顶，并且只动色度
    {
        DiagnosisResult d;
        d.scan = ScanType::Progressive;
        d.detail = DiagLevel::Obvious;

        RestoreIntent keep;
        keep.strength = RestoreStrength::Strong;   // 用户选"强力"
        keep.keepGrain = true;
        const RestorePlan pk = buildPlan(d, keep, 1920, 1080);
        const int di = pk.indexOf(StageKind::Denoise);
        check(di >= 0, QStringLiteral("检出明显噪点时加入降噪"));
        if (di >= 0) {
            check(pk.stages[di].denoiseStrength <= 1,
                  QStringLiteral("保颗粒时降噪强度被封顶（实际 %1）")
                      .arg(pk.stages[di].denoiseStrength));
            check(pk.stages[di].chromaOnly, QStringLiteral("保颗粒时只降色度"));
        }

        RestoreIntent nokeep = keep;
        nokeep.keepGrain = false;
        const RestorePlan pn = buildPlan(d, nokeep, 1920, 1080);
        const int dn = pn.indexOf(StageKind::Denoise);
        check(dn >= 0 && pn.stages[dn].denoiseStrength > 1,
              QStringLiteral("不保颗粒时降噪强度可更高（实际 %1）")
                  .arg(dn >= 0 ? pn.stages[dn].denoiseStrength : -1));
    }

    // 2.3 / 2.4：**穷举** 扫描方式 × 强度 × 目标短边，断言顺序约束永远成立。
    // 这是这一组最核心的断言：去隔行/反胶转必须早于放大。
    {
        const ScanType scans[] = { ScanType::Unknown, ScanType::Progressive,
                                   ScanType::InterlacedTff, ScanType::InterlacedBff,
                                   ScanType::Pulldown32 };
        const RestoreStrength strengths[] = { RestoreStrength::None, RestoreStrength::Light,
                                              RestoreStrength::Medium, RestoreStrength::Strong };
        const int targets[] = { 0, 576, 1080, 2160 };
        int combos = 0, badOrder = 0, invalid = 0, interBeforeUpscale = 0;
        for (ScanType sc : scans) {
            for (RestoreStrength st : strengths) {
                for (int tg : targets) {
                    DiagnosisResult d;
                    d.scan = sc;
                    d.detail = DiagLevel::Obvious;
                    d.shake = DiagLevel::Obvious;
                    d.lumaShift = DiagLevel::Mild;
                    RestoreIntent in;
                    in.strength = st;
                    in.targetShortSide = tg;
                    in.colorFix = true;
                    const RestorePlan p = buildPlan(d, in, 720, 576);
                    ++combos;
                    if (!p.valid) ++invalid;

                    const int il = std::max(p.indexOf(StageKind::InverseTelecine),
                                            p.indexOf(StageKind::Deinterlace));
                    const int up = p.indexOf(StageKind::Upscale);
                    const int dn = p.indexOf(StageKind::Denoise);
                    const int sh = p.indexOf(StageKind::Sharpen);
                    if (il >= 0 && up >= 0) {
                        ++interBeforeUpscale;
                        if (il > up) ++badOrder;
                    }
                    if (dn >= 0 && up >= 0 && dn > up) ++badOrder;
                    if (up >= 0 && sh >= 0 && up > sh) ++badOrder;

                    // 放大阶段的总是在最后（紧邻编码之前）
                    if (up >= 0 && p.indexOf(StageKind::Encode) < up) ++badOrder;
                }
            }
        }
        check(combos == 80, QStringLiteral("穷举组合数 = 80（实际 %1）").arg(combos));
        check(invalid == 0, QStringLiteral("所有组合的清单都合法（非法 %1）").arg(invalid));
        check(badOrder == 0, QStringLiteral("所有组合都满足顺序约束（违序 %1）").arg(badOrder));
        check(interBeforeUpscale > 0,
              QStringLiteral("确实覆盖到「去隔行 + 放大」并存的情形（%1 例）").arg(interBeforeUpscale));
    }

    // 放大尺寸必须为偶数（奇数尺寸会直接编码失败）
    {
        DiagnosisResult d;
        d.scan = ScanType::Progressive;
        RestoreIntent in;
        in.strength = RestoreStrength::Medium;
        in.targetShortSide = 1080;      // 720×576 → 短边 1080
        const RestorePlan p = buildPlan(d, in, 720, 576);
        const int up = p.indexOf(StageKind::Upscale);
        check(up >= 0, QStringLiteral("请求放大到 1080 时加入放大阶段"));
        if (up >= 0) {
            check(p.stages[up].targetW % 2 == 0 && p.stages[up].targetH % 2 == 0,
                  QStringLiteral("放大尺寸为偶数（%1×%2）")
                      .arg(p.stages[up].targetW).arg(p.stages[up].targetH));
            check(p.stages[up].targetH == 1080,
                  QStringLiteral("横屏素材按短边对齐（实际短边 %1）").arg(p.stages[up].targetH));
        }
        // 目标小于源 → 不放大
        RestoreIntent small = in;
        small.targetShortSide = 480;
        const RestorePlan ps = buildPlan(d, small, 1920, 1080);
        check(ps.indexOf(StageKind::Upscale) < 0, QStringLiteral("目标小于源时不加放大阶段"));
    }

    out << QStringLiteral("\n--- 阶段清单 → ffmpeg 参数 ---\n");

    // 3.10 回归门禁：既有分享类预设的参数必须**逐字不变**。
    // 修复能力是在同一份代码里新增的并行路径，最容易出的事就是顺手改动了分享路径
    // 的参数——而那条路径已经被验证过，任何变化都是回归。
    {
        const ExportPreset share = Exporter::presetById(QStringLiteral("wechat-share"));
        const QStringList got = Exporter::buildArgs(share, QStringLiteral("IN"),
                                                    QStringLiteral("OUT"), 2160, 3840, true, 0);
        const QStringList want = QStringList{
            "-hide_banner", "-nostdin", "-y", "-i", "IN",
            "-vf", "scale=720:-2:flags=lanczos",
            "-c:v", "libx264", "-preset", "veryfast", "-crf", "23",
            "-maxrate", "2500k", "-bufsize", "5000k",
            "-c:a", "aac", "-b:a", "128k",
            "-movflags", "+faststart", "-progress", "pipe:1", "-nostats", "OUT" };
        check(got == want, QStringLiteral("「微信分享」预设参数逐字未变"));
        if (got != want) out << "        实际: " << got.join(' ') << "\n";

        const ExportPreset remux = Exporter::presetById(QStringLiteral("remux"));
        const QStringList gotR = Exporter::buildArgs(remux, QStringLiteral("IN"),
                                                     QStringLiteral("OUT"), 2160, 3840, true, 0);
        const QStringList wantR = QStringList{
            "-hide_banner", "-nostdin", "-y", "-i", "IN", "-c", "copy",
            "-c:a", "copy", "-movflags", "+faststart",
            "-progress", "pipe:1", "-nostats", "OUT" };
        check(gotR == wantR, QStringLiteral("「仅换格式」预设参数逐字未变"));
        if (gotR != wantR) out << "        实际: " << gotR.join(' ') << "\n";
    }

    // 3.9 滤镜链：顺序必须与阶段清单一致
    {
        DiagnosisResult d;
        d.scan = ScanType::InterlacedTff;   // 去隔行
        d.detail = DiagLevel::Obvious;      // 降噪
        d.shake = DiagLevel::None;
        RestoreIntent in;
        in.strength = RestoreStrength::Medium;
        in.targetShortSide = 1080;          // 放大
        const RestorePlan p = buildPlan(d, in, 720, 576);
        const QString chain = Exporter::buildFilterChain(p, 720, 576);

        const int iDeint = chain.indexOf(QStringLiteral("bwdif"));
        const int iDen   = chain.indexOf(QStringLiteral("hqdn3d"));
        const int iScale = chain.indexOf(QStringLiteral("scale="));
        const int iCas   = chain.indexOf(QStringLiteral("cas="));
        check(iDeint >= 0 && iScale >= 0, QStringLiteral("交错源 + 放大：链中含去隔行与放大"));
        check(iDeint < iScale,
              QStringLiteral("滤镜链中「去隔行」在「放大」之前（这是硬约束）"));
        check(iDen >= 0 && iDen < iScale, QStringLiteral("降噪在放大之前"));
        check(iCas < 0 || iCas > iScale, QStringLiteral("锐化在放大之后（若存在）"));

        // 逐条比对完整链：能一眼看出顺序，也便于将来审阅变更
        out << "        链: " << chain << "\n";
    }

    // pulldown 走 fieldmatch+decimate；保颗粒时降噪只动色度
    {
        DiagnosisResult d;
        d.scan = ScanType::Pulldown32;
        d.detail = DiagLevel::Obvious;
        RestoreIntent in;
        in.strength = RestoreStrength::Strong;
        in.keepGrain = true;
        const RestorePlan p = buildPlan(d, in, 720, 576);
        const QString chain = Exporter::buildFilterChain(p, 720, 576);
        check(chain.contains(QStringLiteral("fieldmatch")) && chain.contains(QStringLiteral("decimate")),
              QStringLiteral("pulldown：链中含 fieldmatch + decimate"));
        check(chain.contains(QStringLiteral("hqdn3d=0:9:0:9")),
              QStringLiteral("保颗粒：降噪只作用色度（亮度两项为 0）"));
        out << "        链: " << chain << "\n";
    }

    // 3.7 / 3.8 编码参数：修复路径不设码率上限；10bit 与兼容模式可切换
    {
        DiagnosisResult d;
        d.scan = ScanType::Progressive;
        RestoreIntent in;
        in.strength = RestoreStrength::Medium;
        const RestorePlan p = buildPlan(d, in, 1920, 1080);
        const QStringList a = Exporter::buildRestoreArgs(p, QStringLiteral("IN"),
                                                         QStringLiteral("OUT"), 1920, 1080, true, 0);
        check(!a.contains(QStringLiteral("-maxrate")),
              QStringLiteral("修复路径不设码率上限（-maxrate 不存在）"));
        check(a.contains(QStringLiteral("yuv420p10le")) && a.contains(QStringLiteral("libx265")),
              QStringLiteral("默认 HEVC 10bit"));
        check(a.contains(QStringLiteral("grain")), QStringLiteral("保留颗粒：-tune grain"));

        RestoreIntent compat = in;
        compat.preferCompatibility = true;
        const RestorePlan pc = buildPlan(d, compat, 1920, 1080);
        const QStringList ac = Exporter::buildRestoreArgs(pc, QStringLiteral("IN"),
                                                          QStringLiteral("OUT"), 1920, 1080, true, 0);
        check(ac.contains(QStringLiteral("libx264")) && ac.contains(QStringLiteral("yuv420p")),
              QStringLiteral("兼容优先：H.264 8bit"));
        check(!ac.contains(QStringLiteral("yuv420p10le")), QStringLiteral("兼容优先时不是 10bit"));
    }

    // 非法清单必须**拒绝构造参数**，而不是照跑
    {
        RestorePlan forged;
        forged.valid = false;
        forged.invalidReason = QStringLiteral("测试用：人为构造的非法清单");
        const QStringList a = Exporter::buildRestoreArgs(forged, QStringLiteral("IN"),
                                                         QStringLiteral("OUT"), 1920, 1080, true, 0);
        check(a.isEmpty(), QStringLiteral("非法阶段清单被拒绝构造参数"));
    }

    out << QStringLiteral("\n--- 执行计划（时间段 / 预热 / 进度权重 / 预览）---\n");

    // 时间段校验：越界必须**在开始前**被拒，不能跑到一半才失败
    {
        TimeRange r;
        r.start = 10.0; r.length = 15.0;
        check(validateTimeRange(r, 100.0).isEmpty(), QStringLiteral("合法时间段通过校验"));
        r.start = 95.0; r.length = 15.0;
        check(!validateTimeRange(r, 100.0).isEmpty(), QStringLiteral("末端越界被拒绝"));
        r.start = 120.0; r.length = 0.0;
        check(!validateTimeRange(r, 100.0).isEmpty(), QStringLiteral("起点越界被拒绝"));
        r.start = -1.0; r.length = 5.0;
        check(!validateTimeRange(r, 100.0).isEmpty(), QStringLiteral("负起点被拒绝"));
        r.start = 10.0; r.length = -5.0;
        check(!validateTimeRange(r, 100.0).isEmpty(), QStringLiteral("负时长被拒绝"));
        r.start = 10.0; r.length = 0.0;
        check(validateTimeRange(r, 100.0).isEmpty(), QStringLiteral("时长为 0 表示到片尾，合法"));
    }

    // 预热：只有时域滤波需要
    {
        DiagnosisResult d;
        d.scan = ScanType::Progressive;
        RestoreIntent in;
        in.strength = RestoreStrength::Medium;
        const RestorePlan plain = buildPlan(d, in, 1920, 1080);   // 只有编码
        checkNear(prerollSeconds(plain), 0.0, 0.001,
                  QStringLiteral("仅编码时不需要预热（纯空间处理）"));

        d.scan = ScanType::InterlacedTff;
        const RestorePlan di = buildPlan(d, in, 1920, 1080);
        check(prerollSeconds(di) > 0.0, QStringLiteral("含去隔行时需要预热"));

        d.shake = DiagLevel::Obvious;
        d.detail = DiagLevel::Obvious;
        const RestorePlan big = buildPlan(d, in, 1920, 1080);
        check(prerollSeconds(big) >= prerollSeconds(di),
              QStringLiteral("含稳定时预热不小于仅去隔行"));
    }

    // 进度权重：初值合理，且被实测耗时校正
    {
        DiagnosisResult d;
        d.scan = ScanType::InterlacedTff;
        d.detail = DiagLevel::Obvious;
        RestoreIntent in;
        in.strength = RestoreStrength::Medium;
        const RestorePlan p = buildPlan(d, in, 1920, 1080);
        const QList<double> w = initialStageWeights(p);
        check(w.size() == p.stages.size(), QStringLiteral("权重数量与阶段数一致"));
        const int ei = p.indexOf(StageKind::Encode);
        double maxW = 0.0;
        for (double x : w) maxW = std::max(maxW, x);
        check(ei >= 0 && w[ei] == maxW, QStringLiteral("编码阶段初始权重最大（通常最慢）"));

        // 没有任何阶段完成时，权重保持不变
        QList<bool> done;
        for (int i = 0; i < p.stages.size(); ++i) done << false;
        const QList<double> same = reweightStages(w, QList<double>(w.size(), 0.0), done);
        check(same == w, QStringLiteral("无已完成阶段时权重不变"));

        // 第一个阶段实测远慢于估计 → 剩余时间估计被同步放大
        QList<double> actual(w.size(), 0.0);
        actual[0] = w[0] * 30.0;          // 实测是初值的 30 倍
        done[0] = true;
        const QList<double> re = reweightStages(w, actual, done);
        checkNear(re[0], actual[0], 0.001, QStringLiteral("已完成阶段记入实测耗时"));
        if (p.stages.size() > 1) {
            checkNear(re[1], w[1] * 30.0, 0.001,
                      QStringLiteral("剩余阶段按实测速率重估（×30）"));
            check(re[1] > w[1] * 29.0 && re[1] < w[1] * 31.0,
                  QStringLiteral("ETA 会随实测偏差一起放大，而不是继续显示乐观值"));
        }
    }

    // 预览：跳过稳定（两遍全片处理，无法对单帧执行），并如实标注
    {
        DiagnosisResult d;
        d.scan = ScanType::Progressive;
        d.shake = DiagLevel::Obvious;      // 会加入稳定阶段
        d.detail = DiagLevel::Obvious;
        RestoreIntent in;
        in.strength = RestoreStrength::Medium;
        const RestorePlan p = buildPlan(d, in, 1920, 1080);
        check(p.indexOf(StageKind::Stabilize) >= 0, QStringLiteral("检出明显抖动时加入稳定阶段"));

        const RestorePlan pv = previewPlan(p);
        check(pv.indexOf(StageKind::Stabilize) < 0, QStringLiteral("预览清单中已移除稳定阶段"));
        check(pv.stages.size() == p.stages.size() - 1, QStringLiteral("预览阶段数少 1"));
        bool noted = false;
        for (const QString& n : pv.notes) if (n.contains(QStringLiteral("不含稳定"))) noted = true;
        check(noted, QStringLiteral("预览如实标注「不含稳定效果」"));

        // 原清单不能被改动（preview 是拷贝语义）
        check(p.indexOf(StageKind::Stabilize) >= 0, QStringLiteral("预览不影响原清单"));
    }

    // 临时空间估算：只有稳定阶段需要中间文件
    {
        DiagnosisResult d;
        d.scan = ScanType::Progressive;
        RestoreIntent in;
        in.strength = RestoreStrength::Medium;
        const RestorePlan plain = buildPlan(d, in, 1920, 1080);
        check(estimateTempBytes(plain, 3000) == 0,
              QStringLiteral("无稳定阶段时不需要临时空间"));
        d.shake = DiagLevel::Obvious;
        const RestorePlan stab = buildPlan(d, in, 1920, 1080);
        check(estimateTempBytes(stab, 3000) > 0, QStringLiteral("含稳定阶段时估算出临时空间"));
    }

    out << QStringLiteral("\n纯函数断言：通过 %1，失败 %2\n").arg(g_pass).arg(g_fail);
    out.flush();
}

} // namespace

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    const QStringList args = QCoreApplication::arguments();

    if (args.contains(QStringLiteral("--selftest")) || args.size() < 2) {
        selfTest();
        return g_fail == 0 ? 0 : 1;
    }

    const QString media = args.at(1);
    int segments = 3;
    int segSeconds = 20;
    double duration = 0.0;
    int srcW = 0, srcH = 0;

    const int si = args.indexOf(QStringLiteral("--segments"));
    if (si >= 0 && si + 1 < args.size()) segments = args.at(si + 1).toInt();
    const int st = args.indexOf(QStringLiteral("--seconds"));
    if (st >= 0 && st + 1 < args.size()) segSeconds = args.at(st + 1).toInt();
    const int di = args.indexOf(QStringLiteral("--duration"));
    if (di >= 0 && di + 1 < args.size()) duration = args.at(di + 1).toDouble();
    const int zi = args.indexOf(QStringLiteral("--size"));
    if (zi >= 0 && zi + 1 < args.size()) {
        const QStringList wh = args.at(zi + 1).split(QLatin1Char('x'));
        if (wh.size() == 2) { srcW = wh.at(0).toInt(); srcH = wh.at(1).toInt(); }
    }

    // ---- 完整修复导出（诊断 → 决策 → 执行）----
    // 这是端到端验证路径：一次跑通就能看到真实画面差异。
    if (args.contains(QStringLiteral("--restore"))) {
        const int oi = args.indexOf(QStringLiteral("--restore"));
        const QString outFile = (oi + 1 < args.size() && !args.at(oi + 1).startsWith('-'))
                                    ? args.at(oi + 1) : QString();
        if (outFile.isEmpty()) { out << "--restore 需要一个输出路径\n"; return 1; }

        RestoreIntent in;
        int strength = 2;
        const int si2 = args.indexOf(QStringLiteral("--strength"));
        if (si2 >= 0 && si2 + 1 < args.size()) strength = args.at(si2 + 1).toInt();
        in.strength = static_cast<RestoreStrength>(std::clamp(strength, 0, 3));
        in.keepGrain = !args.contains(QStringLiteral("--no-keep-grain"));
        in.preferCompatibility = args.contains(QStringLiteral("--compat"));
        in.colorFix = args.contains(QStringLiteral("--color"));
        if (srcW > 0 && srcH > 0 && args.contains(QStringLiteral("--upscale"))) {
            in.targetShortSide = std::min(srcW, srcH) * 2;
        }
        TimeRange range;
        const int ri = args.indexOf(QStringLiteral("--start"));
        if (ri >= 0 && ri + 1 < args.size()) range.start = args.at(ri + 1).toDouble();
        const int li = args.indexOf(QStringLiteral("--length"));
        if (li >= 0 && li + 1 < args.size()) range.length = args.at(li + 1).toDouble();

        const QString ff = QStandardPaths::findExecutable(QStringLiteral("ffmpeg"));
        out << QStringLiteral("=== 修复导出：%1 → %2 ===\n").arg(media, outFile)
            << QStringLiteral("ffmpeg: %1\n").arg(ff);
        out.flush();

        auto* diag = new FilmDiagnosis(&app);
        auto* runner = new RestoreRunner(&app);
        runner->setTranscoderPath(ff);

        QObject::connect(diag, &FilmDiagnosis::failed, [&](const QString& m, const QString& d) {
            out << QStringLiteral("诊断失败：%1 %2\n").arg(m, d); out.flush(); app.exit(3);
        });
        QObject::connect(diag, &FilmDiagnosis::finished, [&](const DiagnosisResult& r) {
            out << QStringLiteral("诊断：扫描=%1 细节=%2(%3) 抖动=%4(%5)\n")
                       .arg(scanTypeName(r.scan), diagLevelName(r.detail))
                       .arg(r.detailValue, 0, 'f', 2)
                       .arg(diagLevelName(r.shake)).arg(r.shakeValue, 0, 'f', 2);
            const RestorePlan p = buildPlan(r, in, srcW, srcH);
            out << QStringLiteral("阶段清单：%1\n").arg(p.summary());
            for (const QString& n : p.notes) out << QStringLiteral("  - %1\n").arg(n);
            if (!p.valid) { out << QStringLiteral("清单非法：%1\n").arg(p.invalidReason); app.exit(5); return; }
            out.flush();

            // 单帧预览：不导出，只渲染一帧（同步、秒级），用于验收阶段清单是否真的生效
            const int pi = args.indexOf(QStringLiteral("--preview"));
            if (pi >= 0 && pi + 1 < args.size()) {
                const QString png = args.at(pi + 1);
                const QString err = runner->renderPreview(p, media, 6.0, srcW, srcH, png);
                out << (err.isEmpty() ? QStringLiteral("预览已生成：%1\n").arg(png)
                                      : QStringLiteral("预览失败：%1\n").arg(err));
                out.flush();
                app.exit(err.isEmpty() ? 0 : 6);
                return;
            }

            const bool dryRun = args.contains(QStringLiteral("--dry-run"));
            runner->setDryRun(dryRun);
            QObject::connect(runner, &RestoreRunner::commandBuilt,
                             [&](const QString& pass, const QString& cmd) {
                out << QStringLiteral("cmd[%1]: %2\n").arg(pass, cmd);
                out.flush();
                // 干跑：报告完主命令即可结束，不必真的转码
                if (dryRun && pass == QLatin1String("main")) app.exit(0);
            });
            QObject::connect(runner, &RestoreRunner::progressChanged,
                             [&](double ratio, double speed, qint64, int si, const QString& name) {
                if (ratio >= 0.0) {
                    out << QStringLiteral("\r  [%1] %2  %3%   %4x      ")
                               .arg(si).arg(name).arg(int(ratio * 100)).arg(speed, 0, 'f', 2);
                    out.flush();
                }
            });
            QObject::connect(runner, &RestoreRunner::finished, [&](const QString& o2) {
                out << QStringLiteral("\n完成：%1\n").arg(o2); out.flush(); app.exit(0);
            });
            QObject::connect(runner, &RestoreRunner::failed, [&](const QString& m, const QString& d) {
                out << QStringLiteral("\n失败：%1\n  %2\n").arg(m, d); out.flush(); app.exit(4);
            });
            // 取消路径实测：N 秒后取消，然后**检查是否留下残留**。
            // 取消的后果是"看不见的"（半成品文件、GB 级临时目录），
            // 因此必须由工具自己核对，而不是靠人看。
            const int ci = args.indexOf(QStringLiteral("--cancel-after"));
            if (ci >= 0 && ci + 1 < args.size()) {
                const int secs = args.at(ci + 1).toInt();
                QTimer::singleShot(secs * 1000, runner, [runner]() { runner->cancel(); });
                QObject::connect(runner, &RestoreRunner::cancelled, [&]() {
                    const bool outLeft = QFileInfo::exists(outFile);
                    QStringList tempLeft;
                    const QStringList tdirs = QDir(QDir::tempPath())
                        .entryList({ QStringLiteral("vtplay-restore-*") },
                                   QDir::Dirs | QDir::NoDotAndDotDot);
                    out << QStringLiteral("\n已取消。残留检查：\n")
                        << QStringLiteral("  半成品输出存在: %1\n")
                               .arg(outLeft ? QStringLiteral("是 ✗") : QStringLiteral("否 ✓"))
                        << QStringLiteral("  临时目录残留: %1\n")
                               .arg(tdirs.isEmpty() ? QStringLiteral("无 ✓")
                                                    : tdirs.join(QStringLiteral(", ")));
                    out.flush();
                    app.exit(outLeft ? 7 : 0);
                });
            }

            runner->start(p, media, outFile, srcW, srcH, true, duration, range, 0);
        });
        diag->start(media, duration, srcW, srcH);
        return app.exec();
    }

    out << QStringLiteral("=== 诊断：%1（采样 %2 段 × %3 秒，时长 %4，尺寸 %5×%6）===\n")
               .arg(media).arg(segments).arg(segSeconds)
               .arg(duration > 0 ? QString::number(duration, 'f', 1) : QStringLiteral("未知"))
               .arg(srcW).arg(srcH);
    out.flush();

    FilmDiagnosis diag;
    diag.setSampling(segments, static_cast<double>(segSeconds));
    QObject::connect(&diag, &FilmDiagnosis::finished, [&](const DiagnosisResult& r) {
        out << QStringLiteral("扫描方式  : %1   (交错占比 %2, 重复场占比 %3)%4\n")
                   .arg(scanTypeName(r.scan))
                   .arg(r.interlacedRatio, 0, 'f', 3)
                   .arg(r.repeatedFieldRatio, 0, 'f', 3)
                   .arg(r.scanOverridden ? QStringLiteral("  [用户指定]") : QString())
            << QStringLiteral("高频细节  : %1   (%2)\n").arg(diagLevelName(r.detail)).arg(r.detailValue, 0, 'f', 2)
            << QStringLiteral("抖动      : %1   (%2 像素/帧²)\n").arg(diagLevelName(r.shake)).arg(r.shakeValue, 0, 'f', 2)
            << QStringLiteral("黑边      : %1   (裁到 %2×%3 @ %4,%5；源 %6×%7)\n")
                   .arg(diagLevelName(r.bars))
                   .arg(r.cropW).arg(r.cropH).arg(r.cropX).arg(r.cropY)
                   .arg(r.srcW).arg(r.srcH)
            << QStringLiteral("亮度范围  : %1   (YMIN %2 / YMAX %3 / YAVG %4)\n")
                   .arg(diagLevelName(r.lumaShift))
                   .arg(r.lumaMin, 0, 'f', 1).arg(r.lumaMax, 0, 'f', 1).arg(r.lumaAvg, 0, 'f', 1)
            << QStringLiteral("色彩      : %1   (SATAVG %2)\n").arg(diagLevelName(r.chromaShift)).arg(r.satAvg, 0, 'f', 2)
            << QStringLiteral("采样      : %1 段 × %2 秒 = %3 秒\n")
                   .arg(r.segments).arg(r.analyzedSeconds / std::max(1, r.segments), 0, 'f', 1)
                   .arg(r.analyzedSeconds, 0, 'f', 1);
        if (!r.notes.isEmpty()) {
            out << QStringLiteral("备注:\n");
            for (const QString& n : r.notes) out << QStringLiteral("  - %1\n").arg(n);
        }
        out.flush();
        app.exit(r.anyUsable() ? 0 : 2);
    });
    QObject::connect(&diag, &FilmDiagnosis::failed, [&](const QString& msg, const QString& detail) {
        out << QStringLiteral("诊断失败：%1\n  %2\n").arg(msg, detail);
        out.flush();
        app.exit(3);
    });

    diag.start(media, duration, srcW, srcH);
    return app.exec();
}
