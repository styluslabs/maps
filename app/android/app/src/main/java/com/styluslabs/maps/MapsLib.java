package com.styluslabs.maps;

import android.content.res.AssetManager;

public class MapsLib
{
  static {
    // library name set by app/android/config.cmake
    System.loadLibrary("droidmaps");
  }

  public static native void init(MapsActivity mapsActivity, AssetManager assetManager, String extFileDir);
  public static native void resize(int width, int height);
  public static native void setupGL();
  public static native void drawFrame();
  public static native void touchEvent(int ptrId, int action, int t, float x, float y, float p);
  public static native void keyEvent(int keycode, int action);
  public static native void charInput(int c, int newCursorPosition);
  public static native void onUrlComplete(long requestHandle, byte[] rawDataBytes, String errorMessage);
  public static native void updateLocation(long time, double lat, double lng, float poserr, double alt, float alterr, float dir, float direrr, float spd, float spderr);
  public static native void updateOrientation(float azimuth, float pitch, float roll);
}
