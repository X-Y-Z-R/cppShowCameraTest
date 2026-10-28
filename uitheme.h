// ============================================================
//  uitheme.h
//  全局界面主题：统一风格、调色板、字体与控件样式表。
//  与 Qt 版本解耦：Qt 5.15 与 Qt 6.x 均使用同一套 Fusion 现代化外观，
//  不受系统原生 Windows Vista 风格影响。
// ============================================================

#pragma once

class QApplication;

// 界面主题工具：在创建窗口前调用 apply()，对全局生效。
namespace UiTheme {

// 应用现代化深色主题。
// 说明：
//   1. 强制使用 Fusion 风格，取代 Qt 默认的 windowsvista / windows11 原生风格，
//      保证 Qt5 与 Qt6、Win10 与 Win11 下外观一致；
//   2. 深色 QPalette 覆盖菜单、提示框、滚动条等未被样式表覆盖的部分；
//   3. Qt 样式表统一圆角、边框、悬停与强调色。
// @param app Qt 应用对象，应在 QApplication 构造之后、主窗口显示之前调用
void apply(QApplication& app);

} // namespace UiTheme
