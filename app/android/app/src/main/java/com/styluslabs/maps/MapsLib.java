package com.styluslabs.maps;

public class MapsLib
{
  static {
    // library name set by app/android/config.cmake
    System.loadLibrary("droidmaps");
  }

  public static native void init(int width, int height);
  public static native void step();
  public static native void touchEvent(int ptrId, int action, int t, float x, float y, float p);
  public static native void textInput(String text, int newCursorPosition);
  public static native void onUrlComplete(long requestHandle, byte[] rawDataBytes, String errorMessage);
  public static native void updateLocation(long time, double lat, double lng, float poserr, double alt, float alterr, float dir, float direrr, float spd, float spderr);
}
