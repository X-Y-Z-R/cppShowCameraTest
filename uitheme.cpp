// ============================================================
//  uitheme.cpp
//  现代化深色主题实现：Fusion 风格 + 深色调色板 + 全局样式表。
//  仅做外观设置，不涉及任何采集 / 业务逻辑。
// ============================================================

#include "uitheme.h"

#include <QApplication>
#include <QColor>
#include <QFont>
#include <QPalette>
#include <QStyle>
#include <QStyleFactory>
#include <QString>

namespace {

// ---------------- 主题配色 ----------------
const QColor kBackground(0x15, 0x17, 0x1C);   ///< 窗口背景
const QColor kSurface(0x1C, 0x1F, 0x26);      ///< 卡片 / 面板背景
const QColor kField(0x12, 0x15, 0x1A);        ///< 输入框背景
const QColor kBorder(0x2A, 0x2E, 0x37);       ///< 常规边框
const QColor kText(0xE6, 0xE8, 0xEB);         ///< 主文本
const QColor kMuted(0x9A, 0xA4, 0xB2);        ///< 次要文本
const QColor kDisabled(0x6B, 0x72, 0x80);     ///< 禁用文本
const QColor kAccent(0x3D, 0x8B, 0xFD);       ///< 强调色（按钮 / 选中）
const QColor kAccentHover(0x5F, 0xA2, 0xFF);  ///< 强调色悬停
const QColor kAccentPress(0x2F, 0x6F, 0xD0);  ///< 强调色按下
const QColor kOnAccent(0xFF, 0xFF, 0xFF);     ///< 强调色上的文字

// 全局样式表：圆角、边框、悬停 / 按下 / 禁用状态。
// 说明：颜色与上面调色板保持一致，避免两套配色冲突。
const char* const kStyleSheet = R"QSS(
QWidget {
    color: #e6e8eb;
}
QMainWindow, QDialog {
    background-color: #15171c;
}
QGroupBox {
    background-color: #1c1f26;
    border: 1px solid #2a2e37;
    border-radius: 10px;
    margin-top: 16px;
    padding: 12px 12px 14px 12px;
}
QGroupBox::title {
    subcontrol-origin: margin;
    subcontrol-position: top left;
    left: 14px;
    padding: 0 6px;
    color: #9aa4b2;
    font-weight: 600;
}
QLabel {
    background: transparent;
}
QComboBox, QLineEdit {
    background-color: #12151a;
    border: 1px solid #2a2e37;
    border-radius: 8px;
    padding: 7px 10px;
    selection-background-color: #3d8bfd;
    selection-color: #ffffff;
}
QComboBox:hover, QComboBox:focus, QLineEdit:focus {
    border-color: #3d8bfd;
}
QComboBox:disabled {
    color: #6b7280;
    border-color: #23262e;
}
QComboBox::drop-down {
    width: 24px;
    border: none;
}
QComboBox QAbstractItemView {
    background-color: #1c1f26;
    border: 1px solid #2a2e37;
    border-radius: 8px;
    padding: 4px;
    outline: 0;
    selection-background-color: #3d8bfd;
    selection-color: #ffffff;
}
QPushButton {
    background-color: #3d8bfd;
    color: #ffffff;
    border: none;
    border-radius: 8px;
    padding: 8px 16px;
    font-weight: 600;
}
QPushButton:hover {
    background-color: #5fa2ff;
}
QPushButton:pressed {
    background-color: #2f6fd0;
}
QPushButton:disabled {
    background-color: #262a33;
    color: #6b7280;
}
QStatusBar {
    background-color: #1c1f26;
    color: #9aa4b2;
    border-top: 1px solid #2a2e37;
}
QStatusBar::item {
    border: none;
}
QToolTip {
    background-color: #2a2e37;
    color: #e6e8eb;
    border: 1px solid #3d8bfd;
    border-radius: 6px;
    padding: 4px 6px;
}
QMenu {
    background-color: #1c1f26;
    border: 1px solid #2a2e37;
}
QMenu::item:selected {
    background-color: #3d8bfd;
    color: #ffffff;
}
)QSS";

// 构造深色调色板。
// @param baseStyle 目标风格对象，用于获取其基准调色板后再着色
// @return 深色调色板
QPalette makeDarkPalette(const QStyle* baseStyle) {
    QPalette palette = baseStyle ? baseStyle->standardPalette() : QPalette();
    palette.setColor(QPalette::Window, kBackground);
    palette.setColor(QPalette::WindowText, kText);
    palette.setColor(QPalette::Base, kField);
    palette.setColor(QPalette::AlternateBase, kSurface);
    palette.setColor(QPalette::Text, kText);
    palette.setColor(QPalette::PlaceholderText, kDisabled);
    palette.setColor(QPalette::Button, kSurface);
    palette.setColor(QPalette::ButtonText, kText);
    palette.setColor(QPalette::BrightText, QColor(0xFF, 0x6B, 0x6B));
    palette.setColor(QPalette::Link, kAccent);
    palette.setColor(QPalette::Highlight, kAccent);
    palette.setColor(QPalette::HighlightedText, kOnAccent);
    palette.setColor(QPalette::ToolTipBase, kBorder);
    palette.setColor(QPalette::ToolTipText, kText);

    // 派生色（明暗阶梯）同样压暗，避免个别控件仍使用 Fusion 默认的浅色描边
    palette.setColor(QPalette::Light, kBorder);
    palette.setColor(QPalette::Midlight, kBorder);
    palette.setColor(QPalette::Mid, kBorder);
    palette.setColor(QPalette::Dark, kBackground);
    palette.setColor(QPalette::Shadow, QColor(0x00, 0x00, 0x00));

    // 禁用态：整体压暗，避免禁用控件仍然抢眼
    palette.setColor(QPalette::Disabled, QPalette::WindowText, kDisabled);
    palette.setColor(QPalette::Disabled, QPalette::Text, kDisabled);
    palette.setColor(QPalette::Disabled, QPalette::ButtonText, kDisabled);
    palette.setColor(QPalette::Disabled, QPalette::Highlight, kSurface);
    palette.setColor(QPalette::Disabled, QPalette::HighlightedText, kDisabled);
    return palette;
}

} // namespace

void UiTheme::apply(QApplication& app) {
    // 1. 统一切换到 Fusion 风格：
    //    Windows 上 Qt 默认走 windowsvista（Qt5）/ windows11（Qt6，仅 Win11），
    //    外观完全依赖系统主题；Fusion 为自绘扁平风格，跨版本表现一致。
    if (QStyle* fusion = QStyleFactory::create(QStringLiteral("Fusion"))) {
        app.setStyle(fusion);
    }

    // 2. 深色调色板（需在 setStyle 之后设置，否则会被风格重置）
    app.setPalette(makeDarkPalette(app.style()));

    // 3. 字体：中文界面优先使用 Microsoft YaHei UI，缺失时由 Qt 自动回退
    QFont font = app.font();
    font.setFamily(QStringLiteral("Microsoft YaHei UI"));
    font.setPointSize(9);
    font.setStyleStrategy(QFont::PreferAntialias);
    app.setFont(font);

    // 4. 全局样式表
    app.setStyleSheet(QString::fromUtf8(kStyleSheet));
}
