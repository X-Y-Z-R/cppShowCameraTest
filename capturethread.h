// ============================================================
//  capturethread.h
//  采集线程：在独立线程中打开设备、循环读帧并写入帧队列。
// ============================================================

#pragma once

#include <QImage>
#include <QString>
#include <QThread>

#include <atomic>
#include <memory>

class FrameQueue;
namespace cv { class VideoCapture; }

// 摄像头采集线程（生产者）。
// 只负责设备打开、读帧、BGR→RGB 转换和入队，不做界面显示与业务处理，
// 与处理线程 / 界面线程通过 FrameQueue 解耦。
//
// 断流检测分三类：
//   1. 读帧失败：滑动窗口内失败次数达阈值；
//   2. 长时间无有效画面：看门狗超时；
//   3. 画面停止更新：read 成功但反复返回同一份陈旧缓冲（拔掉 USB 后 DSHOW 常见）。
// 判定断流后释放设备，按设备名重新定位索引并循环重连。
class CaptureThread : public QThread {
    Q_OBJECT

public:
    // 构造函数。
    // @param deviceId   OpenCV 设备索引（Windows 下固定使用 DirectShow 后端）
    // @param deviceName 设备友好名称，断流后按名称重新定位索引（可为空）
    // @param output     帧输出队列（线程持有一份引用，保证队列生命周期覆盖线程）
    // @param parent     父对象，默认无父对象，由调用方管理生命周期
    explicit CaptureThread(int deviceId, const QString& deviceName,
                           std::shared_ptr<FrameQueue> output,
                           QObject* parent = nullptr);

    // 析构函数：确保采集线程已退出。
    ~CaptureThread() override;

    // 请求停止采集（不阻塞）。线程会在当前帧读取结束后退出。
    void requestStop();

    // 请求停止并等待线程退出。
    // @param timeoutMs 最长等待毫秒数，超时后返回但线程可能仍在运行
    // @return true 表示线程已结束
    bool stop(int timeoutMs = 2000);

signals:
    // 设备打开成功（含断流重连成功，此时界面只需刷新分辨率）。
    // @param width  实际画面宽度
    // @param height 实际画面高度
    void opened(int width, int height);

    // 采集出错（仅用于设备打开失败等终止性错误）。
    // @param message 错误描述
    void errorOccurred(const QString& message);

    // 采集过程中的运行日志（读帧失败、断流重连进度等）。
    // @param message 日志文本
    void logMessage(const QString& message);

    // 采集帧率（约每秒上报一次）。
    // @param fps 每秒采集的帧数
    void statsUpdated(double fps);

protected:
    // 线程入口：打开设备 → 循环读帧并写入队列 → 断流重连 → 退出时释放设备。
    void run() override;

private:
    // 打开设备并设置期望分辨率 / 缓冲区大小。
    // @param cap 需要初始化的采集对象
    // @return true 表示打开成功
    bool openDevice(cv::VideoCapture& cap);

    // 断流后循环重新打开设备，直到成功或收到停止请求。
    // @param cap 已释放的采集对象
    // @return true 表示重连成功；false 表示因停止请求而放弃
    bool reconnectDevice(cv::VideoCapture& cap);

    // 重连间隔内分片休眠，期间持续检查停止请求。
    // @return true 表示等待结束且未收到停止请求
    bool waitRetryInterval();

    int               m_deviceId;    ///< OpenCV 设备索引（重连时可能更新）
    QString           m_deviceName;  ///< 设备友好名称，用于重连时重新定位索引
    std::shared_ptr<FrameQueue> m_output;  ///< 帧输出队列
    std::atomic<bool> m_running;     ///< 采集运行标志
};
