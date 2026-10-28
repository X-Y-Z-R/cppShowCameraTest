// ============================================================
//  capturethread.h
//  采集线程：在独立线程中打开设备、循环读帧，避免阻塞 Qt 界面。
// ============================================================

#pragma once

#include <QImage>
#include <QThread>

#include <atomic>

// 摄像头采集线程。
// 负责设备打开、读帧与 BGR→RGB 转换，界面线程只接收 QImage 并显示。
class CaptureThread : public QThread {
    Q_OBJECT

public:
    // 构造函数。
    // @param deviceId OpenCV 设备索引（Windows 下固定使用 DirectShow 后端）
    // @param parent   父对象，默认无父对象，由调用方管理生命周期
    explicit CaptureThread(int deviceId, QObject* parent = nullptr);

    // 析构函数：确保采集线程已退出。
    ~CaptureThread() override;

    // 请求停止采集（不阻塞）。线程会在当前帧读取结束后退出。
    void requestStop();

    // 请求停止并等待线程退出。
    // @param timeoutMs 最长等待毫秒数，超时后返回但线程可能仍在运行
    // @return true 表示线程已结束
    bool stop(int timeoutMs = 2000);

signals:
    // 设备打开成功。
    // @param width  实际画面宽度
    // @param height 实际画面高度
    void opened(int width, int height);

    // 采集到一帧画面（已转为 RGB888，并做过深拷贝，可安全跨线程传递）。
    // @param image 帧图像
    void frameReady(const QImage& image);

    // 采集出错（设备打开失败或读取中断）。
    // @param message 错误描述
    void errorOccurred(const QString& message);

protected:
    // 线程入口：打开设备 → 循环读帧 → 退出时释放设备。
    void run() override;

private:
    int               m_deviceId;  ///< OpenCV 设备索引
    std::atomic<bool> m_running;   ///< 采集运行标志
};
