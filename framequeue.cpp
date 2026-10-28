// ============================================================
//  framequeue.cpp
//  有界阻塞帧队列实现：std::mutex + std::condition_variable。
// ============================================================

#include "framequeue.h"

#include <chrono>

FrameQueue::FrameQueue(int capacity)
    : m_capacity(capacity > 0 ? capacity : 1),
      m_closed(false) {
}

void FrameQueue::close() {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_closed = true;
    m_notEmpty.notify_all();
}

void FrameQueue::reopen() {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_frames.clear();
    m_closed = false;
}

void FrameQueue::clear() {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_frames.clear();
}

bool FrameQueue::push(const QImage& frame) {
    if (frame.isNull()) return false;

    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_closed) return false;

        // 满则丢最旧：保持队列里始终是“较新”的帧，避免延迟累积
        while (static_cast<int>(m_frames.size()) >= m_capacity) {
            m_frames.pop_front();
        }
        m_frames.push_back(frame);
    }

    // 通知放在锁外，减少消费者被唤醒后又立刻阻塞的概率
    m_notEmpty.notify_one();
    return true;
}

bool FrameQueue::pop(QImage& frame, int timeoutMs) {
    std::unique_lock<std::mutex> lock(m_mutex);
    if (m_closed) return false;

    // 带超时等待，调用方可周期性检查退出标志，避免线程永久阻塞
    const bool ready = m_notEmpty.wait_for(
        lock, std::chrono::milliseconds(timeoutMs > 0 ? timeoutMs : 0),
        [this] { return m_closed || !m_frames.empty(); });

    if (!ready || m_closed || m_frames.empty()) return false;

    frame = m_frames.front();
    m_frames.pop_front();
    return true;
}

bool FrameQueue::takeLatest(QImage& frame) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_closed || m_frames.empty()) return false;

    // 只保留最新一帧，更旧的直接丢弃（界面永远显示最新画面）
    frame = m_frames.back();
    m_frames.clear();
    return true;
}

int FrameQueue::size() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return static_cast<int>(m_frames.size());
}

bool FrameQueue::isClosed() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_closed;
}
