// ============================================================
//  capturethread.cpp
//  采集线程实现：OpenCV 读帧 + 帧转换 + 入队 + 断流检测（读帧失败 / 无画面 / 画面停滞）
//  + 按设备名重定位并重连 + 采集帧率统计。
// ============================================================

#include "capturethread.h"

#include "cameradevice.h"
#include "framequeue.h"

#include <QDebug>
#include <QElapsedTimer>

#include <algorithm>
#include <deque>
#include <utility>

#include <opencv2/imgproc.hpp>
#include <opencv2/videoio.hpp>

namespace {
constexpr int kTargetIntervalMs       = 30;    ///< 目标帧间隔（约 33 FPS）
constexpr int kCaptureWidth           = 640;   ///< 期望画面宽度
constexpr int kCaptureHeight          = 480;   ///< 期望画面高度
constexpr int kFailureWindowSize      = 10;    ///< 断流判定窗口：最近多少次读帧结果
constexpr int kFailureThreshold       = 5;     ///< 窗口内失败达到该次数即判定断流
constexpr qint64 kNoFrameTimeoutMs    = 3000;  ///< 首帧之后超过该时长无有效画面判定断流
constexpr qint64 kInitialFrameTimeoutMs = 10000; ///< 首帧到达前的宽限时长（部分采集卡启动较慢）
constexpr qint64 kStalledTimeoutMs    = 3000;  ///< 黑画面停滞多久判定断流（拔设备后的陈旧缓冲）
constexpr qint64 kStalledBrightTimeoutMs = 10000; ///< 明亮画面停滞多久判定断流（避免静态场景误判）
constexpr double kDarkMeanThreshold   = 8.0;   ///< 平均灰度低于该值视为黑画面
constexpr int kFailureRetryDelayMs    = 50;    ///< 未达重连阈值时的重试间隔
constexpr int kReconnectIntervalMs    = 1000;  ///< 重连失败后的重试间隔
constexpr int kReconnectSliceMs       = 100;   ///< 重连等待的切片长度，保证及时响应停止

// 判断两帧是否位级完全相同（尺寸 / 类型需一致）。
// 拔掉 USB 设备后 DirectShow 常见两种表现：read 返回失败，或 read “成功”但
// 反复返回同一份陈旧缓冲（界面全黑）。真实传感器每帧都有噪声，
// 位级完全相同说明数据已停止更新，可按断流处理。
// @param lhs 上一帧
// @param rhs 当前帧
// @return true 表示两帧完全相同
bool isIdenticalFrame(const cv::Mat& lhs, const cv::Mat& rhs) {
    if (lhs.empty() || rhs.empty()) return false;
    if (lhs.size() != rhs.size() || lhs.type() != rhs.type()) return false;
    return cv::norm(lhs, rhs, cv::NORM_L1) == 0.0;
}

// 判断一帧是否接近全黑。
// 拔掉 USB 设备后 DirectShow 往往反复输出同一份全黑缓冲；黑画面停滞可以快速
// 判定断流，而明亮画面停滞需要更长超时，避免静止场景（如静态素材）被误判。
// @param frame 待检查的 BGR 帧
// @return true 表示平均灰度低于阈值
bool isDarkFrame(const cv::Mat& frame) {
    if (frame.empty()) return true;
    const cv::Scalar mean = cv::mean(frame);
    return (mean[0] + mean[1] + mean[2]) / 3.0 < kDarkMeanThreshold;
}
} // namespace

CaptureThread::CaptureThread(int deviceId, const QString& deviceName,
                             std::shared_ptr<FrameQueue> output, QObject* parent)
    : QThread(parent),
      m_deviceId(deviceId),
      m_deviceName(deviceName),
      m_output(std::move(output)),
      m_running(false) {
}

CaptureThread::~CaptureThread() {
    stop();
}

void CaptureThread::requestStop() {
    m_running.store(false);
    requestInterruption();
}

bool CaptureThread::stop(int timeoutMs) {
    requestStop();
    if (!wait(timeoutMs)) {
        qWarning() << "CaptureThread: 等待采集线程退出超时，设备可能无响应";
        return false;
    }
    return true;
}

bool CaptureThread::openDevice(cv::VideoCapture& cap) {
#ifdef _WIN32
    cap.open(m_deviceId, cv::CAP_DSHOW);
#else
    cap.open(m_deviceId);
#endif
    if (!cap.isOpened()) return false;

    cap.set(cv::CAP_PROP_FRAME_WIDTH, kCaptureWidth);
    cap.set(cv::CAP_PROP_FRAME_HEIGHT, kCaptureHeight);
    cap.set(cv::CAP_PROP_BUFFERSIZE, 1);    // 最小缓冲，降低延迟
    return true;
}

bool CaptureThread::waitRetryInterval() {
    // 分段等待，避免长时间 sleep 导致停止请求响应迟缓
    for (int waited = 0; waited < kReconnectIntervalMs; waited += kReconnectSliceMs) {
        if (!m_running.load() || isInterruptionRequested()) return false;
        msleep(kReconnectSliceMs);
    }
    return m_running.load() && !isInterruptionRequested();
}

bool CaptureThread::reconnectDevice(cv::VideoCapture& cap) {
    int attempt = 0;
    while (m_running.load() && !isInterruptionRequested()) {
        ++attempt;

        // 拔插后设备索引可能整体前移 / 后移，先按名称重新定位再打开，
        // 避免误打开列表中位置相同的其他设备（如虚拟摄像头）
        const QStringList names = CameraEnumerator::deviceNames();
        const int resolvedId = m_deviceName.isEmpty() ? -1 : names.indexOf(m_deviceName);

        if (resolvedId >= 0) {
            if (resolvedId != m_deviceId) {
                qWarning() << "CaptureThread: 设备索引已变化" << m_deviceId << "->" << resolvedId;
                emit logMessage(QStringLiteral("设备索引已变化（%1 → %2），按设备名重新定位")
                                    .arg(m_deviceId)
                                    .arg(resolvedId));
                m_deviceId = resolvedId;
            }
        } else if (!m_deviceName.isEmpty() && !names.isEmpty()) {
            // 名称列表非空却找不到目标：设备确实还没重新出现，继续等待。
            // 此时不能按旧索引打开，否则拔插后该索引可能已指向其他设备
            qWarning() << "CaptureThread: 未找到设备" << m_deviceName << "，等待重新插入";
            emit logMessage(QStringLiteral("等待设备 %1 重新出现…（第 %2 次）")
                                .arg(m_deviceName)
                                .arg(attempt));
            if (!waitRetryInterval()) return false;
            continue;
        }
        // 名称为空或名称枚举不可用时，回退到原索引尝试打开

        if (openDevice(cap)) {
            qInfo() << "CaptureThread: 设备重连成功，第" << attempt << "次尝试";
            emit logMessage(QStringLiteral("设备重连成功（第 %1 次尝试）").arg(attempt));
            return true;
        }

        qWarning() << "CaptureThread: 设备重连失败，第" << attempt << "次尝试，"
                   << kReconnectIntervalMs << "ms 后重试";
        emit logMessage(QStringLiteral("设备重连失败（第 %1 次尝试），%2 ms 后重试")
                            .arg(attempt)
                            .arg(kReconnectIntervalMs));

        if (!waitRetryInterval()) return false;
    }
    return false;
}

void CaptureThread::run() {
    cv::VideoCapture cap;
    if (!openDevice(cap)) {
        emit errorOccurred(QStringLiteral("无法打开设备索引 %1").arg(m_deviceId));
        return;
    }

    const int width  = static_cast<int>(cap.get(cv::CAP_PROP_FRAME_WIDTH));
    const int height = static_cast<int>(cap.get(cv::CAP_PROP_FRAME_HEIGHT));
    emit opened(width, height);

    m_running.store(true);

    cv::Mat frame;
    cv::Mat rgb;
    QElapsedTimer interval;
    interval.start();
    QElapsedTimer statsClock;
    statsClock.start();
    qint64 captured = 0;

    // 最近若干次读帧结果：即使失败与成功交替出现（设备拔掉后 DSHOW 常见），
    // 窗口内的失败比例也能及时触发重连，而不是被偶发成功反复清零
    std::deque<bool> recentReads;
    int consecutiveFailures = 0;

    // 最后一次收到有效画面的时刻：读帧阻塞可能很慢（断流后约 1s/次），
    // 仅靠失败次数会让重连迟迟不触发，因此再加一道“无有效画面超时”看门狗
    QElapsedTimer sinceValidFrame;
    sinceValidFrame.start();
    bool seenValidFrame = false;   ///< 是否已收到过有效画面（决定看门狗宽限时长）

    // 上一次画面与最后更新时间：用于检测“read 成功但画面不再变化”的陈旧缓冲
    cv::Mat previousFrame;
    QElapsedTimer sinceFrameChange;
    sinceFrameChange.start();

    // 每秒上报一次采集帧率（无有效帧时上报 0，便于状态栏反映断流）
    auto emitStats = [&]() {
        const qint64 elapsed = statsClock.elapsed();
        if (elapsed < 1000) return;
        emit statsUpdated(static_cast<double>(captured) * 1000.0 /
                          static_cast<double>(elapsed));
        captured = 0;
        statsClock.restart();
    };

    // 判定断流后释放设备并重连，返回 false 表示收到停止请求需退出线程。
    auto reconnectAfterBreak = [&]() -> bool {
        cap.release();
        if (!reconnectDevice(cap)) return false;

        recentReads.clear();
        consecutiveFailures = 0;
        sinceValidFrame.restart();      // 给重连后的设备留出出帧时间
        previousFrame.release();
        sinceFrameChange.restart();
        // 重连成功后分辨率可能变化，通知界面刷新
        emit opened(static_cast<int>(cap.get(cv::CAP_PROP_FRAME_WIDTH)),
                    static_cast<int>(cap.get(cv::CAP_PROP_FRAME_HEIGHT)));
        emitStats();
        return true;
    };

    while (m_running.load() && !isInterruptionRequested()) {
        const bool ok = cap.read(frame) && !frame.empty();

        recentReads.push_back(ok);
        while (static_cast<int>(recentReads.size()) > kFailureWindowSize) {
            recentReads.pop_front();
        }

        if (!ok) {
            // 主动停止时读帧失败属于正常退出，不提示错误
            if (!m_running.load() || isInterruptionRequested()) break;

            ++consecutiveFailures;
            const int failures =
                static_cast<int>(std::count(recentReads.begin(), recentReads.end(), false));

            // 只在失败开始时打印 / 提示一次，避免读帧失败很快时刷屏
            if (consecutiveFailures == 1) {
                qWarning() << "CaptureThread: 读帧失败，窗口内" << failures << "/"
                           << kFailureWindowSize << "次失败";
                emit logMessage(
                    QStringLiteral("读取画面失败（最近 %1 次中失败 %2 次），正在重试…")
                        .arg(static_cast<int>(recentReads.size()))
                        .arg(failures));
            }

            // 窗口内失败次数达到阈值，或长时间收不到有效画面：判定断流
            const qint64 silenceMs = sinceValidFrame.elapsed();
            const qint64 noFrameTimeout =
                seenValidFrame ? kNoFrameTimeoutMs : kInitialFrameTimeoutMs;
            if (failures >= kFailureThreshold || silenceMs >= noFrameTimeout) {
                qWarning() << "CaptureThread: 最近" << static_cast<int>(recentReads.size())
                           << "次读帧中失败" << failures << "次，已" << silenceMs
                           << "ms无有效画面，判定设备断流，开始重连";
                emit logMessage(
                    QStringLiteral("最近 %1 次读帧中失败 %2 次（%3 ms 无有效画面），判定设备断流，开始重连…")
                        .arg(static_cast<int>(recentReads.size()))
                        .arg(failures)
                        .arg(silenceMs));

                if (!reconnectAfterBreak()) break;
                continue;
            }

            emitStats();
            msleep(kFailureRetryDelayMs);
            continue;
        }

        // 拔掉设备后 DSHOW 可能仍“成功读取”同一份陈旧缓冲（全黑画面），
        // 此时 read 不会失败，只能靠“画面是否还在变化”判断断流
        if (isIdenticalFrame(previousFrame, frame)) {
            const qint64 stalledMs = sinceFrameChange.elapsed();
            // 黑画面说明设备无有效输出（陈旧缓冲），快速重连；
            // 明亮画面可能是静止素材，放宽超时以免误判
            const qint64 stalledTimeout =
                isDarkFrame(frame) ? kStalledTimeoutMs : kStalledBrightTimeoutMs;
            if (stalledMs >= stalledTimeout) {
                qWarning() << "CaptureThread: 画面已" << stalledMs
                           << "ms无变化（read 成功但输出陈旧），判定设备断流，开始重连";
                emit logMessage(QStringLiteral("画面已 %1 ms 无变化，判定设备断流，开始重连…")
                                    .arg(stalledMs));

                if (!reconnectAfterBreak()) break;
                continue;
            }
        } else {
            frame.copyTo(previousFrame);
            sinceFrameChange.restart();
        }

        consecutiveFailures = 0;
        sinceValidFrame.restart();
        seenValidFrame = true;

        // BGR → RGB：QImage::Format_RGB888 需要 RGB 排列
        cv::cvtColor(frame, rgb, cv::COLOR_BGR2RGB);

        // 先用 Mat 内存构造 QImage，再 copy() 做深拷贝，
        // 避免下一帧复用 rgb 缓冲导致处理 / 显示线程读到半新半旧的数据
        const QImage view(rgb.data, rgb.cols, rgb.rows,
                          static_cast<int>(rgb.step), QImage::Format_RGB888);
        ++captured;

        // 入队失败说明队列已关闭（会话结束），直接退出
        if (!m_output->push(view.copy())) break;

        emitStats();

        // 按 30ms 节流，高帧率设备也不会把队列与处理线程压满
        const qint64 remain = kTargetIntervalMs - interval.elapsed();
        if (remain > 0) msleep(static_cast<unsigned long>(remain));
        interval.restart();
    }

    cap.release();
    m_running.store(false);
}
