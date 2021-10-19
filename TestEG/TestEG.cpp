// TestEG.cpp : Defines the entry point for the application.
//

#include "framework.h"
#include "TestEG.h"

#include "..\FpsTracker\FpsTracker.h"
#include "..\GpuTracker\GpuTracker.h"
#include "..\GameDetectionLogic\GameDetectionLogic.h"
#include "..\EnduranceGamingPolicy\EnduranceGamingPolicy.h"
#include <dxgi1_6.h>
#include <combaseapi.h>
#include <atlbase.h>

#define MAX_LOADSTRING 100

// Global Variables:
HINSTANCE hInst;                                // current instance
WCHAR szTitle[MAX_LOADSTRING];                  // The title bar text
WCHAR szWindowClass[MAX_LOADSTRING];            // the main window class name
std::mutex LockTitle;

// Forward declarations of functions included in this code module:
ATOM                MyRegisterClass(HINSTANCE hInstance);
BOOL                InitInstance(HINSTANCE, int);
LRESULT CALLBACK    WndProc(HWND, UINT, WPARAM, LPARAM);
INT_PTR CALLBACK    About(HWND, UINT, WPARAM, LPARAM);

HWND ghWnd;
int gFPS = 0;
double gGPU = 0;
GpuTracker gGpuTracker;
FpsTracker gFpsTracker;
GameDetectionLogic gGDL;
EnduranceGamingPolicy gEGP;
bool gIsIntelIntegratedGfx = false;
bool gIsDirty = false;
uint64_t gGpuLuid = 0;

void UpdateText()
{
    PostMessageA(ghWnd, WM_USER, 0, 0);
}

void UpdateTextEx()
{
    uint32_t pid;
    bool gm;
    bool dc;
    bool eg;
    {
        pid = gGDL.GetGamePID();
        gm = gGDL.GetIsGameMode();
        dc = gEGP.IsPowerStatusDC(); 
        eg = gEGP.IsEnduranceGamingLogicActive();
    }

    {
        std::lock_guard<std::mutex> lock(LockTitle);
        if (gIsDirty)
        {
            gIsDirty = false;
            swprintf_s(szTitle, MAX_LOADSTRING, L"TestEG: fps=%d gpu=%d, EG=%d pid=%d GM=%d DC=%d igfx=%d",
                gFPS, (int)gGPU, eg, pid, gm, dc, gIsIntelIntegratedGfx);
            SetWindowText(ghWnd, szTitle);
        }
    }
}

void UpdateFPS(int fps)
{
    std::lock_guard<std::mutex> lock(LockTitle);
    if (gFPS != fps)
    {
        gIsDirty = true;
        gFPS = fps;
    }
}

void UpdateGPU(double gpu, bool isigfx, uint64_t gpuLUID)
{
    std::lock_guard<std::mutex> lock(LockTitle);
    if (gGPU != gpu)
    {
        gIsDirty = true;
        gGPU = gpu;
    }

    if (gIsIntelIntegratedGfx != isigfx)
    {
        gIsDirty = true;
        gIsIntelIntegratedGfx = isigfx;
    }

    if (gGpuLuid != gpuLUID)
    {
        gIsDirty = true;
        gGpuLuid = gpuLUID;
    }
}

void OnFpsChanged(void* context, uint32_t processId, int fps)
{
    if (gGDL.GetGamePID() == processId)
    {
        UpdateFPS(fps);
    }
    else if (gGDL.GetGamePID() == 0)
    {
        UpdateFPS(0);
    }
    UpdateText();
}

bool IsIntelIntegratedGfx(uint64_t gpuLUID)
{
    bool retval = false;

    CComPtr<IDXGIAdapter1> adapter;
    CComPtr<IDXGIFactory6> factory6;
    DXGI_ADAPTER_DESC desc;

    if (SUCCEEDED(CreateDXGIFactory(__uuidof(IDXGIFactory6), (void**)&factory6)))
    {
        if (DXGI_ERROR_NOT_FOUND != factory6->EnumAdapterByGpuPreference(0, DXGI_GPU_PREFERENCE_MINIMUM_POWER, IID_PPV_ARGS(&adapter)))
        {
            if (SUCCEEDED(adapter->GetDesc(&desc)))
            {
                uint64_t luid = desc.AdapterLuid.HighPart;
                luid = luid << 32;
                luid = luid | desc.AdapterLuid.LowPart;

                if (luid == gpuLUID)
                {
                    if (desc.VendorId == 0x8086)
                    {
                        // If the first adapter returned, when sorted by DXGI_GPU_PREFERENCE_MINIMUM_POWER,
                        // matches the LUID and it's an intel adapter, then it's likely that the device is 
                        // Intel Integrated Gfx. However, for added confidence, check to see if the dedicated
                        // video memory is only 128MB. if so, it's intel integrated gfx.

                        if (134217728 == desc.DedicatedVideoMemory)
                        {
                            retval = true;
                        }
                    }
                }
            }
        }
    }

    return retval;
}

void OnGpuChanged(void* context, uint32_t processId, uint64_t gpuLUID, double gpuUtilization)
{
    if (gGDL.IsGamingPID(processId, gpuUtilization))
    {
        bool isigfx = false;
        if (true)
        {
            std::lock_guard<std::mutex> lock(LockTitle);
            if (gpuLUID != gGpuLuid)
            {
                isigfx = IsIntelIntegratedGfx(gpuLUID);
            }
            else
            {
                isigfx = gIsIntelIntegratedGfx;
            }
        }
        
        UpdateGPU(gpuUtilization, isigfx, gpuLUID);
        gEGP.UpdateGameStatus(processId, isigfx);
    }
    else if (gGDL.GetGamePID() == 0)
    {
        UpdateGPU(0, false, false);
    }

    UpdateText();
}

void OnGameChanged(void* context, bool isGameMode, uint32_t gamePID)
{
    if (gamePID == 0)
    {
        gEGP.UpdateGameStatus(gamePID, 0);
    }

    UpdateText();
}

void OnEnduranceGamingStatusChanged(void* context, bool isEnduranceGaming, bool isDC, uint32_t gamePID)
{
    UpdateText();
}

int APIENTRY wWinMain(_In_ HINSTANCE hInstance,
                     _In_opt_ HINSTANCE hPrevInstance,
                     _In_ LPWSTR    lpCmdLine,
                     _In_ int       nCmdShow)
{
    UNREFERENCED_PARAMETER(hPrevInstance);
    UNREFERENCED_PARAMETER(lpCmdLine);

    // TODO: Place code here.

    LoadStringW(hInstance, IDS_APP_TITLE, szTitle, MAX_LOADSTRING);
    LoadStringW(hInstance, IDC_TESTEG, szWindowClass, MAX_LOADSTRING);
    MyRegisterClass(hInstance);

    // Perform application initialization:
    if (!InitInstance (hInstance, nCmdShow))
    {
        return FALSE;
    }

    HACCEL hAccelTable = LoadAccelerators(hInstance, MAKEINTRESOURCE(IDC_TESTEG));

    SYSTEM_POWER_STATUS powerstatus;
    gEGP.UpdateEnduranceGamingEnabled(true);
    if (GetSystemPowerStatus(&powerstatus))
    {
        gEGP.UpdatePowerStatus();
    }
    RegisterPowerSettingNotification(ghWnd, &GUID_ACDC_POWER_SOURCE, DEVICE_NOTIFY_WINDOW_HANDLE);

    std::vector<std::string> exclusions;
    exclusions.push_back("<error>");
    gFpsTracker.SetExcludeProcessNames(exclusions);
    gFpsTracker.SubscribeOnFpsChanged(OnFpsChanged, &gFpsTracker);
    gFpsTracker.Start();

    gGpuTracker.SubscribeOnGpuChanged(OnGpuChanged, &gGpuTracker);
    gGpuTracker.Start();

    gGDL.SubscribeOnGameStatusChanged(OnGameChanged, &gGDL);
    gGDL.Start();

    gEGP.SubscribeOnEnduranceGameStatusChanged(OnEnduranceGamingStatusChanged, &gEGP);

    MSG msg;

    // Main message loop:
    while (GetMessage(&msg, nullptr, 0, 0))
    {
        if (!TranslateAccelerator(msg.hwnd, hAccelTable, &msg))
        {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
    }

    gGDL.Stop();
    gGDL.UnsubscribeOnGameStatusChanged(OnGameChanged, &gGDL);

    gFpsTracker.Stop();
    gFpsTracker.UnsubscribeOnFpsChanged(OnFpsChanged, &gFpsTracker);

    gGpuTracker.Stop();
    gGpuTracker.UnsubscribeOnGpuChanged(OnGpuChanged, &gGpuTracker);

    return (int) msg.wParam;
}



//
//  FUNCTION: MyRegisterClass()
//
//  PURPOSE: Registers the window class.
//
ATOM MyRegisterClass(HINSTANCE hInstance)
{
    WNDCLASSEXW wcex;

    wcex.cbSize = sizeof(WNDCLASSEX);

    wcex.style          = CS_HREDRAW | CS_VREDRAW;
    wcex.lpfnWndProc    = WndProc;
    wcex.cbClsExtra     = 0;
    wcex.cbWndExtra     = 0;
    wcex.hInstance      = hInstance;
    wcex.hIcon          = LoadIcon(hInstance, MAKEINTRESOURCE(IDI_TESTEG));
    wcex.hCursor        = LoadCursor(nullptr, IDC_ARROW);
    wcex.hbrBackground  = (HBRUSH)(COLOR_WINDOW+1);
    wcex.lpszMenuName   = MAKEINTRESOURCEW(IDC_TESTEG);
    wcex.lpszClassName  = szWindowClass;
    wcex.hIconSm        = LoadIcon(wcex.hInstance, MAKEINTRESOURCE(IDI_SMALL));

    return RegisterClassExW(&wcex);
}

//
//   FUNCTION: InitInstance(HINSTANCE, int)
//
//   PURPOSE: Saves instance handle and creates main window
//
//   COMMENTS:
//
//        In this function, we save the instance handle in a global variable and
//        create and display the main program window.
//
BOOL InitInstance(HINSTANCE hInstance, int nCmdShow)
{
   hInst = hInstance; // Store instance handle in our global variable

   HWND hWnd = CreateWindowW(szWindowClass, szTitle, WS_OVERLAPPEDWINDOW,
      CW_USEDEFAULT, 0, CW_USEDEFAULT, 0, nullptr, nullptr, hInstance, nullptr);

   if (!hWnd)
   {
      return FALSE;
   }
   SetWindowPos(hWnd, NULL, 0, 0, 600, 35, 0);
   ShowWindow(hWnd, nCmdShow);
   UpdateWindow(hWnd);

   ghWnd = hWnd;

   return TRUE;
}

//
//  FUNCTION: WndProc(HWND, UINT, WPARAM, LPARAM)
//
//  PURPOSE: Processes messages for the main window.
//
//  WM_COMMAND  - process the application menu
//  WM_PAINT    - Paint the main window
//  WM_DESTROY  - post a quit message and return
//
//
LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message)
    {
    case WM_COMMAND:
        {
            int wmId = LOWORD(wParam);
            // Parse the menu selections:
            switch (wmId)
            {
            case IDM_ABOUT:
                DialogBox(hInst, MAKEINTRESOURCE(IDD_ABOUTBOX), hWnd, About);
                break;
            case IDM_EXIT:
                DestroyWindow(hWnd);
                break;
            default:
                return DefWindowProc(hWnd, message, wParam, lParam);
            }
        }
        break;
    case WM_PAINT:
        {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hWnd, &ps);
            // TODO: Add any drawing code that uses hdc here...
            EndPaint(hWnd, &ps);
        }
        break;
    case WM_DESTROY:
        PostQuitMessage(0);
        break;
    case WM_POWERBROADCAST:
        if (wParam == PBT_POWERSETTINGCHANGE)
        {
            POWERBROADCAST_SETTING* pbs = (POWERBROADCAST_SETTING*)lParam;
            if (pbs->PowerSetting == GUID_ACDC_POWER_SOURCE)
            {
                gEGP.UpdatePowerStatus();
            }
        }
        break;
    case WM_USER:
        UpdateTextEx();
        break;
    default:
        return DefWindowProc(hWnd, message, wParam, lParam);
    }
    return 0;
}

// Message handler for about box.
INT_PTR CALLBACK About(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
    UNREFERENCED_PARAMETER(lParam);
    switch (message)
    {
    case WM_INITDIALOG:
        return (INT_PTR)TRUE;

    case WM_COMMAND:
        if (LOWORD(wParam) == IDOK || LOWORD(wParam) == IDCANCEL)
        {
            EndDialog(hDlg, LOWORD(wParam));
            return (INT_PTR)TRUE;
        }
        break;
    }
    return (INT_PTR)FALSE;
}
