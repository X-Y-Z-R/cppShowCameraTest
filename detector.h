// ============================================================
//  detector.h
//  示例视觉检测器：阈值分割 + 轮廓提取 + 面积 / 长宽比判定 + 简单跟踪计数。
//  用于阶段 3 演示“检测框 + OK / NG 计数”，算法本身可按实际工艺替换。
// ============================================================

#pragma once

#include <QImage>

#include <opencv2/core.hpp>

#include <vector>

// 单帧中的一个检测目标。
struct Detection {
    cv::Rect        box;   ///< 轴对齐包围框（用于跟踪匹配与标签定位）
    cv::RotatedRect shape; ///< 最小外接旋转矩形（用于矩形度判定与贴合绘制）
    bool            ok;    ///< true 表示规格内（OK），false 表示超出规格（NG）
};

// 检测与计数的累计结果。
struct DetectionStats {
    int currentCount = 0;  ///< 当前画面中已确认的目标数
    int okTotal      = 0;  ///< 本次会话累计 OK 数
    int ngTotal      = 0;  ///< 本次会话累计 NG 数
};

// 示例视觉检测器。
// 处理流程：灰度 → 高斯模糊 → Otsu 阈值 → 形态学去噪 → 轮廓 → 矩形度 / 长宽比判定。
// 用 IoU 把同一目标在连续帧中的检出关联起来，目标稳定出现若干帧后才计数，
// 避免同一工件在每一帧都被重复累加。
class ObjectDetector {
public:
    // 构造函数：初始化跟踪状态与累计计数。
    ObjectDetector();

    // 处理一帧：检测、更新计数，并把检测框与计数 HUD 绘制到图像上。
    // @param frame 输入帧（任意 QImage 格式，内部统一转 RGB888）
    // @param stats 输出累计统计（当前目标数 / OK / NG）
    // @return 叠加检测结果后的新图像；输入帧不会被修改
    QImage process(const QImage& frame, DetectionStats& stats);

    // 重置跟踪状态与累计计数（开始新的采集会话时调用）。
    void reset();

    // 设置工件与背景的亮度关系，用于选择二值化的前景极性。
    // @param brightObject true 表示浅色工件 / 深色背景（默认，正向 Otsu）；
    //                     false 表示深色工件 / 浅色背景（反向 Otsu）
    void setBrightObject(bool brightObject);

private:
    // 跟踪中的目标。
    struct Track {
        cv::Rect box;        ///< 最近一次的包围框
        bool     ok;         ///< 最近一次的判定结果
        int      age;        ///< 连续被匹配到的帧数
        int      missed;     ///< 连续未匹配到的帧数
        bool     confirmed;  ///< 是否已计入累计统计
    };

    // 检测轮廓并做 OK / NG 判定。
    // @param rgb 输入 RGB 图像（CV_8UC3）
    // @return 当前帧的检测结果列表
    std::vector<Detection> detect(const cv::Mat& rgb) const;

    // 用 IoU 关联当前检测与已有跟踪，并更新累计计数。
    // @param detections 当前帧检测结果
    // @param stats      输出累计统计
    // @return 当前帧中已确认的目标数
    int updateTracks(const std::vector<Detection>& detections, DetectionStats& stats);

    // 把检测框与计数 HUD 绘制到图像上（仅用于显示，不参与判定）。
    // @param canvas     待绘制的 RGB888 图像
    // @param detections 当前帧检测结果
    // @param stats      当前累计统计
    static void draw(QImage& canvas, const std::vector<Detection>& detections,
                     const DetectionStats& stats);

    std::vector<Track> m_tracks;  ///< 跟踪目标列表
    int                m_okTotal; ///< 累计 OK 数
    int                m_ngTotal; ///< 累计 NG 数
    bool               m_brightObject; ///< true: 浅色工件/深色背景；false: 深色工件/浅色背景
};
