// ============================================================
//  capturethread.cpp
//  采集线程实现：OpenCV 读帧 + 帧转换 + 30ms 节流。
// ============================================================

#include "capturethread.h"

#include <QDebug>
#include <QElapsedTimer>

#include <opencv2/imgproc.hpp>
#include <opencv2/videoio.hpp>

namespace {
constexpr int kTargetIntervalMs = 30;   ///< 目标帧间隔（约 33 FPS）
constexpr int kCaptureWidth     = 640;  ///< 期望画面宽度
constexpr int kCaptureHeight    = 480;  ///< 期望画面高度
} // namespace

CaptureThread::CaptureThread(int deviceId, QObject* parent)
    : QThread(parent),
      m_deviceId(deviceId),
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

void CaptureThread::run() {
    cv::VideoCapture cap;
#ifdef _WIN32
    cap.open(m_deviceId, cv::CAP_DSHOW);
#else
    cap.open(m_deviceId);
#endif
    if (!cap.isOpened()) {
        emit errorOccurred(QStringLiteral("无法打开设备索引 %1").arg(m_deviceId));
        return;
    }

    cap.set(cv::CAP_PROP_FRAME_WIDTH, kCaptureWidth);
    cap.set(cv::CAP_PROP_FRAME_HEIGHT, kCaptureHeight);
    cap.set(cv::CAP_PROP_BUFFERSIZE, 1);    // 最小缓冲，降低延迟

    const int width  = static_cast<int>(cap.get(cv::CAP_PROP_FRAME_WIDTH));
    const int height = static_cast<int>(cap.get(cv::CAP_PROP_FRAME_HEIGHT));
    emit opened(width, height);

    m_running.store(true);

    cv::Mat frame;
    cv::Mat rgb;
    QElapsedTimer interval;
    interval.start();

    while (m_running.load() && !isInterruptionRequested()) {
        if (!cap.read(frame) || frame.empty()) {
            // 主动停止时读帧失败属于正常退出，不提示错误
            if (m_running.load() && !isInterruptionRequested())
                emit errorOccurred(QStringLiteral("读取设备索引 %1 画面失败，设备可能已断开").arg(m_deviceId));
            break;
        }

        // BGR → RGB：QImage::Format_RGB888 需要 RGB 排列
        cv::cvtColor(frame, rgb, cv::COLOR_BGR2RGB);

        // 先用 Mat 内存构造 QImage，再 copy() 做深拷贝，
        // 避免下一帧复用 rgb 缓冲导致界面显示撕裂
        const QImage view(rgb.data, rgb.cols, rgb.rows,
                          static_cast<int>(rgb.step), QImage::Format_RGB888);
        emit frameReady(view.copy());

        // 按 30ms 节流，高帧率设备也不会把界面刷爆
        const qint64 remain = kTargetIntervalMs - interval.elapsed();
        if (remain > 0) msleep(static_cast<unsigned long>(remain));
        interval.restart();
    }

    cap.release();
}
