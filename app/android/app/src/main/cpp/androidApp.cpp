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

static SDL_Scancode Android_Keycodes[] = {
    SDL_SCANCODE_UNKNOWN, /* AKEYCODE_UNKNOWN */
    SDL_SCANCODE_UNKNOWN, /* AKEYCODE_SOFT_LEFT */
    SDL_SCANCODE_UNKNOWN, /* AKEYCODE_SOFT_RIGHT */
    SDL_SCANCODE_AC_HOME, /* AKEYCODE_HOME */
    SDL_SCANCODE_AC_BACK, /* AKEYCODE_BACK */
    SDL_SCANCODE_UNKNOWN, /* AKEYCODE_CALL */
    SDL_SCANCODE_UNKNOWN, /* AKEYCODE_ENDCALL */
    SDL_SCANCODE_0, /* AKEYCODE_0 */
    SDL_SCANCODE_1, /* AKEYCODE_1 */
    SDL_SCANCODE_2, /* AKEYCODE_2 */
    SDL_SCANCODE_3, /* AKEYCODE_3 */
    SDL_SCANCODE_4, /* AKEYCODE_4 */
    SDL_SCANCODE_5, /* AKEYCODE_5 */
    SDL_SCANCODE_6, /* AKEYCODE_6 */
    SDL_SCANCODE_7, /* AKEYCODE_7 */
    SDL_SCANCODE_8, /* AKEYCODE_8 */
    SDL_SCANCODE_9, /* AKEYCODE_9 */
    SDL_SCANCODE_UNKNOWN, /* AKEYCODE_STAR */
    SDL_SCANCODE_UNKNOWN, /* AKEYCODE_POUND */
    SDL_SCANCODE_UP, /* AKEYCODE_DPAD_UP */
    SDL_SCANCODE_DOWN, /* AKEYCODE_DPAD_DOWN */
    SDL_SCANCODE_LEFT, /* AKEYCODE_DPAD_LEFT */
    SDL_SCANCODE_RIGHT, /* AKEYCODE_DPAD_RIGHT */
    SDL_SCANCODE_SELECT, /* AKEYCODE_DPAD_CENTER */
    SDL_SCANCODE_VOLUMEUP, /* AKEYCODE_VOLUME_UP */
    SDL_SCANCODE_VOLUMEDOWN, /* AKEYCODE_VOLUME_DOWN */
    SDL_SCANCODE_POWER, /* AKEYCODE_POWER */
    SDL_SCANCODE_UNKNOWN, /* AKEYCODE_CAMERA */
    SDL_SCANCODE_CLEAR, /* AKEYCODE_CLEAR */
    SDL_SCANCODE_A, /* AKEYCODE_A */
    SDL_SCANCODE_B, /* AKEYCODE_B */
    SDL_SCANCODE_C, /* AKEYCODE_C */
    SDL_SCANCODE_D, /* AKEYCODE_D */
    SDL_SCANCODE_E, /* AKEYCODE_E */
    SDL_SCANCODE_F, /* AKEYCODE_F */
    SDL_SCANCODE_G, /* AKEYCODE_G */
    SDL_SCANCODE_H, /* AKEYCODE_H */
    SDL_SCANCODE_I, /* AKEYCODE_I */
    SDL_SCANCODE_J, /* AKEYCODE_J */
    SDL_SCANCODE_K, /* AKEYCODE_K */
    SDL_SCANCODE_L, /* AKEYCODE_L */
    SDL_SCANCODE_M, /* AKEYCODE_M */
    SDL_SCANCODE_N, /* AKEYCODE_N */
    SDL_SCANCODE_O, /* AKEYCODE_O */
    SDL_SCANCODE_P, /* AKEYCODE_P */
    SDL_SCANCODE_Q, /* AKEYCODE_Q */
    SDL_SCANCODE_R, /* AKEYCODE_R */
    SDL_SCANCODE_S, /* AKEYCODE_S */
    SDL_SCANCODE_T, /* AKEYCODE_T */
    SDL_SCANCODE_U, /* AKEYCODE_U */
    SDL_SCANCODE_V, /* AKEYCODE_V */
    SDL_SCANCODE_W, /* AKEYCODE_W */
    SDL_SCANCODE_X, /* AKEYCODE_X */
    SDL_SCANCODE_Y, /* AKEYCODE_Y */
    SDL_SCANCODE_Z, /* AKEYCODE_Z */
    SDL_SCANCODE_COMMA, /* AKEYCODE_COMMA */
    SDL_SCANCODE_PERIOD, /* AKEYCODE_PERIOD */
    SDL_SCANCODE_LALT, /* AKEYCODE_ALT_LEFT */
    SDL_SCANCODE_RALT, /* AKEYCODE_ALT_RIGHT */
    SDL_SCANCODE_LSHIFT, /* AKEYCODE_SHIFT_LEFT */
    SDL_SCANCODE_RSHIFT, /* AKEYCODE_SHIFT_RIGHT */
    SDL_SCANCODE_TAB, /* AKEYCODE_TAB */
    SDL_SCANCODE_SPACE, /* AKEYCODE_SPACE */
    SDL_SCANCODE_UNKNOWN, /* AKEYCODE_SYM */
    SDL_SCANCODE_WWW, /* AKEYCODE_EXPLORER */
    SDL_SCANCODE_MAIL, /* AKEYCODE_ENVELOPE */
    SDL_SCANCODE_RETURN, /* AKEYCODE_ENTER */
    SDL_SCANCODE_BACKSPACE, /* AKEYCODE_DEL */
    SDL_SCANCODE_GRAVE, /* AKEYCODE_GRAVE */
    SDL_SCANCODE_MINUS, /* AKEYCODE_MINUS */
    SDL_SCANCODE_EQUALS, /* AKEYCODE_EQUALS */
    SDL_SCANCODE_LEFTBRACKET, /* AKEYCODE_LEFT_BRACKET */
    SDL_SCANCODE_RIGHTBRACKET, /* AKEYCODE_RIGHT_BRACKET */
    SDL_SCANCODE_BACKSLASH, /* AKEYCODE_BACKSLASH */
    SDL_SCANCODE_SEMICOLON, /* AKEYCODE_SEMICOLON */
    SDL_SCANCODE_APOSTROPHE, /* AKEYCODE_APOSTROPHE */
    SDL_SCANCODE_SLASH, /* AKEYCODE_SLASH */
    SDL_SCANCODE_UNKNOWN, /* AKEYCODE_AT */
    SDL_SCANCODE_UNKNOWN, /* AKEYCODE_NUM */
    SDL_SCANCODE_UNKNOWN, /* AKEYCODE_HEADSETHOOK */
    SDL_SCANCODE_UNKNOWN, /* AKEYCODE_FOCUS */
    SDL_SCANCODE_UNKNOWN, /* AKEYCODE_PLUS */
    SDL_SCANCODE_MENU, /* AKEYCODE_MENU */
    SDL_SCANCODE_UNKNOWN, /* AKEYCODE_NOTIFICATION */
    SDL_SCANCODE_AC_SEARCH, /* AKEYCODE_SEARCH */
    SDL_SCANCODE_AUDIOPLAY, /* AKEYCODE_MEDIA_PLAY_PAUSE */
    SDL_SCANCODE_AUDIOSTOP, /* AKEYCODE_MEDIA_STOP */
    SDL_SCANCODE_AUDIONEXT, /* AKEYCODE_MEDIA_NEXT */
    SDL_SCANCODE_AUDIOPREV, /* AKEYCODE_MEDIA_PREVIOUS */
    SDL_SCANCODE_AUDIOREWIND, /* AKEYCODE_MEDIA_REWIND */
    SDL_SCANCODE_AUDIOFASTFORWARD, /* AKEYCODE_MEDIA_FAST_FORWARD */
    SDL_SCANCODE_MUTE, /* AKEYCODE_MUTE */
    SDL_SCANCODE_PAGEUP, /* AKEYCODE_PAGE_UP */
    SDL_SCANCODE_PAGEDOWN, /* AKEYCODE_PAGE_DOWN */
    SDL_SCANCODE_UNKNOWN, /* AKEYCODE_PICTSYMBOLS */
    SDL_SCANCODE_UNKNOWN, /* AKEYCODE_SWITCH_CHARSET */
    SDL_SCANCODE_UNKNOWN, /* AKEYCODE_BUTTON_A */
    SDL_SCANCODE_UNKNOWN, /* AKEYCODE_BUTTON_B */
    SDL_SCANCODE_UNKNOWN, /* AKEYCODE_BUTTON_C */
    SDL_SCANCODE_UNKNOWN, /* AKEYCODE_BUTTON_X */
    SDL_SCANCODE_UNKNOWN, /* AKEYCODE_BUTTON_Y */
    SDL_SCANCODE_UNKNOWN, /* AKEYCODE_BUTTON_Z */
    SDL_SCANCODE_UNKNOWN, /* AKEYCODE_BUTTON_L1 */
    SDL_SCANCODE_UNKNOWN, /* AKEYCODE_BUTTON_R1 */
    SDL_SCANCODE_UNKNOWN, /* AKEYCODE_BUTTON_L2 */
    SDL_SCANCODE_UNKNOWN, /* AKEYCODE_BUTTON_R2 */
    SDL_SCANCODE_UNKNOWN, /* AKEYCODE_BUTTON_THUMBL */
    SDL_SCANCODE_UNKNOWN, /* AKEYCODE_BUTTON_THUMBR */
    SDL_SCANCODE_UNKNOWN, /* AKEYCODE_BUTTON_START */
    SDL_SCANCODE_UNKNOWN, /* AKEYCODE_BUTTON_SELECT */
    SDL_SCANCODE_UNKNOWN, /* AKEYCODE_BUTTON_MODE */
    SDL_SCANCODE_ESCAPE, /* AKEYCODE_ESCAPE */
    SDL_SCANCODE_DELETE, /* AKEYCODE_FORWARD_DEL */
    SDL_SCANCODE_LCTRL, /* AKEYCODE_CTRL_LEFT */
    SDL_SCANCODE_RCTRL, /* AKEYCODE_CTRL_RIGHT */
    SDL_SCANCODE_CAPSLOCK, /* AKEYCODE_CAPS_LOCK */
    SDL_SCANCODE_SCROLLLOCK, /* AKEYCODE_SCROLL_LOCK */
    SDL_SCANCODE_LGUI, /* AKEYCODE_META_LEFT */
    SDL_SCANCODE_RGUI, /* AKEYCODE_META_RIGHT */
    SDL_SCANCODE_UNKNOWN, /* AKEYCODE_FUNCTION */
    SDL_SCANCODE_PRINTSCREEN, /* AKEYCODE_SYSRQ */
    SDL_SCANCODE_PAUSE, /* AKEYCODE_BREAK */
    SDL_SCANCODE_HOME, /* AKEYCODE_MOVE_HOME */
    SDL_SCANCODE_END, /* AKEYCODE_MOVE_END */
    SDL_SCANCODE_INSERT, /* AKEYCODE_INSERT */
    SDL_SCANCODE_AC_FORWARD, /* AKEYCODE_FORWARD */
    SDL_SCANCODE_UNKNOWN, /* AKEYCODE_MEDIA_PLAY */
    SDL_SCANCODE_UNKNOWN, /* AKEYCODE_MEDIA_PAUSE */
    SDL_SCANCODE_UNKNOWN, /* AKEYCODE_MEDIA_CLOSE */
    SDL_SCANCODE_EJECT, /* AKEYCODE_MEDIA_EJECT */
    SDL_SCANCODE_UNKNOWN, /* AKEYCODE_MEDIA_RECORD */
    SDL_SCANCODE_F1, /* AKEYCODE_F1 */
    SDL_SCANCODE_F2, /* AKEYCODE_F2 */
    SDL_SCANCODE_F3, /* AKEYCODE_F3 */
    SDL_SCANCODE_F4, /* AKEYCODE_F4 */
    SDL_SCANCODE_F5, /* AKEYCODE_F5 */
    SDL_SCANCODE_F6, /* AKEYCODE_F6 */
    SDL_SCANCODE_F7, /* AKEYCODE_F7 */
    SDL_SCANCODE_F8, /* AKEYCODE_F8 */
    SDL_SCANCODE_F9, /* AKEYCODE_F9 */
    SDL_SCANCODE_F10, /* AKEYCODE_F10 */
    SDL_SCANCODE_F11, /* AKEYCODE_F11 */
    SDL_SCANCODE_F12, /* AKEYCODE_F12 */
    SDL_SCANCODE_UNKNOWN, /* AKEYCODE_NUM_LOCK */
    SDL_SCANCODE_KP_0, /* AKEYCODE_NUMPAD_0 */
    SDL_SCANCODE_KP_1, /* AKEYCODE_NUMPAD_1 */
    SDL_SCANCODE_KP_2, /* AKEYCODE_NUMPAD_2 */
    SDL_SCANCODE_KP_3, /* AKEYCODE_NUMPAD_3 */
    SDL_SCANCODE_KP_4, /* AKEYCODE_NUMPAD_4 */
    SDL_SCANCODE_KP_5, /* AKEYCODE_NUMPAD_5 */
    SDL_SCANCODE_KP_6, /* AKEYCODE_NUMPAD_6 */
    SDL_SCANCODE_KP_7, /* AKEYCODE_NUMPAD_7 */
    SDL_SCANCODE_KP_8, /* AKEYCODE_NUMPAD_8 */
    SDL_SCANCODE_KP_9, /* AKEYCODE_NUMPAD_9 */
    SDL_SCANCODE_KP_DIVIDE, /* AKEYCODE_NUMPAD_DIVIDE */
    SDL_SCANCODE_KP_MULTIPLY, /* AKEYCODE_NUMPAD_MULTIPLY */
    SDL_SCANCODE_KP_MINUS, /* AKEYCODE_NUMPAD_SUBTRACT */
    SDL_SCANCODE_KP_PLUS, /* AKEYCODE_NUMPAD_ADD */
    SDL_SCANCODE_KP_PERIOD, /* AKEYCODE_NUMPAD_DOT */
    SDL_SCANCODE_KP_COMMA, /* AKEYCODE_NUMPAD_COMMA */
    SDL_SCANCODE_KP_ENTER, /* AKEYCODE_NUMPAD_ENTER */
    SDL_SCANCODE_KP_EQUALS, /* AKEYCODE_NUMPAD_EQUALS */
    SDL_SCANCODE_KP_LEFTPAREN, /* AKEYCODE_NUMPAD_LEFT_PAREN */
    SDL_SCANCODE_KP_RIGHTPAREN, /* AKEYCODE_NUMPAD_RIGHT_PAREN */
    SDL_SCANCODE_UNKNOWN, /* AKEYCODE_VOLUME_MUTE */
    SDL_SCANCODE_UNKNOWN, /* AKEYCODE_INFO */
    SDL_SCANCODE_UNKNOWN, /* AKEYCODE_CHANNEL_UP */
    SDL_SCANCODE_UNKNOWN, /* AKEYCODE_CHANNEL_DOWN */
    SDL_SCANCODE_UNKNOWN, /* AKEYCODE_ZOOM_IN */
    SDL_SCANCODE_UNKNOWN, /* AKEYCODE_ZOOM_OUT */
    SDL_SCANCODE_UNKNOWN, /* AKEYCODE_TV */
    SDL_SCANCODE_UNKNOWN, /* AKEYCODE_WINDOW */
    SDL_SCANCODE_UNKNOWN, /* AKEYCODE_GUIDE */
    SDL_SCANCODE_UNKNOWN, /* AKEYCODE_DVR */
    SDL_SCANCODE_AC_BOOKMARKS, /* AKEYCODE_BOOKMARK */
    SDL_SCANCODE_UNKNOWN, /* AKEYCODE_CAPTIONS */
    SDL_SCANCODE_UNKNOWN, /* AKEYCODE_SETTINGS */
    SDL_SCANCODE_UNKNOWN, /* AKEYCODE_TV_POWER */
    SDL_SCANCODE_UNKNOWN, /* AKEYCODE_TV_INPUT */
    SDL_SCANCODE_UNKNOWN, /* AKEYCODE_STB_POWER */
    SDL_SCANCODE_UNKNOWN, /* AKEYCODE_STB_INPUT */
    SDL_SCANCODE_UNKNOWN, /* AKEYCODE_AVR_POWER */
    SDL_SCANCODE_UNKNOWN, /* AKEYCODE_AVR_INPUT */
    SDL_SCANCODE_UNKNOWN, /* AKEYCODE_PROG_RED */
    SDL_SCANCODE_UNKNOWN, /* AKEYCODE_PROG_GREEN */
    SDL_SCANCODE_UNKNOWN, /* AKEYCODE_PROG_YELLOW */
    SDL_SCANCODE_UNKNOWN, /* AKEYCODE_PROG_BLUE */
    SDL_SCANCODE_UNKNOWN, /* AKEYCODE_APP_SWITCH */
    SDL_SCANCODE_UNKNOWN, /* AKEYCODE_BUTTON_1 */
    SDL_SCANCODE_UNKNOWN, /* AKEYCODE_BUTTON_2 */
    SDL_SCANCODE_UNKNOWN, /* AKEYCODE_BUTTON_3 */
    SDL_SCANCODE_UNKNOWN, /* AKEYCODE_BUTTON_4 */
    SDL_SCANCODE_UNKNOWN, /* AKEYCODE_BUTTON_5 */
    SDL_SCANCODE_UNKNOWN, /* AKEYCODE_BUTTON_6 */
    SDL_SCANCODE_UNKNOWN, /* AKEYCODE_BUTTON_7 */
    SDL_SCANCODE_UNKNOWN, /* AKEYCODE_BUTTON_8 */
    SDL_SCANCODE_UNKNOWN, /* AKEYCODE_BUTTON_9 */
    SDL_SCANCODE_UNKNOWN, /* AKEYCODE_BUTTON_10 */
    SDL_SCANCODE_UNKNOWN, /* AKEYCODE_BUTTON_11 */
    SDL_SCANCODE_UNKNOWN, /* AKEYCODE_BUTTON_12 */
    SDL_SCANCODE_UNKNOWN, /* AKEYCODE_BUTTON_13 */
    SDL_SCANCODE_UNKNOWN, /* AKEYCODE_BUTTON_14 */
    SDL_SCANCODE_UNKNOWN, /* AKEYCODE_BUTTON_15 */
    SDL_SCANCODE_UNKNOWN, /* AKEYCODE_BUTTON_16 */
    SDL_SCANCODE_UNKNOWN, /* AKEYCODE_LANGUAGE_SWITCH */
    SDL_SCANCODE_UNKNOWN, /* AKEYCODE_MANNER_MODE */
    SDL_SCANCODE_UNKNOWN, /* AKEYCODE_3D_MODE */
    SDL_SCANCODE_UNKNOWN, /* AKEYCODE_CONTACTS */
    SDL_SCANCODE_UNKNOWN, /* AKEYCODE_CALENDAR */
    SDL_SCANCODE_UNKNOWN, /* AKEYCODE_MUSIC */
    SDL_SCANCODE_CALCULATOR, /* AKEYCODE_CALCULATOR */
    SDL_SCANCODE_LANG5, /* AKEYCODE_ZENKAKU_HANKAKU */
    SDL_SCANCODE_UNKNOWN, /* AKEYCODE_EISU */
    SDL_SCANCODE_INTERNATIONAL5, /* AKEYCODE_MUHENKAN */
    SDL_SCANCODE_INTERNATIONAL4, /* AKEYCODE_HENKAN */
    SDL_SCANCODE_LANG3, /* AKEYCODE_KATAKANA_HIRAGANA */
    SDL_SCANCODE_INTERNATIONAL3, /* AKEYCODE_YEN */
    SDL_SCANCODE_UNKNOWN, /* AKEYCODE_RO */
    SDL_SCANCODE_UNKNOWN, /* AKEYCODE_KANA */
    SDL_SCANCODE_UNKNOWN, /* AKEYCODE_ASSIST */
    SDL_SCANCODE_BRIGHTNESSDOWN, /* AKEYCODE_BRIGHTNESS_DOWN */
    SDL_SCANCODE_BRIGHTNESSUP, /* AKEYCODE_BRIGHTNESS_UP */
    SDL_SCANCODE_UNKNOWN, /* AKEYCODE_MEDIA_AUDIO_TRACK */
    SDL_SCANCODE_SLEEP, /* AKEYCODE_SLEEP */
    SDL_SCANCODE_UNKNOWN, /* AKEYCODE_WAKEUP */
    SDL_SCANCODE_UNKNOWN, /* AKEYCODE_PAIRING */
    SDL_SCANCODE_UNKNOWN, /* AKEYCODE_MEDIA_TOP_MENU */
    SDL_SCANCODE_UNKNOWN, /* AKEYCODE_11 */
    SDL_SCANCODE_UNKNOWN, /* AKEYCODE_12 */
    SDL_SCANCODE_UNKNOWN, /* AKEYCODE_LAST_CHANNEL */
    SDL_SCANCODE_UNKNOWN, /* AKEYCODE_TV_DATA_SERVICE */
    SDL_SCANCODE_UNKNOWN, /* AKEYCODE_VOICE_ASSIST */
    SDL_SCANCODE_UNKNOWN, /* AKEYCODE_TV_RADIO_SERVICE */
    SDL_SCANCODE_UNKNOWN, /* AKEYCODE_TV_TELETEXT */
    SDL_SCANCODE_UNKNOWN, /* AKEYCODE_TV_NUMBER_ENTRY */
    SDL_SCANCODE_UNKNOWN, /* AKEYCODE_TV_TERRESTRIAL_ANALOG */
    SDL_SCANCODE_UNKNOWN, /* AKEYCODE_TV_TERRESTRIAL_DIGITAL */
    SDL_SCANCODE_UNKNOWN, /* AKEYCODE_TV_SATELLITE */
    SDL_SCANCODE_UNKNOWN, /* AKEYCODE_TV_SATELLITE_BS */
    SDL_SCANCODE_UNKNOWN, /* AKEYCODE_TV_SATELLITE_CS */
    SDL_SCANCODE_UNKNOWN, /* AKEYCODE_TV_SATELLITE_SERVICE */
    SDL_SCANCODE_UNKNOWN, /* AKEYCODE_TV_NETWORK */
    SDL_SCANCODE_UNKNOWN, /* AKEYCODE_TV_ANTENNA_CABLE */
    SDL_SCANCODE_UNKNOWN, /* AKEYCODE_TV_INPUT_HDMI_1 */
    SDL_SCANCODE_UNKNOWN, /* AKEYCODE_TV_INPUT_HDMI_2 */
    SDL_SCANCODE_UNKNOWN, /* AKEYCODE_TV_INPUT_HDMI_3 */
    SDL_SCANCODE_UNKNOWN, /* AKEYCODE_TV_INPUT_HDMI_4 */
    SDL_SCANCODE_UNKNOWN, /* AKEYCODE_TV_INPUT_COMPOSITE_1 */
    SDL_SCANCODE_UNKNOWN, /* AKEYCODE_TV_INPUT_COMPOSITE_2 */
    SDL_SCANCODE_UNKNOWN, /* AKEYCODE_TV_INPUT_COMPONENT_1 */
    SDL_SCANCODE_UNKNOWN, /* AKEYCODE_TV_INPUT_COMPONENT_2 */
    SDL_SCANCODE_UNKNOWN, /* AKEYCODE_TV_INPUT_VGA_1 */
    SDL_SCANCODE_UNKNOWN, /* AKEYCODE_TV_AUDIO_DESCRIPTION */
    SDL_SCANCODE_UNKNOWN, /* AKEYCODE_TV_AUDIO_DESCRIPTION_MIX_UP */
    SDL_SCANCODE_UNKNOWN, /* AKEYCODE_TV_AUDIO_DESCRIPTION_MIX_DOWN */
    SDL_SCANCODE_UNKNOWN, /* AKEYCODE_TV_ZOOM_MODE */
    SDL_SCANCODE_UNKNOWN, /* AKEYCODE_TV_CONTENTS_MENU */
    SDL_SCANCODE_UNKNOWN, /* AKEYCODE_TV_MEDIA_CONTEXT_MENU */
    SDL_SCANCODE_UNKNOWN, /* AKEYCODE_TV_TIMER_PROGRAMMING */
    SDL_SCANCODE_HELP, /* AKEYCODE_HELP */
    SDL_SCANCODE_UNKNOWN, /* AKEYCODE_NAVIGATE_PREVIOUS */
    SDL_SCANCODE_UNKNOWN, /* AKEYCODE_NAVIGATE_NEXT */
    SDL_SCANCODE_UNKNOWN, /* AKEYCODE_NAVIGATE_IN */
    SDL_SCANCODE_UNKNOWN, /* AKEYCODE_NAVIGATE_OUT */
    SDL_SCANCODE_UNKNOWN, /* AKEYCODE_STEM_PRIMARY */
    SDL_SCANCODE_UNKNOWN, /* AKEYCODE_STEM_1 */
    SDL_SCANCODE_UNKNOWN, /* AKEYCODE_STEM_2 */
    SDL_SCANCODE_UNKNOWN, /* AKEYCODE_STEM_3 */
    SDL_SCANCODE_UNKNOWN, /* AKEYCODE_DPAD_UP_LEFT */
    SDL_SCANCODE_UNKNOWN, /* AKEYCODE_DPAD_DOWN_LEFT */
    SDL_SCANCODE_UNKNOWN, /* AKEYCODE_DPAD_UP_RIGHT */
    SDL_SCANCODE_UNKNOWN, /* AKEYCODE_DPAD_DOWN_RIGHT */
    SDL_SCANCODE_UNKNOWN, /* AKEYCODE_MEDIA_SKIP_FORWARD */
    SDL_SCANCODE_UNKNOWN, /* AKEYCODE_MEDIA_SKIP_BACKWARD */
    SDL_SCANCODE_UNKNOWN, /* AKEYCODE_MEDIA_STEP_FORWARD */
    SDL_SCANCODE_UNKNOWN, /* AKEYCODE_MEDIA_STEP_BACKWARD */
    SDL_SCANCODE_UNKNOWN, /* AKEYCODE_SOFT_SLEEP */
    SDL_SCANCODE_CUT, /* AKEYCODE_CUT */
    SDL_SCANCODE_COPY, /* AKEYCODE_COPY */
    SDL_SCANCODE_PASTE, /* AKEYCODE_PASTE */
};


static MapsApp* app = NULL;

static std::thread mainThread;

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

struct SDL_Window
{
  EGLDisplay eglDisplay;
  EGLSurface eglSurface;
};

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

//void SDL_GL_GetDrawableSize(SDL_Window* win, int* w, int* h)
//{
//  eglQuerySurface(win->eglDisplay, win->eglSurface, EGL_WIDTH, w);
//  eglQuerySurface(win->eglDisplay, win->eglSurface, EGL_HEIGHT, h);
//}
//
//void SDL_GL_SwapWindow(SDL_Window* win) { eglSwapBuffers(win->eglDisplay, win->eglSurface); }

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

int eglMain(ANativeWindow* nativeWin)
{
  if(!nativeWin) { LOGE("ANativeWindow_fromSurface returned NULL!"); return -1; }

  EGLDisplay display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
  if(display == EGL_NO_DISPLAY) { LOGE("eglGetDisplay() error %X", eglGetError()); return -1; }

  auto init_res = eglInitialize(display, 0, 0);
  if(init_res == EGL_FALSE) { LOGE("eglInitialize() error %X", eglGetError()); return -1; }

  EGLConfig config;
  bool ok = chooseConfig(display, 24, 4, &config) || chooseConfig(display, 24, 2, &config)
      || chooseConfig(display, 24, 0, &config) || chooseConfig(display, 16, 0, &config);
  if(!ok) { LOGE("eglChooseConfig failed"); return -1; }

  const EGLint eglAttribs[] = {EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE};
  EGLContext context = eglCreateContext(display, config, nullptr, eglAttribs);
  if(!context) { LOGE("eglCreateContext() error %X", eglGetError()); return -1; }

  //auto native_window = ANativeWindow_fromSurface(env, jsurface);
  EGLSurface surface = eglCreateWindowSurface(display, config, nativeWin, NULL);
  if (!surface) { LOGE("eglCreateWindowSurface() error %X", eglGetError()); return -1; }

  auto curr_res = eglMakeCurrent(display, surface, surface, context);
  if (curr_res == EGL_FALSE) { LOGE("eglMakeCurrent() error %X", eglGetError()); return -1; }

  app = new MapsApp(MapsApp::platform);

  SDL_Window sdlWin = {display, surface};
  app->createGUI(&sdlWin);
  app->mapsOffline->resumeDownloads();
  app->mapsSources->rebuildSource(app->config["sources"]["last_source"].Scalar());

  //SDL_Window sdlWindow = {display, surface};
  //int res = MapsApp::mainLoop(&sdlWindow, MapsApp::platform);
  while(MapsApp::runApplication) {
    MapsApp::taskQueue.wait();

    int fbWidth = 0, fbHeight = 0;
    eglQuerySurface(display, surface, EGL_WIDTH, &fbWidth);
    eglQuerySurface(display, surface, EGL_HEIGHT, &fbHeight);
    if(app->drawFrame(fbWidth, fbHeight))
      eglSwapBuffers(display, surface);
  }

  delete app;
  app = NULL;

  eglDestroySurface(display, surface);
  eglDestroyContext(display, context);
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
  MapsApp::baseDir = JniHelpers::stringFromJavaString(env, extFileDir) + "/";
  FSPath configPath(MapsApp::baseDir, "config.yaml");
  MapsApp::configFile = configPath.c_str();
  try {
    MapsApp::config = YAML::LoadFile(configPath.exists() ? configPath.path
        : configPath.parent().childPath(configPath.baseName() + ".default.yaml"));
  } catch(...) {
    LOGE("Unable to load config file!");
    *(volatile int*)0 = 0;  //exit(1) -- Android repeatedly restarts app
  }

  MapsApp::platform = new AndroidPlatform(env, mapsActivity, assetManager);

  mapsActivityRef = env->NewWeakGlobalRef(mapsActivity);

  //app = MapsApp::createApp();
  //if(!app) return;
  //
  //MapsApp::win->sdlWindow = (SDL_Window*)glfwWin;
  //MapsApp::gui->showWindow(MapsApp::win, NULL);
  //
  //MapsApp::baseDir = JniHelpers::stringFromJavaString(env, extFileDir) + "/";  //"/sdcard/Android/data/com.styluslabs.maps/files/";
  //MapsApp::apiKey = NEXTZEN_API_KEY;
  //auto p = std::make_unique<AndroidPlatform>(env, mapsActivity, assetManager);
  //app = new MapsApp(std::move(p));
  //app->sceneFile = "asset:///scene.yaml";
  //app->loadSceneFile();

  //ImGui::GetIO().ImeSetInputScreenPosFn = [](int x, int y){
  //  JniThreadBinding jniEnv(JniHelpers::getJVM());
  //  jniEnv->CallVoidMethod(mapsActivityRef, showTextInputMID, x, y, 20, 20);
  //};
}

JNI_FN(surfaceCreated)(JNIEnv* env, jclass, jobject jsurface)
{
  ANativeWindow* nativeWin = ANativeWindow_fromSurface(env, jsurface);
  if(!nativeWin || mainThread.joinable()) return;
  mainThread = std::thread(eglMain, nativeWin);
}

JNI_FN(surfaceDestroyed)(JNIEnv* env, jclass)
{
  MapsApp::runOnMainThread([=](){ MapsApp::runApplication = false; });
  if(mainThread.joinable())
    mainThread.join();
}

JNI_FN(resize)(JNIEnv* env, jclass, jint w, jint h)
{
  MapsApp::runOnMainThread([=](){ app->onResize(w, h, w, h); });
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
  static const int translateAction[] = {SDL_FINGERDOWN, SDL_FINGERUP, SDL_FINGERMOTION,
      SDL_FINGERUP /*cancel*/, SDL_FINGERMOTION, SDL_FINGERDOWN, SDL_FINGERUP};

  SDL_Event event = {0};
  event.type = translateAction[action];
  event.tfinger.touchId = SDL_TOUCH_MOUSEID;
  event.tfinger.fingerId = ptrId;
  event.tfinger.x = x;
  event.tfinger.y = y;
  event.tfinger.dx = 0;  //button;
  event.tfinger.dy = 0;  //device->buttons;
  event.tfinger.pressure = p;
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
    //PLATFORM_LOG("readlink returned: %s\n", buff);
    //const char* filename = env->GetStringUTFChars(jfilename, 0);
    if(openFileCallback)
      openFileCallback(buff);
    //env->ReleaseStringUTFChars(jfilename, filename);
  }
  openFileCallback = {};  // clear
}
