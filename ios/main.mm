// ios/main.mm — the lantern iOS host app. UIKit lifecycle + CADisplayLink
// game loop driving a C-ABI game (games/touchdemo, compiled in with
// LANTERN_NO_MAIN). iOS inverts control — there is no while(lt_frame_poll())
// main loop here; the display link calls one frame's worth of engine per
// tick, which is exactly what the manual-loop half of the ABI is for.
#import <UIKit/UIKit.h>
#import <QuartzCore/CAMetalLayer.h>
#include "lantern.h"
#include "platform_ios.h"

extern "C" {
void touchdemo_init(void);
void touchdemo_update(double dt);
void touchdemo_draw(void);
}

// ---- the Metal-backed view; forwards touches in drawable pixels --------

@interface LanternView : UIView
@end

@implementation LanternView
+ (Class)layerClass {
    return [CAMetalLayer class];
}
- (void)forward:(NSSet<UITouch*>*)touches began:(BOOL)began {
    UITouch* t = [touches anyObject];
    CGPoint p = [t locationInView:self];
    float s = (float)self.contentScaleFactor;
    if (began)
        lt::iosTouchBegan((float)p.x * s, (float)p.y * s);
    else
        lt::iosTouchMoved((float)p.x * s, (float)p.y * s);
}
- (void)touchesBegan:(NSSet<UITouch*>*)touches withEvent:(UIEvent*)event {
    [self forward:touches began:YES];
}
- (void)touchesMoved:(NSSet<UITouch*>*)touches withEvent:(UIEvent*)event {
    [self forward:touches began:NO];
}
- (void)touchesEnded:(NSSet<UITouch*>*)touches withEvent:(UIEvent*)event {
    lt::iosTouchEnded();
}
- (void)touchesCancelled:(NSSet<UITouch*>*)touches withEvent:(UIEvent*)event {
    lt::iosTouchEnded();
}
@end

// ---- view controller: boots the engine once layout is real -------------

@interface LanternVC : UIViewController
@end

@implementation LanternVC {
    CADisplayLink* _link;
    BOOL _booted;
}
- (void)loadView {
    self.view = [[LanternView alloc] init];
    self.view.multipleTouchEnabled = NO; // single touch, 3DS-style
}
- (BOOL)prefersStatusBarHidden {
    return YES;
}
- (BOOL)prefersHomeIndicatorAutoHidden {
    return YES;
}
- (void)viewDidLayoutSubviews {
    [super viewDidLayoutSubviews];
    CAMetalLayer* layer = (CAMetalLayer*)self.view.layer;
    CGFloat s = self.view.window ? self.view.window.screen.nativeScale
                                 : UIScreen.mainScreen.nativeScale;
    self.view.contentScaleFactor = s;
    layer.contentsScale = s;
    layer.drawableSize = CGSizeMake(self.view.bounds.size.width * s,
                                    self.view.bounds.size.height * s);
}
- (void)viewDidAppear:(BOOL)animated {
    [super viewDidAppear:animated];
    if (_booted) return;
    lt::iosSetMetalLayer((CAMetalLayer*)self.view.layer);
    if (!lt_boot("lantern", 1)) {
        NSLog(@"lantern: boot failed");
        return;
    }
    touchdemo_init();
    _booted = YES;
    _link = [CADisplayLink displayLinkWithTarget:self selector:@selector(tick)];
    _link.preferredFramesPerSecond = 60; // the engine's native cadence
    [_link addToRunLoop:NSRunLoop.mainRunLoop forMode:NSRunLoopCommonModes];
}
- (void)tick {
    if (!lt_frame_poll()) { // lt_quit / LANTERN_SHOT capture finished
        [_link invalidate];
        lt_shutdown();
        exit(0); // dev/CI harness behavior; a store build would idle instead
    }
    touchdemo_update(lt_frame_dt());
    lt_frame_begin();
    touchdemo_draw();
    lt_frame_end();
}
@end

// ---- app delegate -------------------------------------------------------

@interface LanternAppDelegate : UIResponder <UIApplicationDelegate>
@property(strong, nonatomic) UIWindow* window;
@end

@implementation LanternAppDelegate
- (BOOL)application:(UIApplication*)application
    didFinishLaunchingWithOptions:(NSDictionary*)launchOptions {
    self.window = [[UIWindow alloc] initWithFrame:UIScreen.mainScreen.bounds];
    self.window.rootViewController = [[LanternVC alloc] init];
    [self.window makeKeyAndVisible];
    return YES;
}
@end

int main(int argc, char* argv[]) {
    @autoreleasepool {
        return UIApplicationMain(argc, argv, nil,
                                 NSStringFromClass([LanternAppDelegate class]));
    }
}
