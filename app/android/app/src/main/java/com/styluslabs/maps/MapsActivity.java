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
import androidx.core.content.FileProvider;
import android.app.Activity;
import android.content.Context;
import android.os.Build;
import android.os.Bundle;
import android.os.ParcelFileDescriptor;
import android.net.Uri;
import android.util.Log;
import android.text.Editable;
import android.text.InputType;
import android.text.Selection;
import android.graphics.Color;
import android.view.WindowManager;
import android.view.View;
import android.view.ViewGroup;
import android.view.KeyEvent;
import android.view.Window;
import android.view.inputmethod.InputMethodManager;
import android.view.inputmethod.InputConnection;
import android.view.inputmethod.BaseInputConnection;
import android.view.inputmethod.EditorInfo;
import android.view.inputmethod.ExtractedText;
import android.view.inputmethod.ExtractedTextRequest;
import android.widget.EditText;
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
  private DummyEdit mTextEdit;
  private LocationManager locationManager;
  private SensorManager mSensorManager;
  //private Sensor mAccelSensor;
  //private Sensor mMagSensor;
  private Sensor mOrientSensor;
  private HttpHandler httpHandler;
  private final Map<Long, Object> httpRequestHandles = Collections.synchronizedMap(new HashMap<Long, Object>());
  private float mDeclination = 0;
  private boolean replaceAssets = true;  // for development
  private boolean sensorsEnabled = true;

  public static final int PERM_REQ_LOCATION = 1;

  @Override
  protected void onCreate(Bundle icicle)
  {
    super.onCreate(icicle);
    //String extfiles = getExternalFilesDir(null).getAbsolutePath();
    String extfiles = getExternalMediaDirs()[0].getAbsolutePath() + "/files";
    File file = new File(extfiles, "config.default.yaml");
    if(replaceAssets || !file.exists())
      extractAssets(getAssets(), "", extfiles + "/");

    MapsLib.init(this, getAssets(), extfiles);

    httpHandler = new DefaultHttpHandler();

    // stackoverflow.com/questions/1513485 ; github.com/streetcomplete/StreetComplete ... FineLocationManager.kt
    locationManager = (LocationManager) getSystemService(LOCATION_SERVICE);

    // stackoverflow.com/questions/20339942 ; github.com/streetcomplete/StreetComplete ... Compass.kt
    mSensorManager = (SensorManager)getSystemService(SENSOR_SERVICE);
    //mAccelSensor = mSensorManager.getDefaultSensor(Sensor.TYPE_ACCELEROMETER);
    //mMagSensor = mSensorManager.getDefaultSensor(Sensor.TYPE_MAGNETIC_FIELD);
    mOrientSensor = mSensorManager.getDefaultSensor(Sensor.TYPE_ROTATION_VECTOR);

    if(!canGetLocation()) {
      requestPermissions(//this,  //ActivityCompat
          new String[]{Manifest.permission.ACCESS_FINE_LOCATION}, PERM_REQ_LOCATION);
    }

    mMapsView = new MapsView(getApplication());
    mLayout = new RelativeLayout(this);
    mLayout.addView(mMapsView);
    mTextEdit = new DummyEdit(this);
    mLayout.addView(mTextEdit, new RelativeLayout.LayoutParams(0, 0));
    setContentView(mLayout);
    setEdgeToEdge();
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

  // ref: SDLActivity.java
  protected void setEdgeToEdge()
  {
    Window window = getWindow();
    window.clearFlags(WindowManager.LayoutParams.FLAG_TRANSLUCENT_STATUS);
    window.getDecorView().setSystemUiVisibility(
        View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN | View.SYSTEM_UI_FLAG_LAYOUT_STABLE | View.SYSTEM_UI_FLAG_LIGHT_STATUS_BAR);
    //View.SYSTEM_UI_FLAG_FULLSCREEN | View.INVISIBLE |
    window.addFlags(WindowManager.LayoutParams.FLAG_DRAWS_SYSTEM_BAR_BACKGROUNDS);
    //WindowManager.LayoutParams.FLAG_LAYOUT_NO_LIMITS
    window.setStatusBarColor(Color.TRANSPARENT);
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
    if(sensorsEnabled)
      stopSensors();
    MapsLib.onPause();
  }

  @Override
  protected void onResume()
  {
    super.onResume();
    if(sensorsEnabled)
      startSensors();
    MapsLib.onResume();
  }

  public void startSensors()
  {
    // looks like you may need to use Play Services (or LocationManagerCompat?) for fused location prior to API 31 (Android 12)
    // - see https://developer.android.com/training/location/request-updates
    // min GPS dt = 0 (ms), dr = 1 (meters)
    if(canGetLocation()) {
      locationManager.requestLocationUpdates(LocationManager.GPS_PROVIDER, 0, 1, this);  //FUSED_PROVIDER || "fused"
      locationManager.addGpsStatusListener(this);  //catch (SecurityException e)
    }
    //mSensorManager.registerListener(this, mAccelSensor, SensorManager.SENSOR_DELAY_UI);
    //mSensorManager.registerListener(this, mMagSensor, SensorManager.SENSOR_DELAY_UI);
    mSensorManager.registerListener(this, mOrientSensor, SensorManager.SENSOR_DELAY_UI);
  }

  public void stopSensors()
  {
    if(canGetLocation()) {
      locationManager.removeUpdates(this);
      locationManager.removeGpsStatusListener(this);
    }
    mSensorManager.unregisterListener(this);
  }

  public void setSensorsEnabled(boolean enabled)
  {
    runOnUiThread(new Runnable() { @Override public void run() {
      if(sensorsEnabled && !enabled)
        stopSensors();
      else if(!sensorsEnabled && enabled)
        startSensors();
      sensorsEnabled = enabled;
     } });
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
    // for correcting orientation - convert degrees to radians
    mDeclination = new GeomagneticField((float)lat, (float)lng, (float)alt, time).getDeclination()*(float)java.lang.Math.PI/180;

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

  //private float[] mGravity;
  //private float[] mGeomagnetic;

  @Override
  public void onSensorChanged(SensorEvent event)
  {
    if(event.sensor.getType() == Sensor.TYPE_ROTATION_VECTOR) {
      float rotmat[] = new float[9];  //or 16
      float orient[] = new float[3];
      float values[] = new float[4];
      System.arraycopy(event.values, 0, values, 0, 4);
      SensorManager.getRotationMatrixFromVector(rotmat, values);
      SensorManager.getOrientation(rotmat, orient);
      MapsLib.updateOrientation(orient[0] + mDeclination, orient[1], orient[2]);
    }

    //if(event.sensor.getType() == Sensor.TYPE_ACCELEROMETER)
    //  mGravity = event.values;

    //if(event.sensor.getType() == Sensor.TYPE_MAGNETIC_FIELD)
    //  mGeomagnetic = event.values;

    //if(mGravity != null && mGeomagnetic != null) {
    //  float R[] = new float[9];
    //  float I[] = new float[9];
    //  if(SensorManager.getRotationMatrix(R, I, mGravity, mGeomagnetic)) {
    //    // orientation contains azimut, pitch and roll
    //    float orient[] = new float[3];
    //    SensorManager.getOrientation(R, orient);
    //    MapsLib.updateOrientation(orient[0] + mDeclination, orient[1], orient[2]);
    //    //Log.v("Tangram", "Orientation from mag field: " + (180.0/java.lang.Math.PI)*(orient[0] + mDeclination));
    //  }
    //}
  }

  public void onAccuracyChanged(Sensor sensor, int accuracy) {}

  // required for tangram AndroidPlatform.cpp
  public void requestRender() { /*mGLSurfaceView.requestRender();*/  }
  public void setRenderMode(int cont) { /*mGLSurfaceView.setRenderMode(...);*/ }

  public String getFontFilePath(final String key)
  {
    return FontConfig.getFontFile(key);
  }

  public String getFontFallbackFilePath(final int importance, final int weightHint)
  {
    return FontConfig.getFontFallback(importance, weightHint);
  }

  public void cancelUrlRequest(final long requestHandle)
  {
    Object request = httpRequestHandles.remove(requestHandle);
    if (request != null) {
      httpHandler.cancelRequest(request);
    }
  }

  public void startUrlRequest(@NonNull final String url,
      @NonNull final String headers, @NonNull final String payload, final long requestHandle)
  {
    final HttpHandler.Callback callback = new HttpHandler.Callback() {
      @Override
      public void onFailure(@Nullable final IOException e) {
        if (httpRequestHandles.remove(requestHandle) == null) { return; }
        String msg = (e == null) ? "" : e.getMessage();
        MapsLib.onUrlComplete(requestHandle, null, msg);
        //Log.v("Tangram", "Got failure " + msg + " for " + url);
      }

      @Override
      public void onResponse(final int code, @Nullable final byte[] rawDataBytes) {
        //Log.v("Tangram", "Got response " + code + " for " + url);
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

    //Log.v("Tangram", "Starting request for " + url);
    Object request = httpHandler.startRequest(url, headers, payload, callback);
    if (request != null) {
      httpRequestHandles.put(requestHandle, request);
    }
  }

  // assetpath = "" reads from assets/  outpath = "" writes to external files path
  public boolean extractAssets(AssetManager assetManager, String assetpath, String outpath)
  {
    try {
      if(assetpath.equals("webkit/") || assetpath.equals("images/")) return true;
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
          if(!replaceAssets && dstfile.exists()) continue;  // don't overwrite existing file
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
      Log.v("Tangram extractAssets", "Error: ", e);
      return false;
    }
  }

  public void openUrl(String url)
  {
    Intent viewUrlIntent = new Intent(Intent.ACTION_VIEW, Uri.parse(url));
    startActivity(viewUrlIntent);
  }

  private static final int ID_OPEN_DOCUMENT = 2;

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

  public void shareFile(String filepath, String mimetype, String title)
  {
    final String finaltitle = title;
    String authority = getApplicationContext().getPackageName() + ".fileprovider";
    File file = new File(filepath);
    Uri contentUri = FileProvider.getUriForFile(this, authority, file);
    final Intent intent = new Intent(android.content.Intent.ACTION_SEND);
    intent.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK);
    intent.addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION);
    intent.putExtra(Intent.EXTRA_SUBJECT, title);
    intent.putExtra(Intent.EXTRA_STREAM, contentUri);
    intent.setType(mimetype);
    startActivity(Intent.createChooser(intent, finaltitle));
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
  private void _showTextInput(int x, int y, int w, int h)
  {
    Log.v("Tangram", "_showTextInput");
    RelativeLayout.LayoutParams params = new RelativeLayout.LayoutParams(w, h + 15);  //HEIGHT_PADDING);
    params.leftMargin = x;
    params.topMargin = y;
    mTextEdit.setLayoutParams(params);
    mTextEdit.setVisibility(View.VISIBLE);
    mTextEdit.requestFocus();
    InputMethodManager imm = (InputMethodManager) getSystemService(INPUT_METHOD_SERVICE);
    imm.showSoftInput(mTextEdit, 0);
  }

  public void hideTextInput()
  {
    runOnUiThread(new Runnable() { @Override public void run() { _hideTextInput(); } });
  }

  private void _hideTextInput()
  {
    Log.v("Tangram", "_hideTextInput");
    mTextEdit.setLayoutParams(new RelativeLayout.LayoutParams(0, 0));
    InputMethodManager imm = (InputMethodManager) getSystemService(INPUT_METHOD_SERVICE);
    imm.hideSoftInputFromWindow(mTextEdit.getWindowToken(), 0);
  }

  public void setImeText(String text, int selStart, int selEnd)
  {
    runOnUiThread(new Runnable() { @Override public void run() {
      if(mTextEdit.setImeText(text, selStart, selEnd)) {
        Log.v("Tangram", "restartInput");
        InputMethodManager imm = (InputMethodManager) getSystemService(INPUT_METHOD_SERVICE);
        imm.restartInput(mTextEdit);  // need to clear composing state of keyboard, etc.
      }
    } });
  }

  public void notifyStatusBarBG(boolean isLight)
  {
    runOnUiThread(new Runnable() { @Override public void run() {
      //SYSTEM_UI_FLAG_LIGHT_STATUS_BAR requests black text (for light background)
      Window window = getWindow();
      int flags = window.getDecorView().getSystemUiVisibility();
      if(((flags & View.SYSTEM_UI_FLAG_LIGHT_STATUS_BAR) != 0) != isLight)
        window.getDecorView().setSystemUiVisibility(flags ^ View.SYSTEM_UI_FLAG_LIGHT_STATUS_BAR);
    } });
  }
}

/*
class MapsInputConnection extends BaseInputConnection
{
  DummyEdit mEditView;

  public MapsInputConnection(DummyEdit targetView, boolean fullEditor) {
    super(targetView, fullEditor);
    mEditView = targetView;
    Log.v("Tangram", "MapsInputConnection ctor " + toString());
  }

  @Override
  public Editable getEditable() {
    return mEditView.getEditable();
  }

  // this must be implemented for SwiftKey keyboard to show suggestions
  @Override
  public ExtractedText getExtractedText(ExtractedTextRequest request, int flags) {
    Editable editable = getEditable();
    ExtractedText et = new ExtractedText();
    et.text = editable.toString();
    et.partialEndOffset = editable.length();
    et.selectionStart = Selection.getSelectionStart(editable);
    et.selectionEnd = Selection.getSelectionEnd(editable);
    et.flags = ExtractedText.FLAG_SINGLE_LINE;  //mSingleLine ? ExtractedText.FLAG_SINGLE_LINE : 0;
    return et;
  }

  //@Override public boolean sendKeyEvent(KeyEvent event) { if (event.getKeyCode() == KeyEvent.KEYCODE_ENTER) {

  @Override
  public void closeConnection() {
    Log.v("Tangram", "closeConnection " + toString());
    super.closeConnection();
  }

  @Override
  public boolean commitText(CharSequence text, int newCursorPosition) {
    Log.v("Tangram", "commitText: " + text);
    return super.commitText(text, newCursorPosition) && updateText();
  }

  @Override
  public boolean setComposingText(CharSequence text, int newCursorPosition) {
    Log.v("Tangram", "setComposingText: " + text);
    return super.setComposingText(text, newCursorPosition) && updateText();
  }

  @Override
  public boolean deleteSurroundingText(int beforeLength, int afterLength) {
    Log.v("Tangram", "deleteSurroundingText: " + beforeLength + ", " + afterLength);
    return super.deleteSurroundingText(beforeLength, afterLength) && updateText();
  }

  @Override
  public boolean setSelection(int start, int end) {
    Log.v("Tangram", "setSelection: " + start + ", " + end + " len: " + getEditable().length());
    return super.setSelection(start, end) && updateText();
  }



  @Override
  public boolean setComposingRegion(int start, int end) {
    Log.v("Tangram", "setComposingRegion: " + start + ", " + end);
    return super.setComposingRegion(start, end);
  }
  @Override
  public boolean finishComposingText() {
    Log.v("Tangram", "finishComposingText");
    return super.finishComposingText();
  }



  protected boolean updateText() {
    final Editable content = getEditable();
    if (content == null) { return false; }
    Log.v("Tangram", "updateText (Java -> C++): " + content.toString() + "; sel: " + Selection.getSelectionStart(content) + ", " + Selection.getSelectionEnd(content));
    MapsLib.imeTextUpdate(content.toString(),
        Selection.getSelectionStart(content), Selection.getSelectionEnd(content));
    return true;
  }
}
*/

// Some keyboards (e.g. SwiftKey) edit entire string via InputConnection, while others (e.g. Samsung) only edit
//  composing text via InputConnection and send key events to edit already commited text, so easiest to just
//  use Android EditText to handle everything
// refs:
// - https://github.com/sillsdev/chromium-crosswalk/blob/master/content/public/android/java/src/org/chromium/content/browser/input/AdapterInputConnection.java
// - https://android.googlesource.com/platform/frameworks/base.git/+/refs/heads/main/core/java/com/android/internal/inputmethod/EditableInputConnection.java
// - https://github.com/gioui/gio/blob/main/app/GioView.java
class MapsInputConnection extends BaseInputConnection
{
  DummyEdit mEditView;

  public MapsInputConnection(DummyEdit targetView, boolean fullEditor) {
    super(targetView, fullEditor);
    mEditView = targetView;
    Log.v("Tangram", "MapsInputConnection ctor " + toString());
  }

  @Override
  public void closeConnection() {
    Log.v("Tangram", "closeConnection " + toString());
    super.closeConnection();
  }

  @Override
  public Editable getEditable() {
    return mEditView.getEditable();
  }

  // this must be implemented for SwiftKey keyboard to show suggestions
  @Override
  public ExtractedText getExtractedText(ExtractedTextRequest request, int flags) {
    Editable editable = getEditable();
    ExtractedText et = new ExtractedText();
    et.text = editable.toString();
    et.partialEndOffset = editable.length();
    et.selectionStart = Selection.getSelectionStart(editable);
    et.selectionEnd = Selection.getSelectionEnd(editable);
    et.flags = ExtractedText.FLAG_SINGLE_LINE;  //mSingleLine ? ExtractedText.FLAG_SINGLE_LINE : 0;
    return et;
  }

  // override these 4 methods to call updateText() if not using EditText
  //public boolean commitText(CharSequence text, int newCursorPosition) {
  //public boolean setComposingText(CharSequence text, int newCursorPosition) {
  //public boolean setSelection(int start, int end) {

  // deleteSurroundingText() ignores composing text ... can't figure out why this isn't a problem for EditText
  @Override
  public boolean deleteSurroundingText(int beforeLength, int afterLength) {
    finishComposingText();
    return super.deleteSurroundingText(beforeLength, afterLength);
  }
}

class ProxyEdit extends EditText
{
  public boolean enableUpdate = true;

  public ProxyEdit(Context context) {
    super(context);
  }

  @Override
  protected void onSelectionChanged(int selStart, int selEnd) {
    super.onSelectionChanged(selStart, selEnd);
    updateText();
  }

  @Override
  protected void onTextChanged(CharSequence text, int start, int lengthBefore, int lengthAfter) {
    super.onTextChanged(text, start, lengthBefore, lengthAfter);
    updateText();
  }

  protected boolean updateText() {
    final Editable content = getEditableText();
    if (!enableUpdate || content == null) { return false; }
    Log.v("Tangram", "updateText (Java -> C++): " + content.toString() + "; sel: " + Selection.getSelectionStart(content) + ", " + Selection.getSelectionEnd(content));
    MapsLib.imeTextUpdate(content.toString(),
        Selection.getSelectionStart(content), Selection.getSelectionEnd(content));
    return true;
  }
}


class DummyEdit extends View //implements View.OnFocusChangeListener  //, View.OnKeyListener
{
  MapsInputConnection inputConn;
  //EditText mEditText;
  ProxyEdit mEditText;

  public DummyEdit(Context context)
  {
    super(context);
    setFocusableInTouchMode(true);
    setFocusable(true);
    //setOnKeyListener(this);
    //mEditText = new EditText(context);

    mEditText = new ProxyEdit(context);
  }

  public Editable getEditable() { return mEditText.getEditableText(); }

  public boolean setImeText(String text, int selStart, int selEnd) {
    Log.v("Tangram", "setImeText (C++ -> Java): " + text + " sel: " + selStart + ", " + selEnd + " was: " + mEditText.getText());
    mEditText.enableUpdate = false;
    try {  // prevent crash if text editing state gets messed up
      mEditText.setText(text);
      mEditText.setSelection(selStart, selEnd);
    } catch(Exception e) {
      Log.v("Tangram setImeText", "Error: ", e);
    }
    mEditText.enableUpdate = true;
    return isFocused();
  }

  @Override
  protected void onFocusChanged(boolean gainFocus, int direction, android.graphics.Rect previouslyFocusedRect) {
    Log.v("Tangram", "Focus change: " + (gainFocus ? "true" : "false"));
    super.onFocusChanged(gainFocus, direction, previouslyFocusedRect);
    if(!gainFocus)
      MapsLib.keyEvent(-1, -1);  // keyboard hidden
  }

  @Override
  public boolean onCheckIsTextEditor() { return true; }

  @Override
  public boolean onKeyDown(int keyCode, KeyEvent event) { return mEditText.onKeyDown(keyCode, event); }

  @Override
  public boolean onKeyUp(int keyCode, KeyEvent event) { return mEditText.onKeyUp(keyCode, event); }

  //@Override public boolean onKeyMultiple(int keyCode, int repeatCount, KeyEvent event) {

  //@Override
  //public boolean onKey(View v, int keyCode, KeyEvent event)
  //{
  //  if (event.getAction() == KeyEvent.ACTION_DOWN) {
  //    if(!event.isCtrlPressed() && (event.isPrintingKey() || event.getKeyCode() == KeyEvent.KEYCODE_SPACE)) {
  //      inputConn.commitText(String.valueOf((char) event.getUnicodeChar()), 1);
  //    } else {
  //      MapsLib.keyEvent(keyCode, 1);
  //    }
  //    return true;
  //  } else if (event.getAction() == KeyEvent.ACTION_UP) {
  //    MapsLib.keyEvent(keyCode, -1);
  //    return true;
  //  }
  //  return false;
  //}

  @Override
  public boolean onKeyPreIme(int keyCode, KeyEvent event)
  {
    if(event.getAction() == KeyEvent.ACTION_UP && keyCode == KeyEvent.KEYCODE_BACK) {
      clearFocus();  //MapsLib.keyEvent(-1, -1);  // keyboard hidden
    }
    return super.onKeyPreIme(keyCode, event);
  }

  @Override
  public InputConnection onCreateInputConnection(EditorInfo outAttrs)
  {
    inputConn = new MapsInputConnection(this, true);
    outAttrs.inputType = InputType.TYPE_CLASS_TEXT | InputType.TYPE_TEXT_FLAG_CAP_SENTENCES | InputType.TYPE_TEXT_FLAG_AUTO_CORRECT;  //TYPE_TEXT_VARIATION_VISIBLE_PASSWORD;
    outAttrs.imeOptions = EditorInfo.IME_FLAG_NO_FULLSCREEN | EditorInfo.IME_FLAG_NO_EXTRACT_UI | EditorInfo.IME_ACTION_DONE;
    return inputConn;
  }
}
