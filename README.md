# cppShowCameraTest

Windows 摄像头 / 采集卡枚举、实时预览与示例视觉检测 Demo：**C++17 + Qt + OpenCV + DirectShow**。

演示流程：枚举设备 → 列表选择 → 打开预览 → 检测框 → OK / NG 计数 → 状态栏性能数据。

> 30 秒演示视频：**（待录制）**，录制脚本见 [docs/demo-recording.md](docs/demo-recording.md)。

## 功能

- **设备枚举**：DirectShow 读取友好名称，逐个索引探测，连续 3 次失败停止。
- **Qt 界面**：左侧设备列表 / 开关 / 检测选项，右侧实时画面，中文不乱码。
- **三线程流水线**：采集 → 有界帧队列（容量 4，满丢最旧）→ 处理 → 显示队列 → 界面定时取帧。
- **断流重连**：读帧失败、3 秒无有效画面、画面长时间不变三类判定，按设备名重新定位并每秒重试。
- **示例检测**：Otsu 阈值 → 形态学去噪 → 轮廓 → 面积 / 长宽比 / 矩形度判定，绘制贴合工件的旋转矩形框。
- **OK / NG 计数**：IoU 跟踪，连续 5 帧才计数，丢失 10 帧后移除，避免重复累加。
- **性能监控**：状态栏显示采集 / 处理帧率、平均处理耗时、两个队列深度；可勾选 100 ms 模拟延时验证丢帧策略。
- **亮度极性**：默认浅色工件 / 深色背景；勾选「深色工件（浅色背景）」改用反向阈值。

## 环境

| 项目 | 要求 |
| --- | --- |
| 系统 | Windows 10 / 11 x64 |
| 编译器 | MSVC 2019 / 2022 / 2026（C++17） |
| 构建 | CMake 3.16+ |
| Qt | 5.15.x 或 6.x（Widgets） |
| OpenCV | 4.x（当前 4.12.0，`opencv_world4120.dll`） |

## 构建

1. 修改 `CMakeLists.txt` 中的 OpenCV 路径（普通 `set()`，命令行 `-DOpenCV_DIR` 会被覆盖）：

```cmake
set(OpenCV_DIR "D:/opencv/build/x64/vc16/lib")
```

2. 生成器必须与本机 Visual Studio 一致：

| VS | 生成器 |
| --- | --- |
| 2019 | `"Visual Studio 16 2019"` |
| 2022 | `"Visual Studio 17 2022"` |
| 2026 | `"Visual Studio 18 2026"` |

```bat
:: VS2026 + Qt 6.8.3
cmake -S . -B build-vs2026 -G "Visual Studio 18 2026" -A x64 -DCMAKE_PREFIX_PATH=F:/Qt/6.8.3/msvc2022_64
cmake --build build-vs2026 --config Release

:: VS2019 + Qt 5.15.2
cmake -S . -B build-vs2019 -G "Visual Studio 16 2019" -A x64 -DCMAKE_PREFIX_PATH=F:/Qt/5.15.2/msvc2019_64
cmake --build build-vs2019 --config Release
```

产物：`build-vs2026/Release/cppShowCameraTest.exe`。构建后自动执行 `windeployqt`，并在 OpenCV `bin` 存在时复制 `opencv_world*.dll`。

> 换 Qt 或生成器请使用新的构建目录；旧 `CMakeCache.txt` 里的路径不会因命令行参数改变。

## 使用

1. 启动后自动枚举设备，格式 `[索引] 名称`，默认选中第一项。
2. 选中目标设备，点击 **打开设备**。
3. 保持 **启用检测（OK / NG 计数）** 勾选：绿框 = OK，红框 = NG，左上角 `OK / NG / NOW` 为累计与当前目标数。
4. 深色工件（如浅色桌面上的手机）勾选 **深色工件（浅色背景）**，切换会清零计数。
5. **关闭设备** 释放摄像头；直接关窗口也会同步停止线程。

检测规则（参数在 `detector.cpp` 顶部）：

- 候选：面积 ≥ 画面 1%、短边 ≥ 画面短边 2.5%、长宽比 ≤ 6、矩形度 ≥ 0.75；
- OK：面积 1%~60%、长宽比 1~4、矩形度 ≥ 0.85；
- 工件需完整入镜、不贴边，背景尽量均匀；本 Demo 只适合规则长方形工件。

## 性能数据

测试环境：i7-9700 / Windows 10 22H2 / VS2026 + Qt 6.8.3 + OpenCV 4.12.0 / USB 摄像头 640×480。

| 指标 | 数值 | 位置 |
| --- | --- | --- |
| 采集分辨率 | 640 × 480（设备默认） | 设备信息 |
| 采集帧率 | 待实测（设计目标约 30 FPS） | 状态栏「采集」 |
| 处理帧率（检测开） | 待实测 | 状态栏「处理」 |
| 平均处理耗时 | 待实测 ms | 状态栏「耗时」 |
| 峰值处理耗时 | 待实测 ms | 设备信息 |
| 采集 / 显示队列 | 待实测（容量 4，正常 0~1） | 状态栏「队列」 |

复测：稳定运行 60 s，记录第 10 / 30 / 60 秒读数；关闭检测再测一次得到直通基线；勾选 100 ms 模拟延时应看到处理帧率约 10 FPS、队列 2~4 且画面不累积延迟。

## 目录

```text
main.cpp / mainwindow.*     程序入口与主窗口
capturethread.*             采集线程、断流重连
processorthread.*           处理线程、耗时统计
detector.*                  阈值检测、IoU 跟踪、OK/NG 绘制
framequeue.*                有界阻塞帧队列
cameradevice.*              DirectShow 设备枚举
uitheme.*                   主题与配色
docs/demo-recording.md      OBS 30 秒录制脚本
```

## 常见问题

- **无设备 / 打不开**：检查驱动、关闭占用摄像头的程序；索引探测上限在 `cameradevice.cpp` 的 `enumerate()`。
- **DSHOW 警告**：`can't be used to capture by index` 是逐索引探测的正常日志，设备列表正常即可忽略。
- **缺少 DLL**：把 OpenCV `bin` 加入 `PATH` 或复制到 exe 目录；Qt 缺失时手动运行 `windeployqt`。
- **检测不到**：确认工件与背景有明显亮度差、完整入镜不贴边；深色工件勾选极性开关。
- **红框误检**：手、衣服、反光可能形成近似矩形亮块；保持画面中只有工件，或调整候选阈值。
- **计数重复**：目标需稳定 5 帧；移动过快会新建跟踪，可提高 `kMatchIou` 或降低节流。

## 阶段与标签

| 标签 | 提交 | 内容 |
| --- | --- | --- |
| `v1.0.0` | `4f1dc80` | 设备枚举、OpenCV 预览 |
| `v2.0.0` | `a6d3a27` | Qt 界面、采集线程 |
| `v2.1.0` | `18dedb4` | UiTheme 主题 |
| `v2.1.1` | `d392186` | 设备列表、深色标题栏 |
| `v2.2.0` | `0d4b58a` | 三线程、有界队列、断流重连 |
| `v3.0.0-algo` | 阶段 3 | 检测框、OK/NG 计数、极性切换、性能数据 |

```bat
git tag -a v2.2.0 0d4b58a -m "阶段2：三线程采集/处理架构、断流重连、状态栏监控"
git tag -a v3.0.0-algo -m "阶段3：示例检测 + OK/NG 计数 + 处理耗时监控"
git push origin main --tags
```

发布：GitHub **Releases → Draft a new release** 选择 `v3.0.0-algo`，上传演示 mp4（视频不入库）。

## License

MIT
