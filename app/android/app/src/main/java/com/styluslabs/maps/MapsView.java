package com.styluslabs.maps;

import android.content.Context;
//import android.graphics.PixelFormat;
import android.opengl.GLSurfaceView;
//import android.util.AttributeSet;
import android.util.Log;
import android.view.KeyEvent;
import android.view.MotionEvent;

import javax.microedition.khronos.egl.EGL10;
import javax.microedition.khronos.egl.EGLConfig;
import javax.microedition.khronos.egl.EGLContext;
import javax.microedition.khronos.egl.EGLDisplay;
import javax.microedition.khronos.opengles.GL10;

class MapsView extends GLSurfaceView {
  private static String TAG = "GL2JNIView";
  private static final boolean DEBUG = false;

  public MapsView(Context context)
  {
    super(context);
    // Pick an EGLConfig with RGB8 color, 16-bit depth, no stencil,
    // supporting OpenGL ES 2.0 or later backwards-compatible versions.
    setEGLConfigChooser(8, 8, 8, 0, 16, 0);
    setEGLContextClientVersion(3);
    setPreserveEGLContextOnPause(true);
    setRenderer(new Renderer());
  }

  private void sendTouchEvent(MotionEvent event, int action, int i, float x, float y, float p)
  {
    int t = (int)(event.getEventTime() % Integer.MAX_VALUE);
    //int tool = event.getToolType(i);
    if (p > 1.0f) { p = 1.0f; }  // p = 1.0 is "normal" pressure, so >1 is possible
    MapsLib.touchEvent(event.getPointerId(i), action, t, x, y, p);  //, event.getTouchMajor(i), event.getTouchMinor(i));
  }

  // an alternative approach is to use View.setOnTouchListener() to specify some other class implementing
  //  the View.OnTouchListener interface (e.g., the main activity) to receive the touch events
  @Override
  public boolean onTouchEvent(MotionEvent event)
  {
    // Ref: http://developer.android.com/training/gestures/multi.html
    final int pointerCount = event.getPointerCount();
    int action = event.getActionMasked();

    switch(action) {
    case MotionEvent.ACTION_MOVE:
      for (int j = 0; j < event.getHistorySize(); j++) {
        for (int i = 0; i < pointerCount; i++) {
          sendTouchEvent(event, action, i, event.getHistoricalX(i, j),
              event.getHistoricalY(i, j), event.getHistoricalPressure(i, j));
        }
      }
      for (int i = 0; i < pointerCount; i++) {
        sendTouchEvent(event, action, i, event.getX(i), event.getY(i), event.getPressure(i));
      }
      break;
    case MotionEvent.ACTION_UP:
    case MotionEvent.ACTION_DOWN:
      // Primary pointer up/down, the index is always zero
      sendTouchEvent(event, action, 0, event.getX(0), event.getY(0), event.getPressure(0));
      break;
    case MotionEvent.ACTION_POINTER_UP:
    case MotionEvent.ACTION_POINTER_DOWN:
    {
      // Non primary pointer up/down
      int i = event.getActionIndex();
      sendTouchEvent(event, action, i, event.getX(i), event.getY(i), event.getPressure(i));
      break;
    }
    case MotionEvent.ACTION_CANCEL:
      for (int i = 0; i < pointerCount; i++) {
        sendTouchEvent(event, action, i, event.getX(i), event.getY(i), event.getPressure(i));
      }
      break;
    default:
      break;
    }

    return true;
  }

  private static class Renderer implements GLSurfaceView.Renderer {
    public void onDrawFrame(GL10 gl) {
      MapsLib.drawFrame();
    }

    public void onSurfaceChanged(GL10 gl, int width, int height) {
      MapsLib.resize(width, height);
    }

    public void onSurfaceCreated(GL10 gl, EGLConfig config) {
      MapsLib.setupGL();
    }
  }
}
