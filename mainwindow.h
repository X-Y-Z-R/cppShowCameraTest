// ============================================================
//  mainwindow.h
//  主窗口：左侧设备选择 / 开关控制，右侧实时画面显示。
//  三线程模型：采集线程 → 处理线程 → 界面线程（QTimer 定时取帧）。
// ============================================================

#pragma once

#include <QImage>
#include <QMainWindow>
#include <QString>

#include <memory>

#include "framequeue.h"

class QCheckBox;
class QCloseEvent;
class QLabel;
class QListWidget;
class QPushButton;
class QResizeEvent;
class QShowEvent;
class QTimer;
class CaptureThread;
class ProcessThread;

// 摄像头实时预览主窗口
class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    // 构造函数：创建界面并扫描本机设备。
    // @param parent 父窗口
    explicit MainWindow(QWidget* parent = nullptr);

    // 析构函数：确保采集 / 处理线程退出。
    ~MainWindow() override;

protected:
    // 窗口关闭：同步停止采集与处理线程，避免退出时线程仍在运行。
    // @param event 关闭事件
    void closeEvent(QCloseEvent* event) override;

    // 窗口尺寸变化：按新尺寸重新缩放当前画面。
    // @param event 尺寸变化事件
    void resizeEvent(QResizeEvent* event) override;

    // 窗口显示：确保 Windows 深色标题栏在可见前已生效。
    // @param event 显示事件
    void showEvent(QShowEvent* event) override;

private slots:
    // 打开 / 关闭按钮点击。
    void onToggleClicked();

    // 设备打开成功（含断流重连成功）。
    // @param width  画面宽度
    // @param height 画面高度
    void onCaptureOpened(int width, int height);

    // 采集出错（终止性错误）。
    // @param message 错误描述
    void onCaptureError(const QString& message);

    // 采集线程运行日志（读帧失败、断流重连进度）。
    // @param message 日志文本
    void onCaptureLog(const QString& message);

    // 采集线程结束（正常关闭、打开失败或重连放弃）。
    void onCaptureFinished();

    // 处理线程结束。
    void onProcessorFinished();

    // 采集帧率更新（每秒一次）。
    // @param fps 每秒采集帧数
    void onCaptureStats(double fps);

    // 处理帧率更新（每秒一次）。
    // @param fps 每秒处理帧数
    void onProcessStats(double fps);

    // 界面刷新定时器：从显示队列取最新帧刷新画面，绝不阻塞。
    void onDisplayTick();

    // 状态监控定时器：刷新状态栏中的帧率与队列深度。
    void onStatsTick();

private:
    // 采集状态机
    enum class State {
        Idle,       ///< 未打开
        Opening,    ///< 正在打开
        Streaming,  ///< 预览中
        Closing     ///< 正在关闭
    };

    // 创建界面控件与布局。
    void setupUi();

    // 扫描本机设备并填充设备列表。
    void scanDevices();

    // 按设备数量调整列表高度（最多 5 行），避免少量设备时列表留大片空白。
    void updateDeviceListHeight();

    // 根据当前状态刷新按钮、设备列表与状态文本。
    void updateUiState();

    // 按标签当前尺寸缩放并显示最后一帧。
    void updateFrameView();

    // 清空画面与设备信息文本。
    void clearFrame();

    // 请求关闭采集与处理（异步，等待线程结束信号后清理）。
    void stopCapture();

    // 停止并回收处理线程（阻塞等待，仅用于窗口关闭 / 析构等同步场景）。
    void shutdownProcessor();

    // 刷新状态栏中的采集 / 处理帧率与队列深度。
    void updateStatsLabel();

    QListWidget*   m_deviceList;        ///< 可用设备列表（全部设备一次列出）
    QLabel*        m_deviceCaption;     ///< 设备数量提示
    QPushButton*   m_toggleButton;      ///< 打开 / 关闭按钮
    QCheckBox*     m_slowProcessCheck;  ///< 模拟处理耗时开关（验收用）
    QLabel*        m_frameLabel;        ///< 画面显示区域
    QLabel*        m_stateLabel;        ///< 连接状态文本
    QLabel*        m_deviceNameLabel;   ///< 设备名称文本
    QLabel*        m_resolutionLabel;   ///< 分辨率文本
    QLabel*        m_fpsLabel;          ///< 帧率文本
    QLabel*        m_statsLabel;        ///< 状态栏监控文本（帧率 + 队列深度）

    CaptureThread* m_capture;           ///< 当前采集线程，未打开时为空
    ProcessThread* m_processor;         ///< 当前处理线程，未打开时为空
    QTimer*        m_displayTimer;      ///< 界面刷帧定时器
    QTimer*        m_statsTimer;        ///< 状态监控定时器

    State          m_state;             ///< 当前状态
    QImage         m_lastFrame;         ///< 最近一帧画面
    double         m_captureFps;        ///< 最近一次采集帧率
    double         m_processFps;        ///< 最近一次处理帧率
    QString        m_currentDeviceName; ///< 当前设备名称
    QString        m_lastError;         ///< 最近一次终止性错误信息

    // 队列用 shared_ptr 持有：即使线程在极端情况下未能按时退出，
    // 队列对象也会随线程持有的引用一起存活，避免悬空指针
    std::shared_ptr<FrameQueue> m_captureQueue;  ///< 采集 → 处理 队列（有界，满丢最旧）
    std::shared_ptr<FrameQueue> m_displayQueue;  ///< 处理 → 显示 队列（有界，满丢最旧）
};
