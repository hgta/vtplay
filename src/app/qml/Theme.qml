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

    readonly property int   radiusS:    8
    readonly property int   radiusM:    12
    readonly property int   radiusL:    16

    readonly property int   padS:       6
    readonly property int   padM:       10
    readonly property int   padL:       16

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