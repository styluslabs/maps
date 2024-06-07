#import <UIKit/UIKit.h>
#import <QuartzCore/QuartzCore.h>
#include <OpenGLES/ES3/gl.h>
#include <OpenGLES/ES3/glext.h>

@interface OpenGLView : UIView {
  CAEAGLLayer* _eaglLayer;
  EAGLContext* _context;
  GLuint _colorRenderBuffer;
  GLuint _frameBuffer;
  int width;
  int height;
}
@end
