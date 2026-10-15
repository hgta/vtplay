#include "RestorePlan.h"

#include <algorithm>

namespace vtapp {

namespace {

/// 保颗粒策略下的降噪强度上限。
///
/// 胶片颗粒是画面的**质感本身**，不是压缩噪声。用中强度以上的降噪会把老片
/// 洗成"塑料蜡像"，而且超分阶段本身也倾向磨平颗粒——两者叠加就再也回不来。
/// 因此保颗粒时强度封顶在最轻档，并把降噪限制在色度通道（老片常见的彩色噪点），
/// 亮度颗粒原样保留。
constexpr int kMaxDenoiseUnderKeepGrain = 1;

int evenSide(int v) { return v > 1 ? (v & ~1) : 2; }

} // namespace

QString stageKindName(StageKind k) {
    switch (k) {
        case StageKind::InverseTelecine: return QStringLiteral("反胶转");
        case StageKind::Deinterlace:     return QStringLiteral("去隔行");
        case StageKind::Stabilize:       return QStringLiteral("稳定");
        case StageKind::Denoise:         return QStringLiteral("降噪");
        case StageKind::Upscale:         return QStringLiteral("放大");
        case StageKind::AddGrain:        return QStringLiteral("加回颗粒");
        case StageKind::Sharpen:         return QStringLiteral("锐化");
        case StageKind::ColorCorrect:    return QStringLiteral("色彩校正");
        case StageKind::Encode:          return QStringLiteral("编码");
    }
    return QStringLiteral("未知阶段");
}

bool RestorePlan::onlyEncode() const {
    for (const Stage& s : stages) {
        if (s.kind != StageKind::Encode) return false;
    }
    return true;
}

int RestorePlan::indexOf(StageKind k) const {
    for (int i = 0; i < stages.size(); ++i) {
        if (stages[i].kind == k) return i;
    }
    return -1;
}

QString RestorePlan::summary() const {
    QStringList parts;
    for (const Stage& s : stages) {
        if (s.kind == StageKind::Encode) continue;
        QString name = stageKindName(s.kind);
        if (s.kind == StageKind::Denoise && s.denoiseStrength > 0) {
            static const char* kLevels[] = { "", "轻", "中", "强" };
            name = QStringLiteral("%1%2")
                       .arg(QString::fromUtf8(kLevels[std::clamp(s.denoiseStrength, 0, 3)]), name);
        }
        if (s.kind == StageKind::Upscale && s.targetW > 0) {
            name = QStringLiteral("%1 → %2×%3").arg(name).arg(s.targetW).arg(s.targetH);
        }
        parts << name;
    }
    return parts.isEmpty() ? QStringLiteral("仅编码") : parts.join(QStringLiteral(" → "));
}

RestorePlan buildPlan(const DiagnosisResult& diag, const RestoreIntent& intent,
                      int srcW, int srcH) {
    RestorePlan plan;

    // ---- 编码阶段总是在场（即使不修复也要编码）----
    Stage enc;
    enc.kind = StageKind::Encode;
    enc.reason = QStringLiteral("输出编码");
    switch (intent.strength) {
        case RestoreStrength::Strong: enc.crf = 16; break;
        case RestoreStrength::Medium: enc.crf = 18; break;
        default:                      enc.crf = 20; break;
    }
    enc.tenBit         = (intent.strength != RestoreStrength::None) && !intent.preferCompatibility;
    enc.keepGrainEncode = intent.keepGrain;

    if (intent.strength == RestoreStrength::None) {
        // 「不修复」路径：**不得**加入任何会改变画面的阶段。
        // 对高质量源做修复就是破坏，所以这里必须是干净的空计划。
        plan.stages << enc;
        plan.notes << QStringLiteral("未启用修复：输出仅做编码");
        return plan;
    }

    // ---- ① 反胶转 / 去隔行（必须在放大之前）----
    if (intent.allowDeinterlace) {
        switch (diag.scan) {
            case ScanType::Pulldown32: {
                Stage s;
                s.kind = StageKind::InverseTelecine;
                s.reason = QStringLiteral("检出 3:2 pulldown：还原原始帧率（帧数会减少，属正确结果）");
                s.changesFrameCount = true;
                plan.stages << s;
                break;
            }
            case ScanType::InterlacedTff:
            case ScanType::InterlacedBff: {
                Stage s;
                s.kind = StageKind::Deinterlace;
                s.reason = QStringLiteral("检出交错：去除梳状纹，避免被后续放大固化");
                plan.stages << s;
                break;
            }
            case ScanType::Progressive:
                break;   // 逐行源不做任何去隔行
            case ScanType::Unknown:
                // 未知按**保守**处理：不猜、不自动动画面，但把情况说清楚并给出解决办法。
                // 不能默认成"逐行"——那样遇到真交错素材会把梳状纹固化；
                // 也不该直接套去隔行——那样会软化真正的逐行画面。
                plan.notes << QStringLiteral(
                    "扫描方式未确定：未加入去隔行阶段。若画面有水平梳状纹，"
                    "请在诊断中手动指定为交错后重试");
                break;
        }
    }

    // ---- ② 稳定（两遍 + 中间文件）----
    if (intent.allowStabilize) {
        const bool addAuto = (diag.shake == DiagLevel::Obvious)
                          || (diag.shake == DiagLevel::Mild
                              && intent.strength >= RestoreStrength::Medium);
        if (addAuto) {
            Stage s;
            s.kind           = StageKind::Stabilize;
            s.twoPass        = true;
            s.needsTempFiles = true;
            s.reason = QStringLiteral("检出画面抖动：两遍稳定（会产生中间文件）");
            plan.stages << s;
        } else if (diag.shake == DiagLevel::Unknown) {
            plan.notes << QStringLiteral("抖动未能判定：未加入稳定阶段");
        }
    }

    // ---- ③ 降噪（保颗粒时封顶 + 只动色度）----
    if (intent.allowDenoise) {
        int want = 0;
        if (diag.detail == DiagLevel::Obvious)      want = (intent.strength >= RestoreStrength::Strong) ? 3 : 2;
        else if (diag.detail == DiagLevel::Mild)    want = 1;
        else if (diag.detail == DiagLevel::Unknown)
            plan.notes << QStringLiteral("颗粒/噪点未能判定：未加入降噪阶段");

        if (intent.keepGrain && want > kMaxDenoiseUnderKeepGrain) {
            want = kMaxDenoiseUnderKeepGrain;
            plan.notes << QStringLiteral("已按「保颗粒」限制降噪强度（上限 %1）")
                              .arg(kMaxDenoiseUnderKeepGrain);
        }
        if (want > 0) {
            Stage s;
            s.kind            = StageKind::Denoise;
            s.denoiseStrength = want;
            s.chromaOnly      = intent.keepGrain;   // 保颗粒时只动色度
            s.reason = intent.keepGrain
                           ? QStringLiteral("检出高频噪点：仅降色度噪点以保留胶片颗粒")
                           : QStringLiteral("检出高频噪点：降噪强度 %1").arg(want);
            plan.stages << s;
        }
    }

    // ---- ④ 放大（唯一允许"变大"的阶段；将来此处可换成 AI 超分）----
    bool didUpscale = false;
    const int srcShort = (srcW > 0 && srcH > 0) ? std::min(srcW, srcH) : 0;
    if (intent.targetShortSide > 0 && srcShort > 0
        && intent.targetShortSide > srcShort) {
        // 保持宽高比：短边对齐目标，长边按同比例算并取偶
        // （H.264/H.265 要求偶数尺寸，奇数会直接编码失败）
        const double k = static_cast<double>(intent.targetShortSide) / srcShort;
        const bool portrait = srcH > srcW;
        Stage s;
        s.kind = StageKind::Upscale;
        if (portrait) {
            s.targetW = evenSide(intent.targetShortSide);
            s.targetH = evenSide(static_cast<int>(srcH * k + 0.5));
        } else {
            s.targetH = evenSide(intent.targetShortSide);
            s.targetW = evenSide(static_cast<int>(srcW * k + 0.5));
        }
        s.reason = QStringLiteral("放大到短边 %1（源 %2×%3 → %4×%5）")
                       .arg(intent.targetShortSide).arg(srcW).arg(srcH)
                       .arg(s.targetW).arg(s.targetH);
        plan.stages << s;
        didUpscale = true;
    }

    // ---- ⑤ 加回颗粒 ----
    // 降噪或放大之后补一点回来：降噪会磨掉颗粒，放大又会让画面偏"平滑"，
    // 老片失去颗粒就失去了胶片感。
    if (intent.keepGrain && (didUpscale || plan.indexOf(StageKind::Denoise) >= 0)) {
        Stage s;
        s.kind = StageKind::AddGrain;
        s.reason = QStringLiteral("补回少量颗粒，保留胶片质感");
        plan.stages << s;
    }

    // ---- ⑥ 锐化（必须在放大之后）----
    if (intent.allowSharpen && intent.strength >= RestoreStrength::Medium) {
        Stage s;
        s.kind = StageKind::Sharpen;
        s.reason = QStringLiteral("轻度锐化（在放大之后，避免过冲被放大）");
        plan.stages << s;
    }

    // ---- ⑦ 色彩 / 亮度校正 ----
    if (intent.colorFix) {
        const bool luma   = diag.lumaShift == DiagLevel::Mild || diag.lumaShift == DiagLevel::Obvious;
        const bool chroma = diag.chromaShift == DiagLevel::Mild || diag.chromaShift == DiagLevel::Obvious;
        if (luma || chroma) {
            Stage s;
            s.kind = StageKind::ColorCorrect;
            s.reason = QStringLiteral("检出%1偏移：做轻度校正")
                           .arg(luma && chroma ? QStringLiteral("亮度与色彩")
                                               : (luma ? QStringLiteral("亮度") : QStringLiteral("色彩")));
            plan.stages << s;
        } else if (diag.lumaShift == DiagLevel::Unknown) {
            plan.notes << QStringLiteral("亮度/色彩未能判定：未加入校正阶段");
        }
    }

    plan.stages << enc;

    // ---- 顺序约束自检 ----
    // 构造方自己校验：这样将来任何一次改动破坏了顺序，都会被 headless 断言直接抓到，
    // 而不是等到导出一段画面已经毁掉的视频之后才发现。
    auto bad = [&](const QString& what) {
        plan.valid = false;
        plan.invalidReason = what;
    };
    const int il    = std::max(plan.indexOf(StageKind::InverseTelecine),
                               plan.indexOf(StageKind::Deinterlace));
    const int up    = plan.indexOf(StageKind::Upscale);
    const int den   = plan.indexOf(StageKind::Denoise);
    const int stab  = plan.indexOf(StageKind::Stabilize);
    const int sharp = plan.indexOf(StageKind::Sharpen);

    if (il >= 0 && up >= 0 && il > up) {
        bad(QStringLiteral("去隔行/反胶转排在放大之后：梳状纹会被放大固化"));
    } else if (den >= 0 && up >= 0 && den > up) {
        bad(QStringLiteral("降噪排在放大之后：会把放大后的噪点再处理一遍"));
    } else if (up >= 0 && sharp >= 0 && up > sharp) {
        bad(QStringLiteral("锐化排在放大之前：锐化的过冲会被放大成黑边白边"));
    } else if (stab >= 0 && up >= 0 && stab > up) {
        bad(QStringLiteral("稳定排在放大之后：应该先稳定再放大"));
    }
    return plan;
}

// ---------------------------------------------------------------- 执行计划

QString validateTimeRange(const TimeRange& r, double durationSec) {
    if (r.start < 0.0)  return QStringLiteral("起始时间不能为负");
    if (r.length < 0.0) return QStringLiteral("时长不能为负");
    if (durationSec > 0.0) {
        if (r.start >= durationSec) {
            return QStringLiteral("起始时间 %1 秒超出素材时长 %2 秒")
                .arg(r.start, 0, 'f', 1).arg(durationSec, 0, 'f', 1);
        }
        if (r.length > 0.0 && r.start + r.length > durationSec + 0.001) {
            return QStringLiteral("时间段末端 %1 秒超出素材时长 %2 秒")
                .arg(r.start + r.length, 0, 'f', 1).arg(durationSec, 0, 'f', 1);
        }
    }
    return QString();
}

double prerollSeconds(const RestorePlan& plan) {
    // 只有**时域**滤波需要预热：它们依赖前后帧/场的上下文，
    // 片段开头的若干帧会不稳定（去隔行尤其会闪）。
    // 纯空间滤波（缩放、锐化、色彩）不需要。
    //
    // 具体秒数是经验值，需要用真实素材标定。
    double s = 0.0;
    for (const Stage& st : plan.stages) {
        switch (st.kind) {
            case StageKind::InverseTelecine: s = std::max(s, 1.0); break;
            case StageKind::Deinterlace:     s = std::max(s, 1.0); break;
            case StageKind::Stabilize:       s = std::max(s, 2.0); break;  // 运动估计窗口更大
            case StageKind::Denoise:         s = std::max(s, 0.5); break;  // hqdn3d 含时域项
            default: break;
        }
    }
    return s;
}

QList<double> initialStageWeights(const RestorePlan& plan) {
    // 经验初值（相对权重，不是秒）。
    // 真正决定 ETA 准确度的是 reweightStages —— 一个阶段跑完后立刻按实测校正。
    QList<double> w;
    w.reserve(plan.stages.size());
    for (const Stage& s : plan.stages) {
        switch (s.kind) {
            case StageKind::InverseTelecine: w << 1.0; break;
            case StageKind::Deinterlace:     w << 1.0; break;
            case StageKind::Stabilize:       w << 2.0; break;   // 两遍
            case StageKind::Denoise:         w << 1.5; break;
            case StageKind::Upscale:         w << 1.5; break;
            case StageKind::AddGrain:        w << 0.5; break;
            case StageKind::Sharpen:         w << 0.5; break;
            case StageKind::ColorCorrect:    w << 0.5; break;
            case StageKind::Encode:          w << 4.0; break;   // 编码通常最慢
        }
    }
    return w;
}

QList<double> reweightStages(const QList<double>& weights,
                             const QList<double>& actualSeconds,
                             const QList<bool>& done) {
    QList<double> out = weights;
    if (weights.size() != done.size()) return out;

    // 已完成的阶段：权重直接换成**实测秒数**；
    // 再由「已完成阶段的权重 ↔ 实测耗时」标定出「一个权重单位 ≈ 多少秒」，
    // 用它把剩余阶段的权重也换算成秒。
    //
    // 效果是**预计剩余时间会立刻被纠正**：如果第一个阶段比估计慢得多，
    // 剩余时间估计会同步放大，而不是继续按乐观值显示。
    // （进度条在各阶段之间的分配比例由权重之比决定，这一步不改变比例，
    //   它改变的是"剩余多少秒"这个数字本身。）
    double doneWeight = 0.0, doneSeconds = 0.0;
    for (int i = 0; i < out.size(); ++i) {
        if (!done[i]) continue;
        doneWeight  += weights.value(i, 0.0);
        doneSeconds += actualSeconds.value(i, 0.0);
        out[i] = actualSeconds.value(i, 0.0);
    }
    if (doneWeight <= 0.0 || doneSeconds <= 0.0) return out;   // 还没有可用样本

    const double rate = doneSeconds / doneWeight;
    for (int i = 0; i < out.size(); ++i) {
        if (done[i]) continue;
        out[i] = weights.value(i, 0.0) * rate;
    }
    return out;
}

RestorePlan previewPlan(const RestorePlan& plan) {
    RestorePlan p = plan;
    for (int i = p.stages.size() - 1; i >= 0; --i) {
        if (p.stages[i].kind == StageKind::Stabilize) {
            p.stages.removeAt(i);
            p.notes << QStringLiteral(
                "预览不含稳定效果：稳定需要对整段做两遍处理，单帧预览无法执行");
        }
    }
    return p;
}

qint64 estimateTempBytes(const RestorePlan& plan, int frameCount) {
    if (frameCount <= 0) return 0;
    qint64 bytes = 0;
    for (const Stage& s : plan.stages) {
        if (!s.needsTempFiles) continue;
        // vidstabdetect 的变换记录是文本，每帧约百余字节。
        // 这个估算刻意偏保守（宁可多留空间）：空间不足导致的失败发生在
        // 处理到一半的时候，代价远高于多占一点盘。
        bytes += static_cast<qint64>(frameCount) * 128;
    }
    return bytes;
}

} // namespace vtapp
