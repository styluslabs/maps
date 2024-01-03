#include <jni.h>
#include <android/log.h>
#include <android/native_window_jni.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <unistd.h>  // readlink

#include "mapsapp.h"
#include "offlinemaps.h"
#include "mapsources.h"
#include "AndroidPlatform.h"
#include "JniHelpers.h"
#include "JniThreadBinding.h"

using Tangram::AndroidPlatform;
using Tangram::JniHelpers;

//#include "ugui/svggui.h"
#include "ugui/svggui_platform.h"
#include "ulib/fileutil.h"

static SDL_Keycode Android_Keycodes[] = {
    SDLK_UNKNOWN, /* AKEYCODE_UNKNOWN */
    SDLK_UNKNOWN, /* AKEYCODE_SOFT_LEFT */
    SDLK_UNKNOWN, /* AKEYCODE_SOFT_RIGHT */
    SDLK_AC_HOME, /* AKEYCODE_HOME */
    SDLK_AC_BACK, /* AKEYCODE_BACK */
    SDLK_UNKNOWN, /* AKEYCODE_CALL */
    SDLK_UNKNOWN, /* AKEYCODE_ENDCALL */
    SDLK_0, /* AKEYCODE_0 */
    SDLK_1, /* AKEYCODE_1 */
    SDLK_2, /* AKEYCODE_2 */
    SDLK_3, /* AKEYCODE_3 */
    SDLK_4, /* AKEYCODE_4 */
    SDLK_5, /* AKEYCODE_5 */
    SDLK_6, /* AKEYCODE_6 */
    SDLK_7, /* AKEYCODE_7 */
    SDLK_8, /* AKEYCODE_8 */
    SDLK_9, /* AKEYCODE_9 */
    SDLK_UNKNOWN, /* AKEYCODE_STAR */
    SDLK_UNKNOWN, /* AKEYCODE_POUND */
    SDLK_UP, /* AKEYCODE_DPAD_UP */
    SDLK_DOWN, /* AKEYCODE_DPAD_DOWN */
    SDLK_LEFT, /* AKEYCODE_DPAD_LEFT */
    SDLK_RIGHT, /* AKEYCODE_DPAD_RIGHT */
    SDLK_SELECT, /* AKEYCODE_DPAD_CENTER */
    SDLK_VOLUMEUP, /* AKEYCODE_VOLUME_UP */
    SDLK_VOLUMEDOWN, /* AKEYCODE_VOLUME_DOWN */
    SDLK_POWER, /* AKEYCODE_POWER */
    SDLK_UNKNOWN, /* AKEYCODE_CAMERA */
    SDLK_CLEAR, /* AKEYCODE_CLEAR */
    SDLK_a, /* AKEYCODE_A */
    SDLK_b, /* AKEYCODE_B */
    SDLK_c, /* AKEYCODE_C */
    SDLK_d, /* AKEYCODE_D */
    SDLK_e, /* AKEYCODE_E */
    SDLK_f, /* AKEYCODE_F */
    SDLK_g, /* AKEYCODE_G */
    SDLK_h, /* AKEYCODE_H */
    SDLK_i, /* AKEYCODE_I */
    SDLK_j, /* AKEYCODE_J */
    SDLK_k, /* AKEYCODE_K */
    SDLK_l, /* AKEYCODE_L */
    SDLK_m, /* AKEYCODE_M */
    SDLK_n, /* AKEYCODE_N */
    SDLK_o, /* AKEYCODE_O */
    SDLK_p, /* AKEYCODE_P */
    SDLK_q, /* AKEYCODE_Q */
    SDLK_r, /* AKEYCODE_R */
    SDLK_s, /* AKEYCODE_S */
    SDLK_t, /* AKEYCODE_T */
    SDLK_u, /* AKEYCODE_U */
    SDLK_v, /* AKEYCODE_V */
    SDLK_w, /* AKEYCODE_W */
    SDLK_x, /* AKEYCODE_X */
    SDLK_y, /* AKEYCODE_Y */
    SDLK_z, /* AKEYCODE_Z */
    SDLK_COMMA, /* AKEYCODE_COMMA */
    SDLK_PERIOD, /* AKEYCODE_PERIOD */
    SDLK_LALT, /* AKEYCODE_ALT_LEFT */
    SDLK_RALT, /* AKEYCODE_ALT_RIGHT */
    SDLK_LSHIFT, /* AKEYCODE_SHIFT_LEFT */
    SDLK_RSHIFT, /* AKEYCODE_SHIFT_RIGHT */
    SDLK_TAB, /* AKEYCODE_TAB */
    SDLK_SPACE, /* AKEYCODE_SPACE */
    SDLK_UNKNOWN, /* AKEYCODE_SYM */
    SDLK_WWW, /* AKEYCODE_EXPLORER */
    SDLK_MAIL, /* AKEYCODE_ENVELOPE */
    SDLK_RETURN, /* AKEYCODE_ENTER */
    SDLK_BACKSPACE, /* AKEYCODE_DEL */
    SDLK_BACKQUOTE, /* AKEYCODE_GRAVE */
    SDLK_MINUS, /* AKEYCODE_MINUS */
    SDLK_EQUALS, /* AKEYCODE_EQUALS */
    SDLK_LEFTBRACKET, /* AKEYCODE_LEFT_BRACKET */
    SDLK_RIGHTBRACKET, /* AKEYCODE_RIGHT_BRACKET */
    SDLK_BACKSLASH, /* AKEYCODE_BACKSLASH */
    SDLK_SEMICOLON, /* AKEYCODE_SEMICOLON */
    SDLK_QUOTE, /* AKEYCODE_APOSTROPHE */
    SDLK_SLASH, /* AKEYCODE_SLASH */
    SDLK_UNKNOWN, /* AKEYCODE_AT */
    SDLK_UNKNOWN, /* AKEYCODE_NUM */
    SDLK_UNKNOWN, /* AKEYCODE_HEADSETHOOK */
    SDLK_UNKNOWN, /* AKEYCODE_FOCUS */
    SDLK_UNKNOWN, /* AKEYCODE_PLUS */
    SDLK_MENU, /* AKEYCODE_MENU */
    SDLK_UNKNOWN, /* AKEYCODE_NOTIFICATION */
    SDLK_AC_SEARCH, /* AKEYCODE_SEARCH */
    SDLK_AUDIOPLAY, /* AKEYCODE_MEDIA_PLAY_PAUSE */
    SDLK_AUDIOSTOP, /* AKEYCODE_MEDIA_STOP */
    SDLK_AUDIONEXT, /* AKEYCODE_MEDIA_NEXT */
    SDLK_AUDIOPREV, /* AKEYCODE_MEDIA_PREVIOUS */
    SDLK_AUDIOREWIND, /* AKEYCODE_MEDIA_REWIND */
    SDLK_AUDIOFASTFORWARD, /* AKEYCODE_MEDIA_FAST_FORWARD */
    SDLK_MUTE, /* AKEYCODE_MUTE */
    SDLK_PAGEUP, /* AKEYCODE_PAGE_UP */
    SDLK_PAGEDOWN, /* AKEYCODE_PAGE_DOWN */
    SDLK_UNKNOWN, /* AKEYCODE_PICTSYMBOLS */
    SDLK_UNKNOWN, /* AKEYCODE_SWITCH_CHARSET */
    SDLK_UNKNOWN, /* AKEYCODE_BUTTON_A */
    SDLK_UNKNOWN, /* AKEYCODE_BUTTON_B */
    SDLK_UNKNOWN, /* AKEYCODE_BUTTON_C */
    SDLK_UNKNOWN, /* AKEYCODE_BUTTON_X */
    SDLK_UNKNOWN, /* AKEYCODE_BUTTON_Y */
    SDLK_UNKNOWN, /* AKEYCODE_BUTTON_Z */
    SDLK_UNKNOWN, /* AKEYCODE_BUTTON_L1 */
    SDLK_UNKNOWN, /* AKEYCODE_BUTTON_R1 */
    SDLK_UNKNOWN, /* AKEYCODE_BUTTON_L2 */
    SDLK_UNKNOWN, /* AKEYCODE_BUTTON_R2 */
    SDLK_UNKNOWN, /* AKEYCODE_BUTTON_THUMBL */
    SDLK_UNKNOWN, /* AKEYCODE_BUTTON_THUMBR */
    SDLK_UNKNOWN, /* AKEYCODE_BUTTON_START */
    SDLK_UNKNOWN, /* AKEYCODE_BUTTON_SELECT */
    SDLK_UNKNOWN, /* AKEYCODE_BUTTON_MODE */
    SDLK_ESCAPE, /* AKEYCODE_ESCAPE */
    SDLK_DELETE, /* AKEYCODE_FORWARD_DEL */
    SDLK_LCTRL, /* AKEYCODE_CTRL_LEFT */
    SDLK_RCTRL, /* AKEYCODE_CTRL_RIGHT */
    SDLK_CAPSLOCK, /* AKEYCODE_CAPS_LOCK */
    SDLK_SCROLLLOCK, /* AKEYCODE_SCROLL_LOCK */
    SDLK_LGUI, /* AKEYCODE_META_LEFT */
    SDLK_RGUI, /* AKEYCODE_META_RIGHT */
    SDLK_UNKNOWN, /* AKEYCODE_FUNCTION */
    SDLK_PRINTSCREEN, /* AKEYCODE_SYSRQ */
    SDLK_PAUSE, /* AKEYCODE_BREAK */
    SDLK_HOME, /* AKEYCODE_MOVE_HOME */
    SDLK_END, /* AKEYCODE_MOVE_END */
    SDLK_INSERT, /* AKEYCODE_INSERT */
    SDLK_AC_FORWARD, /* AKEYCODE_FORWARD */
    SDLK_UNKNOWN, /* AKEYCODE_MEDIA_PLAY */
    SDLK_UNKNOWN, /* AKEYCODE_MEDIA_PAUSE */
    SDLK_UNKNOWN, /* AKEYCODE_MEDIA_CLOSE */
    SDLK_EJECT, /* AKEYCODE_MEDIA_EJECT */
    SDLK_UNKNOWN, /* AKEYCODE_MEDIA_RECORD */
    SDLK_F1, /* AKEYCODE_F1 */
    SDLK_F2, /* AKEYCODE_F2 */
    SDLK_F3, /* AKEYCODE_F3 */
    SDLK_F4, /* AKEYCODE_F4 */
    SDLK_F5, /* AKEYCODE_F5 */
    SDLK_F6, /* AKEYCODE_F6 */
    SDLK_F7, /* AKEYCODE_F7 */
    SDLK_F8, /* AKEYCODE_F8 */
    SDLK_F9, /* AKEYCODE_F9 */
    SDLK_F10, /* AKEYCODE_F10 */
    SDLK_F11, /* AKEYCODE_F11 */
    SDLK_F12, /* AKEYCODE_F12 */
    SDLK_UNKNOWN, /* AKEYCODE_NUM_LOCK */
    SDLK_KP_0, /* AKEYCODE_NUMPAD_0 */
    SDLK_KP_1, /* AKEYCODE_NUMPAD_1 */
    SDLK_KP_2, /* AKEYCODE_NUMPAD_2 */
    SDLK_KP_3, /* AKEYCODE_NUMPAD_3 */
    SDLK_KP_4, /* AKEYCODE_NUMPAD_4 */
    SDLK_KP_5, /* AKEYCODE_NUMPAD_5 */
    SDLK_KP_6, /* AKEYCODE_NUMPAD_6 */
    SDLK_KP_7, /* AKEYCODE_NUMPAD_7 */
    SDLK_KP_8, /* AKEYCODE_NUMPAD_8 */
    SDLK_KP_9, /* AKEYCODE_NUMPAD_9 */
    SDLK_KP_DIVIDE, /* AKEYCODE_NUMPAD_DIVIDE */
    SDLK_KP_MULTIPLY, /* AKEYCODE_NUMPAD_MULTIPLY */
    SDLK_KP_MINUS, /* AKEYCODE_NUMPAD_SUBTRACT */
    SDLK_KP_PLUS, /* AKEYCODE_NUMPAD_ADD */
    SDLK_KP_PERIOD, /* AKEYCODE_NUMPAD_DOT */
    SDLK_KP_COMMA, /* AKEYCODE_NUMPAD_COMMA */
    SDLK_KP_ENTER, /* AKEYCODE_NUMPAD_ENTER */
    SDLK_KP_EQUALS, /* AKEYCODE_NUMPAD_EQUALS */
    SDLK_KP_LEFTPAREN, /* AKEYCODE_NUMPAD_LEFT_PAREN */
    SDLK_KP_RIGHTPAREN, /* AKEYCODE_NUMPAD_RIGHT_PAREN */
    SDLK_UNKNOWN, /* AKEYCODE_VOLUME_MUTE */
    SDLK_UNKNOWN, /* AKEYCODE_INFO */
    SDLK_UNKNOWN, /* AKEYCODE_CHANNEL_UP */
    SDLK_UNKNOWN, /* AKEYCODE_CHANNEL_DOWN */
    SDLK_UNKNOWN, /* AKEYCODE_ZOOM_IN */
    SDLK_UNKNOWN, /* AKEYCODE_ZOOM_OUT */
    SDLK_UNKNOWN, /* AKEYCODE_TV */
    SDLK_UNKNOWN, /* AKEYCODE_WINDOW */
    SDLK_UNKNOWN, /* AKEYCODE_GUIDE */
    SDLK_UNKNOWN, /* AKEYCODE_DVR */
    SDLK_AC_BOOKMARKS, /* AKEYCODE_BOOKMARK */
    SDLK_UNKNOWN, /* AKEYCODE_CAPTIONS */
    SDLK_UNKNOWN, /* AKEYCODE_SETTINGS */
    SDLK_UNKNOWN, /* AKEYCODE_TV_POWER */
    SDLK_UNKNOWN, /* AKEYCODE_TV_INPUT */
    SDLK_UNKNOWN, /* AKEYCODE_STB_POWER */
    SDLK_UNKNOWN, /* AKEYCODE_STB_INPUT */
    SDLK_UNKNOWN, /* AKEYCODE_AVR_POWER */
    SDLK_UNKNOWN, /* AKEYCODE_AVR_INPUT */
    SDLK_UNKNOWN, /* AKEYCODE_PROG_RED */
    SDLK_UNKNOWN, /* AKEYCODE_PROG_GREEN */
    SDLK_UNKNOWN, /* AKEYCODE_PROG_YELLOW */
    SDLK_UNKNOWN, /* AKEYCODE_PROG_BLUE */
    SDLK_UNKNOWN, /* AKEYCODE_APP_SWITCH */
    SDLK_UNKNOWN, /* AKEYCODE_BUTTON_1 */
    SDLK_UNKNOWN, /* AKEYCODE_BUTTON_2 */
    SDLK_UNKNOWN, /* AKEYCODE_BUTTON_3 */
    SDLK_UNKNOWN, /* AKEYCODE_BUTTON_4 */
    SDLK_UNKNOWN, /* AKEYCODE_BUTTON_5 */
    SDLK_UNKNOWN, /* AKEYCODE_BUTTON_6 */
    SDLK_UNKNOWN, /* AKEYCODE_BUTTON_7 */
    SDLK_UNKNOWN, /* AKEYCODE_BUTTON_8 */
    SDLK_UNKNOWN, /* AKEYCODE_BUTTON_9 */
    SDLK_UNKNOWN, /* AKEYCODE_BUTTON_10 */
    SDLK_UNKNOWN, /* AKEYCODE_BUTTON_11 */
    SDLK_UNKNOWN, /* AKEYCODE_BUTTON_12 */
    SDLK_UNKNOWN, /* AKEYCODE_BUTTON_13 */
    SDLK_UNKNOWN, /* AKEYCODE_BUTTON_14 */
    SDLK_UNKNOWN, /* AKEYCODE_BUTTON_15 */
    SDLK_UNKNOWN, /* AKEYCODE_BUTTON_16 */
    SDLK_UNKNOWN, /* AKEYCODE_LANGUAGE_SWITCH */
    SDLK_UNKNOWN, /* AKEYCODE_MANNER_MODE */
    SDLK_UNKNOWN, /* AKEYCODE_3D_MODE */
    SDLK_UNKNOWN, /* AKEYCODE_CONTACTS */
    SDLK_UNKNOWN, /* AKEYCODE_CALENDAR */
    SDLK_UNKNOWN, /* AKEYCODE_MUSIC */
    SDLK_CALCULATOR, /* AKEYCODE_CALCULATOR */
    SDLK_UNKNOWN, /* AKEYCODE_ZENKAKU_HANKAKU */
    SDLK_UNKNOWN, /* AKEYCODE_EISU */
    SDLK_UNKNOWN, /* AKEYCODE_MUHENKAN */
    SDLK_UNKNOWN, /* AKEYCODE_HENKAN */
    SDLK_UNKNOWN, /* AKEYCODE_KATAKANA_HIRAGANA */
    SDLK_UNKNOWN, /* AKEYCODE_YEN */
    SDLK_UNKNOWN, /* AKEYCODE_RO */
    SDLK_UNKNOWN, /* AKEYCODE_KANA */
    SDLK_UNKNOWN, /* AKEYCODE_ASSIST */
    SDLK_BRIGHTNESSDOWN, /* AKEYCODE_BRIGHTNESS_DOWN */
    SDLK_BRIGHTNESSUP, /* AKEYCODE_BRIGHTNESS_UP */
    SDLK_UNKNOWN, /* AKEYCODE_MEDIA_AUDIO_TRACK */
    SDLK_SLEEP, /* AKEYCODE_SLEEP */
    SDLK_UNKNOWN, /* AKEYCODE_WAKEUP */
    SDLK_UNKNOWN, /* AKEYCODE_PAIRING */
    SDLK_UNKNOWN, /* AKEYCODE_MEDIA_TOP_MENU */
    SDLK_UNKNOWN, /* AKEYCODE_11 */
    SDLK_UNKNOWN, /* AKEYCODE_12 */
    SDLK_UNKNOWN, /* AKEYCODE_LAST_CHANNEL */
    SDLK_UNKNOWN, /* AKEYCODE_TV_DATA_SERVICE */
    SDLK_UNKNOWN, /* AKEYCODE_VOICE_ASSIST */
    SDLK_UNKNOWN, /* AKEYCODE_TV_RADIO_SERVICE */
    SDLK_UNKNOWN, /* AKEYCODE_TV_TELETEXT */
    SDLK_UNKNOWN, /* AKEYCODE_TV_NUMBER_ENTRY */
    SDLK_UNKNOWN, /* AKEYCODE_TV_TERRESTRIAL_ANALOG */
    SDLK_UNKNOWN, /* AKEYCODE_TV_TERRESTRIAL_DIGITAL */
    SDLK_UNKNOWN, /* AKEYCODE_TV_SATELLITE */
    SDLK_UNKNOWN, /* AKEYCODE_TV_SATELLITE_BS */
    SDLK_UNKNOWN, /* AKEYCODE_TV_SATELLITE_CS */
    SDLK_UNKNOWN, /* AKEYCODE_TV_SATELLITE_SERVICE */
    SDLK_UNKNOWN, /* AKEYCODE_TV_NETWORK */
    SDLK_UNKNOWN, /* AKEYCODE_TV_ANTENNA_CABLE */
    SDLK_UNKNOWN, /* AKEYCODE_TV_INPUT_HDMI_1 */
    SDLK_UNKNOWN, /* AKEYCODE_TV_INPUT_HDMI_2 */
    SDLK_UNKNOWN, /* AKEYCODE_TV_INPUT_HDMI_3 */
    SDLK_UNKNOWN, /* AKEYCODE_TV_INPUT_HDMI_4 */
    SDLK_UNKNOWN, /* AKEYCODE_TV_INPUT_COMPOSITE_1 */
    SDLK_UNKNOWN, /* AKEYCODE_TV_INPUT_COMPOSITE_2 */
    SDLK_UNKNOWN, /* AKEYCODE_TV_INPUT_COMPONENT_1 */
    SDLK_UNKNOWN, /* AKEYCODE_TV_INPUT_COMPONENT_2 */
    SDLK_UNKNOWN, /* AKEYCODE_TV_INPUT_VGA_1 */
    SDLK_UNKNOWN, /* AKEYCODE_TV_AUDIO_DESCRIPTION */
    SDLK_UNKNOWN, /* AKEYCODE_TV_AUDIO_DESCRIPTION_MIX_UP */
    SDLK_UNKNOWN, /* AKEYCODE_TV_AUDIO_DESCRIPTION_MIX_DOWN */
    SDLK_UNKNOWN, /* AKEYCODE_TV_ZOOM_MODE */
    SDLK_UNKNOWN, /* AKEYCODE_TV_CONTENTS_MENU */
    SDLK_UNKNOWN, /* AKEYCODE_TV_MEDIA_CONTEXT_MENU */
    SDLK_UNKNOWN, /* AKEYCODE_TV_TIMER_PROGRAMMING */
    SDLK_HELP, /* AKEYCODE_HELP */
    SDLK_UNKNOWN, /* AKEYCODE_NAVIGATE_PREVIOUS */
    SDLK_UNKNOWN, /* AKEYCODE_NAVIGATE_NEXT */
    SDLK_UNKNOWN, /* AKEYCODE_NAVIGATE_IN */
    SDLK_UNKNOWN, /* AKEYCODE_NAVIGATE_OUT */
    SDLK_UNKNOWN, /* AKEYCODE_STEM_PRIMARY */
    SDLK_UNKNOWN, /* AKEYCODE_STEM_1 */
    SDLK_UNKNOWN, /* AKEYCODE_STEM_2 */
    SDLK_UNKNOWN, /* AKEYCODE_STEM_3 */
    SDLK_UNKNOWN, /* AKEYCODE_DPAD_UP_LEFT */
    SDLK_UNKNOWN, /* AKEYCODE_DPAD_DOWN_LEFT */
    SDLK_UNKNOWN, /* AKEYCODE_DPAD_UP_RIGHT */
    SDLK_UNKNOWN, /* AKEYCODE_DPAD_DOWN_RIGHT */
    SDLK_UNKNOWN, /* AKEYCODE_MEDIA_SKIP_FORWARD */
    SDLK_UNKNOWN, /* AKEYCODE_MEDIA_SKIP_BACKWARD */
    SDLK_UNKNOWN, /* AKEYCODE_MEDIA_STEP_FORWARD */
    SDLK_UNKNOWN, /* AKEYCODE_MEDIA_STEP_BACKWARD */
    SDLK_UNKNOWN, /* AKEYCODE_SOFT_SLEEP */
    SDLK_CUT, /* AKEYCODE_CUT */
    SDLK_COPY, /* AKEYCODE_COPY */
    SDLK_PASTE, /* AKEYCODE_PASTE */
};

// Getting annotated stack traces:
// ~/android-sdk/ndk/23.1.7779620/ndk-stack -sym app/build/intermediates/merged_native_libs/release/out/lib/arm64-v8a/ -dump crash-1.txt

struct SDL_Window
{
  EGLDisplay eglDisplay;
  EGLSurface eglSurface;
};

static MapsApp* app = NULL;
static std::thread mainThread;
static SDL_Window sdlWin = {0, 0};

static jobject mapsActivityRef = nullptr;
static jmethodID showTextInputMID = nullptr;
static jmethodID hideTextInputMID = nullptr;
static jmethodID getClipboardMID = nullptr;
static jmethodID setClipboardMID = nullptr;
static jmethodID openFileMID = nullptr;
static jmethodID openUrlMID = nullptr;

// since Android event loop waits on MapsApp::taskQueue, no need for PLATFORM_WakeEventLoop
void PLATFORM_WakeEventLoop() {}
void TANGRAM_WakeEventLoop() { MapsApp::runOnMainThread([](){}); }

Uint32 SDL_GetTicks()
{
  static Timestamp t0 = mSecSinceEpoch();
  return mSecSinceEpoch() - t0;
}

char* SDL_GetClipboardText()
{
  JniThreadBinding jniEnv(JniHelpers::getJVM());

  auto jtext = (jstring)(jniEnv->CallObjectMethod(mapsActivityRef, getClipboardMID));
  const char* text = jniEnv->GetStringUTFChars(jtext, 0);
  char* out = NULL;
  if(text && text[0]) {
    out = (char*)malloc(strlen(text)+1);  // caller frees w/ SDL_free()
    strcpy(out, text);
  }
  jniEnv->ReleaseStringUTFChars(jtext, text);
  return out;
}

SDL_bool SDL_HasClipboardText()
{
  char* text = SDL_GetClipboardText();
  if(text) SDL_free(text);
  return text != NULL ? SDL_TRUE : SDL_FALSE;
}

int SDL_SetClipboardText(const char* text)
{
  JniThreadBinding jniEnv(JniHelpers::getJVM());
  jstring jtext = jniEnv->NewStringUTF(text);
  jniEnv->CallVoidMethod(mapsActivityRef, setClipboardMID, jtext);
  return 0;
}

void SDL_free(void* mem) { free(mem); }

SDL_Keymod SDL_GetModState()
{
  return (SDL_Keymod)(0);
}

void SDL_SetTextInputRect(SDL_Rect* rect)
{
  JniThreadBinding jniEnv(JniHelpers::getJVM());
  jniEnv->CallVoidMethod(mapsActivityRef, showTextInputMID, rect->x, rect->y, rect->w, rect->h);
}

SDL_bool SDL_IsTextInputActive() { return SDL_FALSE; }
void SDL_StartTextInput() {}
void SDL_StopTextInput()
{
  JniThreadBinding jniEnv(JniHelpers::getJVM());
  jniEnv->CallVoidMethod(mapsActivityRef, hideTextInputMID);
}

void SDL_RaiseWindow(SDL_Window* window) {}
void SDL_SetWindowTitle(SDL_Window* win, const char* title) {}
void SDL_GetWindowSize(SDL_Window* win, int* w, int* h) //{ SDL_GL_GetDrawableSize(win, w, h); }
{
  if(!win || !win->eglDisplay || !win->eglSurface) { *w = 1000; *h = 1000; return; }
  eglQuerySurface(win->eglDisplay, win->eglSurface, EGL_WIDTH, w);
  eglQuerySurface(win->eglDisplay, win->eglSurface, EGL_HEIGHT, h);
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

// open file dialog
static MapsApp::OpenFileFn_t openFileCallback;

// filters ignored for now
void MapsApp::openFileDialog(std::vector<FileDialogFilter_t>, OpenFileFn_t callback)
{
  openFileCallback = callback;
  JniThreadBinding jniEnv(JniHelpers::getJVM());
  jniEnv->CallVoidMethod(mapsActivityRef, openFileMID);
}

bool MapsApp::openURL(const char* url)
{
  JniThreadBinding jniEnv(JniHelpers::getJVM());
  jstring jurl = jniEnv->NewStringUTF(url);
  jniEnv->CallVoidMethod(mapsActivityRef, openUrlMID, jurl);
  return true;
}

// EGL setup and main loop

bool chooseConfig(EGLDisplay display, int depth, int samples, EGLConfig* config)
{
  constexpr int rgb = 8, alpha = 0, stencil = 8;  // in other situations we might vary these
  const EGLint attribs[] = {
    EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
    EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT_KHR,  //EGL_OPENGL_ES2_BIT,
    EGL_BLUE_SIZE, rgb,
    EGL_GREEN_SIZE, rgb,
    EGL_RED_SIZE, rgb,
    EGL_ALPHA_SIZE, alpha,
    EGL_DEPTH_SIZE, depth,
    EGL_STENCIL_SIZE, stencil,
    EGL_SAMPLES, samples,
    EGL_NONE // The list is terminated with EGL_NONE
  };
  EGLint numConfigs = 0;
  eglChooseConfig(display, attribs, config, 1, &numConfigs);
  return numConfigs;
}

// http://directx.com/2014/06/egl-understanding-eglchooseconfig-then-ignoring-it/
//struct Config {
//  EGLConfig cfg; int rgb; int a; int depth; int samps;
//  bool operator<(const Config& rhs) {
//    return std::tie (rhs.rgb, depth, samps) < std::tie (rgb, rhs.depth, rhs.samps);
//  }
//};

// see github.com/tsaarni/android-native-egl-example
int eglMain(ANativeWindow* nativeWin, float dpi)
{
  static EGLContext context = EGL_NO_CONTEXT;
  if(!nativeWin) { LOGE("ANativeWindow_fromSurface returned NULL!"); return -1; }

  EGLDisplay display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
  if(display == EGL_NO_DISPLAY) { LOGE("eglGetDisplay() error %X", eglGetError()); return -1; }

  auto init_res = eglInitialize(display, 0, 0);
  if(init_res == EGL_FALSE) { LOGE("eglInitialize() error %X", eglGetError()); return -1; }

  EGLConfig config;
  bool ok = chooseConfig(display, 24, 4, &config) || chooseConfig(display, 24, 2, &config)
      || chooseConfig(display, 24, 0, &config) || chooseConfig(display, 16, 0, &config);
  if(!ok) { LOGE("eglChooseConfig failed"); return -1; }

  if(context == EGL_NO_CONTEXT) {
    const EGLint eglAttribs[] = {EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE};
    context = eglCreateContext(display, config, nullptr, eglAttribs);
    if(!context) { LOGE("eglCreateContext() error %X", eglGetError()); return -1; }
    if(app) app->glNeedsInit = true;
  }

  EGLSurface surface = eglCreateWindowSurface(display, config, nativeWin, NULL);
  if(!surface) { LOGE("eglCreateWindowSurface() error %X", eglGetError()); return -1; }

  auto curr_res = eglMakeCurrent(display, surface, surface, context);
  //if(eglGetError() == EGL_CONTEXT_LOST) { context = 0; return eglMain(nativeWin); }
  if(curr_res == EGL_FALSE) { LOGE("eglMakeCurrent() error %X", eglGetError()); return -1; }

  sdlWin = {display, surface};
  if(!app) {
    app = new MapsApp(MapsApp::platform);
    app->createGUI(&sdlWin);
    app->mapsOffline->resumeDownloads();
    app->mapsSources->rebuildSource(app->config["sources"]["last_source"].Scalar());
  }
  app->setDpi(dpi);
  MapsApp::runApplication = true;
  //LOGW("Starting event loop");
  while(MapsApp::runApplication) {
    MapsApp::taskQueue.wait();

    int fbWidth = 0, fbHeight = 0;
    eglQuerySurface(display, surface, EGL_WIDTH, &fbWidth);
    eglQuerySurface(display, surface, EGL_HEIGHT, &fbHeight);
    if(app->drawFrame(fbWidth, fbHeight))
      eglSwapBuffers(display, surface);
  }
  //LOGW("Stopping event loop");
  sdlWin = {0, 0};
  //eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
  eglDestroySurface(display, surface);
  //eglDestroyContext(display, context);
  ANativeWindow_release(nativeWin);
  return 0;
}

#define TANGRAM_JNI_VERSION JNI_VERSION_1_6

extern "C" JNIEXPORT jint JNI_OnLoad(JavaVM* javaVM, void*)
{
  static const char* ctrlClassName = "com/styluslabs/maps/MapsActivity";
  JNIEnv* jniEnv = nullptr;
  if (javaVM->GetEnv(reinterpret_cast<void**>(&jniEnv), TANGRAM_JNI_VERSION) != JNI_OK)
    return -1;
  JniHelpers::jniOnLoad(javaVM, NULL);  // pass NULL jniEnv to skip stuff used for full Android library
  AndroidPlatform::jniOnLoad(javaVM, jniEnv, ctrlClassName);

  jclass cls = jniEnv->FindClass(ctrlClassName);
  showTextInputMID = jniEnv->GetMethodID(cls, "showTextInput", "(IIII)V");
  hideTextInputMID = jniEnv->GetMethodID(cls, "hideTextInput", "()V");
  getClipboardMID = jniEnv->GetMethodID(cls, "getClipboard", "()Ljava/lang/String;");
  setClipboardMID = jniEnv->GetMethodID(cls, "setClipboard", "(Ljava/lang/String;)V");
  openFileMID = jniEnv->GetMethodID(cls, "openFile", "()V");
  openUrlMID = jniEnv->GetMethodID(cls, "openUrl", "(Ljava/lang/String;)V");

  return TANGRAM_JNI_VERSION;
}

#define JNI_FN(name) extern "C" JNIEXPORT void JNICALL Java_com_styluslabs_maps_MapsLib_##name

JNI_FN(init)(JNIEnv* env, jclass, jobject mapsActivity, jobject assetManager, jstring extFileDir)
{
  mapsActivityRef = env->NewWeakGlobalRef(mapsActivity);

  MapsApp::baseDir = JniHelpers::stringFromJavaString(env, extFileDir) + "/";
  FSPath configPath(MapsApp::baseDir, "config.yaml");
  MapsApp::configFile = configPath.c_str();

  try {
    MapsApp::config = YAML::LoadFile(configPath.exists() ? configPath.path
        : configPath.parent().childPath(configPath.baseName() + ".default.yaml"));
  } catch(...) {
    LOGE("Unable to load config file %s", configPath.c_str());
    *(volatile int*)0 = 0;  //exit(1) -- Android repeatedly restarts app
  }

  MapsApp::platform = new AndroidPlatform(env, mapsActivity, assetManager);

  //ImGui::GetIO().ImeSetInputScreenPosFn = [](int x, int y){
  //  JniThreadBinding jniEnv(JniHelpers::getJVM());
  //  jniEnv->CallVoidMethod(mapsActivityRef, showTextInputMID, x, y, 20, 20);
  //};
}

JNI_FN(destroy)(JNIEnv* env, jclass)
{
  delete app;
  app = NULL;
}

JNI_FN(surfaceCreated)(JNIEnv* env, jclass, jobject jsurface, jfloat dpi)
{
  ANativeWindow* nativeWin = ANativeWindow_fromSurface(env, jsurface);
  if(!nativeWin || mainThread.joinable()) return;
  mainThread = std::thread(eglMain, nativeWin, dpi);
}

JNI_FN(surfaceDestroyed)(JNIEnv* env, jclass)
{
  MapsApp::runOnMainThread([=](){ MapsApp::runApplication = false; });
  if(mainThread.joinable())
    mainThread.join();
}

JNI_FN(resize)(JNIEnv* env, jclass, jint w, jint h)
{
  SDL_Event event = {0};
  event.type = SDL_WINDOWEVENT;
  event.window.event = SDL_WINDOWEVENT_SIZE_CHANGED;
  event.window.data1 = w;
  event.window.data2 = h;
  MapsApp::sdlEvent(&event);
  //MapsApp::runOnMainThread([=](){ app->onResize(w, h, w, h); });
}

//JNI_FN(setupGL)(JNIEnv* env, jobject obj)
//{
//  app->map->setupGL();
//}
//
//JNI_FN(drawFrame)(JNIEnv* env, jobject obj)
//{
//  app->drawFrame(currTime);
//}

JNI_FN(onPause)(JNIEnv* env, jclass)
{
  MapsApp::runOnMainThread([=](){ app->onSuspend(); });
}

JNI_FN(touchEvent)(JNIEnv* env, jclass, jint ptrId, jint action, jint t, jfloat x, jfloat y, jfloat p)
{
  // ACTION_DOWN = 0, ACTION_UP = 1, ACTION_MOVE = 2, ACTION_CANCEL = 3,
  //  ACTION_OUTSIDE = 4, ACTION_POINTER_DOWN = 5, ACTION_POINTER_UP = 6
  static const int actionToSDL[] =
      {SDL_FINGERDOWN, SDL_FINGERUP, SDL_FINGERMOTION, SVGGUI_FINGERCANCEL, 0, SDL_FINGERDOWN, SDL_FINGERUP};

  SDL_Event event = {0};
  event.type = actionToSDL[action];
  event.tfinger.touchId = 0;
  event.tfinger.fingerId = ptrId;
  event.tfinger.x = x;
  event.tfinger.y = y;
  event.tfinger.dx = 0;  //button;
  event.tfinger.dy = 0;  //device->buttons;
  event.tfinger.pressure = p;
  event.tfinger.timestamp = t;  // ms
  MapsApp::sdlEvent(&event);
}

JNI_FN(charInput)(JNIEnv* env, jclass, jint cp, jint cursorPos)
{
  static const uint8_t firstByteMark[7] = { 0x00, 0x00, 0xC0, 0xE0, 0xF0, 0xF8, 0xFC };
  unsigned short bytesToWrite = 0;
  const uint32_t byteMask = 0xBF;
  const uint32_t byteMark = 0x80;

  SDL_Event event = {0};
  event.text.type = SDL_TEXTINPUT;
  event.text.windowID = 0;  //keyboard->focus ? keyboard->focus->id : 0;

  // UTF-32 codepoint to UTF-8
  if(cp < 0x80) bytesToWrite = 1;
  else if(cp < 0x800) bytesToWrite = 2;
  else if(cp < 0x10000) bytesToWrite = 3;
  else bytesToWrite = 4;

  uint8_t* out = (uint8_t*)event.text.text;
  switch (bytesToWrite) {
    case 4: out[3] = (uint8_t)((cp | byteMark) & byteMask); cp >>= 6;
    case 3: out[2] = (uint8_t)((cp | byteMark) & byteMask); cp >>= 6;
    case 2: out[1] = (uint8_t)((cp | byteMark) & byteMask); cp >>= 6;
    case 1: out[0] = (uint8_t) (cp | firstByteMark[bytesToWrite]);
  }
  out[bytesToWrite] = '\0';

  MapsApp::sdlEvent(&event);
}

JNI_FN(keyEvent)(JNIEnv* env, jclass, jint key, jint action)
{
  SDL_Event event = {0};
  event.key.type = action < 0 ? SDL_KEYUP : SDL_KEYDOWN;
  event.key.state = action < 0 ? SDL_RELEASED : SDL_PRESSED;
  event.key.repeat = 0;  //action == GLFW_REPEAT;
  event.key.keysym.scancode = (SDL_Scancode)key;
  event.key.keysym.sym = Android_Keycodes[key];  //key < 0 || key > GLFW_KEY_LAST ? SDLK_UNKNOWN : keyMap[key];
  //event.key.keysym.mod = (mods & GLFW_MOD_SHIFT ? KMOD_SHIFT : 0) | (mods & GLFW_MOD_CONTROL ? KMOD_CTRL : 0)
  //    | (mods & GLFW_MOD_ALT ? KMOD_ALT : 0) | (mods & GLFW_MOD_SUPER ? KMOD_GUI : 0)
  //    | (mods & GLFW_MOD_CAPS_LOCK ? KMOD_CAPS : 0) | (mods & GLFW_MOD_NUM_LOCK ? KMOD_NUM : 0);
  event.key.windowID = 0;  //keyboard->focus ? keyboard->focus->id : 0;
  MapsApp::sdlEvent(&event);
}

JNI_FN(onUrlComplete)(JNIEnv* env, jclass, jlong handle, jbyteArray data, jstring err)
{
  static_cast<AndroidPlatform*>(MapsApp::platform)->onUrlComplete(env, handle, data, err);
}

JNI_FN(updateLocation)(JNIEnv* env, jclass, long time, double lat, double lng, float poserr,
    double alt, float alterr, float dir, float direrr, float spd, float spderr)
{
  MapsApp::runOnMainThread([=](){
    app->updateLocation(Location{time/1000.0, lat, lng, poserr, alt, alterr, dir, direrr, spd, spderr});
  });
}

JNI_FN(updateOrientation)(JNIEnv* env, jclass, jfloat azimuth, jfloat pitch, jfloat roll)
{
  MapsApp::runOnMainThread([=](){ app->updateOrientation(azimuth, pitch, roll); });
}

JNI_FN(updateGpsStatus)(JNIEnv* env, jclass, int satsVisible, int satsUsed)
{
  MapsApp::runOnMainThread([=](){ app->updateGpsStatus(satsVisible, satsUsed); });
}

JNI_FN(openFileDesc)(JNIEnv* env, jclass, jstring jfilename, jint jfd)
{
  char buff[256];
  int len = readlink(("/proc/self/fd/" + std::to_string(jfd)).c_str(), buff, 256);
  if(len > 0 && len < 256) {
    buff[len] = '\0';
    // This hack seems to require storage permission (API 29 app on Android 13); we'll see what happens with later API version!
    // another option: https://sqlite.org/forum/info/57aaaf20cf703d301fed5aeaef59e70723f1d9454fb3a4e6383b2bfac6897e5a
    // last resort is reading into a file in app's cache directory in Java
    std::string s(buff);
    size_t pos = s.find("/mnt/user/0/");
    if(pos != std::string::npos)
      s.replace(pos, sizeof("/mnt/user/0/") - 1, "/storage/");
    //PLATFORM_LOG("readlink returned: %s\n", buff);
    //const char* filename = env->GetStringUTFChars(jfilename, 0);
    if(openFileCallback)
      MapsApp::runOnMainThread([s, cb=std::move(openFileCallback)](){ cb(s.c_str()); });
    //env->ReleaseStringUTFChars(jfilename, filename);
  }
  openFileCallback = {};  // clear
}
