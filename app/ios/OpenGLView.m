#import "OpenGLView.h"
#import <QuartzCore/QuartzCore.h>
//#import <Metal/Metal.h>
//#include <OpenGLES/ES3/gl.h>
//#include <OpenGLES/ES3/glext.h>

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES3/gl3.h>

#include "iosApp.h"

@interface OpenGLView() {
  //CAEAGLLayer* _eaglLayer;
  //EAGLContext* _context;
  //EAGLContext* _context2;

  CAMetalLayer* mtl_layer;
  EGLContext egl_context;
  EGLSurface egl_surface;
  EGLDisplay egl_display;
  EGLContext egl_context2;

  //GLuint _colorRenderBuffer, _depthRenderBuffer, _msaaRenderBuffer;
  //GLuint _frameBuffer, _msaaFrameBuffer;
  int width, height, samples;
}

- (void)setupLayerAndContext;
- (void)setupBuffers;
- (void)destroyBuffers;

@end

@implementation OpenGLView

+ (Class)layerClass
{
  return [CAMetalLayer class];
}

- (void)setupLayerAndContext
{
    iosApp_getGLConfig(&samples);  // in the future this could also provide, e.g., sRGB setting
    // MSAA is cheap on mobile (tile-based) GPUs and 2x MSAA apparently can have artifacts on iPhone
    if(samples > 1) { samples = 4; }

    mtl_layer = (CAMetalLayer*) self.layer;
    //mtl_layer.opaque = YES;
    //mtl_layer.device = MTLCreateSystemDefaultDevice();
    //mtl_layer.contentsScale = [UIScreen mainScreen].scale;

    EGLAttrib egl_display_attribs[] = {
        EGL_PLATFORM_ANGLE_TYPE_ANGLE, EGL_PLATFORM_ANGLE_TYPE_METAL_ANGLE,
        EGL_POWER_PREFERENCE_ANGLE, EGL_HIGH_POWER_ANGLE,
        EGL_NONE
    };

    EGLDisplay display = eglGetPlatformDisplay(EGL_PLATFORM_ANGLE_ANGLE, (void*) EGL_DEFAULT_DISPLAY, egl_display_attribs);
    egl_display = display;
    if (display == EGL_NO_DISPLAY)
    {
        NSLog(@"Failed to get EGL display");
        exit(1);  //return SDL_APP_FAILURE;
    }

    if (eglInitialize(display, NULL, NULL) == false)
    {
        NSLog(@"Failed to initialize EGL");
        exit(1);  //return SDL_APP_FAILURE;
    }

    EGLint egl_config_attribs[] = {
        EGL_RED_SIZE, 8,
        EGL_GREEN_SIZE, 8,
        EGL_BLUE_SIZE, 8,
        EGL_ALPHA_SIZE, 8,
        EGL_DEPTH_SIZE, 24,
        EGL_STENCIL_SIZE, 8,
        EGL_COLOR_BUFFER_TYPE, EGL_RGB_BUFFER,
        EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT,
        EGL_SAMPLES, samples,
        EGL_NONE
    };

    EGLConfig config;
    EGLint configs_count;
    if (!eglChooseConfig(display, egl_config_attribs, &config, 1, &configs_count))
    {
        NSLog(@"Failed to choose EGL config");
        exit(1);  //return SDL_APP_FAILURE;
    }

    EGLint egl_context_attribs[] = {
        EGL_CONTEXT_CLIENT_VERSION, 3,
        EGL_NONE
    };
    EGLContext context = eglCreateContext(display, config, EGL_NO_CONTEXT, egl_context_attribs);
    egl_context = context;
    if (context == EGL_NO_CONTEXT) {
        NSLog(@"Failed to create EGL context");
        exit(1);  //return SDL_APP_FAILURE;
    }

    egl_context2 = eglCreateContext(display, config, context, egl_context_attribs);
    if(!egl_context2) { NSLog(@"Offscreen context: eglCreateContext() error %X", eglGetError()); }

    EGLSurface surface = eglCreateWindowSurface(display, config, (__bridge EGLNativeWindowType)mtl_layer, NULL);
    egl_surface = surface;
    if (surface == EGL_NO_SURFACE)
    {
        NSLog(@"Failed to create EGL surface");
        exit(1);  //return SDL_APP_FAILURE;
    }

    if (!eglMakeCurrent(display, surface, surface, context))
    {
        NSLog(@"Failed to make EGL context current");
        exit(1);  //return SDL_APP_FAILURE;
    }

    const char* renderer = glGetString(GL_RENDERER);
    const char* vendor = glGetString(GL_VENDOR);
    const char* version = glGetString(GL_VERSION);
    NSLog(@"\nOpenGL ES Renderer: %s, Vendor: %s, Version: %s\n", renderer, vendor, version);

    eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);

/*
  iosApp_getGLConfig(&samples);  // in the future this could also provide, e.g., sRGB setting
  // MSAA is cheap on mobile (tile-based) GPUs and 2x MSAA apparently can have artifacts on iPhone
  if(samples > 1) { samples = 4; }

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
  _context2 = [[EAGLContext alloc] initWithAPI:[_context API] sharegroup:[_context sharegroup]];
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
*/
}

// this should be called from a thread
- (void)createSharedContext
{
/*
  // seems there can be a deadlock between creating shared context on worker thread and using main context
  //  on main thread, so we have to create shared context on main thread too
  //EAGLContext* _context2 = [[EAGLContext alloc] initWithAPI:[_context API] sharegroup:[_context sharegroup]];
  if (!_context2) {
      NSLog(@"Failed to create shared OpenGL context");
  } else if (![EAGLContext setCurrentContext:_context2]) {
      NSLog(@"setCurrentContext failed for shared OpenGL context");
  }
*/


  int curr_res = eglMakeCurrent(egl_display, EGL_NO_SURFACE, EGL_NO_SURFACE, egl_context2);
  if(curr_res == EGL_FALSE) { NSLog(@"Offscreen context: eglMakeCurrent() error %X", eglGetError()); }

}

- (void)setupBuffers
{
/*
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
*/
}

- (void)destroyBuffers
{
/*
  glDeleteFramebuffers(1, &_frameBuffer);
  glDeleteFramebuffers(1, &_msaaFrameBuffer);
  _frameBuffer = 0; _msaaFrameBuffer = 0;
  glDeleteRenderbuffers(1, &_depthRenderBuffer);
  glDeleteRenderbuffers(1, &_colorRenderBuffer);
  glDeleteRenderbuffers(1, &_msaaRenderBuffer);
  _depthRenderBuffer = 0; _colorRenderBuffer = 0; _msaaRenderBuffer = 0;
*/
}

- (void)swapBuffers
{
/*
  if (samples > 1) {
    const GLenum attachments[] = {GL_COLOR_ATTACHMENT0, GL_DEPTH_ATTACHMENT, GL_STENCIL_ATTACHMENT};
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, _frameBuffer);
    glBlitFramebuffer(0, 0, width, height, 0, 0, width, height, GL_COLOR_BUFFER_BIT, GL_NEAREST);
    glInvalidateFramebuffer(GL_READ_FRAMEBUFFER, 3, attachments);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, _msaaFrameBuffer);
  }
  glBindRenderbuffer(GL_RENDERBUFFER, _colorRenderBuffer);  // make sure correct renderbuffer is bound
  [_context presentRenderbuffer:GL_RENDERBUFFER];
*/
  eglSwapBuffers(egl_display, egl_surface);
}

- (void)makeContextCurrent
{
  //[EAGLContext setCurrentContext:_context];
  if(!eglMakeCurrent(egl_display, egl_surface, egl_surface, egl_context))
  {
      NSLog(@"Failed to make EGL context current on thread!");
  }
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
    NSLog(@"layoutSubviews: %d x %d (scale %f)", w, h, self.contentScaleFactor);
    UIWindow *window = UIApplication.sharedApplication.keyWindow;
    CGFloat topInset = window.safeAreaInsets.top;
    CGFloat bottomInset = window.safeAreaInsets.bottom;

    iosApp_stopLoop();
    
    width = w;
    height = h;
    //eglQuerySurface(eglDisplay, eglSurface, EGL_WIDTH, &width);
    //eglQuerySurface(eglDisplay, eglSurface, EGL_HEIGHT, &height);

    //[self makeContextCurrent];
    //[self setupBuffers];
    iosApp_startLoop(width, height, self.contentScaleFactor*163, (float)topInset, (float)bottomInset);
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

