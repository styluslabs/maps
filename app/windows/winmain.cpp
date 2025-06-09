#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#include <glad/glad.h>
#include <glad/glad_wgl.h>

#include "mapsapp.h"
#include "ugui/svggui.h"
//#include "usvg/svgwriter.h"
#include "windowsPlatform.h"
#include "util/yamlPath.h"
#include "util/elevationManager.h"
#include "util.h"
#include "nfd.h"

#include "mapsources.h"
#include "plugins.h"
#include "offlinemaps.h"


// MapsApp

static HWND msgWindowHandle = NULL;
void PLATFORM_WakeEventLoop() { PostMessage(msgWindowHandle, WM_NULL, 0, 0); }
void TANGRAM_WakeEventLoop() { PLATFORM_WakeEventLoop(); }

void PLATFORM_setImeText(const char* text, int selStart, int selEnd) {}

bool MapsApp::openURL(const char* url)
{
  HINSTANCE result = ShellExecute(0, 0, PLATFORM_STR(url), 0, 0, SW_SHOWNORMAL);
  // ShellExecute returns a value greater than 32 if successful
  return (int)result > 32;
}

void MapsApp::notifyStatusBarBG(bool) {}
void MapsApp::setSensorsEnabled(bool enabled) {}
void MapsApp::setServiceState(int state, float intervalSec, float minDist) {}
void MapsApp::getSafeAreaInsets(float *top, float *bottom) { *top = 0; *bottom = 0; }
void MapsApp::extractAssets(const char*) {}

// SDL

Uint32 SDL_GetTicks()
{
  static Timestamp t0 = mSecSinceEpoch();
  return mSecSinceEpoch() - t0;
}

char* SDL_GetClipboardText()
{
  if(!OpenClipboard(msgWindowHandle)) { return NULL; }
  char* res = NULL;
  HANDLE object = GetClipboardData(CF_UNICODETEXT);
  if(object) {
    LPVOID buffer = GlobalLock(object);
    if(buffer) {
      res = strdup(wstr_to_utf8((wchar_t*)buffer).c_str());
      GlobalUnlock(object);
    }
  }
  CloseClipboard();
  return res;
}

SDL_bool SDL_HasClipboardText()
{
  char* text = SDL_GetClipboardText();
  if(text) SDL_free(text);
  return text != NULL ? SDL_TRUE : SDL_FALSE;
}

int SDL_SetClipboardText(const char* text)
{
  int nchars = MultiByteToWideChar(CP_UTF8, 0, text, -1, NULL, 0);
  if (!nchars) { return 0; }
  HANDLE object = GlobalAlloc(GMEM_MOVEABLE, nchars * sizeof(WCHAR));
  if (!object) { return 0; }
  LPVOID buffer = GlobalLock(object);
  if (!buffer) { GlobalFree(object); return 0; }
  MultiByteToWideChar(CP_UTF8, 0, text, -1, (WCHAR*)buffer, nchars);
  GlobalUnlock(object);

  if (!OpenClipboard(msgWindowHandle)) { GlobalFree(object); return 0; }
  EmptyClipboard();
  SetClipboardData(CF_UNICODETEXT, object);
  CloseClipboard();
  return 0;
}

void SDL_free(void* mem) { free(mem); }

SDL_Keymod SDL_GetModState()
{
  uint32_t mods = GetKeyState(VK_CONTROL) < 0 ? KMOD_CTRL : 0;  // high order bit set if key is down
  mods |= GetKeyState(VK_SHIFT) < 0 ? KMOD_SHIFT : 0;
  mods |= GetKeyState(VK_MENU) < 0 ? KMOD_ALT : 0;
  mods |= GetKeyState(VK_LWIN) < 0 || GetKeyState(VK_RWIN) < 0 ? KMOD_GUI : 0;
  return (SDL_Keymod)mods;
}

void SDL_SetTextInputRect(SDL_Rect* rect)
{
  //iosPlatform_showKeyboard(sdlWin, rect);
}

SDL_bool SDL_IsTextInputActive() { return SDL_FALSE; }
void SDL_StartTextInput() {}
void SDL_StopTextInput()
{
  //iosPlatform_hideKeyboard(sdlWin);
}

void SDL_RaiseWindow(SDL_Window* window) {}
void SDL_SetWindowTitle(SDL_Window* win, const char* title) {}
void SDL_GetWindowSize(SDL_Window* win, int* w, int* h) //{ SDL_GL_GetDrawableSize(win, w, h); }
{
  RECT rect;
  GetClientRect(msgWindowHandle, &rect);
  *w = rect.right;  *h = rect.bottom;
}
void SDL_GetWindowPosition(SDL_Window* win, int* x, int* y) { *x = 0; *y = 0; }
void SDL_DestroyWindow(SDL_Window* win) {}
SDL_Window* SDL_GetWindowFromID(Uint32 id) { return NULL; }

int SDL_PushEvent(SDL_Event* event)
{
  MapsApp::sdlEvent(event);
  return 1;
}

int SDL_PeepEvents(SDL_Event* events, int numevents, SDL_eventaction action, Uint32 minType, Uint32 maxType)
{
  if(action != SDL_ADDEVENT) return 0;
  for(int ii = 0; ii < numevents; ++ii)
    MapsApp::sdlEvent(&events[ii]);
  return numevents;
}

// WM_POINTER

// always link WM_POINTER fns at runtime so we support *running* on Win 7 and earlier
#ifndef WMPOINTER_NEEDED
// use different names for fns if already defined
#define GetPointerInfo FnGetPointerInfo
#define GetPointerFrameInfo FnGetPointerFrameInfo
#define GetPointerPenInfo FnGetPointerPenInfo
#define GetPointerPenInfoHistory FnGetPointerPenInfoHistory
#define GetPointerDeviceRects FnGetPointerDeviceRects
#define InjectTouchInput FnInjectTouchInput
#define InitializeTouchInjection FnInitializeTouchInjection
#define WMPOINTER_NEEDED 1
#endif

#ifdef WMPOINTER_NEEDED
// WM_POINTER functions
typedef BOOL (WINAPI *PtrGetPointerInfo)(UINT32, POINTER_INFO*);
typedef BOOL (WINAPI *PtrGetPointerFrameInfo)(UINT32, UINT32*, POINTER_INFO*);
typedef BOOL (WINAPI *PtrGetPointerPenInfo)(UINT32, POINTER_PEN_INFO*);
typedef BOOL (WINAPI *PtrGetPointerPenInfoHistory)(UINT32, UINT32*, POINTER_PEN_INFO*);
typedef BOOL (WINAPI *PtrGetPointerDeviceRects)(HANDLE, RECT*, RECT*);
typedef BOOL (WINAPI *PtrInjectTouchInput)(UINT32, POINTER_TOUCH_INFO*);
typedef BOOL (WINAPI *PtrInitializeTouchInjection)(UINT32, DWORD);

static PtrGetPointerInfo GetPointerInfo = NULL;
static PtrGetPointerFrameInfo GetPointerFrameInfo;
static PtrGetPointerPenInfo GetPointerPenInfo;
static PtrGetPointerPenInfoHistory GetPointerPenInfoHistory;
static PtrGetPointerDeviceRects GetPointerDeviceRects;
static PtrInjectTouchInput InjectTouchInput;
static PtrInitializeTouchInjection InitializeTouchInjection;
#endif

#define MAX_N_POINTERS 10
static POINTER_INFO pointerInfo[MAX_N_POINTERS];
static POINTER_PEN_INFO penPointerInfo[MAX_N_POINTERS];
static bool winTabProximity = false;

enum TouchPointState { TouchPointPressed = SDL_FINGERDOWN, TouchPointMoved = SDL_FINGERMOTION, TouchPointReleased = SDL_FINGERUP };

typedef double Dim;
struct TouchPoint
{
  Point screenPos;
  Dim pressure;
  TouchPointState state;
  uint32_t id;
};

static Rect clientRect;

static void updateClientRect(HWND hwnd)
{
  // same code that SDL uses for WM_TOUCH, so hopefully it will work
  RECT rect;
  if(!GetClientRect(hwnd, &rect) || (rect.right == rect.left && rect.bottom == rect.top))
    return;
  ClientToScreen(hwnd, (LPPOINT)&rect);
  ClientToScreen(hwnd, (LPPOINT)&rect + 1);
  clientRect = Rect::ltrb(rect.left, rect.top, rect.right, rect.bottom);
}

static void notifyTabletEvent(TouchPointState eventtype, const Point& globalpos,
    Dim pressure, Dim tiltX, Dim tiltY, SDL_TouchID touchid, int button, int buttons, int deviceid, uint32_t t)
{
  // combine all extra buttons into RightButton
  //if (button & ~0x1) button = (button & 0x1) | 0x2;  if (buttons & ~0x1) buttons = (buttons & 0x1) | 0x2;

  SDL_Event event = {0};
  event.type = eventtype;
  event.tfinger.timestamp = t;  // SDL_GetTicks();  // normally done by SDL_PushEvent()
  event.tfinger.touchId = touchid;
  //event.tfinger.fingerId = 0;  // if SDL sees more than one finger id for a touch id, that's multitouch
  // POINTER_FLAGS >> 4 == SDL_BUTTON_LMASK for pen down w/ no barrel buttons pressed, as desired
  event.tfinger.fingerId = eventtype == SDL_FINGERMOTION ? buttons : button;
  event.tfinger.x = (globalpos.x - clientRect.left);  // / clientRect.width();
  event.tfinger.y = (globalpos.y - clientRect.top);  // / clientRect.height();
  // stick buttons in dx, dy for now
  event.tfinger.dx = tiltX;
  event.tfinger.dy = tiltY;
  event.tfinger.pressure = pressure;
  // PeepEvents bypasses gesture recognizer and event filters
  SDL_PeepEvents(&event, 1, SDL_ADDEVENT, 0, 0);  //SDL_PushEvent(&event);
}

static void notifyTouchEvent(TouchPointState touchstate, const std::vector<TouchPoint>& _points, uint32_t t)
{
  for (const TouchPoint& p : _points) {
    SDL_Event event = {0};
    event.type = p.state;
    event.tfinger.timestamp = t;  // SDL_GetTicks();  // normally done by SDL_PushEvent()
    event.tfinger.touchId = 0;
    event.tfinger.fingerId = p.id;
    event.tfinger.x = (p.screenPos.x - clientRect.left);  // /clientRect.width();
    event.tfinger.y = (p.screenPos.y - clientRect.top);  // /clientRect.height();
    event.tfinger.pressure = p.pressure;
    SDL_PeepEvents(&event, 1, SDL_ADDEVENT, 0, 0);  //SDL_PushEvent(&event);
  }
}

static void processPenInfo(const POINTER_PEN_INFO& ppi, TouchPointState eventtype)
{
  static UINT32 prevButtons = 0;

  if(winTabProximity) return;
  const POINT pix = ppi.pointerInfo.ptPixelLocation;
  const POINT him = ppi.pointerInfo.ptHimetricLocation;
  Dim x = pix.x;
  Dim y = pix.y;
  RECT pRect, dRect;  // pointer (Himetric) rect, display rect
  if(GetPointerDeviceRects(ppi.pointerInfo.sourceDevice, &pRect, &dRect)) {
    x = dRect.left + (dRect.right - dRect.left) * Dim(him.x - pRect.left)/(pRect.right - pRect.left);
    y = dRect.top + (dRect.bottom - dRect.top) * Dim(him.y - pRect.top)/(pRect.bottom - pRect.top);
  }
  // Confirmed that HIMETRIC is higher resolution than pixel location on Surface Pro: saw different HIMETRIC
  //  locations for the same pixel loc, including updates to HIMETRIC loc with no change in pixel loc
  //PLATFORM_LOG("Pix: %d %d; HIMETRIC: %d %d", pix.x, pix.y, him.x, him.y);
  //PLATFORM_LOG("Pointer flags: 0x%x; pen flags: 0x%x\n", ppi.pointerInfo.pointerFlags, ppi.penFlags);

  // if barrel button pressed when pen down, penFlags changes but pointerFlags doesn't (the first time)
  UINT32 buttons = ((ppi.pointerInfo.pointerFlags >> 4) & 0x1F) | (ppi.penFlags & PEN_FLAG_BARREL ? SDL_BUTTON_RMASK : 0);
  bool isEraser = ppi.penFlags & (PEN_FLAG_ERASER | PEN_FLAG_INVERTED);  // PEN_FLAG_INVERTED means eraser in proximity
  notifyTabletEvent(eventtype, Point(x, y), ppi.pressure/1024.0, ppi.tiltX/90.0, ppi.tiltY/90.0,
      isEraser ? PenPointerEraser : PenPointerPen, buttons ^ prevButtons, buttons,
      int(ppi.pointerInfo.sourceDevice), ppi.pointerInfo.dwTime);
  prevButtons = buttons;
}

// ideally, we wouldn't process history unless mode is STROKE
static void processPenHistory(UINT32 ptrid)
{
  if(winTabProximity) return;
  UINT32 historycount = MAX_N_POINTERS;
  POINTER_PEN_INFO* ppi = &penPointerInfo[0];
  if(GetPointerPenInfoHistory(ptrid, &historycount, ppi)) {
    if(historycount > MAX_N_POINTERS) {
      // need more room ... we want to get all history at once since it's returned newest first!
      ppi = new POINTER_PEN_INFO[historycount];
      GetPointerPenInfoHistory(ptrid, &historycount, ppi);
    }
    // process items oldest to newest
    for(int ii = historycount - 1; ii >= 0; ii--)
      processPenInfo(ppi[ii], TouchPointMoved);
    if(ppi != &penPointerInfo[0])
      delete[] ppi;
  }
}

static bool processPointerFrame(UINT32 ptrid, TouchPointState eventtype)
{
  static UINT32 prevButtons = 0;
  UINT32 pointercount = MAX_N_POINTERS;
  std::vector<TouchPoint> pts;
  if(GetPointerFrameInfo(ptrid, &pointercount, &pointerInfo[0])) {
    for(unsigned int ii = 0; ii < pointercount; ii++) {
      if(pointerInfo[ii].pointerType == PT_PEN) {
        // for hovering pen
        if(GetPointerPenInfo(pointerInfo[ii].pointerId, &penPointerInfo[0]))
          processPenInfo(penPointerInfo[0], TouchPointMoved);
        // propagate pen hover events to DefWindowProc so mouse cursor follows pen - see explanation below
        return false;
      }
      else if (pointerInfo[ii].pointerType == PT_TOUCH) {
        TouchPoint pt;
        pt.id = pointerInfo[ii].pointerId;
        pt.state = pointerInfo[ii].pointerId == ptrid ? eventtype : TouchPointMoved;
        pt.screenPos = Point(pointerInfo[ii].ptPixelLocation.x, pointerInfo[ii].ptPixelLocation.y);
        pt.pressure = 1;
        pts.push_back(pt);
      }
      else { // PT_MOUSE or PT_TOUCHPAD
        UINT32 buttons = (pointerInfo[ii].pointerFlags >> 4) & 0x1F;
        if((buttons & 0x2) != (buttons & 0x4)) { buttons ^= 0x6; }  // swap middle and right btns for SDL
        Point pos(pointerInfo[ii].ptPixelLocation.x, pointerInfo[ii].ptPixelLocation.y);
        notifyTabletEvent(eventtype, pos, 1, 0, 0, SDL_TOUCH_MOUSEID,
            buttons ^ prevButtons, buttons, int(pointerInfo[ii].sourceDevice), pointerInfo[ii].dwTime);
        prevButtons = buttons;
        return true;
      }
    }
    if(pts.empty())
      return false;
    //event.t = pointerInfo[0].performanceCount;
    notifyTouchEvent(eventtype, pts, pointerInfo[0].dwTime);
    return true;
  }
  else if(eventtype == TouchPointReleased) {
    // seems GetPointerFrameInfo/GetPointerInfo sometimes returns error for WM_POINTERUP ... didn't notice this before
    TouchPoint pt;
    pt.id = ptrid;
    pt.state = eventtype;
    pt.screenPos = Point(0, 0);
    pt.pressure = 0;
    pts.push_back(pt);
    notifyTouchEvent(eventtype, pts, pointerInfo[0].dwTime);  // reuse time from last event
    return true;
  }
  return false;
}

static void initWMPointer()
{
#ifdef WMPOINTER_NEEDED
  HINSTANCE user32 = LoadLibraryA("user32.dll");
  if(user32) {
    GetPointerInfo = (PtrGetPointerInfo)(GetProcAddress(user32, "GetPointerInfo"));
    GetPointerFrameInfo = (PtrGetPointerFrameInfo)(GetProcAddress(user32, "GetPointerFrameInfo"));
    GetPointerPenInfo = (PtrGetPointerPenInfo)(GetProcAddress(user32, "GetPointerPenInfo"));
    GetPointerPenInfoHistory = (PtrGetPointerPenInfoHistory)(GetProcAddress(user32, "GetPointerPenInfoHistory"));
    GetPointerDeviceRects = (PtrGetPointerDeviceRects)(GetProcAddress(user32, "GetPointerDeviceRects"));
    InjectTouchInput = (PtrInjectTouchInput)(GetProcAddress(user32, "InjectTouchInput"));
    InitializeTouchInjection = (PtrInitializeTouchInjection)(GetProcAddress(user32, "InitializeTouchInjection"));
  }
#endif
  EnableMouseInPointer(TRUE);
  // Attempt to get HIMETRIC to pixel conversion factor; on Surface Pro, result is close, but not quite
  // 1 HIMETRIC = 0.01 mm
  //QWidget* screen = QApplication::desktop()->screen(0);
  // this is equiv to GetDeviceCaps(HORZRES)/GetDeviceCaps(HORZSIZE)
  //HimetricToPix = (96.0 / 2540);  //screen->width()/qreal(100*screen->widthMM());
}

static bool winInputEvent(MSG* m) //, long* result)
{
  // we're assuming pointerIds are never 0, which seems to be the case, but probably isn't a good idea
  static UINT32 penPointerId = 0;

#ifdef WMPOINTER_NEEDED
  if(!GetPointerInfo)
    return false;
#endif
  switch(m->message) {
  // WM_POINTER:
  // WM_POINTERDOWN with type PT_PEN: ignore all other pointers, use GetPointerPenInfoHistory
  // otherwise, use GetPointerFrameInfo (discard history)
  case WM_POINTERDOWN:
    updateClientRect(m->hwnd);
    if(GetPointerInfo(GET_POINTERID_WPARAM(m->wParam), &pointerInfo[0])) {
      if(pointerInfo[0].pointerType == PT_PEN) {
        penPointerId = pointerInfo[0].pointerId;
        if(GetPointerPenInfo(penPointerId, &penPointerInfo[0]))
          processPenInfo(penPointerInfo[0], TouchPointPressed);
        return true;
      }
      else
        return processPointerFrame(GET_POINTERID_WPARAM(m->wParam), TouchPointPressed);
    }
    break;
  case WM_POINTERUPDATE:
    updateClientRect(m->hwnd);
    if(penPointerId && penPointerId == GET_POINTERID_WPARAM(m->wParam)) {
      processPenHistory(penPointerId);
      return true;
    }
    else
      return processPointerFrame(GET_POINTERID_WPARAM(m->wParam), TouchPointMoved);
    break;
  case WM_POINTERUP:
    updateClientRect(m->hwnd);
    if(penPointerId && penPointerId == GET_POINTERID_WPARAM(m->wParam)) {
      if(GetPointerPenInfo(penPointerId, &penPointerInfo[0]))
        processPenInfo(penPointerInfo[0], TouchPointReleased);
      penPointerId = 0;
      return true;
    }
    else
      return processPointerFrame(GET_POINTERID_WPARAM(m->wParam), TouchPointReleased);

  case WM_MOUSEWHEEL:
  case WM_MOUSEHWHEEL:
  {
    SDL_Event event = { 0 };  // we'll leave windowID and which == 0
    event.type = SDL_MOUSEWHEEL;
    //event.wheel.timestamp = SDL_GetTicks();
    event.wheel.x = m->message == WM_MOUSEWHEEL ? 0 : GET_WHEEL_DELTA_WPARAM(m->wParam);
    event.wheel.y = m->message == WM_MOUSEWHEEL ? GET_WHEEL_DELTA_WPARAM(m->wParam) : 0;
    event.wheel.direction = SDL_MOUSEWHEEL_NORMAL | (SDL_GetModState() << 16);
    SDL_PushEvent(&event);  //SDL_PeepEvents(&event, 1, SDL_ADDEVENT, 0, 0);
    break;
  }
  case WM_CLIPBOARDUPDATE:
  {
    SDL_Event event = { 0 };
    event.type = SDL_CLIPBOARDUPDATE;
    SDL_PushEvent(&event);
    break;
  }
  // Windows shutdown message; followed by WM_ENDSESSION
  case WM_QUERYENDSESSION:
  {
    SDL_Event event = { 0 };
    event.type = SDL_APP_WILLENTERBACKGROUND;
    SDL_PushEvent(&event);  // currently handled by event filter, so use PushEvent instead of PeepEvents
    break;
  }
  default:
    break;
  }
  return false;
}

static int keyMap[256];

// generated by GPT-4o, might have errors
static void initKeyMap()
{
  // Replace GLFW key mappings with Win32 Virtual-Key mappings
  keyMap[VK_SPACE] = SDLK_SPACE;
  keyMap[VK_OEM_7] = SDLK_QUOTE;         // Apostrophe
  keyMap[VK_OEM_COMMA] = SDLK_COMMA;
  keyMap[VK_OEM_MINUS] = SDLK_MINUS;
  keyMap[VK_OEM_PERIOD] = SDLK_PERIOD;
  keyMap[VK_OEM_2] = SDLK_SLASH;         // Forward slash
  keyMap['0'] = SDLK_0;
  keyMap['1'] = SDLK_1;
  keyMap['2'] = SDLK_2;
  keyMap['3'] = SDLK_3;
  keyMap['4'] = SDLK_4;
  keyMap['5'] = SDLK_5;
  keyMap['6'] = SDLK_6;
  keyMap['7'] = SDLK_7;
  keyMap['8'] = SDLK_8;
  keyMap['9'] = SDLK_9;
  keyMap[VK_OEM_1] = SDLK_SEMICOLON;     // Semicolon
  keyMap[VK_OEM_PLUS] = SDLK_EQUALS;     // Equals sign
  keyMap['A'] = SDLK_a;
  keyMap['B'] = SDLK_b;
  keyMap['C'] = SDLK_c;
  keyMap['D'] = SDLK_d;
  keyMap['E'] = SDLK_e;
  keyMap['F'] = SDLK_f;
  keyMap['G'] = SDLK_g;
  keyMap['H'] = SDLK_h;
  keyMap['I'] = SDLK_i;
  keyMap['J'] = SDLK_j;
  keyMap['K'] = SDLK_k;
  keyMap['L'] = SDLK_l;
  keyMap['M'] = SDLK_m;
  keyMap['N'] = SDLK_n;
  keyMap['O'] = SDLK_o;
  keyMap['P'] = SDLK_p;
  keyMap['Q'] = SDLK_q;
  keyMap['R'] = SDLK_r;
  keyMap['S'] = SDLK_s;
  keyMap['T'] = SDLK_t;
  keyMap['U'] = SDLK_u;
  keyMap['V'] = SDLK_v;
  keyMap['W'] = SDLK_w;
  keyMap['X'] = SDLK_x;
  keyMap['Y'] = SDLK_y;
  keyMap['Z'] = SDLK_z;
  keyMap[VK_OEM_4] = SDLK_LEFTBRACKET;   // Left bracket
  keyMap[VK_OEM_5] = SDLK_BACKSLASH;     // Backslash
  keyMap[VK_OEM_6] = SDLK_RIGHTBRACKET;  // Right bracket
  keyMap[VK_ESCAPE] = SDLK_ESCAPE;
  keyMap[VK_RETURN] = SDLK_RETURN;       // Enter key
  keyMap[VK_TAB] = SDLK_TAB;
  keyMap[VK_BACK] = SDLK_BACKSPACE;
  keyMap[VK_INSERT] = SDLK_INSERT;
  keyMap[VK_DELETE] = SDLK_DELETE;
  keyMap[VK_RIGHT] = SDLK_RIGHT;
  keyMap[VK_LEFT] = SDLK_LEFT;
  keyMap[VK_DOWN] = SDLK_DOWN;
  keyMap[VK_UP] = SDLK_UP;
  keyMap[VK_PRIOR] = SDLK_PAGEUP;        // Page Up
  keyMap[VK_NEXT] = SDLK_PAGEDOWN;       // Page Down
  keyMap[VK_HOME] = SDLK_HOME;
  keyMap[VK_END] = SDLK_END;
  keyMap[VK_CAPITAL] = SDLK_CAPSLOCK;    // Caps Lock
  keyMap[VK_SCROLL] = SDLK_SCROLLLOCK;   // Scroll Lock
  keyMap[VK_NUMLOCK] = SDLK_NUMLOCKCLEAR;// Num Lock
  keyMap[VK_SNAPSHOT] = SDLK_PRINTSCREEN;// Print Screen
  keyMap[VK_PAUSE] = SDLK_PAUSE;
  keyMap[VK_F1] = SDLK_F1;
  keyMap[VK_F2] = SDLK_F2;
  keyMap[VK_F3] = SDLK_F3;
  keyMap[VK_F4] = SDLK_F4;
  keyMap[VK_F5] = SDLK_F5;
  keyMap[VK_F6] = SDLK_F6;
  keyMap[VK_F7] = SDLK_F7;
  keyMap[VK_F8] = SDLK_F8;
  keyMap[VK_F9] = SDLK_F9;
  keyMap[VK_F10] = SDLK_F10;
  keyMap[VK_F11] = SDLK_F11;
  keyMap[VK_F12] = SDLK_F12;
  keyMap[VK_NUMPAD0] = SDLK_KP_0;        // Keypad 0
  keyMap[VK_NUMPAD1] = SDLK_KP_1;
  keyMap[VK_NUMPAD2] = SDLK_KP_2;
  keyMap[VK_NUMPAD3] = SDLK_KP_3;
  keyMap[VK_NUMPAD4] = SDLK_KP_4;
  keyMap[VK_NUMPAD5] = SDLK_KP_5;
  keyMap[VK_NUMPAD6] = SDLK_KP_6;
  keyMap[VK_NUMPAD7] = SDLK_KP_7;
  keyMap[VK_NUMPAD8] = SDLK_KP_8;
  keyMap[VK_NUMPAD9] = SDLK_KP_9;
  keyMap[VK_DECIMAL] = SDLK_KP_PERIOD;   // Keypad decimal
  keyMap[VK_DIVIDE] = SDLK_KP_DIVIDE;    // Keypad divide
  keyMap[VK_MULTIPLY] = SDLK_KP_MULTIPLY;// Keypad multiply
  keyMap[VK_SUBTRACT] = SDLK_KP_MINUS;   // Keypad subtract
  keyMap[VK_ADD] = SDLK_KP_PLUS;         // Keypad add
  keyMap[VK_LSHIFT] = SDLK_LSHIFT;       // Left Shift
  keyMap[VK_RSHIFT] = SDLK_RSHIFT;       // Right Shift
  keyMap[VK_LCONTROL] = SDLK_LCTRL;      // Left Control
  keyMap[VK_RCONTROL] = SDLK_RCTRL;      // Right Control
  keyMap[VK_LMENU] = SDLK_LALT;          // Left Alt
  keyMap[VK_RMENU] = SDLK_RALT;          // Right Alt
  keyMap[VK_LWIN] = SDLK_LGUI;           // Left Windows key
  keyMap[VK_RWIN] = SDLK_RGUI;           // Right Windows key
  keyMap[VK_APPS] = SDLK_MENU;           // Menu key
}

LONG WINAPI WindowProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
  //static PAINTSTRUCT ps;

  switch(uMsg) {
  //case WM_PAINT:
  //  display();
  //  BeginPaint(hWnd, &ps);
  //  EndPaint(hWnd, &ps);
  //  return 0;

  case WM_SIZE:
    if (wParam != SIZE_MINIMIZED) {
      SDL_Event event = {0};
      event.type = SDL_WINDOWEVENT;
      event.window.event = SDL_WINDOWEVENT_SIZE_CHANGED;
      event.window.data1 = LOWORD(lParam);
      event.window.data2 = HIWORD(lParam);
      SDL_PushEvent(&event);
      //glViewport(0, 0, LOWORD(lParam), HIWORD(lParam));
      //PostMessage(hWnd, WM_PAINT, 0, 0);
    }
    return 0;

  case WM_KEYDOWN:
  case WM_KEYUP:
  case WM_SYSKEYDOWN:
  case WM_SYSKEYUP: {
    auto key = wParam;
    bool down = uMsg == WM_KEYDOWN || uMsg == WM_SYSKEYDOWN;
    SDL_Event event = {0};
    event.key.type = !down ? SDL_KEYUP : SDL_KEYDOWN;
    event.key.state = !down ? SDL_RELEASED : SDL_PRESSED;
    event.key.repeat = LOWORD(lParam);  //action == GLFW_REPEAT;
    event.key.keysym.scancode = (SDL_Scancode)LOBYTE(HIWORD(lParam));
    event.key.keysym.sym = key < 0 || key > 255 ? SDLK_UNKNOWN : keyMap[key];
    event.key.keysym.mod = SDL_GetModState();
    event.key.windowID = 0;  //keyboard->focus ? keyboard->focus->id : 0;
    SDL_PushEvent(&event);
    return 0;
  }

  case WM_CHAR: {
    wchar_t utf16Char = static_cast<wchar_t>(wParam);
    // control characters are handled as key events above
    if(utf16Char < 32 || (utf16Char > 126 && utf16Char < 160)) { return 0; }
    SDL_Event event = {0};
    event.text.type = SDL_TEXTINPUT;
    event.text.windowID = 0;  //keyboard->focus ? keyboard->focus->id : 0;
    uint8_t* out = (uint8_t*)event.text.text;
    int nbytes = WideCharToMultiByte(CP_UTF8, 0, &utf16Char, 1, (LPSTR)out, 8, nullptr, nullptr);
    out[nbytes] = '\0';
    SDL_PushEvent(&event);
    return 0;
  }

  case WM_CLOSE: {
    //PostQuitMessage(0);
    SDL_Event event = {0};
    event.type = SDL_QUIT;
    SDL_PushEvent(&event);
    return 0;
  }

  default: {
    MSG msg = { hWnd, uMsg, wParam, lParam, 0, {0, 0} };
    if(winInputEvent(&msg)) { return 0; }
  }

  }
  return DefWindowProc(hWnd, uMsg, wParam, lParam);
}

bool initWGL(const WNDCLASS& wc)
{
  // create temporary window
  auto style = WS_OVERLAPPEDWINDOW | WS_CLIPSIBLINGS | WS_CLIPCHILDREN;
  HWND fakeWND = CreateWindow(wc.lpszClassName, L"Fake Window", style, 0, 0, 1, 1, NULL, NULL, wc.hInstance, NULL);
  HDC fakeDC = GetDC(fakeWND);  // Device Context

  PIXELFORMATDESCRIPTOR fakePFD;
  ZeroMemory(&fakePFD, sizeof(fakePFD));
  fakePFD.nSize = sizeof(fakePFD);
  fakePFD.nVersion = 1;
  fakePFD.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
  fakePFD.iPixelType = PFD_TYPE_RGBA;
  fakePFD.cColorBits = 32;
  fakePFD.cAlphaBits = 8;
  fakePFD.cDepthBits = 24;

  const int fakePFDID = ChoosePixelFormat(fakeDC, &fakePFD);
  if (!fakePFDID) { LOGE("ChoosePixelFormat() failed."); return false; }
  if (!SetPixelFormat(fakeDC, fakePFDID, &fakePFD)) { LOGE("SetPixelFormat() failed."); return false; }
  HGLRC fakeRC = wglCreateContext(fakeDC);
  if (!fakeRC) { LOGE("wglCreateContext() failed."); return false; }
  if (!wglMakeCurrent(fakeDC, fakeRC)) { LOGE("wglMakeCurrent() failed."); return false; }

  if(!gladLoadWGL(fakeDC)) { LOGE("gladLoadWGL() failed."); return false; }

  wglMakeCurrent(NULL, NULL);
  wglDeleteContext(fakeRC);
  ReleaseDC(fakeWND, fakeDC);
  DestroyWindow(fakeWND);
  return true;
}

HGLRC createGLContext(HDC DC, HGLRC sharectx)
{
  const int pixelAttribs[] = {
    WGL_DRAW_TO_WINDOW_ARB, GL_TRUE,
    WGL_SUPPORT_OPENGL_ARB, GL_TRUE,
    WGL_DOUBLE_BUFFER_ARB, GL_TRUE,
    //WGL_SWAP_METHOD_ARB, WGL_SWAP_COPY_ARB,
    WGL_PIXEL_TYPE_ARB, WGL_TYPE_RGBA_ARB,
    WGL_ACCELERATION_ARB, WGL_FULL_ACCELERATION_ARB,
    WGL_COLOR_BITS_ARB, 24,  // some sources use 32, but docs clearly state this does not include alpha bits
    WGL_ALPHA_BITS_ARB, 8,
    WGL_DEPTH_BITS_ARB, 24,
    WGL_STENCIL_BITS_ARB, 8,
    WGL_SAMPLE_BUFFERS_ARB, GL_TRUE,
    WGL_SAMPLES_ARB, 4,
    0
  };
  int pixelFormatID; UINT numFormats;
  bool status = wglChoosePixelFormatARB(DC, pixelAttribs, NULL, 1, &pixelFormatID, &numFormats);
  if (!status || !numFormats) { LOGE("wglChoosePixelFormatARB() failed."); return 0; }

  PIXELFORMATDESCRIPTOR PFD;
  DescribePixelFormat(DC, pixelFormatID, sizeof(PFD), &PFD);
  SetPixelFormat(DC, pixelFormatID, &PFD);

  const int contextAttribs[] = {
    WGL_CONTEXT_MAJOR_VERSION_ARB, 3,
    WGL_CONTEXT_MINOR_VERSION_ARB, 3,
    WGL_CONTEXT_PROFILE_MASK_ARB, WGL_CONTEXT_CORE_PROFILE_BIT_ARB,  // WGL_CONTEXT_ES2_PROFILE_BIT_EXT
    //WGL_CONTEXT_FLAGS_ARB, WGL_CONTEXT_DEBUG_BIT_ARB,
    0
  };
  HGLRC RC = wglCreateContextAttribsARB(DC, sharectx, contextAttribs);
  if (!RC) { LOGE("wglCreateContextAttribsARB() failed."); return 0; }
  return RC;
}

// refs:
// - https://github.com/blender/blender/blob/main/intern/ghost/intern/GHOST_SystemWin32.cc
// - https://github.com/glfw/glfw/blob/master/src/wgl_context.c

int APIENTRY wWinMain(HINSTANCE hCurrentInst, HINSTANCE hPreviousInst, PWSTR lpszCmdLine, int nCmdShow)
{
  SetProcessDPIAware();
  winLogToConsole = attachParentConsole();  // printing to old console is slow, but Powershell is fine
  Tangram::WindowsPlatform::logToConsole = winLogToConsole;

  int argc = 0;
  LPWSTR* wargv = CommandLineToArgvW(lpszCmdLine, &argc);
  std::vector<std::string> argv;
  for(int ii = 0; ii < argc; ++ii) { argv.push_back(wstr_to_utf8(wargv[ii])); }

  // config
  MapsApp::baseDir = canonicalPath("./");
  if(!FSPath(MapsApp::baseDir, "config.default.yaml").exists()) {
    if(argc > 0)
      MapsApp::baseDir = canonicalPath(FSPath(argv[0]).parentPath());
    if(!FSPath(MapsApp::baseDir, "config.default.yaml").exists())
      MapsApp::baseDir = canonicalPath(FSPath(MapsApp::baseDir, "../../assets/"));
  }
  MapsApp::loadConfig("");

  int screenw = GetSystemMetrics(SM_CXSCREEN);
  int screenh = GetSystemMetrics(SM_CYSCREEN);
  float dpi = std::max(screenw, screenh)/11.2f;

  initKeyMap();
  initWMPointer();
  if(NFD_Init() != NFD_OKAY)
    LOGE("NFD_Init error: %s", NFD_GetError());

  // Window / OpenGL setup
  WNDCLASS wc;
  memset(&wc, 0, sizeof(wc));
  wc.style = CS_OWNDC;
  wc.lpfnWndProc = (WNDPROC)WindowProc;
  wc.hInstance = hCurrentInst;  //GetModuleHandle(NULL);
  wc.hIcon = LoadIcon(hCurrentInst, L"IDI_ICON1");  //MAKEINTRESOURCE(IDI_ICON1));
  wc.hCursor = LoadCursor(NULL, IDC_ARROW);
  wc.lpszClassName = L"AscendWnd";
  if(!RegisterClass(&wc)) { LOGE("RegisterClass() failed"); return 0; };

  if(!initWGL(wc)) { return 0; };

  RECT winRect = { 0, 0, screenw/2, int(0.9f*screenh) };
  const YAML::Node& posYaml = MapsApp::cfg()["ui"]["position"];
  if(posYaml.size() == 4) {
    winRect.left = posYaml[0].as<int>(0);
    winRect.top = posYaml[1].as<int>(0);
    winRect.right = posYaml[2].as<int>(winRect.right);
    winRect.bottom = posYaml[3].as<int>(winRect.bottom);
  }

  auto winStyle = WS_OVERLAPPEDWINDOW | WS_CLIPSIBLINGS | WS_CLIPCHILDREN;
  HWND mainWnd = CreateWindow(
      wc.lpszClassName, L"Ascend Maps", winStyle,  // class name, window name, styles
      winRect.left, winRect.top,  // posx, posy. If x is set to CW_USEDEFAULT y is ignored
      winRect.right - winRect.left, winRect.bottom - winRect.top,  // width, height
      NULL, NULL, wc.hInstance, NULL);  // parent window, menu, instance, param
  msgWindowHandle = mainWnd;
  HDC mainDC = GetDC(mainWnd);
  HGLRC mainCtx = createGLContext(mainDC, 0);

  // offscreen worker
  HWND auxWnd = CreateWindow(wc.lpszClassName, L"Ascend Offscreen", winStyle, 0, 0, 256, 256, NULL, NULL, wc.hInstance, NULL);
  HDC auxDC = GetDC(auxWnd);
  HGLRC auxCtx = createGLContext(auxDC, mainCtx);

  if(!mainCtx || !auxCtx) { return 0; }
  if(!wglMakeCurrent(mainDC, mainCtx)) { LOGE("wglMakeCurrent() failed."); return 0; }
  gladLoadGL();

  auto offscreenWorker = std::make_unique<Tangram::AsyncWorker>("Ascend offscreen GL worker");
  offscreenWorker->enqueue([=](){ wglMakeCurrent(auxDC, auxCtx); });
  Tangram::ElevationManager::offscreenWorker = std::move(offscreenWorker);

  ShowWindow(mainWnd, nCmdShow);

  // MapsApp setup
  MapsApp* app = new MapsApp(new Tangram::WindowsPlatform());
  app->setDpi(dpi);
  //app->map->setupGL();
  app->createGUI((SDL_Window*)mainWnd);

  app->win->addHandler([&](SvgGui*, SDL_Event* event){
    if(event->type == SDL_KEYDOWN) {
      if(event->key.keysym.sym == SDLK_q && event->key.keysym.mod & KMOD_CTRL)
        MapsApp::runApplication = false;
      else if(event->key.keysym.sym == SDLK_F5) {
        app->mapsSources->reload();
        app->pluginManager->reload();
        app->loadSceneFile();  // reload scene
      }
      else
        return false;
    }
    else if(event->type == SDL_QUIT)
      MapsApp::runApplication = false;
    else
      return false;
    return true;
  });

  app->mapsOffline->resumeDownloads();

  app->mapsSources->rebuildSource(app->config["sources"]["last_source"].Scalar());

  app->updateLocation(Location{0, 37.777, -122.434, 0, 100, 0, NAN, 0, NAN, 0});
  app->updateGpsStatus(10, 10);  // turn location maker blue
  app->updateOrientation(0, M_PI/2, 0, 0);

  // calling wglMakeCurrent() for other context on offscreen thread breaks rendering on main thread unless
  //  we call wglMakeCurrent again at least once, even though wglGetCurrentContext() reports no change.
  // Happens even if other context is not shared! ... looks like it could be a VMware GL issue
  wglMakeCurrent(mainDC, mainCtx);
  wglSwapIntervalEXT(app->cfg()["gl_swap_interval"].as<int>(-1));  // vsync

  // main loop
  MSG msg;
  while(MapsApp::runApplication) {
    if(!app->needsRender() || PeekMessage(&msg, NULL, 0, 0, PM_NOREMOVE)) {
      GetMessage(&msg, NULL, 0, 0);  // wait for message (note WaitMessage() can miss messages)
      do {
        if(msg.message == WM_QUIT) { MapsApp::runApplication = false; break; }
        TranslateMessage(&msg);
        DispatchMessage(&msg);
      } while(PeekMessage(&msg, NULL, 0, 0, PM_REMOVE));
    }
    RECT rect;
    GetClientRect(mainWnd, &rect);
    if(app->drawFrame(rect.right, rect.bottom))
      SwapBuffers(mainDC);
  }

  // save window size
  if(GetWindowRect(mainWnd, &winRect)) {
    app->config["ui"]["position"] = YAML::Array({winRect.left, winRect.top, winRect.right, winRect.bottom});  // YAML::Tag::YAML_FLOW
  }

  app->onSuspend();
  delete app;
  NFD_Quit();

  wglMakeCurrent(0, 0);
  offscreenWorker = std::move(Tangram::ElevationManager::offscreenWorker);
  if(offscreenWorker) {
    // GLFW docs say a context must not be current on any other thread for glfwTerminate()
    offscreenWorker->enqueue([=](){ wglMakeCurrent(0, 0); });
    offscreenWorker->waitForCompletion();
    offscreenWorker.reset();  // wait for thread exit
  }

  //wglMakeCurrent(NULL, NULL);
  //ReleaseDC(hDC, hWnd);
  //wglDeleteContext(hRC);
  //DestroyWindow(hWnd);

  return msg.wParam;
}
