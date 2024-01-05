package com.styluslabs.maps;

import android.content.Context;
import android.util.Log;
import android.util.DisplayMetrics;
import android.view.KeyEvent;
import android.view.MotionEvent;
import android.view.SurfaceView;
import android.view.SurfaceHolder;


class MapsView extends SurfaceView implements SurfaceHolder.Callback
{
  public MapsView(Context context)
  {
    super(context);
    getHolder().addCallback(this);

    //setZOrderMediaOverlay(true);
    //setFocusable(true);
    //setFocusableInTouchMode(true);
    //requestFocus();
  }

  // SurfaceHolder.Callback
  public void surfaceCreated(SurfaceHolder holder)
  {
    // average xdpi and ydpi so that reported dpi doesn't change if screen rotated
    DisplayMetrics dm = getContext().getResources().getDisplayMetrics();
    MapsLib.surfaceCreated(holder.getSurface(), (dm.xdpi + dm.ydpi)/2);
  }
  public void surfaceChanged(SurfaceHolder holder, int format, int w, int h) { MapsLib.resize(w, h); }
  public void surfaceDestroyed(SurfaceHolder holder) { MapsLib.surfaceDestroyed(); }

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
}
