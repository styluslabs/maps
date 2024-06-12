#import "OpenGLView.h"
#import <QuartzCore/QuartzCore.h>
#include <OpenGLES/ES3/gl.h>
#include <OpenGLES/ES3/glext.h>
#include "iosApp.h"

@interface OpenGLView() {
  CAEAGLLayer* _eaglLayer;
  EAGLContext* _context;
  GLuint _colorRenderBuffer, _depthRenderBuffer, _msaaRenderBuffer;
  GLuint _frameBuffer, _msaaFrameBuffer;
  int width, height, samples;
}

- (void)setupLayerAndContext;
- (void)setupBuffers;
- (void)destroyBuffers;

@end

@implementation OpenGLView

+ (Class)layerClass 
{
  return [CAEAGLLayer class];
}

- (void)setupLayerAndContext
{
  iosApp_getGLConfig(&samples);  // in the future this could also provide, e.g., sRGB setting

  _eaglLayer = (CAEAGLLayer*) self.layer;
  _eaglLayer.opaque = YES;
  _eaglLayer.drawableProperties = [NSDictionary dictionaryWithObjectsAndKeys:
      [NSNumber numberWithBool:NO], kEAGLDrawablePropertyRetainedBacking,
      kEAGLColorFormatRGBA8, kEAGLDrawablePropertyColorFormat, nil];

  _context = [[EAGLContext alloc] initWithAPI:kEAGLRenderingAPIOpenGLES3];
  if (!_context) {
      NSLog(@"Failed to initialize OpenGL context");
      exit(1);
  }
  if (![EAGLContext setCurrentContext:_context]) {
      _context = nil;
      NSLog(@"Failed to set current OpenGL context");
      exit(1);
  }
  glGenRenderbuffers(1, &_colorRenderBuffer);
  glGenRenderbuffers(1, &_depthRenderBuffer);
  glGenRenderbuffers(1, &_msaaRenderBuffer);
  glGenFramebuffers(1, &_frameBuffer);
  glGenFramebuffers(1, &_msaaFrameBuffer);
}

- (void)setupBuffers
{
  glBindRenderbuffer(GL_RENDERBUFFER, _colorRenderBuffer);
  [_context renderbufferStorage:GL_RENDERBUFFER fromDrawable:_eaglLayer];
  
  glBindFramebuffer(GL_FRAMEBUFFER, _frameBuffer);
  glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, _colorRenderBuffer);

  glGetRenderbufferParameteriv(GL_RENDERBUFFER, GL_RENDERBUFFER_WIDTH, &width);
  glGetRenderbufferParameteriv(GL_RENDERBUFFER, GL_RENDERBUFFER_HEIGHT, &height);

  // multisample buffer
  if (samples > 1) {
    glBindFramebuffer(GL_FRAMEBUFFER, _msaaFrameBuffer);
    glBindRenderbuffer(GL_RENDERBUFFER, _msaaRenderBuffer);
    glRenderbufferStorageMultisample(GL_RENDERBUFFER, samples, GL_RGBA8, width, height);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, _msaaRenderBuffer);
  }

  // depth,stencil buffer
  glBindRenderbuffer(GL_RENDERBUFFER, _depthRenderBuffer);
  glRenderbufferStorageMultisample(GL_RENDERBUFFER, samples, GL_DEPTH24_STENCIL8, width, height);
  //glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, width, height);
  glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, _depthRenderBuffer);
  glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_STENCIL_ATTACHMENT, GL_RENDERBUFFER, _depthRenderBuffer);

  glBindRenderbuffer(GL_RENDERBUFFER, _colorRenderBuffer);
  if(glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
    NSLog(@"Error creating GLES framebuffer.");
}

- (void)destroyBuffers
{
  glDeleteFramebuffers(1, &_frameBuffer);
  glDeleteFramebuffers(1, &_msaaFrameBuffer);
  _frameBuffer = 0; _msaaFrameBuffer = 0;
  glDeleteRenderbuffers(1, &_depthRenderBuffer);
  glDeleteRenderbuffers(1, &_colorRenderBuffer);
  glDeleteRenderbuffers(1, &_msaaRenderBuffer);
  _depthRenderBuffer = 0; _colorRenderBuffer = 0; _msaaRenderBuffer = 0;
}

- (void)swapBuffers 
{
  if (samples > 1) {
    const GLenum attachments[] = {GL_COLOR_ATTACHMENT0};  //GL_DEPTH_ATTACHMENT, GL_STENCIL_ATTACHMENT
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, _frameBuffer);
    glBlitFramebuffer(0, 0, width, height, 0, 0, width, height, GL_COLOR_BUFFER_BIT, GL_NEAREST);
    glInvalidateFramebuffer(GL_READ_FRAMEBUFFER, 1, attachments);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, _msaaFrameBuffer);
  }
  glBindRenderbuffer(GL_RENDERBUFFER, _colorRenderBuffer);  // make sure correct renderbuffer is bound
  [_context presentRenderbuffer:GL_RENDERBUFFER];
}

- (void)makeContextCurrent 
{
  [EAGLContext setCurrentContext:_context];
}

- (id)initWithFrame:(CGRect)frame
{
  self = [super initWithFrame:frame];
  if (self) {
    width = 0;
    height = 0;
    self.contentScaleFactor = [[UIScreen mainScreen] scale];
    self.multipleTouchEnabled = YES;
    [self setupLayerAndContext];
  }
  return self;
}

- (void)dealloc
{
  [self destroyBuffers];
}

- (void)layoutSubviews 
{
  int w = (int)(self.bounds.size.width * self.contentScaleFactor);
  int h = (int)(self.bounds.size.height * self.contentScaleFactor);
  if(w != width || h != height) {
    iosApp_stopLoop();
    [self makeContextCurrent];
    [self setupBuffers];
    iosApp_startLoop(width, height, self.contentScaleFactor*163);
  }
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
  float scale = self.contentScaleFactor;

  SDL_Event event = {0};
  event.type = eventType;
  event.tfinger.timestamp = ts;  //SDL_GetTicks();  // normally done by SDL_PushEvent()
  event.tfinger.touchId = touchId;
  event.tfinger.fingerId = touchId == PenPointerPen ? SDL_BUTTON_LMASK : (SDL_FingerID)fingerId;
  event.tfinger.x = pos.x * scale;
  event.tfinger.y = pos.y * scale;
  // size of touch point
  event.tfinger.dx = w * scale;
  event.tfinger.dy = (w - 2*touch.majorRadiusTolerance)*scale;
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
