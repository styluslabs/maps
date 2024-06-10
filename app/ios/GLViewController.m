#import "GLViewController.h"
#import "OpenGLView.h"
#include "iosApp.h"

#pragma mark "Location manager"
#import <CoreLocation/CoreLocation.h>

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

@implementation TextFieldMgr

//@synthesize textInputRect;
//@synthesize keyboardHeight;
//@synthesize keyboardVisible;

- (instancetype)init
{
    self = [super init];
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

- (void)showKeyboard
{
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

- (void)setImeText:(const char*)text selStart:(int)selStart selEnd:(int)selEnd
{
  textField.text = @(text);
  // unbelievable
  UITextPosition *beginning = textField.beginningOfDocument;
  UITextPosition *start = [textField positionFromPosition:beginning offset:selStart];
  UITextPosition *end = [textField positionFromPosition:beginning offset:selEnd];
  textField.selectedTextRange = [textField textRangeFromPosition:start toPosition:end];
}

- (void)keyboardWillShow:(NSNotification *)notification
{
    //CGRect kbrect = [[notification userInfo][UIKeyboardFrameEndUserInfoKey] CGRectValue];
    //kbrect = [self.view convertRect:kbrect fromView:nil];
    //[self setKeyboardHeight:(int)kbrect.size.height];
}

- (void)keyboardWillHide:(NSNotification *)notification
{
    //if (!rotatingOrientation) { SDL_StopTextInput(); }
    //[self setKeyboardHeight:0];
}

- (void)imeTextUpdate
{
  const char* text = [textField.text UTF8String];
  // unbelievable
  UITextRange* sel = textField.selectedTextRange;
  if(sel) {
    UITextPosition *beginning = textField.beginningOfDocument;
    const NSInteger start = [textField offsetFromPosition:beginning toPosition:sel.start];
    const NSInteger end = [textField offsetFromPosition:beginning toPosition:sel.end];
    iosApp_imeTextUpdate(text, start, end);
  }
  else
    iosApp_imeTextUpdate(text, 0, 0);
}

- (void)textFieldTextDidChange:(NSNotification *)notification
{
  [self imeTextUpdate];
}

- (void)textFieldDidChangeSelection:(UITextField *)textField0
{
  [self imeTextUpdate];
}

/*- (void)updateKeyboard
{
    CGAffineTransform t = self.view.transform;
    CGPoint offset = CGPointMake(0.0, 0.0);
    CGRect frame = UIKit_ComputeViewFrame(window, self.view.window.screen);
    if (self.keyboardHeight) {
        int rectbottom = self.textInputRect.y + self.textInputRect.h;
        int keybottom = self.view.bounds.size.height - self.keyboardHeight;
        if (keybottom < rectbottom) {
            offset.y = keybottom - rectbottom;
        }
    }
    t.tx = 0.0;
    t.ty = 0.0;
    offset = CGPointApplyAffineTransform(offset, t);
    frame.origin.x += offset.x;
    frame.origin.y += offset.y;
    self.view.frame = frame;
}

- (void)setKeyboardHeight:(int)height
{
    keyboardVisible = height > 0;
    keyboardHeight = height;
    [self updateKeyboard];
}*/

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
}
@end

@implementation GLViewController

- (instancetype)init {
  self = [super init];
  NSString* bundlePath = [[NSBundle mainBundle] bundlePath];
  iosApp_startApp((__bridge void*)self, [bundlePath UTF8String]);
  mapsLocationMgr = [[MapsLocationMgr alloc] init];
  textFieldMgr = [[TextFieldMgr alloc] init];
  [mapsLocationMgr startSensors];
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

//- (UIStatusBarStyle)preferredStatusBarStyle
//{
//  return statusBarBGisLight ? UIStatusBarStyleDarkContent : UIStatusBarStyleLightContent;
//}

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
  if (access([url.path UTF8String], R_OK) == 0) {
    iosApp_filePicked([url.path UTF8String]);
  } else {
    //[url startAccessingSecurityScopedResource];
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
  dispatch_async(dispatch_get_main_queue(), ^{
    NSURL* url = [NSURL fileURLWithPath:@(filename)];
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
  dispatch_async(dispatch_get_main_queue(), ^{ [vc->textFieldMgr setImeText:text selStart:selStart selEnd:selEnd]; });
}

void iosPlatform_showKeyboard(void* _vc, SDL_Rect* rect)
{
  GLViewController* vc = (__bridge GLViewController*)_vc;
  dispatch_async(dispatch_get_main_queue(), ^{ [vc->textFieldMgr showKeyboard]; });
}

void iosPlatform_hideKeyboard(void* _vc)
{
  GLViewController* vc = (__bridge GLViewController*)_vc;
  dispatch_async(dispatch_get_main_queue(), ^{ [vc->textFieldMgr hideKeyboard]; });
}

void iosPlatform_openURL(const char* url)
{
  dispatch_async(dispatch_get_main_queue(), ^{
    [UIApplication.sharedApplication openURL:[NSURL URLWithString:@(url)] options:@{} completionHandler:nil];
  });
}

//void iosPlatform_setStatusBarBG(void* _vc, BOOL isLight)
//{
//  GLViewController* vc = (__bridge GLViewController*)_vc;
//}
