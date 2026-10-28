// ============================================================
//  cppShowCameraTest
//  简述：
//    1. 识别本机已连接的摄像头 / 采集卡（遍历设备索引探测）
//    2. 弹出选择窗口，供用户点击选择设备
//    3. 弹出画面窗口，持续显示所选设备的实时图像流
//  使用说明：
//    - 在画面窗口中按 ESC 或 'q' 退出。
// ============================================================

#include <iostream>
#include <string>
#include <vector>
#include <thread>
#include <chrono>
#include <cstring>

#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#include <dshow.h>
#pragma comment(lib, "ole32.lib")     // CoInitializeEx
#pragma comment(lib, "strmiids.lib")  // DirectShow 设备枚举
#endif

#include <opencv2/videoio.hpp>
#include <opencv2/highgui.hpp>
#include <opencv2/imgproc.hpp>

using namespace cv;
using namespace std;

// ============================================================
//  0. 工具函数（Windows）
// ============================================================

#ifdef _WIN32
// UTF-8 转 UTF-16（宽字符），供 Win32 API / GDI 使用。
// @param s UTF-8 字符串
// @return 宽字符字符串
static wstring utf8ToWide(const string& s) {
    if (s.empty()) return wstring();
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
    if (n <= 0) return wstring();
    wstring w(n - 1, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, &w[0], n);
    return w;
}

// UTF-8 转本地代码页(ANSI/GBK)。OpenCV 4.1.2 的窗口标题按本地代码页解析，
// 直接传 UTF-8 会乱码，需先转换。
// @param s UTF-8 字符串
// @return 本地代码页字符串
static string utf8ToAnsi(const string& s) {
    if (s.empty()) return s;
    wstring w = utf8ToWide(s);
    int n = WideCharToMultiByte(CP_ACP, 0, w.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (n <= 0) return s;
    string a(n - 1, '\0');
    WideCharToMultiByte(CP_ACP, 0, w.c_str(), -1, &a[0], n, nullptr, nullptr);
    return a;
}

// 用 GDI 在图像上绘制 UTF-16 文本（OpenCV putText 不支持中文）。
// @param img   目标图像（BGR，就地修改）
// @param text  待绘制文本（宽字符）
// @param box   文本区域（左对齐、垂直居中）
// @param r,g,b 文本颜色（RGB 分量）
// @param px    字体像素高度
static void drawTextW(Mat& img, const wstring& text, Rect box, int r, int g, int b, int px = 18) {
    if (img.empty() || text.empty()) return;

    HDC screen = GetDC(NULL);
    HDC mem    = CreateCompatibleDC(screen);

    // 32 位 DIB，步长与 BGRA 图像一致，可整块拷贝
    BITMAPINFO bmi = {};
    bmi.bmiHeader.biSize        = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth       = img.cols;
    bmi.bmiHeader.biHeight      = -img.rows;   // 负值表示自顶向下
    bmi.bmiHeader.biPlanes      = 1;
    bmi.bmiHeader.biBitCount    = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    void* bits = nullptr;
    HBITMAP dib = CreateDIBSection(mem, &bmi, DIB_RGB_COLORS, &bits, NULL, 0);
    HGDIOBJ oldBmp = SelectObject(mem, dib);

    // 以图像当前内容为底，绘制后整块写回，未绘制区域保持不变
    Mat bgra;
    cvtColor(img, bgra, COLOR_BGR2BGRA);
    memcpy(bits, bgra.data, (size_t)img.cols * img.rows * 4);

    HFONT font = CreateFontW(-px, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                             DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                             CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Microsoft YaHei");
    HGDIOBJ oldFont = SelectObject(mem, font);
    SetBkMode(mem, TRANSPARENT);
    SetTextColor(mem, RGB(r, g, b));

    RECT rc = {box.x, box.y, box.x + box.width, box.y + box.height};
    DrawTextW(mem, text.c_str(), (int)text.size(), &rc,
              DT_SINGLELINE | DT_VCENTER | DT_LEFT | DT_END_ELLIPSIS);

    SelectObject(mem, oldFont);
    DeleteObject(font);
    memcpy(bgra.data, bits, (size_t)img.cols * img.rows * 4);
    cvtColor(bgra, img, COLOR_BGRA2BGR);

    SelectObject(mem, oldBmp);
    DeleteObject(dib);
    DeleteDC(mem);
    ReleaseDC(NULL, screen);
}
#endif

// ============================================================
//  1. 设备枚举
// ============================================================

// 视频设备信息
struct Device {
    int    id;      ///< OpenCV 设备索引
    string name;    ///< 设备友好名称（UTF-8，可能为空）
};

#ifdef _WIN32
// 通过 DirectShow 枚举设备友好名称，顺序与 OpenCV 的 DSHOW 后端一致。
// @return 设备名称列表（UTF-8），失败时返回空列表
static vector<string> getDeviceFriendlyNames() {
    vector<string> names;
    ICreateDevEnum* pEnum = nullptr;
    IEnumMoniker*   pList = nullptr;
    IMoniker*       pMono = nullptr;
    IPropertyBag*   pBag  = nullptr;

    HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    bool needUninit = SUCCEEDED(hr);
    if (FAILED(hr) && hr != RPC_E_CHANGED_MODE) return names; // 已被他人初始化则继续

    if (FAILED(CoCreateInstance(CLSID_SystemDeviceEnum, nullptr, CLSCTX_INPROC_SERVER,
                                IID_ICreateDevEnum, (void**)&pEnum)) ||
        FAILED(pEnum->CreateClassEnumerator(CLSID_VideoInputDeviceCategory, &pList, 0)) ||
        pList == nullptr) {
        if (pEnum) pEnum->Release();
        if (needUninit) CoUninitialize();
        return names;
    }

    ULONG fetched = 0;
    while (pList->Next(1, &pMono, &fetched) == S_OK) {
        if (SUCCEEDED(pMono->BindToStorage(nullptr, nullptr, IID_IPropertyBag, (void**)&pBag)) && pBag) {
            VARIANT v; VariantInit(&v);
            if (SUCCEEDED(pBag->Read(L"FriendlyName", &v, nullptr)) && v.vt == VT_BSTR) {
                int len = WideCharToMultiByte(CP_UTF8, 0, v.bstrVal, -1, nullptr, 0, nullptr, nullptr);
                string n(len > 0 ? len - 1 : 0, '\0');
                if (len > 0) WideCharToMultiByte(CP_UTF8, 0, v.bstrVal, -1, &n[0], len, nullptr, nullptr);
                names.push_back(n);
            } else {
                names.push_back("");
            }
            VariantClear(&v);
            pBag->Release();
        } else {
            names.push_back("");
        }
        pMono->Release();
    }

    pList->Release();
    pEnum->Release();
    if (needUninit) CoUninitialize();
    return names;
}
#endif

// 识别本机所有可用的视频采集设备（摄像头/采集卡）。
// 依次探测 0..N 索引，连续 3 次打开失败即认为枚举结束，并附带 DirectShow 友好名称。
// @return 设备列表（含索引与名称）
static vector<Device> listDevices() {
    vector<Device> devices;
#ifdef _WIN32
    vector<string> names = getDeviceFriendlyNames();
#endif
    int miss = 0;                       // 连续打开失败次数
    for (int i = 0; i < 12; ++i) {
        VideoCapture probe;
#ifdef _WIN32
        probe.open(i, CAP_DSHOW);       // Windows 使用 DirectShow 后端，更稳定
#else
        probe.open(i);
#endif
        if (probe.isOpened()) {
            Device d;
            d.id = i;
#ifdef _WIN32
            if (i < (int)names.size()) d.name = names[i];
#endif
            devices.push_back(d);
            miss = 0;
        } else if (++miss >= 3) {
            break;                      // 连续多个失败，认为没有更多设备
        }
        probe.release();
        this_thread::sleep_for(chrono::milliseconds(50)); // 确保设备被释放再探测下一个
    }
    return devices;
}

// ============================================================
//  2. 设备选择窗口（鼠标点击）
// ============================================================

namespace {
    bool    g_clicked  = false;    // 是否已点击确定
    int     g_hover    = -1;       // 当前悬停的行索引
    int     g_chosen   = -1;       // 被点击选中的行索引
    vector<Rect> g_rows;           // 每个选项在窗口上的矩形区域
}

// 鼠标回调：更新悬停高亮，并在点击时记录选中的行。
static void onMouse(int event, int x, int y, int, void*) {
    const Point p(x, y);
    if (event == EVENT_MOUSEMOVE) {
        g_hover = -1;
        for (int i = 0; i < (int)g_rows.size(); ++i)
            if (g_rows[i].contains(p)) { g_hover = i; break; }
    } else if (event == EVENT_LBUTTONDOWN && g_hover >= 0) {
        g_chosen = g_hover;
        g_clicked = true;
    }
}

// 弹出设备选择窗口，等待用户点击其中一项。
// @param devs 可用设备列表
// @param out  输出：被选中设备在 devs 中的下标
// @return true 已选择；false 用户取消(ESC/q)或无可选设备
static bool pickDevice(const vector<Device>& devs, int& out) {
    if (devs.empty()) return false;

    g_rows.clear();
    g_clicked = false;
    g_hover   = -1;

    // 预计算每个选项的矩形区域
    const int W = 520, rowH = 56;
    for (size_t i = 0; i < devs.size(); ++i)
        g_rows.emplace_back(16, 70 + (int)i * rowH + 4, W - 32, rowH - 8);

    const string win = utf8ToAnsi("请选择摄像头设备");   // 标题需转本地代码页，否则乱码
    namedWindow(win, WINDOW_NORMAL);
    resizeWindow(win, W, 78 + (int)devs.size() * rowH);
    setMouseCallback(win, onMouse);

    // 初始背景：深色统一背景，便于观察
    Mat menu(78 + (int)devs.size() * rowH, W, CV_8UC3, Scalar(45, 45, 45));

    while (!g_clicked) {
        Mat disp = menu.clone();
        putText(disp, string("Detected ") + to_string(devs.size())
                     + " video device(s), click to select:",
                Point(16, 40), FONT_HERSHEY_SIMPLEX, 0.55, Scalar(230, 230, 230), 1, LINE_AA);

        // 逐行绘制选项：编号 + 设备名称（名称可能含中文，用 GDI 绘制）
        for (size_t i = 0; i < devs.size(); ++i) {
            Scalar c = (g_hover == (int)i) ? Scalar(80, 150, 255) : Scalar(70, 70, 70);
            rectangle(disp, g_rows[i], c, FILLED, LINE_AA);

            string label = "[" + to_string(devs[i].id) + "]  " +
                           (devs[i].name.empty() ? string("(unknown)") : devs[i].name);
            Rect box(g_rows[i].x + 14, g_rows[i].y, g_rows[i].width - 28, g_rows[i].height);
#ifdef _WIN32
            drawTextW(disp, utf8ToWide(label), box, 255, 255, 255);
#else
            putText(disp, label, Point(box.x, g_rows[i].y + 34),
                    FONT_HERSHEY_SIMPLEX, 0.6, Scalar(255, 255, 255), 1, LINE_AA);
#endif
        }

        imshow(win, disp);
        int key = waitKey(30);
        if (key == 27 || key == 'q') { destroyWindow(win); return false; } // 取消
    }

    out = g_chosen;                                 // 返回选中设备的下标
    destroyWindow(win);
    return true;
}

// ============================================================
//  3. 实时画面窗口
// ============================================================

// 打开指定设备并弹出窗口持续显示实时画面。
// @param dev 目标设备
static void showStream(const Device& dev) {
    VideoCapture cap;
#ifdef _WIN32
    cap.open(dev.id, CAP_DSHOW);
#else
    cap.open(dev.id);
#endif
    if (!cap.isOpened()) {
        cerr << "无法打开设备索引 " << dev.id << endl;
        return;
    }

    cap.set(CAP_PROP_FRAME_WIDTH, 640);
    cap.set(CAP_PROP_FRAME_HEIGHT, 480);
    cap.set(CAP_PROP_BUFFERSIZE, 1);    // 最小缓冲，降低延迟

    // 标题含设备名，需转本地代码页，否则中文乱码
    string title = "摄像头实时画面 - " +
                   (dev.name.empty() ? ("Device " + to_string(dev.id)) : dev.name);
    const string win = utf8ToAnsi(title);
    namedWindow(win, WINDOW_NORMAL);

    Mat frame;
    cout << "开始显示设备 " << dev.id << " 画面，按 ESC 或 q 退出..." << endl;
    while (cap.read(frame) && !frame.empty()) {
        imshow(win, frame);
        int key = waitKey(30);
        if (key == 27 || key == 'q') break;
    }

    cap.release();
    destroyAllWindows();
}

// ============================================================
//  主程序
// ============================================================

int main() {
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);        // 控制台输出中文
#endif

    cout << "正在识别本机视频设备（摄像头 / 采集卡）..." << endl;
    vector<Device> devs = listDevices();

    if (devs.empty()) {
        cerr << "未检测到任何摄像头或采集卡！" << endl;
        return 1;
    }
    cout << "共检测到 " << devs.size() << " 个设备。" << endl;

    int sel = 0;
    if (!pickDevice(devs, sel)) {
        cout << "已取消选择。" << endl;
        return 0;
    }

    const Device& dev = devs[sel];      // 选中的设备
    cout << "已选择设备索引: " << dev.id << "，名称: " << dev.name << endl;
    showStream(dev);

    cout << "程序结束。" << endl;
    return 0;
}