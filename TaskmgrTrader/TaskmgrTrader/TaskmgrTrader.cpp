#include <Windows.h>
#include <pdh.h>
#include <winuser.h>
#include <process.h>
#include <string>
#include <vector>
#include <mmsystem.h>
#include <opencv2/opencv.hpp>
#include <iomanip>
#include <shellapi.h>
#include <cmath>
#include <fstream>
#include <locale>
#include <algorithm>

#pragma comment(lib, "pdh.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "winmm.lib")

using namespace std;
using namespace cv;

namespace config {
    wstring WindowClassName = L"TaskManagerWindow";
    wstring WindowTitle = L"任务管理器";
    wstring ChildClassName = L"CvChartWindow";
    Vec3b colorEdge = Vec3b(236, 138, 116);
    Vec3b colorDark = Vec3b(182, 109, 93);
    Vec3b colorBright = Vec3b(25, 25, 25);
    Vec3b colorGrid = Vec3b(50, 50, 50);
    Vec3b colorFrame = Vec3b(50, 50, 50);
    bool DrawGrid = true;

    vector<wstring> split(const wstring& str, wchar_t delimiters) {
        vector<wstring> res;
        size_t l = 0, r = 0;
        for (; r <= str.size(); ++r) {
            if (r == str.size() || str[r] == delimiters) {
                res.push_back(str.substr(l, r - l));
                l = r + 1;
            }
        }
        return res;
    }
};

struct KLine {
    double open, high, low, close;
    time_t timestamp;
};

struct Indicator {
    vector<double> ma5;
    vector<double> ma10;
    vector<double> macd;
};

class PerformanceMonitor {
private:
    PDH_HQUERY cpuQuery;
    PDH_HCOUNTER cpuCounter;
    PDH_HQUERY memQuery;
    PDH_HCOUNTER memCounter;
    vector<KLine> klines;
    Indicator indicators;
    double ema12 = 0, ema26 = 0;

public:
    PerformanceMonitor() : cpuQuery(NULL), cpuCounter(NULL), memQuery(NULL), memCounter(NULL) {
        PdhOpenQuery(NULL, 0, &cpuQuery);
        PdhAddEnglishCounter(cpuQuery, L"\\Processor(_Total)\\% Processor Time", 0, &cpuCounter);
        PdhCollectQueryData(cpuQuery);

        PdhOpenQuery(NULL, 0, &memQuery);
        PdhAddEnglishCounter(memQuery, L"\\Memory\\% Committed Bytes In Use", 0, &memCounter);
        PdhCollectQueryData(memQuery);
    }

    ~PerformanceMonitor() {
        if (cpuQuery) PdhCloseQuery(cpuQuery);
        if (memQuery) PdhCloseQuery(memQuery);
    }

    pair<double, double> GetUsage() {
        PDH_FMT_COUNTERVALUE cpuVal, memVal;
        PdhCollectQueryData(cpuQuery);
        PdhGetFormattedCounterValue(cpuCounter, PDH_FMT_DOUBLE, NULL, &cpuVal);
        PdhCollectQueryData(memQuery);
        PdhGetFormattedCounterValue(memCounter, PDH_FMT_DOUBLE, NULL, &memVal);
        return make_pair(cpuVal.doubleValue, memVal.doubleValue);
    }

    void UpdateKLine(double cpuUsage, time_t now) {
        static time_t lastUpdate = 0;
        static KLine current;

        if (lastUpdate == 0) {
            current = { cpuUsage, cpuUsage, cpuUsage, cpuUsage, now };
            lastUpdate = now;
            return;
        }

        if (now - lastUpdate >= 1) { // 1秒一个K线周期
            current.close = cpuUsage;
            current.timestamp = now;
            klines.push_back(current);
            UpdateIndicators();
            current = { cpuUsage, cpuUsage, cpuUsage, cpuUsage, now };
            lastUpdate = now;
        }
        else {
            current.high = max(current.high, cpuUsage);
            current.low = min(current.low, cpuUsage);
            current.close = cpuUsage;
        }
    }

    void UpdateIndicators() {
        // 计算MA
        if (klines.size() >= 5) {
            double sum5 = 0;
            for (size_t i = klines.size() - 5; i < klines.size(); ++i)
                sum5 += klines[i].close;
            indicators.ma5.push_back(sum5 / 5);
        }

        if (klines.size() >= 10) {
            double sum10 = 0;
            for (size_t i = klines.size() - 10; i < klines.size(); ++i)
                sum10 += klines[i].close;
            indicators.ma10.push_back(sum10 / 10);
        }

        // 计算MACD
        if (klines.size() > 26) {
            double close = klines.back().close;
            ema12 = (close - ema12) * (2.0 / 13) + ema12;
            ema26 = (close - ema26) * (2.0 / 27) + ema26;
            indicators.macd.push_back(ema12 - ema26);
        }
    }

    const vector<KLine>& GetKLines() const { return klines; }
    const Indicator& GetIndicators() const { return indicators; }
};

HWND EnumHWnd = 0;   //用来保存CPU使用记录窗口的句柄
wstring ClassNameToEnum;
BOOL CALLBACK EnumChildWindowsProc(HWND hWnd, LPARAM lParam) //寻找CPU使用记录界面的子窗口ID
{
    wchar_t WndClassName[256];
    GetClassName(hWnd, WndClassName, 256);
    if (WndClassName == ClassNameToEnum && (EnumHWnd == 0 || [&hWnd]() {  //短路求值+lambda真好用 ( ´ρ`)
        RECT cRect, tRect;
        GetWindowRect(hWnd, &tRect);
        GetWindowRect(EnumHWnd, &cRect);
        int tW = (tRect.right - tRect.left),
            tH = (tRect.bottom - tRect.top),
            cW = (cRect.right - cRect.left),
            cH = (cRect.bottom - cRect.top);
        return cW * cH < tW * tH;
        }())) {
        EnumHWnd = hWnd;
    }
    return true;
}

void FindWnd()
{
    wcout << L"Try find " << config::WindowTitle << L" " << config::WindowClassName << endl;
    HWND TaskmgrHwnd = FindWindow(config::WindowClassName.c_str(), config::WindowTitle.c_str());
    if (TaskmgrHwnd != NULL) {
        if (config::ChildClassName.empty())
            EnumHWnd = TaskmgrHwnd;
        else {
            ClassNameToEnum = config::ChildClassName;
            EnumChildWindows(TaskmgrHwnd, EnumChildWindowsProc, NULL);
        }
    }
}

void DrawStockChart(Mat& frame, const PerformanceMonitor& monitor) {
    const vector<KLine>& klines = monitor.GetKLines();
    const Indicator& ind = monitor.GetIndicators();

    int width = frame.cols;
    int height = frame.rows;
    int mainHeight = static_cast<int>(height * 0.7);
    int subHeight = height - mainHeight;

    // 主图区域
    Rect mainArea(0, 0, width, mainHeight);
    // 副图区域
    Rect subArea(0, mainHeight, width, subHeight);

    // 绘制背景
    frame.setTo(Scalar(25, 25, 25));

    // 绘制主图K线
    if (!klines.empty()) {
        double maxVal = 0, minVal = 100;
        for (const auto& k : klines) {
            maxVal = max(maxVal, k.high);
            minVal = min(minVal, k.low);
        }
        double yScale = mainHeight / (maxVal - minVal + 1e-5);
        int kWidth = 5;
        int spacing = 2;

        for (size_t i = 0; i < klines.size(); ++i) {
            const KLine& k = klines[i];
            int x = width - static_cast<int>((klines.size() - i) * (kWidth + spacing));
            int bodyTop = static_cast<int>(mainHeight - (k.close - minVal) * yScale);
            int bodyBottom = static_cast<int>(mainHeight - (k.open - minVal) * yScale);
            int highY = static_cast<int>(mainHeight - (k.high - minVal) * yScale);
            int lowY = static_cast<int>(mainHeight - (k.low - minVal) * yScale);

            // 上下影线
            line(frame, Point(x + kWidth / 2, highY), Point(x + kWidth / 2, lowY),
                Scalar(200, 200, 200), 1);

            // K线实体
            Scalar color = (k.close > k.open) ? Scalar(0, 0, 200) : Scalar(0, 200, 0);
            rectangle(frame, Rect(x, min(bodyTop, bodyBottom), kWidth, abs(bodyTop - bodyBottom)),
                color, FILLED);
        }

        // 绘制均线
        auto drawMA = [&](const vector<double>& ma, Scalar color) {
            vector<Point> points;
            for (size_t i = 0; i < ma.size(); ++i) {
                int x = width - static_cast<int>((ma.size() - i) * (kWidth + spacing)) + kWidth / 2;
                int y = static_cast<int>(mainHeight - (ma[i] - minVal) * yScale);
                points.emplace_back(x, y);
            }
            polylines(frame, points, false, color, 2);
            };

        if (!ind.ma5.empty()) drawMA(ind.ma5, Scalar(0, 255, 255));  // 黄色
        if (!ind.ma10.empty()) drawMA(ind.ma10, Scalar(255, 0, 255)); // 品红
    }

    // 绘制副图MACD
    if (!ind.macd.empty()) {
        double maxMacd = *max_element(ind.macd.begin(), ind.macd.end());
        double minMacd = *min_element(ind.macd.begin(), ind.macd.end());
        double yScale = subHeight / (maxMacd - minMacd + 1e-5);
        int barWidth = 3;

        for (size_t i = 0; i < ind.macd.size(); ++i) {
            int x = width - static_cast<int>((ind.macd.size() - i) * (barWidth + 1));
            int y = mainHeight + static_cast<int>(subHeight - (ind.macd[i] - minMacd) * yScale);
            int zeroY = mainHeight + static_cast<int>(subHeight - (0 - minMacd) * yScale);
            Scalar color = (ind.macd[i] > 0) ? Scalar(0, 0, 200) : Scalar(0, 200, 0);
            line(frame, Point(x, y), Point(x, zeroY), color, barWidth);
        }
    }

    // 绘制网格
    for (int i = 0; i <= 5; ++i) {
        int y = i * mainHeight / 5;
        line(frame, Point(0, y), Point(width, y), config::colorGrid, 1);
    }
    for (int i = 0; i <= 10; ++i) {
        int x = i * width / 10;
        line(frame, Point(x, 0), Point(x, mainHeight), config::colorGrid, 1);
    }
}

bool IsAdmin() {
    HANDLE hToken = NULL;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &hToken)) return false;

    TOKEN_ELEVATION elevation;
    DWORD retSize = sizeof(TOKEN_ELEVATION);
    bool result = GetTokenInformation(hToken, TokenElevation, &elevation, sizeof(elevation), &retSize) &&
        elevation.TokenIsElevated;

    CloseHandle(hToken);
    return result;
}

void RestartAsAdmin() {
    WCHAR path[MAX_PATH];
    GetModuleFileName(NULL, path, MAX_PATH);
    ShellExecute(NULL, L"runas", path, NULL, NULL, SW_SHOW);
    exit(0);
}

int main() {
    setlocale(LC_ALL, "");

    // 检查并设置C++17编译模式
    #if defined(_MSVC_LANG) && _MSVC_LANG >= 201703L
    #else
    #error "This code requires C++17 or later. Please compile with /std:c++17 flag."
    #endif

    if (!IsAdmin()) RestartAsAdmin();

    FindWnd();
    if (!EnumHWnd) {
        MessageBox(NULL, L"找不到任务管理器窗口", L"错误", MB_OK);
        return 1;
    }

    PerformanceMonitor monitor;
    namedWindow("StockChart", WINDOW_NORMAL);
    HWND chartWnd = FindWindowA("Main HighGUI class", "StockChart");

    SetWindowLong(chartWnd, GWL_STYLE, GetWindowLong(chartWnd, GWL_STYLE) & (~(WS_BORDER | WS_CAPTION | WS_SYSMENU | WS_SIZEBOX)));
    SetWindowLong(chartWnd, GWL_EXSTYLE, WS_EX_TRANSPARENT | WS_EX_LAYERED);

    SetParent(chartWnd, EnumHWnd);
    SetWindowPos(chartWnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_SHOWWINDOW);
    while (true) {
        RECT rect;
        GetWindowRect(EnumHWnd, &rect);
        int w = rect.right - rect.left;
        int h = rect.bottom - rect.top;
        MoveWindow(chartWnd, 0, 0, w, h, TRUE);
		
        auto usage = monitor.GetUsage();
        monitor.UpdateKLine(usage.first, time(NULL));

        Mat canvas(h, w, CV_8UC3, Scalar(0));
        DrawStockChart(canvas, monitor);

        // 显示实时数值
        putText(canvas, format("CPU: %.1f%%", usage.first), Point(10, 20),
            FONT_HERSHEY_SIMPLEX, 0.5, Scalar(200, 200, 200));
        putText(canvas, format("Memory: %.1f%%", usage.second), Point(10, 40),
            FONT_HERSHEY_SIMPLEX, 0.5, Scalar(200, 200, 200));

        imshow("StockChart", canvas);
        waitKey(500); // 500ms刷新一次
    }

    return 0;
}