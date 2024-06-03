package com.styluslabs.maps;

import android.content.res.AssetManager;
import android.view.Surface;

public class MapsLib
{
  static {
    // library name set by app/android/config.cmake
    System.loadLibrary("droidmaps");
  }

  public static native void init(MapsActivity mapsActivity, AssetManager assetManager, String extFileDir, int versionCode);
  public static native void surfaceChanged(int width, int height);
  public static native void surfaceCreated(Surface surface, float dpi);
  public static native void surfaceDestroyed();
  public static native void onPause();
  public static native void onResume();
  public static native void onLowMemory();
  public static native void touchEvent(int ptrId, int action, int t, float x, float y, float p);
  public static native void keyEvent(int keycode, int action);
  public static native void charInput(int c, int newCursorPosition);
  public static native void onUrlComplete(long requestHandle, byte[] rawDataBytes, String errorMessage);
  public static native void updateLocation(long time, double lat, double lng, float poserr, double alt, float alterr, float dir, float direrr, float spd, float spderr);
  public static native void updateOrientation(float azimuth, float pitch, float roll);
  public static native void updateGpsStatus(int satsVisible, int satsUsed);
  public static native void openFileDesc(String filename, int fd);
  public static native void handleUri(String uri);
  public static native void imeTextUpdate(String text, int selStart, int selEnd);
}
