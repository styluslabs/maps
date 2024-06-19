#import "GLViewController.h"
#import "OpenGLView.h"
#include "iosApp.h"

#import <CoreLocation/CoreLocation.h>
#import <Photos/Photos.h>

#pragma mark "Location manager"

@interface MapsLocationMgr : NSObject<CLLocationManagerDelegate>
  @property (nonatomic, strong) CLLocationManager *locationManager;
//@property (nonatomic, strong) NSString *instanceName;
@end

@implementation MapsLocationMgr

//- (instancetype)init:(NSString *)instanceNameString {
//    self = [super init];
//    if (self) {
//        self.instanceName = instanceNameString;
//    }
//    return self;
//}

- (BOOL)canGetLocation {
  return [CLLocationManager locationServicesEnabled] && (
      [CLLocationManager authorizationStatus] == kCLAuthorizationStatusAuthorizedAlways ||
      [CLLocationManager authorizationStatus] == kCLAuthorizationStatusAuthorizedWhenInUse);
}

- (void)startSensors {
    if(!self.locationManager) {
        self.locationManager = [[CLLocationManager alloc] init];
        self.locationManager.delegate = self;
        self.locationManager.showsBackgroundLocationIndicator = YES;
    }
    //self.locationManager.desiredAccuracy = kCLLocationAccuracyBest;
    //self.locationManager.distanceFilter = 1.0;
    if ([self canGetLocation]) {
        [self.locationManager startUpdatingLocation];
    } else {
        [self.locationManager requestWhenInUseAuthorization];
    }
    [self.locationManager startUpdatingHeading];
}

- (void)stopSensors {
    [self.locationManager stopUpdatingLocation];
    [self.locationManager stopUpdatingHeading];
}

- (void)setBackgroundState:(BOOL)enabled {
  if(enabled && [CLLocationManager authorizationStatus] != kCLAuthorizationStatusAuthorizedAlways)
    [self.locationManager requestAlwaysAuthorization];
  self.locationManager.allowsBackgroundLocationUpdates = enabled;
  self.locationManager.pausesLocationUpdatesAutomatically = !enabled;
}

- (void)locationManager:(CLLocationManager *)manager didChangeAuthorizationStatus:(CLAuthorizationStatus)status {
    if ([self canGetLocation]) {
        [self.locationManager startUpdatingLocation];
    }
}

- (void)locationManager:(CLLocationManager *)manager didUpdateLocations:(NSArray *)locations {
    for(CLLocation* loc in locations) {  //[locations lastObject];
        //loc.courseAccuracy - iOS 13.4 only
        iosApp_updateLocation(loc.timestamp.timeIntervalSince1970, loc.coordinate.latitude, loc.coordinate.longitude,
            loc.horizontalAccuracy, loc.altitude, loc.verticalAccuracy, loc.course, 0, loc.speed, loc.speedAccuracy);
    }
}

- (void)locationManager:(CLLocationManager *)manager didUpdateHeading:(CLHeading *)newHeading {
    iosApp_updateOrientation(newHeading.trueHeading, 0, 0);
}

@end

static void sendKeyEvent(int keycode, int action)
{
  SDL_Event event = {0};
  event.type = action < 0 ? SDL_KEYUP : SDL_KEYDOWN;
  event.key.state = action < 0 ? SDL_RELEASED : SDL_PRESSED;
  event.key.repeat = 0;  //action == GLFW_REPEAT;
  event.key.keysym.scancode = (SDL_Scancode)keycode;
  event.key.keysym.sym = keycode;
  event.key.windowID = 0;
  SDL_PushEvent(&event);
}

#pragma mark "TextFieldMgr"
@interface TextFieldMgr : NSObject<UITextFieldDelegate> {
  @public UITextField* textField;
}
@end

@implementation TextFieldMgr {
  GLViewController* viewCtrl;
  int inputBottom;
  int keyboardHeight;
  BOOL enableUpdate;
}

- (instancetype)initWithViewCtrl:(GLViewController*)_vc
{
  self = [super init];
  enableUpdate = YES;
  viewCtrl = _vc;
  textField = [[UITextField alloc] initWithFrame:CGRectZero];
  textField.delegate = self;
  //textField.text = @"";
  /* set UITextInputTrait properties, mostly to defaults */
  //textField.autocapitalizationType = UITextAutocapitalizationTypeNone;
  //textField.autocorrectionType = UITextAutocorrectionTypeNo;
  //textField.enablesReturnKeyAutomatically = NO;
  //textField.keyboardAppearance = UIKeyboardAppearanceDefault;
  //textField.keyboardType = UIKeyboardTypeDefault;
  //textField.returnKeyType = UIReturnKeyDefault;
  //textField.secureTextEntry = NO;
  textField.hidden = YES;
  //keyboardVisible = NO;

  NSNotificationCenter *center = [NSNotificationCenter defaultCenter];
  [center addObserver:self selector:@selector(keyboardWillShow:) name:UIKeyboardWillShowNotification object:nil];
  [center addObserver:self selector:@selector(keyboardWillHide:) name:UIKeyboardWillHideNotification object:nil];
  [center addObserver:self selector:@selector(textFieldTextDidChange:) name:UITextFieldTextDidChangeNotification object:nil];
  return self;
}

- (void)dealloc
{
    NSNotificationCenter *center = [NSNotificationCenter defaultCenter];
    [center removeObserver:self name:UIKeyboardWillShowNotification object:nil];
    [center removeObserver:self name:UIKeyboardWillHideNotification object:nil];
    [center removeObserver:self name:UITextFieldTextDidChangeNotification object:nil];
}

- (void)showKeyboard:(int)_inputBottom
{
  inputBottom = _inputBottom;
  //keyboardVisible = YES;
  if (textField.window) {
    [textField becomeFirstResponder];
  }
}

- (void)hideKeyboard
{
  //keyboardVisible = NO;
  [textField resignFirstResponder];
}

- (void)setImeText:(const char*)text selStart:(int)start selEnd:(int)end
{
  enableUpdate = NO;
  //NSLog(@"app -> iOS RECV: '%@', %d,%d", currText, start, end);
  textField.text = @(text);
  // unbelievable
  UITextPosition* pos0 = textField.beginningOfDocument;
  UITextPosition* posStart = [textField positionFromPosition:pos0 offset:start];
  UITextPosition* posEnd = [textField positionFromPosition:pos0 offset:end];
  textField.selectedTextRange = [textField textRangeFromPosition:posStart toPosition:posEnd];
  enableUpdate = YES;
}

- (void)imeTextUpdate
{
  if(!enableUpdate) return;
  // unbelievable
  UITextRange* sel = textField.selectedTextRange;
  int start = 0, end = 0;
  if(sel) {
    UITextPosition* pos0 = textField.beginningOfDocument;
    start = [textField offsetFromPosition:pos0 toPosition:sel.start];
    end = [textField offsetFromPosition:pos0 toPosition:sel.end];
  }
  //NSLog(@"iOS -> app SEND: '%@', %d,%d", currText, start, end);
  iosApp_imeTextUpdate(textField.text.UTF8String, start, end);
}

- (void)textFieldTextDidChange:(NSNotification *)notification
{
  [self imeTextUpdate];
}

- (void)textFieldDidChangeSelection:(UITextField *)textField0
{
  [self imeTextUpdate];
}

- (void)keyboardWillShow:(NSNotification *)notification
{
  CGRect kbrect = [[notification userInfo][UIKeyboardFrameEndUserInfoKey] CGRectValue];
  kbrect = [viewCtrl.view convertRect:kbrect fromView:nil];
  [self setKeyboardHeight:(int)kbrect.size.height];
}

- (void)keyboardWillHide:(NSNotification *)notification
{
  //if (!rotatingOrientation) { SDL_StopTextInput(); }
  [self setKeyboardHeight:0];
}

- (void)updateKeyboard
{
  CGRect bounds = [[UIApplication sharedApplication] keyWindow].bounds;
  int kbbottom = bounds.size.height - keyboardHeight;
  if(keyboardHeight > 0 && inputBottom > kbbottom)
    viewCtrl.view.frame = CGRectOffset(bounds, 0, kbbottom - inputBottom);
  else
    viewCtrl.view.frame = bounds;
}

- (void)setKeyboardHeight:(int)height
{
  //keyboardVisible = height > 0;
  keyboardHeight = height;
  [self updateKeyboard];
}

//- (BOOL)textField:(UITextField *)_textField shouldChangeCharactersInRange:(NSRange)range replacementString:(NSString *)string
//{
//  return YES;
//}

- (BOOL)textFieldShouldReturn:(UITextField*)_textField
{
  sendKeyEvent(SDLK_RETURN, 1);
  sendKeyEvent(SDLK_RETURN, -1);
  //SDL_StopTextInput()
  return YES;
}

@end

#pragma mark "GLViewController"
@interface GLViewController() {
  @public OpenGLView* glView;
  @public MapsLocationMgr* mapsLocationMgr;
  @public TextFieldMgr* textFieldMgr;
  @public BOOL statusBarBGisLight;
}
@end

@implementation GLViewController

- (instancetype)init {
  self = [super init];
  NSString* bundlePath = [[NSBundle mainBundle] bundlePath];
  iosApp_startApp((__bridge void*)self, [bundlePath UTF8String]);
  mapsLocationMgr = [[MapsLocationMgr alloc] init];
  textFieldMgr = [[TextFieldMgr alloc] initWithViewCtrl:self];
  [mapsLocationMgr startSensors];
  statusBarBGisLight = YES;
  return self;
}

- (void)loadView {
  glView = [[OpenGLView alloc] initWithFrame:[[UIScreen mainScreen] bounds]];
  [self setView:glView];
}

- (void)setView:(UIView *)view
{
    [super setView:view];
    [view addSubview:textFieldMgr->textField];
    //if (keyboardVisible) { [self showKeyboard]; }
}

- (UIStatusBarStyle)preferredStatusBarStyle
{
  return statusBarBGisLight ? UIStatusBarStyleDefault : UIStatusBarStyleLightContent;  //UIStatusBarStyleDarkContent
}

@end

#pragma mark "Document picker"
// Document picker
@interface DocumentPicker : UIDocumentPickerViewController
@end

@interface DocumentPicker () <UIDocumentPickerDelegate>
@end

@implementation DocumentPicker

- (void)documentPicker:(UIDocumentPickerViewController*)picker didPickDocumentsAtURLs:(NSArray<NSURL*>*)urls
{
  if(picker.documentPickerMode != UIDocumentPickerModeOpen) return;
  NSURL* url = urls.firstObject;
  NSLog(@"Picked document URL: %@ Path: %@", url.absoluteString, url.path);
  if (access([url.path UTF8String], R_OK) == 0) {
    iosApp_filePicked([url.path UTF8String]);
  } else {
    [url startAccessingSecurityScopedResource];
    iosApp_filePicked([url.path UTF8String]);
    //NSData* data = [NSData dataWithContentsOfURL:url];  // options:NSDataReadingMappedAlways error:nil];

    //void* buffer = malloc(data.length);
    //memcpy(buffer, data.bytes, data.length);
    //[url stopAccessingSecurityScopedResource];

    //iosApp_openFileData(buffer, data.length);
  }
}

@end

void iosPlatform_pickDocument(void* _vc)  //long mode)
{
  GLViewController* vc = (__bridge GLViewController*)_vc;
  dispatch_async(dispatch_get_main_queue(), ^{
    DocumentPicker *documentPicker = [[DocumentPicker alloc]
                                      initWithDocumentTypes:@[@"public.data"] inMode:UIDocumentPickerModeOpen];
    //documentPicker.docTag = mode;
    documentPicker.delegate = documentPicker;
    documentPicker.modalPresentationStyle = UIModalPresentationFormSheet;
    [vc presentViewController:documentPicker animated:YES completion:nil];
  });
}

void iosPlatform_exportDocument(void* _vc, const char* filename)
{
  GLViewController* vc = (__bridge GLViewController*)_vc;
  NSURL* url = [NSURL fileURLWithPath:@(filename)];
  dispatch_async(dispatch_get_main_queue(), ^{
    DocumentPicker *documentPicker = [[DocumentPicker alloc]
                                      initWithURL:url inMode:UIDocumentPickerModeExportToService];
    documentPicker.delegate = documentPicker;
    documentPicker.modalPresentationStyle = UIModalPresentationFormSheet;
    [vc presentViewController:documentPicker animated:YES completion:nil];
  });
}

void iosPlatform_setSensorsEnabled(void* _vc, int enabled)
{
  GLViewController* vc = (__bridge GLViewController*)_vc;
  dispatch_async(dispatch_get_main_queue(), ^{
    enabled ? [vc->mapsLocationMgr startSensors] : [vc->mapsLocationMgr stopSensors];
  });
}

void iosPlatform_setServiceState(void* _vc, int state, float intervalSec, float minDist)
{
  GLViewController* vc = (__bridge GLViewController*)_vc;
  dispatch_async(dispatch_get_main_queue(), ^{
    [vc->mapsLocationMgr setBackgroundState:(state ? YES : NO)];
  });
}

void iosPlatform_getSafeAreaInsets(void* _vc, float* top, float* bottom)
{
  //if (UI_USER_INTERFACE_IDIOM() == UIUserInterfaceIdiomPad)
  //  return 0;
  UIWindow *window = UIApplication.sharedApplication.keyWindow;
  CGFloat topInset = window.safeAreaInsets.top;
  CGFloat bottomInset = window.safeAreaInsets.bottom;
  if(top) *top = (float)topInset;
  if(bottom) *bottom = (float)bottomInset;
  //return 1;
}

void iosPlatform_swapBuffers(void* _vc)
{
  GLViewController* vc = (__bridge GLViewController*)_vc;
  [vc->glView swapBuffers];
}

void iosPlatform_setContextCurrent(void* _vc)
{
  GLViewController* vc = (__bridge GLViewController*)_vc;
  [vc->glView makeContextCurrent];
}

void iosPlatform_setImeText(void*_vc, const char* text, int selStart, int selEnd)
{
  GLViewController* vc = (__bridge GLViewController*)_vc;
  NSString* str = @(text);
  dispatch_async(dispatch_get_main_queue(), ^{
    [vc->textFieldMgr setImeText:str.UTF8String selStart:selStart selEnd:selEnd];
  });
}

void iosPlatform_showKeyboard(void* _vc, SDL_Rect* rect)
{
  GLViewController* vc = (__bridge GLViewController*)_vc;
  int bottom = (rect->y + rect->h);  // get contentScaleFactor on main thread to avoid UIKit complaint
  dispatch_async(dispatch_get_main_queue(), ^{
    [vc->textFieldMgr showKeyboard:(bottom/vc.view.contentScaleFactor)];
  });
}

void iosPlatform_hideKeyboard(void* _vc)
{
  GLViewController* vc = (__bridge GLViewController*)_vc;
  dispatch_async(dispatch_get_main_queue(), ^{ [vc->textFieldMgr hideKeyboard]; });
}

void iosPlatform_openURL(const char* url)
{
  NSURL* nsurl = [NSURL URLWithString:@(url)];
  dispatch_async(dispatch_get_main_queue(), ^{
    [UIApplication.sharedApplication openURL:nsurl options:@{} completionHandler:nil];
  });
}

void iosPlatform_excludeFromBackup(const char* url)
{
  NSURL* nsurl = [NSURL URLWithString:@(url)];
  [nsurl setResourceValue:YES forKey:NSURLIsExcludedFromBackupKey error:nil];
}

void iosPlatform_setStatusBarBG(void* _vc, int isLight)
{
  GLViewController* vc = (__bridge GLViewController*)_vc;
  dispatch_async(dispatch_get_main_queue(), ^{
    vc->statusBarBGisLight = isLight;
    [vc setNeedsStatusBarAppearanceUpdate];
  });
}

// should be safe to access pasteboard from non-UI thread
char* iosPlatform_getClipboardText()
{
  NSString* string = UIPasteboard.generalPasteboard.string;
  if(!string) return NULL;
  char* res = (char*)malloc(string.length+1);  // caller frees w/ SDL_free()
  strcpy(res, string.UTF8String);
  return res;
}

void iosPlatform_setClipboardText(const char* text)
{
  UIPasteboard.generalPasteboard.string = @(text);
}

// photos

int iosPlatform_getGeoTaggedPhotos(int64_t sinceTimestamp, AddGeoTaggedPhotoFn callback)
{
  if([PHPhotoLibrary authorizationStatus] != PHAuthorizationStatusAuthorized) {
    dispatch_async(dispatch_get_main_queue(), ^{
      [PHPhotoLibrary requestAuthorization:^(PHAuthorizationStatus status) {}];
    });
    return 0;
  }
  else if(sinceTimestamp < 0)
    return 1;  // access check

  NSDateFormatter* dateFormatter = [[NSDateFormatter alloc] init];
  [dateFormatter setDateFormat:@"YYYY-MM-dd HH:mm:ss"];
  PHFetchOptions* options = [[PHFetchOptions alloc] init];
  //options.sortDescriptors = @[[NSSortDescriptor sortDescriptorWithKey:@"creationDate" ascending:YES]];
  options.predicate = [NSPredicate predicateWithFormat:@"creationDate > %@",  //AND creationDate < %@
      [NSDate dateWithTimeIntervalSince1970:sinceTimestamp]];
  PHFetchResult<PHAsset*>* results = [PHAsset fetchAssetsWithMediaType:PHAssetMediaTypeImage options:options];
  for (PHAsset* asset in results) {
    if(!asset.location) continue;  // location property is marked as nullable
    CLLocationCoordinate2D loc = asset.location.coordinate;
    if(loc.longitude == 0 && loc.latitude == 0) continue;
    double alti = asset.location.altitude;
    NSString* name = [dateFormatter stringFromDate:asset.creationDate];  //[asset valueForKey:@"filename"];
    callback(name.UTF8String, asset.localIdentifier.UTF8String,
        loc.longitude, loc.latitude, std::isnan(alti) ? 0 : alti, asset.creationDate.timeIntervalSince1970);
  }
  return 1;
}

static float angleFromOrientation(UIImageOrientation imageOrientation)
{
  switch(imageOrientation) {
    case UIImageOrientationUp: return 0;
    case UIImageOrientationDown: return 180;
    case UIImageOrientationLeft: return 270;
    case UIImageOrientationRight: return 90;
    case UIImageOrientationUpMirrored: return -360;  // we use negative angle to indicate mirrored image
    case UIImageOrientationDownMirrored: return -180;
    case UIImageOrientationLeftMirrored: return -90;
    case UIImageOrientationRightMirrored: return -270;
  }
  return 0;
}

void iosPlatform_getPhotoData(const char* localId, GetPhotoFn callback)
{
  PHFetchResult<PHAsset*>* result = [PHAsset fetchAssetsWithLocalIdentifiers:@[@(localId)] options:nil];
  if(result.count == 0) return;

  [[PHImageManager defaultManager] requestImageDataForAsset:result.firstObject options:nil
      resultHandler:^(NSData* imageData, NSString* dataUTI, UIImageOrientation orientation, NSDictionary* info) {
        callback(imageData.bytes, imageData.length, angleFromOrientation(orientation));
      }];
}
