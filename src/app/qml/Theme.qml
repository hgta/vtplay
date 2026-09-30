pragma Singleton
import QtQuick

QtObject {
    // 视觉 token 单例 —— UI 集中引用，避免硬编码
    readonly property color bg:        "#0D0D0D"
    readonly property color panel:     "#1A1A1A"
    readonly property color accent:    "#D4FF00"
    readonly property color accentDim: "#A6CC00"
    readonly property color text:      "#FFFFFF"
    readonly property color textDim:   "#9A9A9A"
    readonly property color border:    "#2A2A2A"

    // 交互态：hover/pressed 统一取色，避免各组件各写一套
    readonly property color hover:     "#242424"
    readonly property color pressed:   "#2E2E2E"
    readonly property color overlay:   "#E60D0D0D"   // 半透明遮罩（对话框底）
    readonly property color danger:    "#FF6464"
    readonly property color dangerBg:  "#5A1F1F"

    readonly property int   radiusS:    8
    readonly property int   radiusM:    12
    readonly property int   radiusL:    16

    readonly property int   padS:       6
    readonly property int   padM:       10
    readonly property int   padL:       16

    // 尺寸常量：顶栏/侧栏/信息条统一，避免布局各处硬编码
    readonly property int   topBarH:    40
    readonly property int   infoBarH:   24
    readonly property int   sidebarW:   248
    readonly property int   rowH:       34

    readonly property int   durFast:    120
    readonly property int   durNormal:  180

    function fmtTime(sec) {
        if (!isFinite(sec) || sec < 0) sec = 0;
        var h = Math.floor(sec / 3600);
        var m = Math.floor((sec % 3600) / 60);
        var s = Math.floor(sec % 60);
        function pad(n) { return n < 10 ? "0" + n : "" + n; }
        return h > 0 ? (h + ":" + pad(m) + ":" + pad(s))
                     : (pad(m) + ":" + pad(s));
    }
}