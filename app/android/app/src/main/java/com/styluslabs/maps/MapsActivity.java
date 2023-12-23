package com.styluslabs.maps;

import java.io.File;
import java.io.IOException;
import java.util.Collections;
import java.util.Map;
import java.util.HashMap;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.InputStream;
import androidx.annotation.Keep;
import androidx.annotation.NonNull;
import androidx.annotation.Nullable;
//import androidx.core.app.ActivityCompat;
import androidx.activity.EdgeToEdge;
import android.content.Context;
import android.app.Activity;
import android.os.Bundle;
import android.os.ParcelFileDescriptor;
import android.net.Uri;
import android.util.Log;
import android.text.InputType;
import android.view.WindowManager;
import android.view.View;
import android.view.ViewGroup;
import android.view.KeyEvent;
import android.view.inputmethod.InputMethodManager;
import android.view.inputmethod.InputConnection;
import android.view.inputmethod.BaseInputConnection;
import android.view.inputmethod.EditorInfo;
import android.widget.RelativeLayout;
import android.Manifest;
import android.content.Intent;
import android.content.pm.PackageManager;
import android.content.res.AssetManager;
import android.content.ClipboardManager;
import android.content.ClipData;
import android.location.Location;
import android.location.LocationManager;
import android.location.LocationListener;
import android.location.GpsStatus;
import android.location.GpsSatellite;
import android.hardware.SensorManager;
import android.hardware.Sensor;
import android.hardware.SensorEvent;
import android.hardware.SensorEventListener;
import android.hardware.GeomagneticField;
import com.mapzen.tangram.FontConfig;
import com.mapzen.tangram.networking.HttpHandler;
import com.mapzen.tangram.networking.DefaultHttpHandler;


public class MapsActivity extends Activity implements GpsStatus.Listener, LocationListener, SensorEventListener
{
  MapsView mMapsView;
  private ViewGroup mLayout;
  private View mTextEdit;
  private LocationManager locationManager;
  private SensorManager mSensorManager;
  private Sensor mAccelSensor;
  private Sensor mMagSensor;
  private HttpHandler httpHandler;
  private final Map<Long, Object> httpRequestHandles = Collections.synchronizedMap(new HashMap<Long, Object>());

  private float mDeclination = 0;

  public static final int PERM_REQ_LOCATION = 1;

  @Override
  protected void onCreate(Bundle icicle)
  {
    super.onCreate(icicle);
    EdgeToEdge.enable(this);
    //String extfiles = getExternalFilesDir(null).getAbsolutePath();
    String extfiles = getExternalMediaDirs()[0].getAbsolutePath() + "/files";
    File file = new File(extfiles, "config.default.yaml");
    if(!file.exists())
      extractAssets(getAssets(), "", extfiles + "/");

    MapsLib.init(this, getAssets(), extfiles);

    httpHandler = new DefaultHttpHandler();

    // stackoverflow.com/questions/1513485 ; github.com/streetcomplete/StreetComplete ... FineLocationManager.kt
    locationManager = (LocationManager) getSystemService(LOCATION_SERVICE);

    // stackoverflow.com/questions/20339942 ; github.com/streetcomplete/StreetComplete ... Compass.kt
    mSensorManager = (SensorManager)getSystemService(SENSOR_SERVICE);
    mAccelSensor = mSensorManager.getDefaultSensor(Sensor.TYPE_ACCELEROMETER);
    mMagSensor = mSensorManager.getDefaultSensor(Sensor.TYPE_MAGNETIC_FIELD);

    if(!canGetLocation()) {
      requestPermissions(//this,  //ActivityCompat
          new String[]{Manifest.permission.ACCESS_FINE_LOCATION}, PERM_REQ_LOCATION);
    }

    mMapsView = new MapsView(getApplication());
    mLayout = new RelativeLayout(this);
    mLayout.addView(mMapsView);
    setContentView(mLayout);
    //setContentView(mMapsView);
    //mGLSurfaceView.setRenderMode(MapsView.RENDERMODE_WHEN_DIRTY);
  }

  @Override
  public void onRequestPermissionsResult(int requestCode, String permissions[], int[] grantResults) {
    switch (requestCode) {
    case PERM_REQ_LOCATION:
      if (grantResults.length > 0 && grantResults[0] == PackageManager.PERMISSION_GRANTED && canGetLocation()) {
        locationManager.requestLocationUpdates(LocationManager.GPS_PROVIDER, 0, 1, this);
        locationManager.addGpsStatusListener(this);
      }
      break;
    }
  }

  protected boolean canGetLocation()
  {
    return checkSelfPermission(//this,  //ContextCompat
        Manifest.permission.ACCESS_FINE_LOCATION) == PackageManager.PERMISSION_GRANTED;
  }

  @Override
  protected void onPause()
  {
    super.onPause();
    //mMapsView.onPause();

    if(canGetLocation()) {
      locationManager.removeUpdates(this);
      locationManager.removeGpsStatusListener(this);
    }
    mSensorManager.unregisterListener(this);
    MapsLib.onPause();
  }

  @Override
  protected void onResume()
  {
    super.onResume();
    //mMapsView.onResume();

    // looks like you may need to use Play Services (or LocationManagerCompat?) for fused location prior to API 31 (Android 12)
    // - see https://developer.android.com/training/location/request-updates
    // min GPS dt = 0 (ms), dr = 1 (meters)
    if(canGetLocation()) {
      locationManager.requestLocationUpdates(LocationManager.GPS_PROVIDER, 0, 1, this);  //FUSED_PROVIDER || "fused"
      locationManager.addGpsStatusListener(this);  //catch (SecurityException e)
    }
    mSensorManager.registerListener(this, mAccelSensor, SensorManager.SENSOR_DELAY_UI);
    mSensorManager.registerListener(this, mMagSensor, SensorManager.SENSOR_DELAY_UI);
  }

  @Override
  public void onLocationChanged(Location loc)
  {
    float poserr = loc.getAccuracy();  // accuracy in meters
    double alt = loc.getAltitude();  // meters
    float dir = loc.getBearing();  // bearing (direction of travel) in degrees
    float direrr = loc.getBearingAccuracyDegrees();
    double lat = loc.getLatitude();  // degrees
    double lng = loc.getLongitude();  // degrees
    float spd = loc.getSpeed();  // m/s
    float spderr = loc.getSpeedAccuracyMetersPerSecond();  // speed accuracy in m/s
    long time = loc.getTime();  // ms since unix epoch
    float alterr = loc.getVerticalAccuracyMeters();  // altitude accuracy in meters
    // for correcting orientation
    mDeclination = new GeomagneticField((float)lat, (float)lng, (float)alt, time).getDeclination()*180/(float)java.lang.Math.PI;

    MapsLib.updateLocation(time, lat, lng, poserr, alt, alterr, dir, direrr, spd, spderr);
  }

  // see https://gitlab.com/mvglasow/satstat ... MainActivity.java
  @Override
  public void onGpsStatusChanged(int event)
  {
    GpsStatus status = locationManager.getGpsStatus(null);
    int satsVisible = 0;
    int satsUsed = 0;
    for(GpsSatellite sat : status.getSatellites()) {
      satsVisible++;
      if(sat.usedInFix())
        satsUsed++;
    }
    MapsLib.updateGpsStatus(satsVisible, satsUsed);
  }

  private float[] mGravity;
  private float[] mGeomagnetic;

  @Override
  public void onSensorChanged(SensorEvent event)
  {
    if(event.sensor.getType() == Sensor.TYPE_ACCELEROMETER)
      mGravity = event.values;

    if(event.sensor.getType() == Sensor.TYPE_MAGNETIC_FIELD)
      mGeomagnetic = event.values;

    if(mGravity != null && mGeomagnetic != null) {
      float R[] = new float[9];
      float I[] = new float[9];
      if(SensorManager.getRotationMatrix(R, I, mGravity, mGeomagnetic)) {
        // orientation contains azimut, pitch and roll
        float orient[] = new float[3];
        SensorManager.getOrientation(R, orient);
        MapsLib.updateOrientation(orient[0] + mDeclination, orient[1], orient[2]);
      }
    }
  }

  @Override
  public void onAccuracyChanged(Sensor sensor, int accuracy) {}

  @Keep
  public void requestRender()
  {
    //mGLSurfaceView.requestRender();
  }

  @Keep
  public void setRenderMode(int cont)
  {
    //mGLSurfaceView.setRenderMode(cont != 0 ? MapsView.RENDERMODE_CONTINUOUSLY : MapsView.RENDERMODE_WHEN_DIRTY);
  }

  @Keep
  public String getFontFilePath(final String key)
  {
    return FontConfig.getFontFile(key);
  }

  @Keep
  public String getFontFallbackFilePath(final int importance, final int weightHint)
  {
    return FontConfig.getFontFallback(importance, weightHint);
  }

  @Keep
  public void cancelUrlRequest(final long requestHandle)
  {
    Object request = httpRequestHandles.remove(requestHandle);
    if (request != null) {
      httpHandler.cancelRequest(request);
    }
  }

  @Keep
  public void startUrlRequest(@NonNull final String url,
      @NonNull final String headers, @NonNull final String payload, final long requestHandle)
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

    Object request = httpHandler.startRequest(url, headers, payload, callback);
    if (request != null) {
      httpRequestHandles.put(requestHandle, request);
    }
  }

  // assetpath = "" reads from assets/  outpath = "" writes to external files path
  public boolean extractAssets(AssetManager assetManager, String assetpath, String outpath)
  {
    try {
      String[] files = assetManager.list(assetpath);
      if(files == null || files.length == 0) return false;
      //if(outpath.isEmpty()) outpath = getExternalFilesDir(null).toString();
      for(String filename : files) {
        String srcpath = assetpath + filename;
        String dstpath = outpath + filename;
        // check for directory
        if(!extractAssets(assetManager, srcpath + "/", dstpath + "/")) {
          Log.v("extractAssets", "Copying " + srcpath + " to " + dstpath);
          File dstfile = new File(dstpath);
          if(dstfile.exists()) continue;  // don't overwrite existing file
          // this returns InputStream object
          InputStream in = assetManager.open(srcpath);
          // ensure that path exists
          dstfile.getParentFile().mkdirs();
          FileOutputStream out = new FileOutputStream(dstfile);
          // copy byte by byte ... doesn't seem to be a more elegant soln!
          byte[] buf = new byte[65536];
          int len;
          while((len = in.read(buf)) > 0)
            out.write(buf, 0, len);
          out.close();
          in.close();
        }
      }
      return true;
    }
    catch(IOException e) {
      Log.v("extractAssets", "Error: ", e);
      return false;
    }
  }

  @Keep
  public void openUrl(String url)
  {
    Intent viewUrlIntent = new Intent(Intent.ACTION_VIEW, Uri.parse(url));
    startActivity(viewUrlIntent);
  }

  private static final int ID_OPEN_DOCUMENT = 2;

  @Keep
  public void openFile()  //Uri pickerInitialUri)
  {
    Intent intent = new Intent(Intent.ACTION_OPEN_DOCUMENT);
    intent.addCategory(Intent.CATEGORY_OPENABLE);
    intent.setType("*/*");
    //intent.putExtra(DocumentsContract.EXTRA_INITIAL_URI, pickerInitialUri);
    startActivityForResult(intent, ID_OPEN_DOCUMENT);
  }

  @Override
  public void onActivityResult(int requestCode, int resultCode, Intent resultData) {
    if(requestCode == ID_OPEN_DOCUMENT) {
      if(resultCode == Activity.RESULT_OK && resultData != null) {
        if(resultData.getData().toString().startsWith("content://")) {
          try {
            // openFileDescriptor only works for mode="r", but /proc/self/fd/<fd> gives us symlink to actual
            //  file which we can open for writing
            // an alternative would be to use android.system.Os.readlink here instead of in androidhelper.cpp

            //getContentResolver().takePersistableUriPermission(resultData.getData(), Intent.FLAG_GRANT_READ_URI_PERMISSION);

            ParcelFileDescriptor pfd = getContentResolver().openFileDescriptor(resultData.getData(), "r");
            MapsLib.openFileDesc(resultData.getData().getPath(), pfd.getFd());
            pfd.close();
          } catch(Exception e) {
            Log.v("onActivityResult", "Error opening document: " + resultData.getData().toString(), e);
          }
        }
        //else
        //  jniOpenFile(intent.getData().getPath());
      }
    }
  }

  public String getClipboard()
  {
    ClipboardManager clipboard = (ClipboardManager) getSystemService(Context.CLIPBOARD_SERVICE);
    if(!clipboard.hasPrimaryClip()) return null;
    ClipData.Item item = clipboard.getPrimaryClip().getItemAt(0);
    return item.coerceToText(this).toString();
  }

  public void setClipboard(String text)
  {
    ClipboardManager clipboard = (ClipboardManager) getSystemService(Context.CLIPBOARD_SERVICE);
    ClipData clip = ClipData.newPlainText("simple text", text);
    clipboard.setPrimaryClip(clip);
  }

  // issue is that Views can only be touched from thread that created them, but these methods are called from
  //  drawFrame() on GL thread
  public void showTextInput(int x, int y, int w, int h)
  {
    runOnUiThread(new Runnable() { @Override public void run() { _showTextInput(x,y,w,h); } });
  }

  // ref: SDL SDLActivity.java
  public void _showTextInput(int x, int y, int w, int h)
  {
    RelativeLayout.LayoutParams params = new RelativeLayout.LayoutParams(w, h + 15);  //HEIGHT_PADDING);
    params.leftMargin = x;
    params.topMargin = y;
    if (mTextEdit == null) {
      mTextEdit = new DummyEdit(this);
      mLayout.addView(mTextEdit, params);
    } else {
      mTextEdit.setLayoutParams(params);
    }
    mTextEdit.setVisibility(View.VISIBLE);
    mTextEdit.requestFocus();
    InputMethodManager imm = (InputMethodManager) getSystemService(INPUT_METHOD_SERVICE);
    imm.showSoftInput(mTextEdit, 0);
    //mScreenKeyboardShown = true;
  }

  public void hideTextInput()
  {
    runOnUiThread(new Runnable() { @Override public void run() { _hideTextInput(); } });
  }

  public void _hideTextInput()
  {
    if(mTextEdit != null) {
      mTextEdit.setLayoutParams(new RelativeLayout.LayoutParams(0, 0));
      InputMethodManager imm = (InputMethodManager) getSystemService(INPUT_METHOD_SERVICE);
      imm.hideSoftInputFromWindow(mTextEdit.getWindowToken(), 0);
      //mScreenKeyboardShown = false;
    }
  }
}


class MapsInputConnection extends BaseInputConnection
{
    public MapsInputConnection(View targetView, boolean fullEditor) { super(targetView, fullEditor); }

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

        for(int c : text.codePoints().toArray()){
            MapsLib.charInput(c, newCursorPosition);
        }
        //MapsLib.textInput(text.toString(), newCursorPosition);
        return super.commitText(text, newCursorPosition);
    }

    //@Override
    //public boolean setComposingText(CharSequence text, int newCursorPosition)
    //{
    //    nativeSetComposingText(text.toString(), newCursorPosition);
    //    return super.setComposingText(text, newCursorPosition);
    //}

    //public static native void textInput(String text, int newCursorPosition);
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
                ic.commitText(String.valueOf((char) event.getUnicodeChar()), 1);
            } else {
                MapsLib.keyEvent(keyCode, 1);
            }
            return true;
        } else if (event.getAction() == KeyEvent.ACTION_UP) {
            MapsLib.keyEvent(keyCode, -1);
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
        outAttrs.inputType = InputType.TYPE_CLASS_TEXT | InputType.TYPE_TEXT_VARIATION_NORMAL;  //TYPE_TEXT_VARIATION_VISIBLE_PASSWORD;
        outAttrs.imeOptions = EditorInfo.IME_FLAG_NO_EXTRACT_UI | EditorInfo.IME_FLAG_NO_FULLSCREEN;
        return ic;
    }
}
