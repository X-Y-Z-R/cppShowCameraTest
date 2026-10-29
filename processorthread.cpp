// ============================================================
//  processorthread.cpp
//  处理线程实现：阻塞取帧 → 处理 → 入显示队列，并统计处理帧率。
// ============================================================

#include "processorthread.h"

#include "framequeue.h"

#include <QDebug>
#include <QElapsedTimer>

#include <utility>

namespace {
constexpr int kPopTimeoutMs = 100;   ///< 队列等待超时，保证能及时响应停止请求
} // namespace

ProcessThread::ProcessThread(std::shared_ptr<FrameQueue> input,
                             std::shared_ptr<FrameQueue> output, QObject* parent)
    : QThread(parent),
      m_input(std::move(input)),
      m_output(std::move(output)),
      m_running(false),
      m_delayMs(0),
      m_detectEnabled(true),
      m_brightObject(true) {
}

ProcessThread::~ProcessThread() {
    stop();
}

void ProcessThread::requestStop() {
    m_running.store(false);
    requestInterruption();
}

bool ProcessThread::stop(int timeoutMs) {
    requestStop();
    if (!wait(timeoutMs)) {
        qWarning() << "ProcessThread: 等待处理线程退出超时";
        return false;
    }
    return true;
}

void ProcessThread::setArtificialDelayMs(int ms) {
    m_delayMs.store(ms > 0 ? ms : 0);
}

void ProcessThread::setDetectionEnabled(bool enabled) {
    m_detectEnabled.store(enabled);
}

void ProcessThread::setBrightObject(bool brightObject) {
    m_brightObject.store(brightObject);
}

void ProcessThread::run() {
    if (!m_input || !m_output) return;

    m_running.store(true);
    m_detector.reset();

    QImage frame;
    QElapsedTimer statsClock;
    statsClock.start();
    QElapsedTimer frameClock;
    qint64 processed = 0;
    double sumProcessMs = 0.0;   ///< 统计窗口内单帧处理耗时之和
    double maxProcessMs = 0.0;   ///< 统计窗口内单帧处理耗时峰值
    DetectionStats detectionStats;
    bool detectionErrorLogged = false;  ///< 检测异常只提示一次，避免刷屏
    bool lastBrightObject = m_brightObject.load();  ///< 上一帧的极性设置

    while (m_running.load() && !isInterruptionRequested()) {
        // 队列关闭（会话结束）或取帧超时：取不到帧时继续等待退出标志
        if (!m_input->pop(frame, kPopTimeoutMs)) {
            if (m_input->isClosed()) break;
            continue;
        }
        if (frame.isNull()) continue;

        // 模拟处理耗时：验收时设为 100ms，界面仍应保持流畅
        const int delay = m_delayMs.load();
        if (delay > 0) {
            msleep(static_cast<unsigned long>(delay));
            if (!m_running.load() || isInterruptionRequested()) break;
        }

        // 只统计算法本身耗时，模拟延时不计入，避免性能数据失真
        frameClock.start();
        QImage result;

        // 极性切换（浅色工件 / 深色工件）后清空旧跟踪与累计计数，避免上一极性残留
        const bool brightObject = m_brightObject.load();
        if (brightObject != lastBrightObject) {
            m_detector.reset();
            detectionStats = DetectionStats{};
            emit detectionUpdated(0, 0, 0);
            lastBrightObject = brightObject;
        }

        try {
            result = processFrame(frame, detectionStats);
        } catch (const cv::Exception& error) {
            // 检测层异常不能让整个进程退出：回退为原始画面继续预览
            if (!detectionErrorLogged) {
                qWarning() << "ProcessThread: 检测异常，后续帧回退为原始画面："
                           << error.what();
                detectionErrorLogged = true;
            }
            detectionStats = DetectionStats{};
            result = frame;
        }
        const double processMs =
            static_cast<double>(frameClock.nsecsElapsed()) / 1000000.0;
        sumProcessMs += processMs;
        if (processMs > maxProcessMs) maxProcessMs = processMs;

        ++processed;
        // 显示队列关闭说明界面已停止消费，直接结束线程
        if (!m_output->push(result)) break;

        // 每秒上报一次处理帧率、处理耗时与检测计数
        const qint64 elapsed = statsClock.elapsed();
        if (elapsed >= 1000) {
            const double fps = static_cast<double>(processed) * 1000.0 /
                               static_cast<double>(elapsed);
            const double avgMs =
                processed > 0 ? sumProcessMs / static_cast<double>(processed) : 0.0;
            emit statsUpdated(fps, avgMs, maxProcessMs);
            emit detectionUpdated(detectionStats.okTotal, detectionStats.ngTotal,
                                  detectionStats.currentCount);

            processed = 0;
            sumProcessMs = 0.0;
            maxProcessMs = 0.0;
            statsClock.restart();
        }
    }

    m_running.store(false);
}

QImage ProcessThread::processFrame(const QImage& frame, DetectionStats& stats) {
    // 关闭检测时退化为阶段 2 的直通模式，便于在演示中对比检测开销
    if (!m_detectEnabled.load()) {
        stats = DetectionStats{};
        return frame;
    }

    // 把当前极性同步给检测器：决定 Otsu 用正向还是反向阈值
    m_detector.setBrightObject(m_brightObject.load());
    return m_detector.process(frame, stats);
}
