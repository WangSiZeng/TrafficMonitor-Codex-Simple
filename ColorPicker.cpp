#define _WIN32_WINNT 0x0601
#include <afxwin.h>
#include <afxdlgs.h>
#include <afxcolordialog.h>
#include "ColorPicker.h"

void InitializePluginModule(HMODULE module);

namespace {
class ColorPickerApplication : public CWinApp {
public:
    BOOL InitInstance() override {
        InitializePluginModule(m_hInstance);
        return TRUE;
    }
};
ColorPickerApplication application;
}

bool ShowGraphColorDialog(HWND parent, COLORREF& color) {
    // Console test hosts do not go through MFC's DLL entry point.
    if (!AfxGetInstanceHandle() && !AfxWinInit(GetModuleHandleW(nullptr), nullptr, GetCommandLineW(), 0))
        return false;
    CMFCColorDialog dialog(color, 0, CWnd::FromHandle(parent));
    if (dialog.DoModal() != IDOK) return false;
    color = dialog.GetColor();
    return true;
}
