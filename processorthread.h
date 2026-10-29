// ============================================================
//  processorthread.h
//  处理线程：从采集队列取帧 → 执行处理（阶段 3 的检测逻辑）→ 写入显示队列。
// ============================================================

#pragma once

#include <QImage>
#include <QThread>

#include <atomic>
#include <memory>

#include "detector.h"

class FrameQueue;

// 帧处理线程。
// 与采集线程解耦：采集只管往队列里放，处理慢也不会阻塞采集与界面。
// 阶段 3 起：每帧经过 ObjectDetector 检测，输出带检测框 / OK-NG 计数的画面，
// 同时统计单帧处理耗时，供界面与 README 性能数据使用。
class ProcessThread : public QThread {
    Q_OBJECT

public:
    // 构造函数。
    // @param input  输入队列（采集线程生产，线程持有引用）
    // @param output 输出队列（界面线程消费，线程持有引用）
    // @param parent 父对象，默认无父对象，由调用方管理生命周期
    ProcessThread(std::shared_ptr<FrameQueue> input, std::shared_ptr<FrameQueue> output,
                  QObject* parent = nullptr);

    // 析构函数：确保处理线程已退出。
    ~ProcessThread() override;

    // 请求停止处理（不阻塞），线程在本次等待超时后退出。
    void requestStop();

    // 请求停止并等待线程退出。
    // @param timeoutMs 最长等待毫秒数
    // @return true 表示线程已结束
    bool stop(int timeoutMs = 3000);

    // 设置每帧的模拟处理耗时（毫秒）。
    // 用于验收“处理线程故意变慢时界面仍流畅”，也可直接模拟算法耗时。
    // @param ms 模拟耗时，0 表示不额外延时
    void setArtificialDelayMs(int ms);

    // 开 / 关示例检测（默认开启）。
    // 关闭后处理线程退化为阶段 2 的直通模式，只做队列转发，便于对比性能。
    // @param enabled true 表示执行检测与 OK / NG 计数
    void setDetectionEnabled(bool enabled);

    // 设置工件相对背景的亮度（切换检测极性）。
    // 切换后会清空跟踪与累计计数，避免上一极性的目标残留。
    // @param brightObject true 表示浅色工件 / 深色背景（默认）；false 表示深色工件 / 浅色背景
    void setBrightObject(bool brightObject);

signals:
    // 处理帧率与单帧处理耗时（约每秒上报一次）。
    // @param fps            每秒处理的帧数
    // @param avgProcessMs   统计窗口内的平均处理耗时（毫秒，不含模拟延时）
    // @param maxProcessMs   统计窗口内的峰值处理耗时（毫秒）
    void statsUpdated(double fps, double avgProcessMs, double maxProcessMs);

    // 检测计数（约每秒上报一次）。
    // @param okTotal      累计 OK 数
    // @param ngTotal      累计 NG 数
    // @param currentCount 当前画面中的稳定目标数
    void detectionUpdated(int okTotal, int ngTotal, int currentCount);

protected:
    // 线程入口：循环取帧 → 处理 → 入显示队列。
    void run() override;

    // 单帧处理钩子：执行检测 / 计数，并把结果绘制到画面。
    // @param frame 输入帧（RGB888）
    // @param stats 输出检测累计统计
    // @return 处理后的帧（送给界面显示）
    QImage processFrame(const QImage& frame, DetectionStats& stats);

private:
    std::shared_ptr<FrameQueue> m_input;   ///< 输入队列（采集 → 处理）
    std::shared_ptr<FrameQueue> m_output;  ///< 输出队列（处理 → 显示）
    std::atomic<bool> m_running;  ///< 运行标志
    std::atomic<int>  m_delayMs;  ///< 模拟处理耗时（毫秒）
    std::atomic<bool> m_detectEnabled;  ///< 是否启用示例检测
    std::atomic<bool> m_brightObject;   ///< true: 浅色工件/深色背景；false: 深色工件/浅色背景
    ObjectDetector    m_detector;       ///< 示例检测器（仅处理线程内访问）
};
