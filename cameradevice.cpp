// ============================================================
//  cameradevice.cpp
//  设备枚举实现：DirectShow 读取友好名称 + OpenCV 逐索引探测。
// ============================================================

#include "cameradevice.h"

#include <QStringList>

#include <chrono>
#include <string>
#include <thread>

#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#include <dshow.h>
#pragma comment(lib, "ole32.lib")     // CoInitializeEx
#pragma comment(lib, "oleaut32.lib")  // VariantClear
#pragma comment(lib, "strmiids.lib")  // DirectShow CLSID / IID
#endif

#include <opencv2/videoio.hpp>

#ifdef _WIN32
// 通过 DirectShow 枚举设备友好名称，顺序与 OpenCV 的 DSHOW 后端一致。
// 名称读取失败的位置会补空字符串，保证下标与 OpenCV 设备索引对应。
// @return 设备名称列表（UTF-8），失败时返回空列表
static QStringList getDeviceFriendlyNames() {
    QStringList names;
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
                std::string n(len > 0 ? len - 1 : 0, '\0');
                if (len > 0) WideCharToMultiByte(CP_UTF8, 0, v.bstrVal, -1, &n[0], len, nullptr, nullptr);
                names.push_back(QString::fromUtf8(n.c_str(), static_cast<int>(n.size())));
            } else {
                names.push_back(QString());
            }
            VariantClear(&v);
            pBag->Release();
        } else {
            names.push_back(QString());
        }
        pMono->Release();
    }

    pList->Release();
    pEnum->Release();
    if (needUninit) CoUninitialize();
    return names;
}
#endif

QVector<CameraDevice> CameraEnumerator::enumerate() {
    QVector<CameraDevice> devices;
#ifdef _WIN32
    const QStringList names = getDeviceFriendlyNames();
#endif

    int miss = 0;                       // 连续打开失败次数
    for (int i = 0; i < 12; ++i) {
        cv::VideoCapture probe;
#ifdef _WIN32
        probe.open(i, cv::CAP_DSHOW);   // Windows 使用 DirectShow 后端，更稳定
#else
        probe.open(i);
#endif
        if (probe.isOpened()) {
            CameraDevice device;
            device.id = i;
#ifdef _WIN32
            if (i < static_cast<int>(names.size())) device.name = names.at(i);
#endif
            devices.push_back(device);
            miss = 0;
        } else if (++miss >= 3) {
            break;                      // 连续多个失败，认为没有更多设备
        }
        probe.release();
        std::this_thread::sleep_for(std::chrono::milliseconds(50)); // 确保设备被释放再探测下一个
    }
    return devices;
}

QString CameraEnumerator::displayName(const CameraDevice& device) {
    const QString name = device.name.isEmpty() ? QStringLiteral("(未知设备)") : device.name;
    return QStringLiteral("[%1] %2").arg(device.id).arg(name);
}
