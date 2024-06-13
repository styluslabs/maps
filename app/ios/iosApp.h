#pragma once

#include "ugui/svggui_platform.h"

#ifdef __cplusplus
#include <functional>

// C++ fns available only in GLViewController.mm
typedef std::function<void(const char* name, const char* path, double lng, double lat, double alt, double ctime)> AddGeoTaggedPhotoFn;
int iosPlatform_getGeoTaggedPhotos(int64_t sinceTimestamp, AddGeoTaggedPhotoFn callback);
typedef std::function<void(const void* data, size_t len, float orientation)> GetPhotoFn;
void iosPlatform_getPhotoData(const char* localId, GetPhotoFn callback);

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
void iosApp_getGLConfig(int* samplesOut);

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
void iosPlatform_setStatusBarBG(void* _vc, int isLight);
void iosPlatform_setServiceState(void* _vc, int state, float intervalSec, float minDist);
char* iosPlatform_getClipboardText();
void iosPlatform_setClipboardText(const char* text);

#ifdef __cplusplus
}
#endif
