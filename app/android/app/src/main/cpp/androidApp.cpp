#include <jni.h>
#include <android/log.h>

#include "mapsapp.h"
#include "AndroidPlatform.h"

static MapsApp* app = NULL;


bool setupGraphics(int w, int h)
{
  if(!app) {
    auto p = std::make_unique<AndroidPlatform>();
    MapsApp::apiKey = NEXTZEN_API_KEY;
    app = new MapsApp(std::move(p));
    app->sceneFile = "asset:///scene.yaml";
    app->loadSceneFile(false);
  }
  app->onResize(w, h, w, h);
  return true;
}


#define TANGRAM_JNI_VERSION JNI_VERSION_1_6

extern "C" JNIEXPORT jint JNI_OnLoad(JavaVM* javaVM, void*)
{
  JNIEnv* jniEnv = nullptr;
  if (javaVM->GetEnv(reinterpret_cast<void**>(&jniEnv), TANGRAM_JNI_VERSION) != JNI_OK)
    return -1;
  Tangram::AndroidPlatform::jniOnLoad(javaVM, jniEnv, "com/styluslabs/maps/GL2JNIActivity");
  return TANGRAM_JNI_VERSION;
}

#define JNI_FN(name) extern "C" JNIEXPORT void JNICALL Java_com_styluslabs_maps_GL2JNILib_##name

JNI_FN(init)(JNIEnv* env, jobject obj,  jint width, jint height)
{
  setupGraphics(width, height);
}

JNI_FN(step)(JNIEnv* env, jobject obj)
{
  ImGui_ImplGeneric_NewFrame();
  ImGui_ImplOpenGL3_NewFrame();

  app->drawFrame();

  if(app->show_gui)
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

}

JNI_FN(touchEvent)(JNIEnv* env, jobject obj, jint ptrId, jint action, jint t, jfloat x, jfloat y, jfloat p)
{
  static const int actionToGLFW[]
      = {GLFW_PRESS, GLFW_RELEASE, 0 /*motion*/, 0 /*cancel*/, 0, GLFW_PRESS, GLFW_RELEASE};

  if(action == 2)
    app->onMouseMove(t, x, y, true);  //action == GLFW_PRESS);
  else
    app->onMouseButton(time, x, y, GLFW_MOUSE_BUTTON_LEFT, actionToGLFW[action], 0); //mods);
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
  app->updateLocation(Location{time, lat, lng, poserr, alt, alterr, dir, direrr, spd, spderr});
}
