# cppShowCameraTest

> 基于 **C++17 + OpenCV + DirectShow** 的 Windows 摄像头 / 采集卡枚举与实时预览 Demo。

- **平台**：Windows 10 / 11（x64）
- **依赖**：OpenCV 4.x、CMake 3.10+、MSVC 2019 / 2022
- **形态**：单文件实现（`main.cpp`），约 370 行

## 简介

直接 `VideoCapture(0)` 写死索引有两个坑：一是索引和设备没有稳定对应关系，插拔后可能指到别的摄像头；
二是设备名含中文时，`putText` 和窗口标题都会乱码。

本项目用一个最小可运行的例子，把完整流程走一遍：**枚举设备 → 鼠标选择 → 实时预览**。

## 功能特性

| 功能 | 说明 |
| --- | --- |
| 设备枚举 | 遍历索引 `0..11` 逐个 `open` 探测，连续 3 次失败即判定枚举结束 |
| 友好名称 | 通过 DirectShow `ICreateDevEnum` 读取 `FriendlyName`，顺序与 OpenCV 的 DSHOW 后端一致 |
| 鼠标选择 | 自绘设备列表窗口，悬停高亮、单击选中，无需控制台输入索引 |
| 实时预览 | `CAP_PROP_BUFFERSIZE = 1` 最小化缓冲以降低延迟，按 `ESC` / `q` 退出 |
| 中文设备名 | OpenCV `putText` 不支持中文，改用 GDI `DrawTextW` 在 32 位 DIB 上绘制后写回 `Mat` |
| 中文窗口标题 | OpenCV 4.1.2 按本地代码页解析标题，UTF-8 需先转 ANSI 再传入 |

## 环境要求

| 项目 | 要求 |
| --- | --- |
| 操作系统 | Windows 10 / 11（x64） |
| 编译器 | MSVC 2019 / 2022，需支持 C++17 |
| 构建工具 | CMake 3.10 及以上 |
| OpenCV | 4.x（开发环境为 4.1.2，对应 `opencv_world4120.dll`） |

> DirectShow / GDI 相关代码仅在 Windows 下生效；其他平台只有基础回退分支，未做测试。

## 快速开始

### 1. 克隆仓库

```bat
git clone https://github.com/X-Y-Z-R/cppShowCameraTest.git
cd cppShowCameraTest
```

### 2. 配置 OpenCV 路径（必做）

打开 `CMakeLists.txt`，把第 12 行改成你本机 OpenCV 的 `lib` 目录：

```cmake
# 原：set(OpenCV_DIR "H:/opencv/build/x64/vc16/lib")
set(OpenCV_DIR "D:/opencv/build/x64/vc16/lib")
```

> ⚠️ 这里用的是普通 `set()`，会覆盖命令行传入的 `-DOpenCV_DIR`，所以**必须直接改这一行**，传参不生效。

### 3. 构建

```bat
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```

若 `build/` 已存在其他生成器的缓存，换一个构建目录即可，例如 `-B build-vs2022`。

### 4. 准备运行环境

把 `opencv_world4120.dll` 复制到 exe 同级目录，否则会因找不到 DLL 而启动失败。
构建产物位置：

```text
build/Release/cppShowCameraTest.exe
```

### 5. 运行

```bat
cd build\Release
cppShowCameraTest.exe
```

## 使用说明

1. 启动后控制台打印检测到的设备数量；
2. 在**设备选择窗口**中鼠标点击目标设备（行格式为 `[索引] 设备名`，悬停高亮）；
3. 进入**实时画面窗口**后按 `ESC` 或 `q` 退出；
4. 在设备选择窗口按 `ESC` / `q` 视为取消，程序直接结束。

## 实现要点

| 模块 | 说明 |
| --- | --- |
| 设备枚举 | `ICreateDevEnum` + `CLSID_VideoInputDeviceCategory` 读取 `FriendlyName` |
| 探测策略 | 逐索引 `open`（Windows 固定 `CAP_DSHOW`），连续 3 次失败停止，避免 USB 设备漏检 |
| 中文绘制 | `CreateDIBSection` 32 位位图 + `DrawTextW`，绘回 `Mat` 后转回 BGR |
| 编码转换 | `utf8ToWide` / `utf8ToAnsi`，分别用于 GDI 绘制与窗口标题 |
| 低延迟 | `CAP_PROP_BUFFERSIZE = 1`，只保留最新一帧 |

## 目录结构

```text
cppShowCameraTest/
├── main.cpp          # 全部实现：设备枚举 / 选择窗口 / 实时预览
├── CMakeLists.txt    # CMake 构建脚本
├── README.md
└── .gitignore        # 排除 build/、exe、dll 等构建产物
```

## 常见问题

### 提示"未检测到任何摄像头或采集卡"

- 确认设备管理器中有"图像设备"/"摄像头"，且驱动正常；
- 关闭可能占用摄像头的程序（会议软件、浏览器、其他预览工具）；
- 探测是"连续 3 次失败即停止"，若设备索引中间存在空档，后面的设备可能被提前截断，
  可把 `listDevices()` 里的上限 `12` 调大。

### 设备名或窗口标题出现乱码

- 控制台依赖 `SetConsoleOutputCP(CP_UTF8)`，旧版控制台可能需要手动 `chcp 65001`；
- 窗口标题会经 `utf8ToAnsi` 转本地代码页，若系统区域设置不是中文（GBK），可能显示异常；
- 图片上的中文依赖 `Microsoft YaHei` 字体，精简版系统若缺字体需自行安装。

### 打开设备失败或画面黑屏

- 同一时刻一个设备通常只能被一个进程占用，先关闭其他预览程序；
- Windows 下必须使用 `CAP_DSHOW` 后端，换成 `CAP_MSMF` 行为会不同；
- 部分采集卡不支持 `640x480`，`set()` 失败时可注释掉分辨率设置再试。

### 启动报错缺少 `opencv_world4120.dll`

把 OpenCV 安装目录 `build/x64/vc16/bin` 下的 `opencv_world4120.dll` 复制到 exe 同级目录；
若还提示缺少 `vcruntime140.dll` / `msvcp140.dll`，安装对应版本的 VC++ 运行库。

## v1.0 范围

已实现：设备枚举与友好名称、鼠标选择窗口、单设备实时预览、中文显示。

暂未实现：动态切换分辨率、录制、多设备同时预览、记住上次选择的设备。

## 版本记录

| 版本 | 内容 |
| --- | --- |
| v1.0.0 | 首个版本：设备枚举、鼠标选择、实时预览 |

## License

MIT
