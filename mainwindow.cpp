// ============================================================
//  mainwindow.cpp
//  主窗口实现：设备选择、打开 / 关闭、实时画面显示与状态统计。
// ============================================================

#include "mainwindow.h"

#include "cameradevice.h"
#include "capturethread.h"
#include "processorthread.h"

#include <QAbstractItemView>
#include <QApplication>
#include <QCheckBox>
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
#include <QTimer>
#include <QVBoxLayout>
#include <QWidget>
#include <QtGlobal>

#include <memory>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace {

// 三线程流水线相关常量
constexpr int kDisplayIntervalMs    = 33;   ///< 界面取帧刷新间隔（约 30 FPS）
constexpr int kStatsIntervalMs      = 1000; ///< 状态栏监控刷新间隔
constexpr int kSlowProcessDelayMs   = 100;  ///< 勾选“模拟处理耗时”时的每帧延时
constexpr int kQueueCapacity        = 4;    ///< 帧队列容量（满则丢最旧）
constexpr int kThreadStopTimeoutMs  = 3000; ///< 线程退出等待上限

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
      m_slowProcessCheck(nullptr),
      m_detectCheck(nullptr),
      m_darkObjectCheck(nullptr),
      m_frameLabel(nullptr),
      m_stateLabel(nullptr),
      m_deviceNameLabel(nullptr),
      m_resolutionLabel(nullptr),
      m_fpsLabel(nullptr),
      m_detectionLabel(nullptr),
      m_processTimeLabel(nullptr),
      m_statsLabel(nullptr),
      m_capture(nullptr),
      m_processor(nullptr),
      m_displayTimer(nullptr),
      m_statsTimer(nullptr),
      m_state(State::Idle),
      m_captureFps(0.0),
      m_processFps(0.0),
      m_avgProcessMs(0.0),
      m_maxProcessMs(0.0),
      m_okTotal(0),
      m_ngTotal(0),
      m_currentCount(0),
      m_captureQueue(std::make_shared<FrameQueue>(kQueueCapacity)),
      m_displayQueue(std::make_shared<FrameQueue>(kQueueCapacity)) {
    setupUi();
    scanDevices();

    // 界面刷帧定时器：处理线程的节奏再慢，界面也只按固定节拍取最新帧
    m_displayTimer = new QTimer(this);
    m_displayTimer->setInterval(kDisplayIntervalMs);
    connect(m_displayTimer, &QTimer::timeout, this, &MainWindow::onDisplayTick);

    // 心跳 / 状态监控定时器：刷新帧率与队列深度
    m_statsTimer = new QTimer(this);
    m_statsTimer->setInterval(kStatsIntervalMs);
    connect(m_statsTimer, &QTimer::timeout, this, &MainWindow::onStatsTick);

#ifdef Q_OS_WIN
    // 提前创建原生句柄并设置深色标题栏，
    // 使窗口首次显示时标题栏已是深色，避免“先白后黑”的闪烁
    applyDarkTitleBar(reinterpret_cast<HWND>(winId()));
#endif
}

MainWindow::~MainWindow() {
    // 先停采集（生产者），再停处理（消费者），队列已关闭可让等待立即返回
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

    m_captureQueue->close();
    m_displayQueue->close();
    shutdownProcessor();
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

    // 验收辅助：勾选后处理线程每帧额外 sleep 100ms，用于验证界面依旧流畅
    m_slowProcessCheck = new QCheckBox(QStringLiteral("模拟处理耗时 100 ms"), deviceGroup);
    m_slowProcessCheck->setToolTip(
        QStringLiteral("勾选后处理线程每帧额外等待 100ms，用于验证采集 / 处理 / 界面解耦效果"));
    connect(m_slowProcessCheck, &QCheckBox::toggled, this, [this](bool checked) {
        if (m_processor) {
            m_processor->setArtificialDelayMs(checked ? kSlowProcessDelayMs : 0);
        }
    });

    // 示例检测开关：默认开启，关闭后即为阶段 2 的纯预览直通模式
    m_detectCheck = new QCheckBox(QStringLiteral("启用检测（OK / NG 计数）"), deviceGroup);
    m_detectCheck->setChecked(true);
    m_detectCheck->setToolTip(
        QStringLiteral("开启后每帧执行阈值分割 + 轮廓检测，并统计 OK / NG 数量"));
    connect(m_detectCheck, &QCheckBox::toggled, this, [this](bool checked) {
        if (m_processor) {
            m_processor->setDetectionEnabled(checked);
        }
    });

    // 极性开关：默认关闭 = 浅色工件 / 深色背景；勾选后 = 深色工件 / 浅色背景（如浅色桌面上的手机）
    m_darkObjectCheck = new QCheckBox(QStringLiteral("深色工件（浅色背景）"), deviceGroup);
    m_darkObjectCheck->setToolTip(
        QStringLiteral("勾选后改用反向阈值，检测比背景暗的长方形工件；切换时会清零计数"));
    connect(m_darkObjectCheck, &QCheckBox::toggled, this, [this](bool checked) {
        if (m_processor) {
            m_processor->setBrightObject(!checked);
        }
    });

    deviceLayout->addWidget(m_deviceCaption);
    deviceLayout->addWidget(m_deviceList);
    deviceLayout->addWidget(m_toggleButton);
    deviceLayout->addWidget(m_slowProcessCheck);
    deviceLayout->addWidget(m_detectCheck);
    deviceLayout->addWidget(m_darkObjectCheck);

    auto* infoGroup = new QGroupBox(QStringLiteral("设备信息"), panel);
    auto* infoLayout = new QVBoxLayout(infoGroup);
    infoLayout->setSpacing(6);

    m_stateLabel        = new QLabel(QStringLiteral("状态：未连接"), infoGroup);
    m_deviceNameLabel   = new QLabel(QStringLiteral("设备：--"), infoGroup);
    m_resolutionLabel   = new QLabel(QStringLiteral("分辨率：--"), infoGroup);
    m_fpsLabel          = new QLabel(QStringLiteral("帧率：--"), infoGroup);
    m_detectionLabel    = new QLabel(QStringLiteral("检测：当前 0 │ OK 0 │ NG 0"), infoGroup);
    m_processTimeLabel  = new QLabel(QStringLiteral("处理耗时：--"), infoGroup);

    m_deviceNameLabel->setWordWrap(true);

    infoLayout->addWidget(m_stateLabel);
    infoLayout->addWidget(m_deviceNameLabel);
    infoLayout->addWidget(m_resolutionLabel);
    infoLayout->addWidget(m_fpsLabel);
    infoLayout->addWidget(m_detectionLabel);
    infoLayout->addWidget(m_processTimeLabel);

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

    // 状态栏常驻监控：采集帧率 / 处理帧率 / 处理耗时 / 两个队列的深度
    m_statsLabel = new QLabel(QStringLiteral("采集 -- │ 处理 -- │ 耗时 -- │ 队列 --"), this);
    m_statsLabel->setToolTip(
        QStringLiteral("采集帧率 │ 处理帧率 │ 平均单帧处理耗时 │ 采集队列 / 显示队列深度"));
    statusBar()->addPermanentWidget(m_statsLabel);

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
        if (m_processor) {
            // 上一轮处理线程尚未完全退出，等其结束再重新打开，避免线程对象重叠
            statusBar()->showMessage(QStringLiteral("上一路处理线程正在退出，请稍候…"));
            return;
        }

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
        m_captureFps = 0.0;
        m_processFps = 0.0;
        m_avgProcessMs = 0.0;
        m_maxProcessMs = 0.0;
        m_okTotal = 0;
        m_ngTotal = 0;
        m_currentCount = 0;
        m_detectionLabel->setText(QStringLiteral("检测：当前 0 │ OK 0 │ NG 0"));
        m_processTimeLabel->setText(QStringLiteral("处理耗时：--"));

        // 队列重新打开并清空上一轮的残留帧
        m_captureQueue->reopen();
        m_displayQueue->reopen();

        // 处理线程：从采集队列取帧 → 检测 / 计数 → 写入显示队列
        m_processor = new ProcessThread(m_captureQueue, m_displayQueue);
        m_processor->setArtificialDelayMs(m_slowProcessCheck->isChecked()
                                              ? kSlowProcessDelayMs
                                              : 0);
        m_processor->setDetectionEnabled(m_detectCheck->isChecked());
        m_processor->setBrightObject(!m_darkObjectCheck->isChecked());
        connect(m_processor, &ProcessThread::statsUpdated, this, &MainWindow::onProcessStats);
        connect(m_processor, &ProcessThread::detectionUpdated, this,
                &MainWindow::onDetectionStats);
        connect(m_processor, &QThread::finished, this, &MainWindow::onProcessorFinished);

        // 采集线程：只负责读帧并写入采集队列；原始名称用于断流后重新定位设备索引
        m_capture = new CaptureThread(deviceId, name, m_captureQueue);
        connect(m_capture, &CaptureThread::opened, this, &MainWindow::onCaptureOpened);
        connect(m_capture, &CaptureThread::errorOccurred, this, &MainWindow::onCaptureError);
        connect(m_capture, &CaptureThread::logMessage, this, &MainWindow::onCaptureLog);
        connect(m_capture, &CaptureThread::statsUpdated, this, &MainWindow::onCaptureStats);
        connect(m_capture, &QThread::finished, this, &MainWindow::onCaptureFinished);

        m_state = State::Opening;
        updateUiState();
        statusBar()->showMessage(QStringLiteral("正在打开设备索引 %1…").arg(deviceId));

        // 先启动消费者再启动生产者，避免采集线程首帧入队时没有消费者
        m_processor->start();
        m_capture->start();
    } else if (m_state == State::Opening || m_state == State::Streaming) {
        stopCapture();
    }
}

void MainWindow::stopCapture() {
    if (!m_capture && !m_processor) {
        m_state = State::Idle;
        updateUiState();
        return;
    }

    m_state = State::Closing;
    updateUiState();
    statusBar()->showMessage(QStringLiteral("正在关闭设备…"));

    m_displayTimer->stop();
    m_statsTimer->stop();

    // 关闭过程中不再接收画面与打开通知
    if (m_capture) {
        disconnect(m_capture, &CaptureThread::opened, this, nullptr);
        disconnect(m_capture, &CaptureThread::errorOccurred, this, nullptr);
        disconnect(m_capture, &CaptureThread::logMessage, this, nullptr);
        disconnect(m_capture, &CaptureThread::statsUpdated, this, nullptr);
        m_capture->requestStop();   // 异步停止，线程结束后由 finished 信号清理
    }
    if (m_processor) {
        disconnect(m_processor, &ProcessThread::statsUpdated, this, nullptr);
        disconnect(m_processor, &ProcessThread::detectionUpdated, this, nullptr);
        m_processor->requestStop();
    }

    // 关闭队列：唤醒阻塞在 pop 的消费者，并阻止采集线程继续入队
    m_captureQueue->close();
    m_displayQueue->close();
}

void MainWindow::onCaptureOpened(int width, int height) {
    // Opening：首次打开；Streaming：断流后重连成功，只需刷新分辨率
    if (m_state != State::Opening && m_state != State::Streaming) return;

    m_deviceNameLabel->setText(QStringLiteral("设备：%1").arg(m_currentDeviceName));
    m_resolutionLabel->setText(QStringLiteral("分辨率：%1 × %2").arg(width).arg(height));

    if (m_state == State::Streaming) {
        statusBar()->showMessage(QStringLiteral("设备断流后已重新连接，恢复预览"));
        return;
    }

    m_state = State::Streaming;
    m_fpsLabel->setText(QStringLiteral("帧率：--"));
    updateUiState();
    updateStatsLabel();
    m_displayTimer->start();
    m_statsTimer->start();
    statusBar()->showMessage(QStringLiteral("正在显示设备 %1 的实时画面").arg(m_currentDeviceName));
}

void MainWindow::onCaptureStats(double fps) {
    m_captureFps = fps;
    if (m_state == State::Streaming) {
        m_fpsLabel->setText(QStringLiteral("帧率：%1 FPS").arg(fps, 0, 'f', 1));
        updateStatsLabel();
    }
}

void MainWindow::onProcessStats(double fps, double avgProcessMs, double maxProcessMs) {
    m_processFps = fps;
    m_avgProcessMs = avgProcessMs;
    m_maxProcessMs = maxProcessMs;
    if (m_state == State::Streaming) {
        m_processTimeLabel->setText(QStringLiteral("耗时：%1 ms（峰值 %2 ms）")
                                        .arg(m_avgProcessMs, 0, 'f', 1)
                                        .arg(m_maxProcessMs, 0, 'f', 1));
        updateStatsLabel();
    }
}

void MainWindow::onDetectionStats(int okTotal, int ngTotal, int currentCount) {
    m_okTotal = okTotal;
    m_ngTotal = ngTotal;
    m_currentCount = currentCount;

    if (m_state != State::Streaming) return;

    if (!m_detectCheck->isChecked()) {
        m_detectionLabel->setText(QStringLiteral("检测：已关闭"));
        return;
    }
    m_detectionLabel->setText(QStringLiteral("检测：当前 %1 │ OK %2 │ NG %3")
                                  .arg(m_currentCount)
                                  .arg(m_okTotal)
                                  .arg(m_ngTotal));
}

void MainWindow::onCaptureLog(const QString& message) {
    statusBar()->showMessage(message);
}

void MainWindow::onDisplayTick() {
    if (m_state != State::Streaming) return;

    // 非阻塞取最新处理结果；取不到就保持上一帧，绝不卡住界面
    QImage frame;
    if (!m_displayQueue->takeLatest(frame) || frame.isNull()) return;

    m_lastFrame = frame;
    updateFrameView();
}

void MainWindow::onStatsTick() {
    if (m_state == State::Streaming || m_state == State::Opening) updateStatsLabel();
}

void MainWindow::updateStatsLabel() {
    m_statsLabel->setText(
        QStringLiteral("采集 %1 FPS │ 处理 %2 FPS │ 耗时 %3 ms │ 队列 %4 / %5")
            .arg(m_captureFps, 0, 'f', 1)
            .arg(m_processFps, 0, 'f', 1)
            .arg(m_avgProcessMs, 0, 'f', 1)
            .arg(m_captureQueue->size())
            .arg(m_displayQueue->size()));
}

void MainWindow::onCaptureError(const QString& message) {
    m_lastError = message;
    statusBar()->showMessage(message);
}

void MainWindow::onCaptureFinished() {
    if (!m_capture) return;

    m_capture->deleteLater();
    m_capture = nullptr;

    // 采集已结束：停止界面刷新与监控，关闭队列促使处理线程退出
    m_displayTimer->stop();
    m_statsTimer->stop();
    m_captureQueue->close();
    m_displayQueue->close();
    if (m_processor) {
        disconnect(m_processor, &ProcessThread::statsUpdated, this, nullptr);
        disconnect(m_processor, &ProcessThread::detectionUpdated, this, nullptr);
        m_processor->requestStop();
    }

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

void MainWindow::onProcessorFinished() {
    if (!m_processor) return;

    m_processor->deleteLater();
    m_processor = nullptr;
}

void MainWindow::shutdownProcessor() {
    if (!m_processor) return;

    QObject::disconnect(m_processor, nullptr, this, nullptr);
    if (m_processor->stop(kThreadStopTimeoutMs)) {
        delete m_processor;
    } else {
        // 与采集线程一致：不析构运行中的 QThread，交由进程退出回收
        qWarning() << "MainWindow: 处理线程未能在超时内退出";
    }
    m_processor = nullptr;
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
    m_detectionLabel->setText(QStringLiteral("检测：当前 0 │ OK 0 │ NG 0"));
    m_processTimeLabel->setText(QStringLiteral("处理耗时：--"));

    // 清空流水线残留数据与统计值
    m_captureQueue->clear();
    m_displayQueue->clear();
    m_captureFps = 0.0;
    m_processFps = 0.0;
    m_avgProcessMs = 0.0;
    m_maxProcessMs = 0.0;
    m_okTotal = 0;
    m_ngTotal = 0;
    m_currentCount = 0;
    m_statsLabel->setText(QStringLiteral("采集 -- │ 处理 -- │ 耗时 -- │ 队列 --"));
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
    m_displayTimer->stop();
    m_statsTimer->stop();

    // 先关队列再停采集，能让阻塞中的读帧 / 取帧尽快返回
    m_captureQueue->close();
    m_displayQueue->close();

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

    shutdownProcessor();
    event->accept();
}
