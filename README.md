# cppShowCameraTest

> 基于 **C++17 + Qt + OpenCV + DirectShow** 的 Windows 摄像头 / 采集卡枚举与实时预览 Demo。
> 阶段 1 已将 OpenCV 原生窗口替换为 Qt 主窗口，采集逻辑仍由 OpenCV + DirectShow 完成。

- **平台**：Windows 10 / 11（x64）
- **依赖**：Qt 5.15 / Qt 6、OpenCV 4.x、CMake 3.16+、MSVC 2019 / 2022
- **形态**：Qt Widgets 主窗口 + 独立采集线程

## 简介

直接 `VideoCapture(0)` 写死索引有两个坑：一是索引和设备没有稳定对应关系，插拔后可能指到别的摄像头；
二是设备名含中文时，`putText` 和窗口标题都会乱码。

本项目把完整流程走一遍：**枚举设备 → 下拉框选择 → 打开/关闭 → 实时预览**。
界面使用 `QMainWindow`：左侧为设备列表与开关按钮，右侧为画面显示区，中文由 Qt 原生支持，无需 GDI 绘制。

## 功能特性

| 功能 | 说明 |
| --- | --- |
| 设备枚举 | 遍历索引 `0..11` 逐个 `open` 探测，连续 3 次失败即判定枚举结束 |
| 友好名称 | 通过 DirectShow `ICreateDevEnum` 读取 `FriendlyName`，顺序与 OpenCV 的 DSHOW 后端一致 |
| Qt 界面 | `QMainWindow` + 左侧 `QComboBox` / `QPushButton`，右侧 `QLabel` 显示画面 |
| 异步采集 | `CaptureThread` 在独立线程读帧，按 30ms 节流后通过信号槽送显，界面不阻塞 |
| 帧显示 | `cv::Mat` → BGR→RGB → `QImage::Format_RGB888`（深拷贝）→ `QPixmap` 等比缩放 |
| 状态统计 | 实时显示连接状态、设备名、分辨率与帧率 |
| 中文显示 | Qt 原生 UTF-8，设备名与界面文本不再乱码 |
| 界面主题 | `UiTheme` 统一 Fusion 风格 + 深色调色板 + 全局面板样式表，圆角卡片、强调色按钮，Qt 5.15 / Qt 6.x 外观一致 |

## 环境要求

| 项目 | 要求 |
| --- | --- |
| 操作系统 | Windows 10 / 11（x64） |
| 编译器 | MSVC 2019 / 2022，需支持 C++17 |
| 构建工具 | CMake 3.16 及以上 |
| Qt | 5.15.x 或 6.x（Widgets 模块），开发环境实测 5.15.2 / 6.8.3 |
| OpenCV | 4.x（开发环境为 4.1.2，对应 `opencv_world4120.dll`） |

> DirectShow 相关代码仅在 Windows 下生效；其他平台只有 OpenCV 基础回退分支，未做测试。

## 快速开始

### 1. 克隆仓库

```bat
git clone https://github.com/X-Y-Z-R/cppShowCameraTest.git
cd cppShowCameraTest
```

### 2. 配置 Qt 与 OpenCV 路径（必做）

- **OpenCV**：打开 `CMakeLists.txt`，把 `set(OpenCV_DIR ...)` 改成你本机 OpenCV 的 `lib` 目录：

```cmake
# 原：set(OpenCV_DIR "H:/opencv/build/x64/vc16/lib")
set(OpenCV_DIR "D:/opencv/build/x64/vc16/lib")
```

> ⚠️ 这里用的是普通 `set()`，会覆盖命令行传入的 `-DOpenCV_DIR`，所以**必须直接改这一行**，传参不生效。

- **Qt**：CMake 优先查找 Qt6，再查找 Qt5。若未自动找到，可设置环境变量 `Qt5_DIR` / `Qt6_DIR`，
  或配置时追加 `-DCMAKE_PREFIX_PATH=F:/Qt/5.15.2/msvc2019_64`（Qt6 则用 `F:/Qt/6.8.3/msvc2022_64`）。

> 切换 Qt 版本时请使用**新的构建目录**：`CMAKE_PREFIX_PATH` / `Qt5_DIR` 会写入 `CMakeCache.txt`，
> 在旧目录里改参数不会生效，界面也仍然是旧 Qt 的产物。

### 3. 构建

```bat
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```

若 `build/` 已存在其他生成器的缓存，换一个构建目录即可，例如 `-B build-vs2022`。

构建后会自动执行：

- `windeployqt` 部署 Qt 运行库到 exe 同级目录；
- 若 `H:/opencv/build/x64/vc16/bin` 存在，自动复制 `opencv_world*.dll`。

产物位置：

```text
build/Release/cppShowCameraTest.exe
```

### 4. 运行

```bat
cd build\Release
cppShowCameraTest.exe
```

## 使用说明

1. 启动后自动扫描本机设备，结果填入左侧下拉框（格式为 `[索引] 设备名`）；
2. 在下拉框中选择目标摄像头 / 采集卡；
3. 点击 **打开设备** 开始预览，右侧显示实时画面，信息栏显示分辨率与帧率；
4. 点击 **关闭设备** 停止预览，可重新选择其他设备；
5. 直接关闭窗口也会同步释放采集线程与设备。

## 实现要点

| 模块 | 说明 |
| --- | --- |
| 设备枚举 | `CameraEnumerator::enumerate()`：`ICreateDevEnum` 读名称 + 逐索引探测 |
| 探测策略 | 逐索引 `open`（Windows 固定 `CAP_DSHOW`），连续 3 次失败停止，避免 USB 设备漏检 |
| 采集线程 | `CaptureThread` 继承 `QThread`，`run()` 内完成打开、读帧、释放，避免跨线程使用 `VideoCapture` |
| 信号槽 | `opened` / `frameReady` / `errorOccurred` / `finished` 以队列连接回到界面线程 |
| 帧转换 | `cvtColor(BGR→RGB)` 后用 Mat 内存构造 `QImage`，立即 `copy()` 深拷贝，防止缓冲区复用导致撕裂 |
| 节流 | 目标帧间隔 30ms，高帧率设备也不会刷爆界面 |
| 关闭流程 | 按钮关闭为异步停止；窗口关闭时阻塞等待线程退出，确保设备被释放 |

## 目录结构

```text
cppShowCameraTest/
├── main.cpp            # 程序入口：QApplication + 主窗口
├── mainwindow.h/.cpp   # 主窗口：布局、状态机、帧显示
├── uitheme.h/.cpp      # 界面主题：Fusion 风格 + 深色调色板 + 全局样式表
├── capturethread.h/.cpp# 采集线程：打开设备、读帧、Mat→QImage
├── cameradevice.h/.cpp # 设备枚举：DirectShow 名称 + OpenCV 索引探测
├── CMakeLists.txt      # CMake 构建脚本（Qt + OpenCV + 运行库部署）
├── README.md
└── .gitignore          # 排除 build/、exe、dll 等构建产物
```

## 常见问题

### 提示"未检测到任何摄像头或采集卡"

- 确认设备管理器中有"图像设备"/"摄像头"，且驱动正常；
- 关闭可能占用摄像头的程序（会议软件、浏览器、其他预览工具）；
- 探测是"连续 3 次失败即停止"，若设备索引中间存在空档，后面的设备可能被提前截断，
  可把 `cameradevice.cpp` 里 `enumerate()` 的上限 `12` 调大后重新启动。

### 设备名出现乱码

- 源码与编译均使用 UTF-8（CMake 已为 MSVC 添加 `/utf-8`）；
- 设备名通过 `QString::fromUtf8` 转换，若系统返回的名称本身异常，请检查驱动。

### 打开设备失败或画面黑屏

- 同一时刻一个设备通常只能被一个进程占用，先关闭其他预览程序；
- Windows 下必须使用 `CAP_DSHOW` 后端，换成 `CAP_MSMF` 行为会不同；
- 部分采集卡不支持 `640x480`，`set()` 失败时可在 `capturethread.cpp` 中调整期望分辨率。

### 启动报错缺少 `opencv_world4120.dll` / Qt DLL

- OpenCV：把 `build/x64/vc16/bin/opencv_world4120.dll` 复制到 exe 同级目录，或修改 CMake 中的 `OpenCV_BIN_DIR`；
- Qt：确认构建日志中出现 `Deploying Qt runtime...`；若提示找不到 `windeployqt`，手动执行：

```bat
F:\Qt\5.15.2\msvc2019_64\bin\windeployqt.exe build\Release\cppShowCameraTest.exe
```

- 若还提示缺少 `vcruntime140.dll` / `msvcp140.dll`，安装对应版本的 VC++ 运行库。

### 界面风格偏旧，升级 Qt 版本能变好看吗

- Qt 5.15 与 Qt 6.x 在 Windows 10 上默认都使用 `windowsvista` 原生风格，升级 Qt **不会**改变控件外观；
- Qt 6.7+ 新增的 `windows11` 风格（`qmodernwindowsstyle` 插件）只在 Windows 11 生效，Win10 不会启用；
- 真正决定外观的是 `uitheme.cpp`：它强制 `Fusion` 风格并叠加深色调色板与样式表，因此该文件与 Qt 版本无关；
- 想换配色 / 圆角 / 强调色，只需改 `uitheme.cpp` 顶部的颜色常量与 `kStyleSheet` 字符串，无需重新选 Qt。

### 点击关闭设备后短时间无响应

- 采集线程可能正阻塞在 `cap.read()` 等待下一帧，通常在一个帧周期（约 33ms）内退出；
- 窗口关闭时的等待上限为 2 秒，超时会打印警告并放弃线程对象，避免退出崩溃。

## 当前范围

已实现：设备枚举与友好名称、Qt 下拉框选择、打开/关闭、单设备实时预览、分辨率与帧率统计、中文显示。

暂未实现：正式的多线程生产级改造（线程池 / 帧队列）、动态切换分辨率、录制、多设备同时预览、记住上次选择的设备。

## 版本记录

| 版本 | 内容 |
| --- | --- |
| v2.1.0 | 界面主题现代化：新增 `uitheme`，Fusion 风格 + 深色调色板 + 全局样式表，统一圆角与强调色 |
| v2.0.0 | 阶段 1：OpenCV 原生窗口替换为 Qt 界面，采集线程化，设备列表改用下拉框 |
| v1.0.0 | 首个版本：设备枚举、鼠标选择、实时预览 |

## License

MIT
