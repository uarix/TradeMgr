﻿#include <Windows.h>
#include <winuser.h>  
#include <process.h>
#include <string>
#include <vector>
#include <mmsystem.h>
#include <opencv2/opencv.hpp>
#include <fstream>
#include <iomanip>
#include <shellapi.h>  
#pragma comment(lib, "shell32.lib")  
#pragma comment(lib, "winmm.lib") 

using namespace std;
using namespace cv;

namespace config{
	wstring WindowClassName;
	wstring WindowTitle;
	wstring ChildClassName;
	Vec3b colorEdge;
	Vec3b colorDark;
	Vec3b colorBright;
	Vec3b colorGrid;
	Vec3b colorFrame;
	bool DrawGrid;

	vector<wstring> split(wstring str,char delimiters) { 
		vector<wstring>res;
		for (size_t l = 0,r = 0;r<=str.size(); ++r) {
			if (r  ==  str.size() || str[r] == delimiters) {
				res.push_back(str.substr(l, r - l));
				l = r+1;
			}
		}
		return res;
	}

	void Parse(wstring line) { //解析一行 config
		if (line.empty() || line[0] == L'#')//跳过注释
			return;
		wstring key, value;
		vector<wstring>lsp = split(line,'=');
		if (lsp.size() < 2) 
			return;
		key = lsp[0].substr(lsp[0].find_first_not_of(' '), lsp[0].find_last_not_of(' ')+1);
		value = lsp[1].substr(lsp[1].find_first_not_of(' '), lsp[1].find_last_not_of(' ')+1);
		if (value.size() > 1 && value.front() == '"' && value.back() == '"')
			value = value.substr(1, value.size() - 2);
		
		if (key == L"WindowClassName")
			WindowClassName = value;
		else if (key == L"WindowTitle")
			WindowTitle = value;
		else if (key == L"ChildClassName")
			ChildClassName = value;
		else if (key == L"DrawGrid")
			DrawGrid = value == L"true";
		else if (key.substr(0,5) == L"Color") {
			Vec3b color;
			for(int i=0;i<3;++i)
				color[i] = _wtoi(split(value,',')[i].c_str());
			if (key == L"ColorEdge")
				colorEdge = color;
			if (key == L"ColorDark")
				colorDark = color;
			if (key == L"ColorBright")
				colorBright = color;
			if (key == L"ColorGrid")
				colorGrid = color;
			if (key == L"ColorFrame")
				colorFrame = color;
		}
		else {
			wcout << L"Unknow config parameter: " << key << endl;
		}
	}

	void ReadConfig() {
		wfstream fs(L"./config.cfg", ios::in);
		if (fs.is_open()) {
			wstring line;
			for (; getline(fs, line);) {
				Parse(line);
			}
		}
	}
};

HWND EnumHWnd = 0;   //用来保存CPU使用记录窗口的句柄
wstring ClassNameToEnum;
BOOL CALLBACK EnumChildWindowsProc(HWND hWnd, LPARAM lParam) //寻找CPU使用记录界面的子窗口ID
{
	wchar_t WndClassName[256];
	GetClassName(hWnd, WndClassName, 256);
	if (WndClassName == ClassNameToEnum && (EnumHWnd == 0 || [&hWnd]() {  //短路求值+lambda真好用 ( ´ρ`)
			RECT cRect,tRect;
			GetWindowRect(hWnd, &tRect);
			GetWindowRect(EnumHWnd, &cRect);
			int tW = (tRect.right - tRect.left),
				tH = (tRect.bottom - tRect.top),
				cW = (cRect.right - cRect.left),
				cH = (cRect.bottom - cRect.top);
			return cW * cH < tW* tH;
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

bool IsSystemDarkMode() {
	// Windows 10 1809版本及以上支持  
	HKEY hKey;
	DWORD darkMode = 0;
	DWORD dataSize = sizeof(DWORD);

	// 检测系统主题  
	if (RegOpenKeyEx(HKEY_CURRENT_USER,
		L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
		0, KEY_READ, &hKey) == ERROR_SUCCESS) {

		if (RegQueryValueEx(hKey, L"AppsUseLightTheme",
			NULL, NULL, (LPBYTE)&darkMode, &dataSize) == ERROR_SUCCESS) {
			RegCloseKey(hKey);
			// 0 表示深色模式，1 表示浅色模式  
			return darkMode == 0;
		}

		RegCloseKey(hKey);
	}

	return false; // 默认返回浅色模式  
}

void Binarylize(Mat& src) {
	bool isDarkMode = IsSystemDarkMode();
	Mat bin, edge;
	cvtColor(src, bin, COLOR_BGR2GRAY);
	inRange(bin, Scalar(128, 128, 128), Scalar(255, 255, 255), bin);

	Canny(bin, edge, 50, 150, 3);

	int gridHeight = src.cols / 10.0;
	int gridWidth = src.rows / 8.0;
	int gridOffset = clock() / 1000 * 10;

	Vec3b colorBright, colorDark, gridColor, edgeColor, frameColor;
	if (isDarkMode) {
		// 深色模式配色  
		colorBright = Vec3b(25, 25, 25);
		colorDark = Vec3b(182, 109, 93);
		gridColor = Vec3b(50, 50, 50);
		edgeColor = Vec3b(236, 138, 116);
		frameColor = Vec3b(50, 50, 50);
	}
	else {
		// 浅色模式配色  
		colorBright = config::colorBright;
		colorDark = config::colorDark;
		gridColor = config::colorGrid;
		edgeColor = config::colorEdge;
		frameColor = config::colorFrame;
	}

	Mat dilatedEdge;
	dilate(edge, dilatedEdge, Mat(), Point(-1, -1), 1);

	for (int r = 0; r < src.rows; ++r) {
		for (int c = 0; c < src.cols; ++c) {
			src.at<Vec3b>(r, c) = ((bin.at<uchar>(r, c) == 255) ? colorBright : colorDark);

			if (config::DrawGrid)
				if (r % gridHeight == 0 || (c + gridOffset) % gridWidth == 0)
					src.at<Vec3b>(r, c) = gridColor;

			if (dilatedEdge.at<uchar>(r, c) == 255)
				src.at<Vec3b>(r, c) = edgeColor;
		}
	}
	rectangle(src, Rect{ 0,0,src.cols,src.rows }, frameColor, 0.1);
}

string FindVideo() {
	WIN32_FIND_DATAA wfd;
	HANDLE hFile = FindFirstFileA("*.*", &wfd);
	for (; hFile != INVALID_HANDLE_VALUE;)
	{
		FindNextFileA(hFile, &wfd);
		string fileName = wfd.cFileName;
		string type = fileName.substr(fileName.find_last_of('.') + 1);
		if (type == "flv" || type == "mp4" || type == "avi") {
			return fileName;
		}
	}
	return "";
}

void Play() {
	cout << endl << endl << "--------------------------------------" << endl;
	config::ReadConfig();
	FindWnd(); //寻找CPU使用记录的窗口,  函数会把窗口句柄保存在全局变量EnumHWnd
	if (EnumHWnd == NULL) {
		cout << "Can't find the window." << endl;
		return;
	}
	string videoName = FindVideo();
	if (videoName.empty()) {
		cout << "No video file detected." << endl;
		return;
	}
	else {
		cout << "Find video " << videoName <<".\nSplitting audio." << endl;
		system(("ffmpeg -i " + videoName + " audio.wav -y").c_str());
	}
	string wndName = "innerPlayer";
	namedWindow(wndName, WINDOW_NORMAL);
	HWND playerWnd = FindWindowA("Main HighGUI class", wndName.c_str());
	SetWindowLong(playerWnd, GWL_STYLE, GetWindowLong(playerWnd, GWL_STYLE) & (~(WS_BORDER | WS_CAPTION | WS_SYSMENU | WS_SIZEBOX)));
	SetWindowLong(playerWnd, GWL_EXSTYLE, WS_EX_TRANSPARENT | WS_EX_LAYERED);
	SetParent(playerWnd, (EnumHWnd));
	RECT rect;
	GetWindowRect(EnumHWnd, &rect);
	SetWindowPos(playerWnd, HWND_TOPMOST, 0, 0, rect.right - rect.left, rect.bottom - rect.top, SWP_SHOWWINDOW);
	InvalidateRect(EnumHWnd, &rect, true);
	UpdateWindow(EnumHWnd);
	VideoCapture video(videoName);
	
	//_wsystem(L"cls");
	SetConsoleCursorPosition(GetStdHandle(STD_OUTPUT_HANDLE), { 0,0 });
	cout << format("%s input %dx%d@%f\n", videoName, (int)video.get(CAP_PROP_FRAME_WIDTH), (int)video.get(CAP_PROP_FRAME_HEIGHT), video.get(CAP_PROP_FPS));
	double frameTime = 1000.0 / video.get(CAP_PROP_FPS);
	PlaySound(L"audio.wav", NULL, SND_FILENAME | SND_ASYNC);
	clock_t startTime = clock();
	int frameCount = 0;
	for (Mat frame; video.read(frame); ++frameCount) {
		clock_t s = clock();
		GetWindowRect(EnumHWnd, &rect);
		int w = rect.right - rect.left;
		int h = rect.bottom - rect.top;
		MoveWindow(playerWnd, 0, 0, w, h, true);
		resize(frame, frame, Size(w, h), 0, 0, INTER_NEAREST);
		Binarylize(frame);
		imshow(wndName, frame);
		SetConsoleCursorPosition(GetStdHandle(STD_OUTPUT_HANDLE), { 0,1 });

		cout << format("Frame %d at %.3fs. output: %dx%d ,cost %d ms.", frameCount, frameCount * frameTime / 1000.0, w, h, clock() - s);
		for (; frameCount * frameTime > clock();)waitKey(1);
	}
	system("rm audio.wav");
	destroyWindow(wndName);
	return;
}

bool IsRunAsAdmin() {
	HANDLE hToken = NULL;
	if (OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &hToken)) {
		TOKEN_ELEVATION elevation;
		DWORD cbSize = sizeof(TOKEN_ELEVATION);
		if (GetTokenInformation(hToken, TokenElevation, &elevation, sizeof(elevation), &cbSize)) {
			CloseHandle(hToken);
			return elevation.TokenIsElevated;
		}
		CloseHandle(hToken);
	}
	return false;
}

void RestartAsAdmin() {
	TCHAR szPath[MAX_PATH];
	if (GetModuleFileName(NULL, szPath, ARRAYSIZE(szPath))) {
		SHELLEXECUTEINFO sei = { sizeof(sei) };
		sei.lpVerb = L"runas";
		sei.lpFile = szPath;
		sei.hwnd = NULL;
		sei.nShow = SW_NORMAL;

		if (!ShellExecuteEx(&sei)) {
			DWORD dwError = GetLastError();
			if (dwError == ERROR_CANCELLED) {
				MessageBox(NULL, L"需要管理员权限", L"提示", MB_OK);
				exit(1);
			}
		}
		exit(0);
	}
}

int main() {
	std::locale::global(std::locale("zh_CN.UTF-8"));

	if (!IsRunAsAdmin()) {
		RestartAsAdmin();
	}

	Play();
	system("pause");
	
}