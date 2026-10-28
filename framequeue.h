// ============================================================
//  framequeue.h
//  有界阻塞帧队列：采集线程 → 处理线程 → 界面线程 之间的帧通道。
// ============================================================

#pragma once

#include <QImage>

#include <condition_variable>
#include <deque>
#include <mutex>

// 线程安全的有界阻塞帧队列（生产者-消费者模型）。
// 容量固定；入队时若已满则丢弃最旧帧，保证工业场景下“宁丢帧、不堆积”，
// 避免处理慢于采集时队列无限增长、延迟持续变大。
class FrameQueue {
public:
    // 构造函数。
    // @param capacity 队列容量（帧数），必须大于 0
    explicit FrameQueue(int capacity = 4);

    // 关闭队列：标记关闭并唤醒所有等待者。
    // 关闭后 push 直接丢弃、pop 立即返回 false，用于快速结束消费者线程。
    void close();

    // 重新打开队列并清空残留帧，开始一次新的采集会话前调用。
    void reopen();

    // 清空队列中的残留帧（不改变打开/关闭状态）。
    void clear();

    // 生产者：放入一帧（帧的深拷贝由调用方保证）。
    // 队列已满时丢弃最旧帧后再入队；队列已关闭时直接丢弃该帧。
    // @param frame 帧图像
    // @return true 表示帧已进入队列
    bool push(const QImage& frame);

    // 消费者：阻塞等待取出一帧。
    // @param frame     输出帧
    // @param timeoutMs 最长等待毫秒数，超时返回 false，便于调用方检查退出标志
    // @return true 表示成功取到帧
    bool pop(QImage& frame, int timeoutMs = 100);

    // 消费者（界面线程）：非阻塞取最新一帧，同时丢弃更旧的帧，避免显示过时画面。
    // @param frame 输出帧
    // @return true 表示取到帧；队列为空或已关闭时返回 false
    bool takeLatest(QImage& frame);

    // 当前队列深度（帧数），用于状态栏监控。
    // @return 队列中的帧数
    int size() const;

    // 队列是否已关闭。
    // @return true 表示已关闭
    bool isClosed() const;

private:
    const int             m_capacity;  ///< 容量上限（帧）
    mutable std::mutex    m_mutex;     ///< 保护 m_frames / m_closed
    std::condition_variable m_notEmpty; ///< 有新帧或关闭时唤醒消费者
    std::deque<QImage>    m_frames;    ///< 帧缓冲区，前端最旧、后端最新
    bool                  m_closed;    ///< 关闭标志
};
