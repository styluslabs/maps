#import <UIKit/UIKit.h>

@interface OpenGLView : UIView

- (void)swapBuffers;
- (void)makeContextCurrent;
- (void)createSharedContext;

@end
