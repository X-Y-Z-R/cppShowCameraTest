// ============================================================
//  detector.cpp
//  示例视觉检测器实现：阈值分割 → 轮廓 → 规格判定 → IoU 跟踪计数 → 绘制。
// ============================================================

#include "detector.h"

#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <string>

namespace {

// ---------------- 检测参数 ----------------
// 以下参数按演示场景（深色背景 + 单个浅色长方形工件）调优，
// 实际产线可替换为工艺规格（如尺寸公差、模板匹配、深度学习模型等）。
// 已知图纸尺寸 L×W 时，可把 kOkMaxAspect 直接设为 L/W 并留约 10% 公差。
constexpr double kMinAreaRatio     = 0.010;  ///< 候选轮廓最小面积占画面比例（1%，低于此值视为亮点/缝隙）
constexpr double kMaxAreaRatio     = 0.800;  ///< 候选轮廓最大面积占画面比例
constexpr double kOkMinAreaRatio   = 0.010;  ///< OK 判定：面积下限（画面占比）
constexpr double kOkMaxAreaRatio   = 0.600;  ///< OK 判定：面积上限（画面占比，允许工件近距离占满画面）
constexpr double kOkMinAspect      = 1.0;    ///< OK 判定：最长边 / 最短边 下限（始终 ≥ 1）
constexpr double kOkMaxAspect      = 4.0;    ///< OK 判定：最长边 / 最短边 上限（长方形可细长）
constexpr double kOkMinRectangularity = 0.85; ///< OK 判定：轮廓面积 / 最小外接矩形面积 下限
constexpr double kMinCandidateRectangularity = 0.75; ///< 候选矩形度下限：低于此值视为不规则亮块（衣袖、反光），不算工件
constexpr double kMaxCandidateAspect  = 6.0;  ///< 候选长宽比上限：更细长的亮缝/桌沿/线缆不当作工件
constexpr double kMinShortSideRatio   = 0.025;///< 候选短边至少占画面短边的比例（滤掉细线状反光）
constexpr int    kConfirmFrames    = 5;      ///< 连续出现多少帧后才确认并计数
constexpr int    kMaxMissedFrames  = 10;     ///< 连续丢失多少帧后移除跟踪目标
constexpr double kMatchIou         = 0.25;   ///< 前后帧目标关联的最小 IoU
constexpr int    kMaxDetections    = 16;     ///< 单帧最多保留的目标数（防止噪声刷屏）
constexpr int    kBorderMarginPx   = 1;      ///< 贴边目标判定余量（贴边目标可能只露出一半）

// 计算两个矩形框的交并比。
// @param a 矩形 A
// @param b 矩形 B
// @return IoU，范围 [0, 1]；任一矩形为空时返回 0
double iou(const cv::Rect& a, const cv::Rect& b) {
    const cv::Rect inter = a & b;
    if (inter.width <= 0 || inter.height <= 0) return 0.0;

    const double interArea = static_cast<double>(inter.area());
    const double unionArea = static_cast<double>(a.area()) +
                             static_cast<double>(b.area()) - interArea;
    return unionArea > 0.0 ? interArea / unionArea : 0.0;
}

// 判断目标是否超出画面边缘（只露出一部分时容易造成跟踪抖动）。
// @param box    目标包围框
// @param width  画面宽度
// @param height 画面高度
// @return true 表示目标接触画面边缘
bool touchesBorder(const cv::Rect& box, int width, int height) {
    return box.x <= kBorderMarginPx || box.y <= kBorderMarginPx ||
           box.x + box.width >= width - kBorderMarginPx ||
           box.y + box.height >= height - kBorderMarginPx;
}

} // namespace

ObjectDetector::ObjectDetector()
    : m_okTotal(0),
      m_ngTotal(0),
      m_brightObject(true) {
}

void ObjectDetector::setBrightObject(bool brightObject) {
    m_brightObject = brightObject;
}

void ObjectDetector::reset() {
    m_tracks.clear();
    m_okTotal = 0;
    m_ngTotal = 0;
}

QImage ObjectDetector::process(const QImage& frame, DetectionStats& stats) {
    if (frame.isNull()) {
        stats = DetectionStats{};
        stats.okTotal = m_okTotal;
        stats.ngTotal = m_ngTotal;
        return frame;
    }

    // 深拷贝一份：检测框直接画在副本上，不修改采集线程产出的原始帧
    QImage canvas = frame.convertToFormat(QImage::Format_RGB888).copy();

    // 用 QImage 内存直接构造 Mat（RGB 三通道），避免整帧再拷贝一次
    cv::Mat rgb(canvas.height(), canvas.width(), CV_8UC3, canvas.bits(),
                static_cast<size_t>(canvas.bytesPerLine()));

    const std::vector<Detection> detections = detect(rgb);
    stats.currentCount = updateTracks(detections, stats);
    draw(canvas, detections, stats);

    return canvas;
}

std::vector<Detection> ObjectDetector::detect(const cv::Mat& rgb) const {
    std::vector<Detection> result;
    if (rgb.empty()) return result;

    // 1) 灰度 + 去噪 + Otsu 二值化。
    //    浅色工件 / 深色背景用正向阈值；深色工件（如手机）放在浅色桌面上则用反向阈值。
    cv::Mat gray;
    cv::cvtColor(rgb, gray, cv::COLOR_RGB2GRAY);
    cv::GaussianBlur(gray, gray, cv::Size(5, 5), 0);

    const int thresholdType = m_brightObject
                                  ? (cv::THRESH_BINARY | cv::THRESH_OTSU)
                                  : (cv::THRESH_BINARY_INV | cv::THRESH_OTSU);
    cv::Mat binary;
    cv::threshold(gray, binary, 0, 255, thresholdType);

    // 2) 形态学开 / 闭运算：去掉小噪点，填补目标内部空洞
    const cv::Mat kernel = cv::getStructuringElement(cv::MORPH_ELLIPSE, cv::Size(5, 5));
    cv::morphologyEx(binary, binary, cv::MORPH_OPEN, kernel);
    cv::morphologyEx(binary, binary, cv::MORPH_CLOSE, kernel);

    // 3) 轮廓提取：只取外轮廓，足够做尺寸 / 位置演示
    std::vector<std::vector<cv::Point>> contours;
    cv::findContours(binary, contours, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);

    const double frameArea = static_cast<double>(rgb.cols) * static_cast<double>(rgb.rows);
    const double minArea = frameArea * kMinAreaRatio;
    const double maxArea = frameArea * kMaxAreaRatio;
    const double okMinArea = frameArea * kOkMinAreaRatio;
    const double okMaxArea = frameArea * kOkMaxAreaRatio;
    // 短边门槛：手与手机之间的亮缝、桌沿反光通常只有十几像素宽，不应算作工件
    const double minShortSide =
        static_cast<double>(std::min(rgb.cols, rgb.rows)) * kMinShortSideRatio;

    for (const auto& contour : contours) {
        const double area = cv::contourArea(contour);
        if (area < minArea || area > maxArea) continue;

        const cv::Rect box = cv::boundingRect(contour);
        if (box.width <= 0 || box.height <= 0) continue;
        // 贴边目标可能只露出一半，先跳过，避免计数在边界反复跳变
        if (touchesBorder(box, rgb.cols, rgb.rows)) continue;

        // 用最小外接旋转矩形做形状判定：对倾斜摆放的长方形同样稳定
        const cv::RotatedRect shape = cv::minAreaRect(contour);
        const double rectArea = static_cast<double>(shape.size.width) *
                                static_cast<double>(shape.size.height);
        if (rectArea <= 0.0) continue;

        const double longSide = std::max(shape.size.width, shape.size.height);
        const double shortSide = std::min(shape.size.width, shape.size.height);
        if (shortSide <= 0.0) continue;

        // 长宽比按“长边 / 短边”计算，与工件横放竖放无关
        const double aspect = static_cast<double>(longSide) /
                              static_cast<double>(shortSide);
        // 矩形度：真实长方形接近 1，圆形约 0.785，三角形约 0.5
        const double rectangularity = area / rectArea;

        // 候选门槛：细长亮缝（如手与手机之间的桌面反光）短边小、长宽比极大，
        // 既不是工件，也不应把它的抖动当成 NG 计数，直接丢弃。
        if (shortSide < minShortSide) continue;
        if (aspect > kMaxCandidateAspect) continue;
        // 候选必须先“像一个矩形”，否则任何不规则亮块都会被当作 NG；
        // 白衣服、手臂间的高光通常只有 0.6~0.7，圆片约 0.785，仍可进入 NG 判定
        if (rectangularity < kMinCandidateRectangularity) continue;

        Detection detection;
        detection.box = box;
        detection.shape = shape;
        // 4) 规格判定：面积、长宽比、矩形度都在范围内才算 OK，否则 NG
        detection.ok = area >= okMinArea && area <= okMaxArea &&
                       aspect >= kOkMinAspect && aspect <= kOkMaxAspect &&
                       rectangularity >= kOkMinRectangularity;
        result.push_back(detection);
    }

    // 目标较多时只保留面积最大的若干个，限制绘制与跟踪开销
    std::sort(result.begin(), result.end(),
              [](const Detection& a, const Detection& b) { return a.box.area() > b.box.area(); });
    if (static_cast<int>(result.size()) > kMaxDetections) {
        result.resize(kMaxDetections);
    }
    return result;
}

int ObjectDetector::updateTracks(const std::vector<Detection>& detections,
                                 DetectionStats& stats) {
    std::vector<bool> used(detections.size(), false);

    // 1) 已跟踪目标与当前检测做贪心 IoU 匹配
    for (Track& track : m_tracks) {
        int bestIndex = -1;
        double bestIou = kMatchIou;

        for (size_t i = 0; i < detections.size(); ++i) {
            if (used[i]) continue;
            const double value = iou(track.box, detections[i].box);
            if (value > bestIou) {
                bestIou = value;
                bestIndex = static_cast<int>(i);
            }
        }

        if (bestIndex >= 0) {
            used[bestIndex] = true;
            track.box = detections[bestIndex].box;
            track.ok = detections[bestIndex].ok;
            ++track.age;
            track.missed = 0;

            // 目标稳定出现足够帧数后才计入统计，避免噪点造成假计数
            if (!track.confirmed && track.age >= kConfirmFrames) {
                track.confirmed = true;
                if (track.ok) {
                    ++m_okTotal;
                } else {
                    ++m_ngTotal;
                }
            }
        } else {
            ++track.missed;
        }
    }

    // 2) 未匹配的检测作为新目标加入跟踪（先不计数，等稳定）
    for (size_t i = 0; i < detections.size(); ++i) {
        if (used[i]) continue;
        Track track;
        track.box = detections[i].box;
        track.ok = detections[i].ok;
        track.age = 1;
        track.missed = 0;
        track.confirmed = false;
        m_tracks.push_back(track);
    }

    // 3) 连续丢失超过阈值的目标移出跟踪列表（目标被拿走后允许再次计数）
    m_tracks.erase(std::remove_if(m_tracks.begin(), m_tracks.end(),
                                  [](const Track& track) {
                                      return track.missed > kMaxMissedFrames;
                                  }),
                   m_tracks.end());

    // 4) 当前画面中的目标数：本帧匹配到且已确认
    int activeConfirmed = 0;
    for (const Track& track : m_tracks) {
        if (track.confirmed && track.missed == 0) ++activeConfirmed;
    }

    stats.currentCount = activeConfirmed;
    stats.okTotal = m_okTotal;
    stats.ngTotal = m_ngTotal;
    return activeConfirmed;
}

void ObjectDetector::draw(QImage& canvas, const std::vector<Detection>& detections,
                          const DetectionStats& stats) {
    cv::Mat rgb(canvas.height(), canvas.width(), CV_8UC3, canvas.bits(),
                static_cast<size_t>(canvas.bytesPerLine()));

    // RGB 顺序：绿色表示 OK，红色表示 NG
    const cv::Scalar colorOk(40, 200, 90);
    const cv::Scalar colorNg(235, 70, 70);
    const cv::Scalar white(255, 255, 255);

    for (const Detection& detection : detections) {
        const cv::Scalar color = detection.ok ? colorOk : colorNg;

        // 画最小外接旋转矩形：长方形倾斜摆放时框仍贴合工件边缘
        if (detection.shape.size.width > 0.0f && detection.shape.size.height > 0.0f) {
            // 用 RotatedRect::points 直接取四个顶点：
            // boxPoints() 内部按 4×2 Mat 创建输出，不能写入 std::vector<Point2f>，
            // 否则会触发 _OutputArray::create 的 d == 2 断言并让进程终止。
            cv::Point2f corners[4];
            detection.shape.points(corners);

            std::vector<cv::Point> points;
            points.reserve(4);
            for (const cv::Point2f& corner : corners) {
                points.emplace_back(cvRound(corner.x), cvRound(corner.y));
            }
            cv::polylines(rgb, points, true, color, 2, cv::LINE_AA);
        } else {
            cv::rectangle(rgb, detection.box, color, 2);
        }

        const std::string label = detection.ok ? "OK" : "NG";
        int baseline = 0;
        const cv::Size textSize =
            cv::getTextSize(label, cv::FONT_HERSHEY_SIMPLEX, 0.6, 2, &baseline);

        // 标签底色块，避免文字落在明亮画面上看不清
        const cv::Rect labelBox(
            detection.box.x,
            std::max(0, detection.box.y - textSize.height - baseline - 8),
            textSize.width + 8,
            textSize.height + baseline + 8);
        cv::rectangle(rgb, labelBox, color, cv::FILLED);
        cv::putText(rgb, label,
                    cv::Point(labelBox.x + 4, labelBox.y + textSize.height + 2),
                    cv::FONT_HERSHEY_SIMPLEX, 0.6, white, 2, cv::LINE_AA);
    }

    // 左上角 HUD：累计 OK / NG 与当前目标数（英文避免 OpenCV 字体缺中文字形）
    const std::string hud = "OK " + std::to_string(stats.okTotal) +
                            "   NG " + std::to_string(stats.ngTotal) +
                            "   NOW " + std::to_string(stats.currentCount);
    cv::putText(rgb, hud, cv::Point(12, 30), cv::FONT_HERSHEY_SIMPLEX, 0.75, white, 2,
                cv::LINE_AA);
}
