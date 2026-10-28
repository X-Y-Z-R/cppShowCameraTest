// ============================================================
//  cameradevice.h
//  设备枚举模块：识别本机摄像头 / 采集卡的 OpenCV 索引与友好名称。
// ============================================================

#pragma once

#include <QString>
#include <QVector>

// 视频采集设备信息
struct CameraDevice {
    int     id   = -1;  ///< OpenCV 设备索引
    QString name;       ///< 设备友好名称（UTF-8 解码后），可能为空
};

// 视频采集设备枚举
namespace CameraEnumerator {

// 识别本机所有可用的视频采集设备（摄像头 / 采集卡）。
// 依次探测 0..N 索引，连续 3 次打开失败即认为枚举结束，并附带 DirectShow 友好名称。
// @return 设备列表（含索引与名称），无设备时为空
QVector<CameraDevice> enumerate();

// 生成设备列表中显示的设备文本。
// @param device 设备信息
// @return 形如 "[0] USB Camera" 的显示文本，名称为空时显示 "(未知设备)"
QString displayName(const CameraDevice& device);

} // namespace CameraEnumerator
