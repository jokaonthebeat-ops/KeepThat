//
//  grab.m - pulls stills out of a finished film.
//
//  The alternative was re-rendering the whole 93-second film once per still,
//  which is six minutes each. AVAssetImageGenerator reads the frame directly.
//
//  usage: grab <movie.mp4> <out-dir> <sec> [sec ...]
//
#import <AVFoundation/AVFoundation.h>
#import <AppKit/AppKit.h>

int main(int argc, const char *argv[]) {
    @autoreleasepool {
        if (argc < 4) { fprintf(stderr, "usage: grab <movie> <out-dir> <sec> [sec...]\n"); return 2; }
        NSString *movie = [NSString stringWithUTF8String:argv[1]];
        NSString *dir   = [NSString stringWithUTF8String:argv[2]];
        [[NSFileManager defaultManager] createDirectoryAtPath:dir
                                  withIntermediateDirectories:YES attributes:nil error:nil];

        AVURLAsset *asset = [AVURLAsset URLAssetWithURL:[NSURL fileURLWithPath:movie] options:nil];
        AVAssetImageGenerator *gen = [AVAssetImageGenerator assetImageGeneratorWithAsset:asset];
        gen.appliesPreferredTrackTransform = YES;
        // Exact frames - the default tolerance snaps to keyframes, which can be
        // two seconds away and land on the wrong act.
        gen.requestedTimeToleranceBefore = kCMTimeZero;
        gen.requestedTimeToleranceAfter  = kCMTimeZero;

        for (int i = 3; i < argc; ++i) {
            double t = atof(argv[i]);
            NSError *err = nil;
            CGImageRef img = [gen copyCGImageAtTime:CMTimeMakeWithSeconds(t, 600)
                                         actualTime:NULL error:&err];
            if (!img) { fprintf(stderr, "  %.1fs: %s\n", t, err.localizedDescription.UTF8String); continue; }

            NSBitmapImageRep *rep = [[NSBitmapImageRep alloc] initWithCGImage:img];
            NSString *base = [NSString stringWithFormat:@"still-%.1fs", t];

            NSData *png = [rep representationUsingType:NSBitmapImageFileTypePNG properties:@{}];
            [png writeToFile:[dir stringByAppendingPathComponent:
                              [base stringByAppendingString:@".png"]] atomically:YES];

            NSData *jpg = [rep representationUsingType:NSBitmapImageFileTypeJPEG
                                            properties:@{NSImageCompressionFactor: @0.9}];
            [jpg writeToFile:[dir stringByAppendingPathComponent:
                              [base stringByAppendingString:@".jpg"]] atomically:YES];

            printf("  %-14s %zux%zu\n", base.UTF8String, CGImageGetWidth(img), CGImageGetHeight(img));
            CGImageRelease(img);
        }
    }
    return 0;
}
