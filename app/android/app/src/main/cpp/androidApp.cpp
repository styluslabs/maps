#include <jni.h>
#include <android/log.h>

#include "mapsapp.h"
#include "AndroidPlatform.h"

#include "imgui.h"
#include "imgui_impl_generic.h"
#include "imgui_impl_opengl3.h"

static MapsApp* app = NULL;

#define TANGRAM_JNI_VERSION JNI_VERSION_1_6

extern "C" JNIEXPORT jint JNI_OnLoad(JavaVM* javaVM, void*)
{
  JNIEnv* jniEnv = nullptr;
  if (javaVM->GetEnv(reinterpret_cast<void**>(&jniEnv), TANGRAM_JNI_VERSION) != JNI_OK)
    return -1;
  Tangram::AndroidPlatform::jniOnLoad(javaVM, jniEnv, "com/styluslabs/maps/MapsActivity");
  return TANGRAM_JNI_VERSION;
}

#define JNI_FN(name) extern "C" JNIEXPORT void JNICALL Java_com_styluslabs_maps_MapsLib_##name

JNI_FN(init)(JNIEnv* env, jobject obj, jobject mapsActivity, jobject assetManager)
{
  // Setup ImGui
  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGui_ImplOpenGL3_Init("#version 120");
  ImGui_ImplGeneric_Init();

  MapsApp::baseDir = "/sdcard/Android/data/com.styluslabs.maps/files";
  MapsApp::apiKey = NEXTZEN_API_KEY;
  auto p = std::make_unique<AndroidPlatform>(env, mapsActivity, assetManager);
  app = new MapsApp(std::move(p));
  app->sceneFile = "asset:///scene.yaml";
  app->loadSceneFile(false);

  //ImGui::ImeSetInputScreenPosFn = [](int x, int y){ showTextInput(x, y, 20, 20); }
}

JNI_FN(resize)(JNIEnv* env, jobject obj, jint width, jint height)
{
  ImGui_ImplGeneric_Resize(w, h, w, h);
  app->onResize(w, h, w, h);
}

JNI_FN(drawFrame)(JNIEnv* env, jobject obj)
{
  auto t0 = std::chrono::high_resolution_clock::now();
  double currTime = std::chrono::duration<double>(t0.time_since_epoch()).count();
  ImGui_ImplGeneric_NewFrame(currTime);
  ImGui_ImplOpenGL3_NewFrame();

  app->drawFrame(currTime);

  if(app->show_gui)
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
}

JNI_FN(touchEvent)(JNIEnv* env, jobject obj, jint ptrId, jint action, jint t, jfloat x, jfloat y, jfloat p)
{
  static const int translateAction[] = {1, -1, 0 /*motion*/, -1 /*cancel*/, 0, 1, -1};

  if(action == 2)
    ImGui_ImplGeneric_UpdateMousePos(x, y);
  else
    ImGui_ImplGeneric_MouseButtonCallback(IMGUI_GENERIC_BUTTON_LEFT, translateAction[action], 0);

  if (!ImGui::GetIO().WantCaptureMouse)
    app->touchHandler.touchEvent(ptrId, translateAction[action], t, x, y, p);
}

JNI_FN(textInput)(JNIEnv* env, jobject obj, jstring text, jint cursorPos)
{
  for(int c : text)
    ImGui_ImplGeneric_CharCallback(c);
}

JNI_FN(keyEvent)(JNIEnv* env, jobject obj, int key, int action)
{
  ImGui_ImplGeneric_KeyCallback(key, 0, action ? GLFW_PRESS : GLFW_RELEASE, 0);
}

JNI_FN(onUrlComplete)(JNIEnv* env, jobject obj, jlong handle, jbyteArray data, jstring err)
{
  static_cast<AndroidPlatform*>(MapsApp::platform)->onUrlComplete(env, handle, data, err);
}

JNI_FN(updateLocation)(long time, double lat, double lng, float poserr,
    double alt, float alterr, float dir, float direrr, float spd, float spderr)
{
  app->updateLocation(Location{time/1000.0, lat, lng, poserr, alt, alterr, dir, direrr, spd, spderr});
}

JNI_FN(updateOrientation)(float azimuth, float pitch, float roll)
{
  app->updateOrientation(azimuth, pitch, roll);
}
