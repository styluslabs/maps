package com.styluslabs.maps;

import android.app.Activity;
import android.os.Bundle;
import android.util.Log;
import android.view.WindowManager;

import java.io.File;


public class MapsActivity extends Activity implements LocationListener
{
  MapsView mGLSurfaceView;
  //private ViewGroup mLayout;
  //private View mTextEdit;
  private LocationManager locationManager;
  private HttpHandler httpHandler;
  private final Map<Long, Object> httpRequestHandles = Collections.synchronizedMap(new HashMap<Long, Object>());

  @Override
  protected void onCreate(Bundle icicle)
  {
    super.onCreate(icicle);
    mGLSurfaceView = new MapsView(getApplication());

    //mLayout = new RelativeLayout(this);
    //mLayout.addView(mGLSurfaceView);
    //setContentView(mLayout);

    setContentView(mView);

    httpHandler = new DefaultHttpHandler();

    locationManager = (LocationManager) mContext.getSystemService(LOCATION_SERVICE);

    // min GPS dt = 0 (ms), dr = 1 (meters)
    locationManager.requestLocationUpdates(LocationManager.FUSED_PROVIDER, 0, 1, this);
  }

  @Override
  protected void onPause()
  {
    super.onPause();
    mGLSurfaceView.onPause();
  }

  @Override
  protected void onResume()
  {
    super.onResume();
    mGLSurfaceView.onResume();
  }

  @Override
  public void onLocationChanged(Location loc)
  {
    float poserr = loc.getAccuracy();  // accuracy in meters
    double alt = loc.getAltitude()  // meters
    float dir = loc.getBearing();  // bearing (direction of travel) in degrees
    float direrr = loc.getBearingAccuracyDegrees()
    double lat = loc.getLatitude();  // degrees
    double lng = loc.getLongitude();  // degrees
    float spd = loc.getSpeed();  // m/s
    float spderr = getSpeedAccuracyMetersPerSecond()  // speed accuracy in m/s
    long time = getTime();  // ms since unix epoch
    float alterr = getVerticalAccuracyMeters();  // altitude accuracy in meters

    MapsLib.updateLocation(time, lat, lng, poserr, alt, alterr, dir, direrr, spd, spderr);
  }

  @Keep
  public void requestRender()
  {
    mGLSurfaceView.requestRender();
  }

  @Keep
  public void setRenderMode(int cont)
  {
    mGLSurfaceView.setRenderMode(cont ? RENDER_CONTINUOUSLY : RENDER_WHEN_DIRTY);
  }

  @Keep
  String getFontFilePath(final String key)
  {
    return FontConfig.getFontFile(key);
  }

  @Keep
  String getFontFallbackFilePath(final int importance, final int weightHint)
  {
    return FontConfig.getFontFallback(importance, weightHint);
  }

  @Keep
  void cancelUrlRequest(final long requestHandle)
  {
    Object request = httpRequestHandles.remove(requestHandle);
    if (request != null) {
      httpHandler.cancelRequest(request);
    }
  }

  @Keep
  void startUrlRequest(@NonNull final String url, final long requestHandle)
  {
    final HttpHandler.Callback callback = new HttpHandler.Callback() {
      @Override
      public void onFailure(@Nullable final IOException e) {
        if (httpRequestHandles.remove(requestHandle) == null) { return; }
        String msg = (e == null) ? "" : e.getMessage();
        MapsLib.onUrlComplete(requestHandle, null, msg);
      }

      @Override
      public void onResponse(final int code, @Nullable final byte[] rawDataBytes) {
        if (httpRequestHandles.remove(requestHandle) == null) { return; }
        if (code >= 200 && code < 300) {
          MapsLib.onUrlComplete(requestHandle, rawDataBytes, null);
        } else {
          MapsLib.onUrlComplete(requestHandle, null,
                  "Unexpected response code: " + code + " for URL: " + url);
        }
      }

      @Override
      public void onCancel() {
        if (httpRequestHandles.remove(requestHandle) == null) { return; }
        MapsLib.onUrlComplete(requestHandle, null, null);
      }
    };

    Object request = httpHandler.startRequest(url, callback);
    if (request != null) {
      httpRequestHandles.put(requestHandle, request);
    }
  }


  /*public void showTextInput(int x, int y, int w, int h)
  {
    RelativeLayout.LayoutParams params = new RelativeLayout.LayoutParams(w, h + HEIGHT_PADDING);
    params.leftMargin = x;
    params.topMargin = y;
    if (mTextEdit == null) {
      mTextEdit = new DummyEdit(SDL.getContext());
      mLayout.addView(mTextEdit, params);
    } else {
      mTextEdit.setLayoutParams(params);
    }
    mTextEdit.setVisibility(View.VISIBLE);
    mTextEdit.requestFocus();
    InputMethodManager imm = (InputMethodManager) SDL.getContext().getSystemService(Context.INPUT_METHOD_SERVICE);
    imm.showSoftInput(mTextEdit, 0);
    //mScreenKeyboardShown = true;
  }

  public void hideTextInput()
  {
    if(mTextEdit != null) {
      mTextEdit.setLayoutParams(new RelativeLayout.LayoutParams(0, 0));
      InputMethodManager imm = (InputMethodManager) context.getSystemService(Context.INPUT_METHOD_SERVICE);
      imm.hideSoftInputFromWindow(mTextEdit.getWindowToken(), 0);
      //mScreenKeyboardShown = false;
    }
  }*/
}

/*
class MapsInputConnection extends BaseInputConnection
{
    public SDLInputConnection(View targetView, boolean fullEditor) { super(targetView, fullEditor); }

    //@Override
    //public boolean sendKeyEvent(KeyEvent event)
    //{
    //    if (event.getKeyCode() == KeyEvent.KEYCODE_ENTER) {
    //        String imeHide = SDLActivity.nativeGetHint("SDL_RETURN_KEY_HIDES_IME");
    //        if ((imeHide != null) && imeHide.equals("1")) {
    //            Context c = SDL.getContext();
    //            if (c instanceof SDLActivity) {
    //                SDLActivity activity = (SDLActivity)c;
    //                activity.sendCommand(SDLActivity.COMMAND_TEXTEDIT_HIDE, null);
    //                return true;
    //            }
    //        }
    //    }
    //    return super.sendKeyEvent(event);
    //}

    @Override
    public boolean commitText(CharSequence text, int newCursorPosition)
    {
        //for (int i = 0; i < text.length(); i++) {
        //    char c = text.charAt(i);
        //    nativeGenerateScancodeForUnichar(c);
        //}
        MapsLib.textInput(text.toString(), newCursorPosition);
        return super.commitText(text, newCursorPosition);
    }

    //@Override
    //public boolean setComposingText(CharSequence text, int newCursorPosition)
    //{
    //    nativeSetComposingText(text.toString(), newCursorPosition);
    //    return super.setComposingText(text, newCursorPosition);
    //}

    public static native void textInput(String text, int newCursorPosition);

    //public native void nativeGenerateScancodeForUnichar(char c);

    //public native void nativeSetComposingText(String text, int newCursorPosition);

    @Override
    public boolean deleteSurroundingText(int beforeLength, int afterLength) {
        // Workaround to capture backspace key. Ref: http://stackoverflow.com/questions/14560344/
        if (beforeLength > 0 && afterLength == 0) {
            boolean ret = true;
            // backspace(s)
            while (beforeLength-- > 0) {
               boolean ret_key = sendKeyEvent(new KeyEvent(KeyEvent.ACTION_DOWN, KeyEvent.KEYCODE_DEL))
                              && sendKeyEvent(new KeyEvent(KeyEvent.ACTION_UP, KeyEvent.KEYCODE_DEL));
               ret = ret && ret_key;
            }
            return ret;
        }

        return super.deleteSurroundingText(beforeLength, afterLength);
    }
}

class DummyEdit extends View implements View.OnKeyListener
{
    InputConnection ic;

    public DummyEdit(Context context)
    {
        super(context);
        setFocusableInTouchMode(true);
        setFocusable(true);
        setOnKeyListener(this);
    }

    @Override
    public boolean onCheckIsTextEditor() { return true; }

    @Override
    public boolean onKey(View v, int keyCode, KeyEvent event)
    {
        if (event.getAction() == KeyEvent.ACTION_DOWN) {
            if(!event.isCtrlPressed() && (event.isPrintingKey() || event.getKeyCode() == KeyEvent.KEYCODE_SPACE)) {
            //if (SDLActivity.isTextInputEvent(event)) {
                ic.commitText(String.valueOf((char) event.getUnicodeChar()), 1);
                return true;
            }
            //SDLActivity.onNativeKeyDown(keyCode);
            return true;
        } else if (event.getAction() == KeyEvent.ACTION_UP) {
            //SDLActivity.onNativeKeyUp(keyCode);
            return true;
        }
        return false;
    }

    //@Override
    //public boolean onKeyPreIme (int keyCode, KeyEvent event)
    //{
    //    if (event.getAction()==KeyEvent.ACTION_UP && keyCode == KeyEvent.KEYCODE_BACK) {
    //        if (SDLActivity.mTextEdit != null && SDLActivity.mTextEdit.getVisibility() == View.VISIBLE) {
    //            SDLActivity.onNativeKeyboardFocusLost();
    //        }
    //    }
    //    return super.onKeyPreIme(keyCode, event);
    //}

    @Override
    public InputConnection onCreateInputConnection(EditorInfo outAttrs)
    {
        ic = new MapsInputConnection(this, true);
        outAttrs.inputType = InputType.TYPE_CLASS_TEXT | InputType.TYPE_TEXT_VARIATION_VISIBLE_PASSWORD;
        outAttrs.imeOptions = EditorInfo.IME_FLAG_NO_EXTRACT_UI | EditorInfo.IME_FLAG_NO_FULLSCREEN;
        return ic;
    }
}
*/
