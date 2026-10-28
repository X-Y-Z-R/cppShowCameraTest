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
      m_delayMs(0) {
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

void ProcessThread::run() {
    if (!m_input || !m_output) return;

    m_running.store(true);

    QImage frame;
    QElapsedTimer statsClock;
    statsClock.start();
    qint64 processed = 0;

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

        const QImage result = processFrame(frame);
        ++processed;
        // 显示队列关闭说明界面已停止消费，直接结束线程
        if (!m_output->push(result)) break;

        // 每秒上报一次处理帧率
        const qint64 elapsed = statsClock.elapsed();
        if (elapsed >= 1000) {
            emit statsUpdated(static_cast<double>(processed) * 1000.0 /
                              static_cast<double>(elapsed));
            processed = 0;
            statsClock.restart();
        }
    }

    m_running.store(false);
}

QImage ProcessThread::processFrame(const QImage& frame) const {
    // 阶段 3 将在此接入检测 / 跟踪算法，当前为占位实现：原样返回输入帧。
    return frame;
}
