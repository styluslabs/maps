#import "OpenGLView.h"

#include "ugui/svggui_platform.h"
extern void iosApp_startLoop(void* glView, int width, int height, float dpi);
extern void iosApp_stopLoop();
extern void iosApp_startApp();

@interface OpenGLView()

- (void)setupLayer;
- (void)setupContext;
- (void)setupBuffers;
- (void)destroyBuffers;
- (void)swapBuffers;
- (void)makeContextCurrent;

@end

@implementation OpenGLView

+ (Class)layerClass 
{
  return [CAEAGLLayer class];
}

- (void)setupLayer
{
  _eaglLayer = (CAEAGLLayer*) self.layer;
  _eaglLayer.opaque = YES;
  _eaglLayer.drawableProperties = [NSDictionary dictionaryWithObjectsAndKeys:
      [NSNumber numberWithBool:NO], kEAGLDrawablePropertyRetainedBacking, 
      kEAGLColorFormatRGBA8, kEAGLDrawablePropertyColorFormat, nil];
}

- (void)setupContext 
{
  _context = [[EAGLContext alloc] initWithAPI:kEAGLRenderingAPIOpenGLES3];
  if (!_context) {
      NSLog(@"Failed to initialize OpenGLES 2.0 context");
      exit(1);
  }
  if (![EAGLContext setCurrentContext:_context]) {
      _context = nil;
      NSLog(@"Failed to set current OpenGL context");
      exit(1);
  }
}

- (void)setupBuffers
{
  glGenRenderbuffers(1, &_colorRenderBuffer);
  glBindRenderbuffer(GL_RENDERBUFFER, _colorRenderBuffer);
  [_context renderbufferStorage:GL_RENDERBUFFER fromDrawable:_eaglLayer];

  glGetRenderbufferParameteriv(GL_RENDERBUFFER, GL_RENDERBUFFER_WIDTH, &width);
  glGetRenderbufferParameteriv(GL_RENDERBUFFER, GL_RENDERBUFFER_HEIGHT, &height);
  
  glGenFramebuffers(1, &_frameBuffer);
  glBindFramebuffer(GL_FRAMEBUFFER, _frameBuffer);
  glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, _colorRenderBuffer);
}

- (void)destroyBuffers
{
  glDeleteFramebuffers(1, &_frameBuffer);
  _frameBuffer = 0;
  glDeleteRenderbuffers(1, &_colorRenderBuffer);
  _colorRenderBuffer = 0;
}

- (void)swapBuffers {
  //glClearColor(0, 1.0, 0, 1.0);
  //glClear(GL_COLOR_BUFFER_BIT);
  [_context presentRenderbuffer:GL_RENDERBUFFER];
}

- (void)makeContextCurrent {
  [EAGLContext setCurrentContext:_context];
}

- (id)initWithFrame:(CGRect)frame
{
  self = [super initWithFrame:frame];
  if (self) {
    [self setupLayer];
    [self setupContext];
  }
  iosApp_startApp();
  return self;
}

- (void)layoutSubviews 
{
  iosApp_stopLoop();
  [self makeContextCurrent];
  [self destroyBuffers];
  [self setupBuffers];
  iosApp_startLoop((__bridge void*)self, width, height, 250);  //dpi);
  //[self render];
}
// touch input

- (void)sendTouchEvent:(UITouch *)touch ofType:(int)eventType forFinger:(size_t)fingerId
{
  SDL_TouchID touchId = touch.type == UITouchTypeStylus ? PenPointerPen : 1;
  CGPoint pos = [touch preciseLocationInView:self];
  // without % INT_MAX, ts gets clamped to INT_MAX and kinetic scroll breaks after ~24 days
  int ts = (int)((long long)([touch timestamp]*1000.0 + 0.5) % INT_MAX);
  // touch.force / touch.maximumPossibleForce? - force is normalized so that 1.0 is normal force but can
  //  be greater than 1, while StrokeBuilder clamps pressure to 1
  // ... for now, just divide by 2 since 0.5 is typical pressure on other platforms
  float pressure = (touchId == PenPointerPen || touch.force > 0) ? (float)touch.force/2 : 1.0f;
  // we'll use diameter instead of radius (closer to Windows, Android)
  float w = 2*touch.majorRadius;

  SDL_Event event = {0};
  event.type = eventType;
  event.tfinger.timestamp = ts;  //SDL_GetTicks();  // normally done by SDL_PushEvent()
  event.tfinger.touchId = touchId;
  event.tfinger.fingerId = touchId == PenPointerPen ? SDL_BUTTON_LMASK : (SDL_FingerID)fingerId;
  event.tfinger.x = pos.x;
  event.tfinger.y = pos.y;
  // size of touch point
  event.tfinger.dx = w;
  event.tfinger.dy = w - 2*touch.majorRadiusTolerance;  // for now, Write just uses larger axis, so this has no effect
  event.tfinger.pressure = pressure;
  // PeepEvents bypasses gesture recognizer and event filters
  SDL_PeepEvents(&event, 1, SDL_ADDEVENT, 0, 0);  //SDL_PushEvent(&event);
  //const char* evname = eventType == SDL_FINGERDOWN ? "SDL_FINGERDOWN" : (eventType == SDL_FINGERUP ? "SDL_FINGERUP"
  //    : (eventType == SVGGUI_FINGERCANCEL ? "SVGGUI_FINGERCANCEL" : "SDL_FINGERMOTION"));
  //NSLog(@"%s touch: %f, %f; force %f; radius %f; time %d", evname, pos.x, pos.y, touch.force, touch.majorRadius, ts);
}

- (void)touchesBegan:(NSSet *)touches withEvent:(UIEvent *)event
{
  for (UITouch* touch in touches)
    [self sendTouchEvent:touch ofType:SDL_FINGERDOWN forFinger:(size_t)touch];
}

- (void)touchesEnded:(NSSet *)touches withEvent:(UIEvent *)event
{
  for (UITouch* touch in touches)
    [self sendTouchEvent:touch ofType:SDL_FINGERUP forFinger:(size_t)touch];
}

- (void)touchesCancelled:(NSSet *)touches withEvent:(UIEvent *)event
{
  for (UITouch* touch in touches)
    [self sendTouchEvent:touch ofType:SVGGUI_FINGERCANCEL forFinger:(size_t)touch];
}

- (void)touchesMoved:(NSSet *)touches withEvent:(UIEvent *)event
{
  for (UITouch* touch in touches) {
    [self sendTouchEvent:touch ofType:SDL_FINGERMOTION forFinger:(size_t)touch];
    //NSArray<UITouch*>* cTouches = [event coalescedTouchesForTouch:touch];
    //for (UITouch* cTouch in cTouches)
    //  [self sendTouchEvent:cTouch ofType:SDL_FINGERMOTION forFinger:(size_t)touch];
  }
}

@end

void OpenGLView_swapBuffers(void* _view)
{
  OpenGLView* view = (__bridge OpenGLView*)_view;
  [view swapBuffers];
}

void OpenGLView_setContextCurrent(void* _view)
{
  OpenGLView* view = (__bridge OpenGLView*)_view;
  [view makeContextCurrent];
}

// main loop
/*static CFRunLoopRef mainRunLoop = NULL;

void iosPumpEventsBlocking(void)
{
  if(!mainRunLoop)
    mainRunLoop = CFRunLoopGetCurrent();
  CFRunLoopRunInMode(kCFRunLoopDefaultMode, 100, TRUE);
  
  // necessary???
  [EAGLContext setCurrentContext:_context];
  
  sdlWin = {display, surface, nativeWin};
  if(!app) {
    app = new MapsApp(MapsApp::platform);
    app->createGUI(&sdlWin);
    app->mapsOffline->resumeDownloads();
    app->mapsSources->rebuildSource(app->config["sources"]["last_source"].Scalar());
  }
  app->setDpi(dpi);
  MapsApp::runApplication = true;
  while(MapsApp::runApplication) {
    //MapsApp::taskQueue.wait();
    int fbWidth = 0, fbHeight = 0;
    SDL_GetWindowSize(&sdlWin, &fbWidth, &fbHeight);
    if(app->drawFrame(fbWidth, fbHeight))
      swapBuffers();  //display, surface);
    // app not fully initialized until after first frame
    //if(!initialQuery.empty()) {
    //  app->mapsSearch->doSearch(initialQuery);
    //  initialQuery.clear();
    //}
  }
  sdlWin = {0, 0};
}*/

/*
// Only override drawRect: if you perform custom drawing.
// An empty implementation adversely affects performance during animation.
- (void)drawRect:(CGRect)rect
{
  // Drawing code
}
*/
