// ============================================================
//  mainwindow.h
//  主窗口：左侧设备选择 / 开关控制，右侧实时画面显示。
//  界面与采集业务分离，采集工作由 CaptureThread 完成。
// ============================================================

#pragma once

#include <QElapsedTimer>
#include <QImage>
#include <QMainWindow>
#include <QString>

class QCloseEvent;
class QComboBox;
class QLabel;
class QPushButton;
class QResizeEvent;
class CaptureThread;

// 摄像头实时预览主窗口
class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    // 构造函数：创建界面并扫描本机设备。
    // @param parent 父窗口
    explicit MainWindow(QWidget* parent = nullptr);

    // 析构函数：确保采集线程退出。
    ~MainWindow() override;

protected:
    // 窗口关闭：同步停止采集线程，避免退出时线程仍在运行。
    // @param event 关闭事件
    void closeEvent(QCloseEvent* event) override;

    // 窗口尺寸变化：按新尺寸重新缩放当前画面。
    // @param event 尺寸变化事件
    void resizeEvent(QResizeEvent* event) override;

private slots:
    // 打开 / 关闭按钮点击。
    void onToggleClicked();

    // 接收采集线程送来的画面。
    // @param image RGB888 帧图像
    void onFrameReady(const QImage& image);

    // 设备打开成功。
    // @param width  画面宽度
    // @param height 画面高度
    void onCaptureOpened(int width, int height);

    // 采集出错。
    // @param message 错误描述
    void onCaptureError(const QString& message);

    // 采集线程结束（正常关闭、打开失败或设备断开）。
    void onCaptureFinished();

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

    // 扫描本机设备并填充下拉框。
    void scanDevices();

    // 根据当前状态刷新按钮、下拉框与状态文本。
    void updateUiState();

    // 按标签当前尺寸缩放并显示最后一帧。
    void updateFrameView();

    // 清空画面与设备信息文本。
    void clearFrame();

    // 请求关闭采集（异步，等待线程结束信号后清理）。
    void stopCapture();

    QComboBox*     m_deviceCombo;      ///< 设备下拉框
    QPushButton*   m_toggleButton;     ///< 打开 / 关闭按钮
    QLabel*        m_frameLabel;       ///< 画面显示区域
    QLabel*        m_stateLabel;       ///< 连接状态文本
    QLabel*        m_deviceNameLabel;  ///< 设备名称文本
    QLabel*        m_resolutionLabel;  ///< 分辨率文本
    QLabel*        m_fpsLabel;         ///< 帧率文本

    CaptureThread* m_capture;          ///< 当前采集线程，未打开时为空
    State          m_state;            ///< 当前状态
    QImage         m_lastFrame;        ///< 最近一帧画面
    QElapsedTimer  m_fpsClock;         ///< 帧率统计时钟
    qint64         m_frameCount;       ///< 统计周期内的帧数
    QString        m_currentDeviceName;///< 当前设备名称
    QString        m_lastError;        ///< 最近一次错误信息
};
