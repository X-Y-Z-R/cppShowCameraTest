
# cppShowCameraTest

一个基于 **C++17 + OpenCV + DirectShow** 的 Windows 摄像头 / 采集卡枚举与实时预览小工具。

## 功能

1. 自动识别本机已连接的所有视频采集设备（摄像头、USB 采集卡等），
   通过遍历设备索引 0..11 探测，连续 3 次打开失败即停止。

2. 弹出设备选择窗口，列出设备索引与 DirectShow 友好名称，鼠标点击即可选中。

3. 弹出实时画面窗口，持续显示所选设备的图像流；按 `ESC` 或 `q` 退出。

## 环境要求

- 操作系统：Windows 10 / 11

- 编译器：MSVC（Visual Studio 2019 / 2022，需支持 C++17）

- CMake：3.10 及以上

- OpenCV：4.x（开发时使用 4.1.2，`opencv_world4120.dll`）

## 构建

```bat
mkdir build && cd build
cmake .. -DOpenCV_DIR=H:/opencv/build/x64/vc16/lib
cmake --build . --config Release

```

> `-DOpenCV_DIR` 指向你自己的 OpenCV 安装目录（`build/x64/vc16/lib`）。

> 也可先设置环境变量 `OpenCV_DIR` 后直接 `cmake ..`。

> 运行前请把 `opencv_world4120.dll` 复制到可执行文件同目录。

## 使用

```bat
cppShowCameraTest.exe

```

- 控制台输出检测到的设备数量；

- 在选择窗口中点击目标设备；

- 在画面窗口中按 `ESC` / `q` 关闭。

## 实现要点

| 模块 | 说明 |
|---|---|
| 设备枚举 | `ICreateDevEnum` + `CLSID_VideoInputDeviceCategory` 读取 DirectShow `FriendlyName`，顺序与 OpenCV DSHOW 后端一致 |
| 中文显示 | OpenCV `putText` 不支持中文，改用 GDI `DrawTextW` 在 32 位 DIB 上绘制后写回 `Mat` |
| 窗口标题 | OpenCV 4.1.2 按本地代码页解析标题，UTF-8 需转 ANSI 后再传入 |
| 低延迟 | `CAP_PROP_BUFFERSIZE = 1` 最小化缓冲 |

## 目录结构
main.cpp 我想把这个demo作为1.0版本上传到github，我应该上传哪些文件？具体怎么做？

10:08

Chat

任务耗时 1m 27s

参考内容

已读取 1 个文件，浏览 2 个目录

思考

已浏览

cppShowCameraTest

已浏览

Camera

思考

已读取

CMakeLists.txt

思考

我先看了目录结构和 CMakeLists.txt ，这个 demo 的"源码"其实只有 2 个文件，其余全是 CMake 生成的构建产物。下面给你一份可直接照做的方案。

## 一、上传哪些文件（核心结论）
只上传"源码 + 工程描述 + 说明文档"，一共 4~5 个文件：

1
2
3
4
5
6
7

`cppShowCameraTest/            ← 仓库根目录
├── main.cpp                  ← 必须（唯一源码）
├── CMakeLists.txt            ← 必须（构建脚本）
├── README.md                 ← 必须（新建，项目说明）
├── .gitignore                ← 必须（新建，排除构建产物）
└── LICENSE                   ← 可选（建议 MIT）
`

### 绝对不要上传的内容
路径 原因 `build/` 整个目录 CMake 生成物：`CMakeCache.txt` 、`*.vcxproj` 、`*.slnx` 、`CMakeFiles/` 、`.tlog` 等，全是本机路径相关，别人拿到也跑不了，还极占体积 `build/Release/*.exe` 、`*.dll` 二进制产物（含`opencv_world4120.dll` 、`vcruntime140.dll` 等），体积大且需随 OpenCV 版本变化 `build/Release.rar` 打包产物，应该放到 GitHub Release 附件里，而不是 Git 历史里 `CMakeCache.txt` 里面写死了`H:/opencv/...` 本机路径 `.vs/` 、`.vscode/` IDE 缓存（你的`Camera` 根目录和`cpp1` 、`cpp` 里都有）

### ⚠️ 一个关键提醒：不要在 f:\BaiduNetdiskDownload\Camera 根目录执行 git init
你的上级目录里还混着`cpp/` 、`cpp1/` 、`json-3.11.3/` （第三方库）、`recordings/*.mp4` 、`.vs/` 等一大堆东西。在根目录初始化仓库会把这些全部推上去（尤其 nlohmann/json 整个源码树、mp4 录制文件）。 请只在`cppShowCameraTest` 目录内初始化仓库 ，或者把该目录单独复制到一个干净位置（如`f:\repos\cppShowCameraTest` ）再初始化。

## 二、新建两个必需文件
### 1. .gitignore
gitignore

1
2
3
4
5
6
7
8
9
10
11
12
13
14
15
16
17
18
19
20
21
22
23
24
25
26
27
28
29
30
31
32
33
34
35
36
37
38
39
40
41
42
43
44
45
46
47
48
49
50

`
# ---- CMake / 构建产物 ----
build/
out/
cmake-build-*/
CMakeCache.txt
CMakeFiles/
CMakeScripts/
cmake_install.cmake
install_manifest.txt
*.sln
*.slnx
*.vcxproj
*.vcxproj.filters
*.vcxproj.user
*.recipe
*.tlog
*.lastbuildstate

# ---- 编译中间件 / 可执行文件 ----
*.obj
*.o
*.pdb
*.ilk
*.exp
*.lib
*.exe
*.dll
*.so
*.dylib

# ---- 打包 / 媒体 ----
*.rar
*.zip
*.7z
*.mp4
*.avi
*.png
*.jpg

# ---- IDE ----
.vs/
.vscode/
*.suo
*.user

# ---- 系统 ----
Thumbs.db
.DS_Store
`

### 2. README.md
Markdown

1
2
3
4
5
6
7
8
9
10
11
12
13
14
15
16
17
18
19
20
21
22
23
24
25
26
27
28
29
30
31
32
33
34
35
36
37
38
39
40
41
42
43
44
45
46
47
48
49
50
51
52
53
54
55
56
57
58
59
60
61
62
63

`
# cppShowCameraTest

一个基于 **C++17 + OpenCV + DirectShow** 的 Windows 摄像头 / 采集卡枚举与实时预览小工具。

## 功能

1. 自动识别本机已连接的所有视频采集设备（摄像头、USB 采集卡等），
   通过遍历设备索引 0..11 探测，连续 3 次打开失败即停止。

2. 弹出设备选择窗口，列出设备索引与 DirectShow 友好名称，鼠标点击即可选中。

3. 弹出实时画面窗口，持续显示所选设备的图像流；按 `ESC` 或 `q` 退出。

## 环境要求

- 操作系统：Windows 10 / 11

- 编译器：MSVC（Visual Studio 2019 / 2022，需支持 C++17）

- CMake：3.10 及以上

- OpenCV：4.x（开发时使用 4.1.2，`opencv_world4120.dll`）

## 构建

```bat
mkdir build && cd build
cmake .. -DOpenCV_DIR=H:/opencv/build/x64/vc16/lib
cmake --build . --config Release

```

> `-DOpenCV_DIR` 指向你自己的 OpenCV 安装目录（`build/x64/vc16/lib`）。

> 也可先设置环境变量 `OpenCV_DIR` 后直接 `cmake ..`。

> 运行前请把 `opencv_world4120.dll` 复制到可执行文件同目录。

## 使用

```bat
cppShowCameraTest.exe

```

- 控制台输出检测到的设备数量；

- 在选择窗口中点击目标设备；

- 在画面窗口中按 `ESC` / `q` 关闭。

## 实现要点

| 模块 | 说明 |
|---|---|
| 设备枚举 | `ICreateDevEnum` + `CLSID_VideoInputDeviceCategory` 读取 DirectShow `FriendlyName`，顺序与 OpenCV DSHOW 后端一致 |
| 中文显示 | OpenCV `putText` 不支持中文，改用 GDI `DrawTextW` 在 32 位 DIB 上绘制后写回 `Mat` |
| 窗口标题 | OpenCV 4.1.2 按本地代码页解析标题，UTF-8 需转 ANSI 后再传入 |
| 低延迟 | `CAP_PROP_BUFFERSIZE = 1` 最小化缓冲 |

## 目录结构
.
├── main.cpp          # 全部实现（枚举 / 选择窗口 / 预览窗口）
├── CMakeLists.txt    # 构建脚本
└── README.md

## 版本

- **v1.0.0** — 首个版本：设备枚举、鼠标选择、实时预览。

## License

MIT
