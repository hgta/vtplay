#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QUrl>

#include <cstdio>

#include "PlayerController.h"
#include "VideoRenderer.h"
#include "AudioOutput.h"

int main(int argc, char* argv[]) {
    QGuiApplication app(argc, argv);
    app.setOrganizationName("VTPlay");
    app.setApplicationName("VTPlay");

    // 启动横幅：便于确认运行的是哪个构建（排查"跑的到底是不是最新版"）
    std::fprintf(stderr,
        "=== VTPlay %s ===\n"
        "build: %s %s\n"
        "qt: %s\n",
        "0.1.0",
        __DATE__, __TIME__,
        qVersion());
    std::fflush(stderr);

    QQuickStyle::setStyle("Basic");

    qmlRegisterUncreatableType<vtapp::PlayerController>("VTPlay", 1, 0, "PlayerController",
        "PlayerController is owned by C++");
    qmlRegisterUncreatableType<vtapp::AudioOutput>("VTPlay", 1, 0, "AudioOutput",
        "Created by Main.qml");
    // VideoRenderer 用独立 URI 注册，避免与 qml 模块的 qmldir（目录式导入）冲突
    qmlRegisterType<vtapp::VideoRenderer>("VTPlayCore", 1, 0, "VideoRenderer");

    vtapp::PlayerController controller;

    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty("player", &controller);
    engine.rootContext()->setContextProperty("appWin", engine.rootObjects().value(0));

    engine.loadFromModule("VTPlay", "Main");
    if (engine.rootObjects().isEmpty()) return -1;

    // 命令行传入媒体文件则直接打开并播放
    if (argc > 1 && argv[1] && *argv[1]) {
        controller.openUrl(QUrl::fromUserInput(QString::fromLocal8Bit(argv[1])));
        controller.play();
    }

    return app.exec();
}