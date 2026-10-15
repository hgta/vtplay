#pragma once

#include <QList>
#include <QString>
#include <QStringList>

#include "FilmDiagnosis.h"

namespace vtapp {

/// 修复阶段种类。
///
/// 顺序即语义：这里的枚举顺序**就是**允许的执行顺序，`buildPlan` 会据此校验，
/// 任何越序组合都会被判为无效而不是照跑（见 `RestorePlan::valid`）。
enum class StageKind {
    InverseTelecine = 0,  ///< 反胶转：fieldmatch + decimate（帧数会减少，这是正确的）
    Deinterlace,          ///< 去隔行：bwdif
    Stabilize,            ///< 稳定：vidstabdetect + vidstabtransform（两遍，有中间文件）
    Denoise,              ///< 降噪：hqdn3d 或 chromanr
    Upscale,              ///< 放大：scale（未来此处可换成 AI 超分阶段）
    AddGrain,             ///< 加回颗粒：noise（胶片质感，不是噪声）
    Sharpen,              ///< 锐化：cas
    ColorCorrect,         ///< 亮度/色彩校正：curves / eq
    Encode,               ///< 编码：总是最后
};

QString stageKindName(StageKind k);

/// 一个阶段。参数按种类解释——刻意不做成完整的 variant：
/// 参数总量很小，扁平结构更易读也更好断言。
struct Stage {
    StageKind kind = StageKind::Encode;
    /// 为什么会有这个阶段（可直接显示给用户）。
    /// 决策是黑盒的话，用户没法判断"该不该让它动我的画面"。
    QString   reason;
    bool      twoPass        = false;   ///< 需要两遍（稳定）
    bool      needsTempFiles = false;   ///< 会产生中间文件（稳定）
    bool      changesFrameCount = false;///< 会改变帧数（反胶转）

    // ---- 降噪 ----
    int  denoiseStrength = 0;           ///< 0=关, 1..3
    bool chromaOnly      = false;       ///< 只降色度（保住亮度颗粒）

    // ---- 放大 ----
    int  targetW = 0, targetH = 0;

    // ---- 编码 ----
    int  crf = 18;
    bool tenBit = true;                 ///< 10bit 减轻色带
    bool keepGrainEncode = true;        ///< -tune grain
};

/// 处理强度。None 表示"不修复"——此时阶段清单里只有编码。
enum class RestoreStrength { None = 0, Light, Medium, Strong };

/// 用户意图。刻意用少而语义明确的开关，而不是把滤镜参数暴露给用户。
struct RestoreIntent {
    RestoreStrength strength = RestoreStrength::None;
    bool  keepGrain      = true;    ///< 保颗粒（老片默认开：颗粒是质感，不是噪声）
    int   targetShortSide = 0;      ///< 0 = 保持原尺寸；>0 = 目标短边（允许放大）
    bool  colorFix       = false;   ///< 亮度/色彩校正
    /// 兼容优先：用 H.264 8bit 而不是 HEVC 10bit。
    /// 10bit 能减轻色带，但 Hi10P 的兼容性差；老设备/部分播放器只认 8bit。
    bool  preferCompatibility = false;
    bool  allowDeinterlace = true;  ///< 是否允许自动加入去隔行/反胶转
    bool  allowStabilize   = true;
    bool  allowDenoise     = true;
    bool  allowSharpen     = true;
};

/// 一次修复的完整阶段清单。
struct RestorePlan {
    QList<Stage> stages;
    QStringList  notes;      ///< 未加入某阶段的原因、以及"未知"结论的提示
    bool valid = true;       ///< 顺序约束是否成立；false 时调用方**必须拒绝执行**
    QString invalidReason;

    /// 是否不含任何会改变画面的阶段（只有编码）——即"保真/分享"路径。
    bool onlyEncode() const;
    /// 人类可读的阶段摘要，如「去隔行 → 轻降噪 → 放大 2× → 锐化」。
    QString summary() const;
    /// 取指定种类的第一个阶段（顺序即语义，用于断言顺序约束）。
    int indexOf(StageKind k) const;
};

/// 由诊断结论与用户意图推导阶段清单。
///
/// **纯函数**：不碰文件、不起进程、不读全局状态，因此可以在无 GPU、无素材的
/// 前提下穷举各种组合做断言。这是整个修复能力里最值得测试的一块。
///
/// 关键约束（违反则把结果标记为 invalid 而不是"尽力而为"）：
///   反胶转 / 去隔行 → 稳定 → 降噪 → 放大 → 加颗粒 → 锐化 / 色彩 → 编码
/// 其中**去隔行必须在放大之前**：梳状纹在放大算法看来就是高频边缘，
/// 会被忠实地放大加粗成永久伪影，之后再怎么处理都去不掉。
RestorePlan buildPlan(const DiagnosisResult& diag, const RestoreIntent& intent,
                      int srcW, int srcH);

// ---------------------------------------------------------------------------
// 执行计划（仍是纯逻辑：不碰文件、不起进程，因此同样可以 headless 断言）
// ---------------------------------------------------------------------------

/// 导出时间段。`length <= 0` 表示"到片尾"。
struct TimeRange {
    double start  = 0.0;
    double length = 0.0;
    bool full() const { return start <= 0.0 && length <= 0.0; }
};

/// 校验时间段是否落在源时长内。返回空串表示合法，否则返回可显示的原因。
/// 起止顺序非法或越界必须在**开始前**拒绝，不能跑到一半才失败。
QString validateTimeRange(const TimeRange& r, double durationSec);

/// 预热秒数。
///
/// 去隔行、降噪、去块都是**时域相关**的滤波器：片段开头的若干帧缺少前序上下文，
/// 结果会不稳定（尤其去隔行会闪）。因此需要在起点之前多解码一小段，只丢弃其输出。
/// 返回 0 表示该阶段组合不需要预热。
double prerollSeconds(const RestorePlan& plan);

/// 各阶段的进度权重初值。
///
/// 不同阶段耗时差异可达十倍以上，等权会让进度条先冲一半再长时间停滞。
/// 初值只是起点——真正的修正在 `reweightStages`。
QList<double> initialStageWeights(const RestorePlan& plan);

/// 用**已完成阶段的实测耗时**重算剩余阶段的权重。
///
/// 每完成一个阶段就按实际耗时比例重新分配剩余权重，于是：
///   · 不需要先跑一段样本测量（那会增加启动延迟）
///   · 估计偏差会在第一个阶段完成后立刻被纠正
/// `actualSeconds` 与 `done` 与阶段列表等长；未完成的项被忽略。
QList<double> reweightStages(const QList<double>& weights,
                             const QList<double>& actualSeconds,
                             const QList<bool>& done);

/// 预览用的阶段清单：**跳过稳定阶段**。
///
/// 稳定是"两遍全片处理 + 中间文件"，单帧预览无法执行它。
/// 与其让预览等上几分钟，不如让预览**不含稳定效果**并在界面如实标注——
/// 半成品预览 + 明确说明，优于为了"完整"而让预览不可用。
RestorePlan previewPlan(const RestorePlan& plan);

/// 临时产物空间的粗略估算（字节）。
/// 目前只有稳定阶段需要中间文件（每帧约百余字节的变换记录），
/// 但把估算集中在一处，将来 AI 超分的帧序也能接进来。
qint64 estimateTempBytes(const RestorePlan& plan, int frameCount);

} // namespace vtapp
