#pragma once

#include "ugui/svggui_platform.h"

#ifdef __cplusplus
extern "C" {
#endif

// iOS -> app
void iosApp_startApp(void* glView, const char* bundlePath);
void iosApp_startLoop(int width, int height, float dpi);
void iosApp_stopLoop();

void iosApp_filePicked(const char* path);
void iosApp_imeTextUpdate(const char* text, int selStart, int selEnd);
void iosApp_onPause();
void iosApp_onResume();
void iosApp_updateLocation(double time, double lat, double lng, float poserr,
    double alt, float alterr, float dir, float direrr, float spd, float spderr);
void iosApp_updateOrientation(float azimuth, float pitch, float roll);

// app -> iOS; in GLViewController.m
void iosPlatform_pickDocument(void* _vc);
void iosPlatform_exportDocument(void* _vc, const char* filename);
void iosPlatform_setSensorsEnabled(void* _vc, int enabled);
void iosPlatform_swapBuffers(void* _view);
void iosPlatform_setContextCurrent(void* _view);
void iosPlatform_setImeText(void*_vc, const char* text, int selStart, int selEnd);
void iosPlatform_showKeyboard(void* _vc, SDL_Rect* rect);
void iosPlatform_hideKeyboard(void* _vc);
void iosPlatform_openURL(const char* url);

#ifdef __cplusplus
}
#endif
