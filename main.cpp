// ============================================================
//  cppShowCameraTest
//  简述：
//    1. 识别本机已连接的摄像头 / 采集卡（DirectShow 读取名称 + OpenCV 逐索引探测）
//    2. 在 Qt 主窗口中通过设备列表选择设备，点击按钮打开 / 关闭
//    3. 采集线程实时读取画面，转换为 QImage 后缩放显示在 QLabel 上
//  使用说明：
//    - 在左侧设备列表中点击选择设备，点击“打开设备”开始预览；
//    - 再次点击“关闭设备”停止预览，关闭窗口时也会自动释放设备。
// ============================================================

#include <QApplication>
#include <QtGlobal>

#include "mainwindow.h"
#include "uitheme.h"

// 程序入口：创建 Qt 应用与主窗口。
// @param argc 命令行参数个数
// @param argv 命令行参数
// @return Qt 事件循环退出码
int main(int argc, char* argv[]) {
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
    // Qt5 需要显式开启高 DPI 缩放，避免 4K 屏幕上界面过小
    QApplication::setAttribute(Qt::AA_EnableHighDpiScaling);
#endif

    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("cppShowCameraTest"));
    app.setApplicationDisplayName(QStringLiteral("摄像头实时预览"));

    // 统一界面风格：Fusion + 深色调色板 + 样式表，
    // 与 Qt 版本无关，Win10 下也能得到现代化的扁平外观
    UiTheme::apply(app);

    MainWindow window;
    window.show();

    return app.exec();
}
