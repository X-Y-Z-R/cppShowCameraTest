// ============================================================
//  processorthread.h
//  处理线程：从采集队列取帧 → 执行处理（阶段 3 的检测逻辑）→ 写入显示队列。
// ============================================================

#pragma once

#include <QImage>
#include <QThread>

#include <atomic>
#include <memory>

class FrameQueue;

// 帧处理线程。
// 与采集线程解耦：采集只管往队列里放，处理慢也不会阻塞采集与界面。
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

signals:
    // 处理帧率（约每秒上报一次）。
    // @param fps 每秒处理的帧数
    void statsUpdated(double fps);

protected:
    // 线程入口：循环取帧 → 处理 → 入显示队列。
    void run() override;

    // 单帧处理钩子：阶段 3 的检测算法在此接入，当前为原样返回的占位实现。
    // @param frame 输入帧（RGB888）
    // @return 处理后的帧（送给界面显示）
    QImage processFrame(const QImage& frame) const;

private:
    std::shared_ptr<FrameQueue> m_input;   ///< 输入队列（采集 → 处理）
    std::shared_ptr<FrameQueue> m_output;  ///< 输出队列（处理 → 显示）
    std::atomic<bool> m_running;  ///< 运行标志
    std::atomic<int>  m_delayMs;  ///< 模拟处理耗时（毫秒）
};
