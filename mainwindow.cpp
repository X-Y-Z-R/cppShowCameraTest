// ============================================================
//  mainwindow.cpp
//  主窗口实现：设备选择、打开 / 关闭、实时画面显示与状态统计。
// ============================================================

#include "mainwindow.h"

#include "cameradevice.h"
#include "capturethread.h"

#include <QAbstractItemView>
#include <QApplication>
#include <QCloseEvent>
#include <QDebug>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QListWidgetItem>
#include <QPixmap>
#include <QPushButton>
#include <QResizeEvent>
#include <QShowEvent>
#include <QSize>
#include <QSizePolicy>
#include <QStatusBar>
#include <QVBoxLayout>
#include <QWidget>
#include <QtGlobal>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace {

#ifdef Q_OS_WIN
// 应用 Windows 深色标题栏，避免系统白色标题栏与深色界面冲突。
// 通过动态加载 dwmapi.dll 调用 DwmSetWindowAttribute，避免引入链接依赖，
// 旧系统（不支持该属性）上失败时只是保持系统默认外观，不影响功能。
// @param hwnd 目标窗口的原生句柄
// @return true 表示深色标题栏属性设置成功
bool applyDarkTitleBar(HWND hwnd) {
    if (!hwnd) return false;

    using DwmSetWindowAttributeFn = HRESULT(WINAPI*)(HWND, DWORD, LPCVOID, DWORD);
    HMODULE dwmapi = ::LoadLibraryW(L"dwmapi.dll");
    if (!dwmapi) return false;

    auto setAttribute = reinterpret_cast<DwmSetWindowAttributeFn>(
        ::GetProcAddress(dwmapi, "DwmSetWindowAttribute"));

    bool applied = false;
    if (setAttribute) {
        const BOOL dark = TRUE;
        // 20 = DWMWA_USE_IMMERSIVE_DARK_MODE（Windows 10 20H1 / Windows 11）
        if (SUCCEEDED(setAttribute(hwnd, 20, &dark, sizeof(dark)))) {
            applied = true;
        }
        // 19 = 同一属性在早期 Windows 10 1809~1903 上的编号，作为兜底
        if (!applied && SUCCEEDED(setAttribute(hwnd, 19, &dark, sizeof(dark)))) {
            applied = true;
        }
    }

    ::FreeLibrary(dwmapi);
    return applied;
}
#endif

} // namespace

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent),
      m_deviceList(nullptr),
      m_deviceCaption(nullptr),
      m_toggleButton(nullptr),
      m_frameLabel(nullptr),
      m_stateLabel(nullptr),
      m_deviceNameLabel(nullptr),
      m_resolutionLabel(nullptr),
      m_fpsLabel(nullptr),
      m_capture(nullptr),
      m_state(State::Idle),
      m_frameCount(0) {
    setupUi();
    scanDevices();

#ifdef Q_OS_WIN
    // 提前创建原生句柄并设置深色标题栏，
    // 使窗口首次显示时标题栏已是深色，避免“先白后黑”的闪烁
    applyDarkTitleBar(reinterpret_cast<HWND>(winId()));
#endif
}

MainWindow::~MainWindow() {
    if (m_capture) {
        QObject::disconnect(m_capture, nullptr, this, nullptr);
        const bool stopped = m_capture->stop(2000);
        if (stopped) {
            delete m_capture;
        } else {
            // 极端情况：线程仍阻塞在设备读帧，避免在运行中析构 QThread，
            // 交由进程退出时回收
            qWarning() << "MainWindow: 采集线程未能在超时内退出";
        }
        m_capture = nullptr;
    }
}

void MainWindow::setupUi() {
    setWindowTitle(QStringLiteral("摄像头实时预览 - Qt 上位机"));
    resize(1080, 680);
    setMinimumSize(940, 560);

    auto* central = new QWidget(this);
    auto* rootLayout = new QHBoxLayout(central);
    rootLayout->setContentsMargins(10, 10, 10, 10);
    rootLayout->setSpacing(10);

    // ---------- 左侧控制面板 ----------
    auto* panel = new QWidget(central);
    panel->setFixedWidth(270);
    auto* panelLayout = new QVBoxLayout(panel);
    panelLayout->setContentsMargins(0, 0, 0, 0);
    panelLayout->setSpacing(10);

    auto* deviceGroup = new QGroupBox(QStringLiteral("设备选择"), panel);
    auto* deviceLayout = new QVBoxLayout(deviceGroup);
    deviceLayout->setSpacing(8);

    // 设备列表：一次列出全部可用设备，替代只能显示一项的下拉框，
    // 让“有几个设备、当前选中哪个”一眼可见
    m_deviceCaption = new QLabel(QStringLiteral("可用视频设备："), deviceGroup);
    m_deviceList = new QListWidget(deviceGroup);
    m_deviceList->setSelectionMode(QAbstractItemView::SingleSelection);
    m_deviceList->setUniformItemSizes(true);
    m_deviceList->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_deviceList->setToolTip(QStringLiteral("点击选择要预览的设备"));

    m_toggleButton = new QPushButton(QStringLiteral("打开设备"), deviceGroup);
    m_toggleButton->setMinimumHeight(36);
    connect(m_toggleButton, &QPushButton::clicked, this, &MainWindow::onToggleClicked);

    deviceLayout->addWidget(m_deviceCaption);
    deviceLayout->addWidget(m_deviceList);
    deviceLayout->addWidget(m_toggleButton);

    auto* infoGroup = new QGroupBox(QStringLiteral("设备信息"), panel);
    auto* infoLayout = new QVBoxLayout(infoGroup);
    infoLayout->setSpacing(6);

    m_stateLabel      = new QLabel(QStringLiteral("状态：未连接"), infoGroup);
    m_deviceNameLabel = new QLabel(QStringLiteral("设备：--"), infoGroup);
    m_resolutionLabel = new QLabel(QStringLiteral("分辨率：--"), infoGroup);
    m_fpsLabel        = new QLabel(QStringLiteral("帧率：--"), infoGroup);

    m_deviceNameLabel->setWordWrap(true);

    infoLayout->addWidget(m_stateLabel);
    infoLayout->addWidget(m_deviceNameLabel);
    infoLayout->addWidget(m_resolutionLabel);
    infoLayout->addWidget(m_fpsLabel);

    panelLayout->addWidget(deviceGroup);
    panelLayout->addWidget(infoGroup);
    panelLayout->addStretch();

    // ---------- 右侧画面区域 ----------
    m_frameLabel = new QLabel(central);
    m_frameLabel->setObjectName(QStringLiteral("frameLabel"));
    m_frameLabel->setAlignment(Qt::AlignCenter);
    m_frameLabel->setMinimumSize(640, 480);
    m_frameLabel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    m_frameLabel->setStyleSheet(QStringLiteral(
        "QLabel#frameLabel { background-color: #0e1013; border: 1px solid #2a2e37; "
        "color: #6b7280; font-size: 15px; }"));
    m_frameLabel->setText(QStringLiteral("未打开设备"));

    rootLayout->addWidget(panel);
    rootLayout->addWidget(m_frameLabel, 1);
    setCentralWidget(central);

    statusBar()->showMessage(QStringLiteral("就绪"));
}

void MainWindow::scanDevices() {
    m_deviceList->clear();
    m_deviceList->setEnabled(false);
    statusBar()->showMessage(QStringLiteral("正在识别本机视频设备…"));

    // 枚举会逐个打开设备，放到窗口显示前完成，避免界面闪动
    QApplication::setOverrideCursor(Qt::WaitCursor);
    const QVector<CameraDevice> devices = CameraEnumerator::enumerate();
    QApplication::restoreOverrideCursor();

    for (const CameraDevice& device : devices) {
        auto* item = new QListWidgetItem(CameraEnumerator::displayName(device),
                                         m_deviceList);
        // 固定行高，保证样式表内边距生效后文字不被裁切，也便于按数量计算列表高度
        item->setSizeHint(QSize(0, 36));
        // UserRole 保存 OpenCV 索引，UserRole + 1 保存不含索引的纯名称
        item->setData(Qt::UserRole, device.id);
        item->setData(Qt::UserRole + 1, device.name);
    }

    if (!devices.isEmpty()) {
        m_deviceList->setCurrentRow(0);   // 默认选中第一个，便于直接点“打开设备”
    }
    updateDeviceListHeight();

    // 数量写进标题，明确告诉用户一共有几个设备可选
    m_deviceCaption->setText(devices.isEmpty()
                                 ? QStringLiteral("可用视频设备：")
                                 : QStringLiteral("可用视频设备（共 %1 个）：").arg(devices.size()));

    m_state = State::Idle;
    updateUiState();

    if (devices.isEmpty()) {
        statusBar()->showMessage(QStringLiteral("未检测到任何摄像头或采集卡"));
        m_deviceNameLabel->setText(QStringLiteral("设备：--"));
    } else {
        statusBar()->showMessage(QStringLiteral("检测到 %1 个视频设备").arg(devices.size()));
    }
}

// 按设备数量调整列表高度（最多显示 5 行，超出后出现滚动条），
// 避免只有 1 个设备时列表留出大片空白。
void MainWindow::updateDeviceListHeight() {
    int rowHeight = 34;   // sizeHintForRow 取不到时的兜底行高（与样式表内边距匹配）
    if (m_deviceList->count() > 0) {
        const int hint = m_deviceList->sizeHintForRow(0);
        if (hint > 0) rowHeight = hint;
    }

    const int rows = qBound(1, m_deviceList->count(), 5);
    // 上下各 1px 边框 + 各 4px 内边距（见 uitheme 中的 QListWidget 样式）+ 2px 余量，
    // 保证内容区高度足够放下整行，避免出现多余的滚动条
    constexpr int kVerticalChrome = 2 + 8 + 2;
    const int height = rows * rowHeight + kVerticalChrome;
    m_deviceList->setFixedHeight(height);
}

void MainWindow::updateUiState() {
    const bool hasDevice = m_deviceList->count() > 0;

    switch (m_state) {
    case State::Idle:
        m_toggleButton->setText(QStringLiteral("打开设备"));
        m_toggleButton->setEnabled(hasDevice);
        m_deviceList->setEnabled(hasDevice);
        m_stateLabel->setText(QStringLiteral("状态：未连接"));
        break;
    case State::Opening:
        m_toggleButton->setText(QStringLiteral("取消"));
        m_toggleButton->setEnabled(true);
        m_deviceList->setEnabled(false);
        m_stateLabel->setText(QStringLiteral("状态：正在打开"));
        break;
    case State::Streaming:
        m_toggleButton->setText(QStringLiteral("关闭设备"));
        m_toggleButton->setEnabled(true);
        m_deviceList->setEnabled(false);
        m_stateLabel->setText(QStringLiteral("状态：预览中"));
        break;
    case State::Closing:
        m_toggleButton->setText(QStringLiteral("关闭中…"));
        m_toggleButton->setEnabled(false);
        m_deviceList->setEnabled(false);
        m_stateLabel->setText(QStringLiteral("状态：正在关闭"));
        break;
    }
}

void MainWindow::onToggleClicked() {
    if (m_state == State::Idle) {
        QListWidgetItem* current = m_deviceList->currentItem();
        if (m_deviceList->count() <= 0 || !current) {
            statusBar()->showMessage(QStringLiteral("没有可选设备"));
            return;
        }

        const int deviceId = current->data(Qt::UserRole).toInt();
        const QString name = current->data(Qt::UserRole + 1).toString();
        m_currentDeviceName = name.isEmpty() ? current->text() : name;

        m_lastFrame = QImage();
        m_lastError.clear();

        // 无父对象：线程结束后由 onCaptureFinished 释放
        m_capture = new CaptureThread(deviceId);
        connect(m_capture, &CaptureThread::opened, this, &MainWindow::onCaptureOpened);
        connect(m_capture, &CaptureThread::frameReady, this, &MainWindow::onFrameReady);
        connect(m_capture, &CaptureThread::errorOccurred, this, &MainWindow::onCaptureError);
        connect(m_capture, &QThread::finished, this, &MainWindow::onCaptureFinished);

        m_state = State::Opening;
        updateUiState();
        statusBar()->showMessage(QStringLiteral("正在打开设备索引 %1…").arg(deviceId));
        m_capture->start();
    } else if (m_state == State::Opening || m_state == State::Streaming) {
        stopCapture();
    }
}

void MainWindow::stopCapture() {
    if (!m_capture) {
        m_state = State::Idle;
        updateUiState();
        return;
    }

    m_state = State::Closing;
    updateUiState();
    statusBar()->showMessage(QStringLiteral("正在关闭设备…"));

    // 关闭过程中不再接收画面与打开通知，避免刷新已关闭的画面
    disconnect(m_capture, &CaptureThread::frameReady, this, nullptr);
    disconnect(m_capture, &CaptureThread::opened, this, nullptr);
    disconnect(m_capture, &CaptureThread::errorOccurred, this, nullptr);

    m_capture->requestStop();   // 异步停止，线程结束后由 finished 信号清理
}

void MainWindow::onCaptureOpened(int width, int height) {
    if (m_state != State::Opening) return;   // 打开过程中被取消则忽略

    m_state = State::Streaming;
    m_deviceNameLabel->setText(QStringLiteral("设备：%1").arg(m_currentDeviceName));
    m_resolutionLabel->setText(QStringLiteral("分辨率：%1 × %2").arg(width).arg(height));
    m_fpsLabel->setText(QStringLiteral("帧率：--"));
    m_frameCount = 0;
    m_fpsClock.start();

    updateUiState();
    statusBar()->showMessage(QStringLiteral("正在显示设备 %1 的实时画面").arg(m_currentDeviceName));
}

void MainWindow::onFrameReady(const QImage& image) {
    if (image.isNull()) return;
    if (m_state != State::Streaming) return;   // 丢弃关闭过程中的残留帧

    m_lastFrame = image;
    updateFrameView();

    ++m_frameCount;
    const qint64 elapsed = m_fpsClock.elapsed();
    if (elapsed >= 1000) {
        const double fps = static_cast<double>(m_frameCount) * 1000.0 /
                           static_cast<double>(elapsed);
        m_fpsLabel->setText(QStringLiteral("帧率：%1 FPS").arg(fps, 0, 'f', 1));
        m_frameCount = 0;
        m_fpsClock.restart();
    }
}

void MainWindow::onCaptureError(const QString& message) {
    m_lastError = message;
    statusBar()->showMessage(message);
}

void MainWindow::onCaptureFinished() {
    if (!m_capture) return;

    m_capture->deleteLater();
    m_capture = nullptr;

    const QString error = m_lastError;
    m_lastError.clear();
    const bool wasClosing = (m_state == State::Closing);

    if (m_state != State::Idle) {
        m_state = State::Idle;
        clearFrame();
        updateUiState();
    }

    if (wasClosing) {
        statusBar()->showMessage(QStringLiteral("设备已关闭"));
    } else if (!error.isEmpty()) {
        statusBar()->showMessage(error);
    }
}

void MainWindow::updateFrameView() {
    if (m_lastFrame.isNull()) return;

    const QSize target = m_frameLabel->size();
    if (target.isEmpty()) return;

    // 等比缩放，保持画面不变形
    const QPixmap pixmap = QPixmap::fromImage(m_lastFrame)
                               .scaled(target, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    m_frameLabel->setPixmap(pixmap);
}

void MainWindow::clearFrame() {
    m_lastFrame = QImage();
    m_frameLabel->clear();
    m_frameLabel->setText(QStringLiteral("未打开设备"));

    m_deviceNameLabel->setText(QStringLiteral("设备：--"));
    m_resolutionLabel->setText(QStringLiteral("分辨率：--"));
    m_fpsLabel->setText(QStringLiteral("帧率：--"));
    m_frameCount = 0;
}

void MainWindow::resizeEvent(QResizeEvent* event) {
    QMainWindow::resizeEvent(event);
    updateFrameView();
}

void MainWindow::showEvent(QShowEvent* event) {
    QMainWindow::showEvent(event);
#ifdef Q_OS_WIN
    // 句柄可能在显示阶段被重建，这里再应用一次（幂等），确保标题栏保持深色
    applyDarkTitleBar(reinterpret_cast<HWND>(winId()));
#endif
}

void MainWindow::closeEvent(QCloseEvent* event) {
    if (m_capture) {
        QObject::disconnect(m_capture, nullptr, this, nullptr);
        const bool stopped = m_capture->stop(2000);
        if (stopped) {
            delete m_capture;
        } else {
            // 线程仍阻塞在设备读帧：不析构运行中的 QThread，由进程退出回收
            qWarning() << "MainWindow: 关闭窗口时采集线程未能在超时内退出";
        }
        m_capture = nullptr;
        m_state = State::Idle;
    }
    event->accept();
}
