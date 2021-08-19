#define _CRT_SECURE_NO_WARNINGS

#include <iostream>
#include <iomanip>
#include <vector>
#include <string>
#include <ctime>

#include <Windows.h>
#include <time.h>
#include <ShlObj.h>
#include <signal.h>
#include <objidl.h>
#include <gdiplus.h>
#include <gdiplusgraphics.h>
#include <shellscalingapi.h>

#pragma comment (lib,"Gdiplus.lib")

// Constant parameters
constexpr double scaling = 1.0;
constexpr unsigned monitor_id = 0;

constexpr int r0 = int(200 * scaling);
constexpr int r = int(50 * scaling);
constexpr int r2 = int(20 * scaling);
constexpr int r3 = int(7 * scaling);
constexpr int e = int(90 * scaling);
constexpr int e2 = int(30 * scaling);
constexpr int d = int(2 * r);
constexpr int d2 = int(2 * r2);
constexpr int d3 = int(2 * r3);

// Loop interruption global variable
static volatile int keepRunning = 1;

// Variables to identify screen infos
HWND workerw;
std::vector<MONITORINFO> monitors;

// Function that gather the current wallpaper image path
std::wstring GetWallpaperW()
{
	// Search for the current wallpaper
	std::wstring path(MAX_PATH, '\0');
	SystemParametersInfoW(SPI_GETDESKWALLPAPER, path.size(), &path[0], 0);
	path.resize(wcslen(&path[0]));

	// Check if the file exists
	DWORD attr = GetFileAttributesW(path.c_str());
	if (attr != INVALID_FILE_ATTRIBUTES && !(attr & FILE_ATTRIBUTE_DIRECTORY))
	{
		return path;
	}

	// Check for TranscodedWallpaper
	path.clear();
	path.resize(MAX_PATH, '\0');
	SHGetFolderPathW(0, CSIDL_APPDATA, 0, 0, &path[0]);
	path.resize(wcslen(&path[0]));
	path += L"\\Microsoft\\Windows\\Themes\\TranscodedWallpaper";
	attr = GetFileAttributesW(path.c_str());
	if (attr != INVALID_FILE_ATTRIBUTES && !(attr & FILE_ATTRIBUTE_DIRECTORY))
	{
		return path;
	}

	// The user has no wallpaper
	return L"";
}

// Set current wallpaper from path
void SetWallpaperW(const std::wstring& path)
{
	SystemParametersInfoW(SPI_SETDESKWALLPAPER, 0, (void*)&path[0], SPIF_UPDATEINIFILE | SPIF_SENDWININICHANGE);
}

// Code to identify screens size and position
BOOL CALLBACK findWWorker(HWND top, LPARAM lparam)
{
	HWND p = FindWindowEx(top, NULL, L"SHELLDLL_DefView", NULL);
	if (p != NULL) {
		workerw = FindWindowEx(NULL, top, L"WorkerW", NULL);
	}
	return TRUE;
}

BOOL CALLBACK MonitorEnumProc(HMONITOR hMonitor,
	HDC      hdcMonitor,
	LPRECT   lprcMonitor,
	LPARAM   dwData)
{
	MONITORINFO info;
	info.cbSize = sizeof(info);
	if (GetMonitorInfo(hMonitor, &info))
	{
		monitors.push_back(info);
	}
	return TRUE;  // continue enumerating
}

std::ostream& operator<<(std::ostream& os, const RECT& r)
{
	os << "x = [" << std::right << std::setw(5) << r.left << " " << std::left << std::setw(5) << r.right << "] ";
	os << "y = [" << std::right << std::setw(5) << r.top << " " << std::left << std::setw(5) << r.bottom << "] ";
	os << "res = " << std::right << r.right - r.left;
	os << "x" << r.bottom - r.top;
	return os;
}

// Interrupt signal reciever
void intHandler(int dummy) {
	keepRunning = 0;
	SetWallpaperW(GetWallpaperW());
	exit(0);
}

// Main function
int main() {

	// Setup interruption catch
	signal(SIGINT, intHandler);

	// Initialize GDI+
	Gdiplus::GdiplusStartupInput gdiplusStartupInput;
	ULONG_PTR gdiplusToken;
	Gdiplus::GdiplusStartup(&gdiplusToken, &gdiplusStartupInput, nullptr);

#ifdef _DEBUG
	AllocConsole();
	freopen("CONOUT$", "wb", stdout);
#else
	HANDLE hHandle = CreateMutex(NULL, TRUE, L"com.arrol.desktopintegration");
	if (ERROR_ALREADY_EXISTS == GetLastError())
	{
		// Program already running somewhere
		return(1); // Exit program
	}
	// Hide console
	ShowWindow(::GetConsoleWindow(), SW_HIDE);
#endif // _DEBUG

	// Attach rendering to desktop
	EnumWindows(findWWorker, 0);

	// Process screen sizes and positions
	EnumDisplayMonitors(NULL, NULL, MonitorEnumProc, 0);

	// Value storing the smallest rect containing all the screens
	RECT container = monitors[0].rcMonitor;

	for (size_t i = 0; i < monitors.size(); i++)
	{
		//cout << (monitors[i].dwFlags ? " [*] " : " [ ] ");
		//cout <<	"monitor  " << i << ": " << monitors[i].rcMonitor << endl;
		if (monitors[i].rcMonitor.left < container.left) container.left = monitors[i].rcMonitor.left;
		if (monitors[i].rcMonitor.right > container.right) container.right = monitors[i].rcMonitor.right;
		if (monitors[i].rcMonitor.top < container.top) container.top = monitors[i].rcMonitor.top;
		if (monitors[i].rcMonitor.bottom > container.bottom) container.bottom = monitors[i].rcMonitor.bottom;
	}

	//cout << "container  = " << container << endl;

	// value containing all the normalized screen positions (> 0)
	std::vector<RECT> monitorsN;
	RECT containerN = container;

	containerN.left -= container.left;
	containerN.right -= container.left;
	containerN.top -= container.top;
	containerN.bottom -= container.top;

	for (size_t i = 0; i < monitors.size(); i++)
	{
		monitorsN.push_back(monitors[i].rcMonitor);

		monitorsN[i].left -= container.left;
		monitorsN[i].right -= container.left;
		monitorsN[i].top -= container.top;
		monitorsN[i].bottom -= container.top;

		//cout << "monitorN " << i << ": " << monitorsN[i] << endl;
	}

	std::cout << "c = " << containerN.right << " x " << containerN.bottom << std::endl;

	// Initialize render
	HDC dc_desktop = GetDCEx(workerw, NULL, 0x403);
	HDC dc_backup = CreateCompatibleDC(dc_desktop);
	HDC dc_buffer = CreateCompatibleDC(dc_desktop);

	SetProcessDPIAware();

	BITMAP structBitmapHeader;
	memset(&structBitmapHeader, 0, sizeof(BITMAP));

	HGDIOBJ hBitmap = GetCurrentObject(dc_desktop, OBJ_BITMAP);
	GetObject(hBitmap, sizeof(BITMAP), &structBitmapHeader);
	//std::cout << "b = " << structBitmapHeader.bmWidth << " x " << structBitmapHeader.bmHeight << std::endl;

	HBITMAP bh = CreateCompatibleBitmap(dc_desktop, structBitmapHeader.bmWidth, structBitmapHeader.bmHeight);
	SelectObject(dc_backup, bh);

	HBITMAP bh2 = CreateCompatibleBitmap(dc_desktop, structBitmapHeader.bmWidth, structBitmapHeader.bmHeight);
	SelectObject(dc_buffer, bh2);

	Gdiplus::Graphics graphics_desktop(dc_desktop);
	Gdiplus::Graphics graphics_backup(dc_backup);
	Gdiplus::Graphics graphics_buffer(dc_buffer);

	graphics_buffer.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);

	//cout << GetScaleFactorForDevice(DEVICE_PRIMARY) << endl;

	// Create a copy of current wallpaper to draw on it / restore it later
	std::wstring wpp = GetWallpaperW();
	Gdiplus::Image image(wpp.c_str());
	Gdiplus::Image imageo(wpp.c_str());

	SetWallpaperW(wpp.c_str());

	// Save backup of wallpaper
	BitBlt(dc_backup, 0, 0, containerN.right, containerN.bottom, dc_desktop, 0, 0, SRCCOPY);

	//std::cout << graphics.GetDpiX() << " x " << graphics.GetDpiY() << endl;
	//graphics.SetPageScale(0.1);
	//std::cout << graphics.GetPageScale() << " x " << endl;

	// Brush and painting configuration
	Gdiplus::Pen pen(Gdiplus::Color(255, 255, 255, 255));
	Gdiplus::SolidBrush brush(Gdiplus::Color(255, 255, 255, 255));
	Gdiplus::SolidBrush brushR(Gdiplus::Color(255, 255, 0, 0));
	Gdiplus::SolidBrush brushT(Gdiplus::Color(255, 0, 0, 0));

	Gdiplus::FontFamily Arial(L"Arial");

	Gdiplus::Font font(&Arial, 45 / scaling);
	Gdiplus::Font font_m(&Arial, 15 / scaling);

	Gdiplus::StringFormat stringFormat = new Gdiplus::StringFormat();
	stringFormat.SetAlignment(Gdiplus::StringAlignment::StringAlignmentCenter);
	stringFormat.SetLineAlignment(Gdiplus::StringAlignment::StringAlignmentCenter);

	// Time variables
	time_t currentTime;
	struct tm* localTime;

	int hour = 0;
	int min = 0;
	int sec = 0;
	int msec = 0;

	while (keepRunning) {

		time(&currentTime);                   // Get the current time
		localTime = localtime(&currentTime);  // Convert the current time to the local time

		hour = localTime->tm_hour;
		min = localTime->tm_min;
		sec = localTime->tm_sec;

		// Clean buffer by putting the background
		BitBlt(dc_buffer, 0, 0, containerN.right, containerN.bottom, dc_backup, 0, 0, SRCCOPY);

		{
			int cx = int(monitorsN[monitor_id].left + (monitorsN[monitor_id].right - monitorsN[monitor_id].left) / 2 * scaling);
			int cy = int(monitorsN[monitor_id].top + (monitorsN[monitor_id].bottom - monitorsN[monitor_id].top) / 2 * scaling);

			graphics_buffer.FillEllipse(&brush,
				(int)((double)cx - r),
				(int)((double)cy - r),
				(int)((double)d),
				(int)((double)d)
			);

			graphics_buffer.DrawString(std::to_wstring(hour).c_str(), -1, &font, Gdiplus::PointF((Gdiplus::REAL)cx, (Gdiplus::REAL)cy), &stringFormat, &brushT);

			double pc3 = msec / 30.0f;
			double pc2 = sec / 30.0f + (1 / 60.0 * pc3);
			double pc = min / 30.0f + (1 / 60.0 * pc2);

			int dx = int((double)e * -sin(pc * 3.14));
			int dy = int((double)e * cos(pc * 3.14));

			graphics_buffer.FillEllipse(&brush,
				(int)((double)cx - (dx + r2)),
				(int)((double)cy - (dy + r2)),
				(int)((double)d2),
				(int)((double)d2)
			);

			graphics_buffer.DrawString(std::to_wstring(min).c_str(), -1, &font_m, Gdiplus::PointF((Gdiplus::REAL)(cx - dx), (Gdiplus::REAL)(cy - dy)), &stringFormat, &brushT);

			int dx2 = int((double)e2 * -sin(pc2 * 3.14));
			int dy2 = int((double)e2 * cos(pc2 * 3.14));

			graphics_buffer.FillEllipse(&brushR,
				(int)((double)cx - (dx + dx2 + r3)),
				(int)((double)cy - (dy + dy2 + r3)),
				(int)((double)d3),
				(int)((double)d3)
			);
		}

		//graphics_buffer.DrawImage(&image, 0, 0);	
		//graphics.DrawLine(&pen, 0, 0, 500, 500);
		//graphics.FillRectangle(&brush, 320 + curr * 10, 200, 100, 100);
		//graphics.DrawRectangle(&pen, 600, 400, 100, 150);
		//graphics.DrawImage(&image, 10, 10);
		//graphics.FillEllipse(&brush, 50, 400, 200, 100);

		// Exit Key catch
		if (GetAsyncKeyState(VK_DIVIDE) & 1)
		{
			keepRunning = 0;
		}

		// Copy buffer to desktop
		BitBlt(dc_desktop, 0, 0, containerN.right, containerN.bottom, dc_buffer, 0, 0, SRCCOPY);

		Sleep(100);
	}

	// RESET Wallpaper before leaving
	SetWallpaperW(GetWallpaperW());

	ReleaseDC(workerw, dc_desktop);

#ifdef _DEBUG

#else
	ReleaseMutex(hHandle); // Explicitly release mutex
	CloseHandle(hHandle); // close handle before terminating
#endif // _DEBUG

}