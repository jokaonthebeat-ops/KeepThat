//
//  mkvideo.m - encodes a numbered PNG sequence into an H.264 .mp4.
//
//  There is no ffmpeg on this machine and installing one would mean either
//  Homebrew (needs an admin password) or trusting a third-party binary off
//  the web. Neither is necessary: macOS ships AVFoundation, which is the same
//  encoder Final Cut and QuickTime use, and the Command Line Tools ship the
//  headers. This is ~150 lines against it.
//
//  Scaling is done by Core Graphics rather than by hand, so the downscale is
//  properly filtered. H.264 wants even dimensions, so both are rounded down
//  to even.
//
//  usage: mkvideo out.mp4 <frame-prefix> [fps] [width] [audio=f] [audiostart=s]
//         reads <frame-prefix>_00000.jpg / _0000.png, ...
//
//  With `audio=`, an AAC track is written alongside the video in the same
//  pass. `audiostart=` skips into the source, and must match the offset the
//  film was rendered with or the picture and the music drift apart.
//
#import <AVFoundation/AVFoundation.h>
#import <CoreVideo/CoreVideo.h>
#import <ImageIO/ImageIO.h>

static CGImageRef loadPNG(NSString *path) {
    NSURL *url = [NSURL fileURLWithPath:path];
    CGImageSourceRef src = CGImageSourceCreateWithURL((__bridge CFURLRef)url, NULL);
    if (!src) return NULL;
    CGImageRef img = CGImageSourceCreateImageAtIndex(src, 0, NULL);
    CFRelease(src);
    return img;
}

int main(int argc, const char *argv[]) {
    @autoreleasepool {
        if (argc < 3) {
            fprintf(stderr, "usage: mkvideo out.mp4 <frame-prefix> [fps] [width]\n");
            return 2;
        }
        NSString *outPath = [NSString stringWithUTF8String:argv[1]];
        NSString *prefix  = [NSString stringWithUTF8String:argv[2]];
        // Named arguments in any order; the bare numbers are positional,
        // first fps then width.
        int fps = 30, wantW = 0;
        NSString *audioPath = nil;
        double audioStart = 0.0;
        NSMutableArray<NSString *> *positional = [NSMutableArray array];
        for (int i = 3; i < argc; ++i) {
            NSString *a = [NSString stringWithUTF8String:argv[i]];
            if ([a hasPrefix:@"audio="])           audioPath  = [a substringFromIndex:6];
            else if ([a hasPrefix:@"audiostart="]) audioStart = [[a substringFromIndex:11] doubleValue];
            else                                   [positional addObject:a];
        }
        if (positional.count > 0) fps   = positional[0].intValue;
        if (positional.count > 1) wantW = positional[1].intValue;
        if (fps <= 0) fps = 30;

        // Collect the frame sequence.
        // Accepts either padding and either format: uishot writes 4-digit PNG,
        // the film tool writes 5-digit JPEG (a 93-second film in PNG masters
        // would be several gigabytes).
        NSMutableArray<NSString *> *frames = [NSMutableArray array];
        NSArray *patterns = @[@"%@_%05d.jpg", @"%@_%05d.png", @"%@_%04d.png", @"%@_%04d.jpg"];
        for (NSString *pat in patterns) {
            for (int i = 0; ; ++i) {
                NSString *p = [NSString stringWithFormat:pat, prefix, i];
                if (![[NSFileManager defaultManager] fileExistsAtPath:p]) break;
                [frames addObject:p];
            }
            if (frames.count > 0) break;
        }
        if (frames.count == 0) {
            fprintf(stderr, "no frames found matching %s_0000.png\n", argv[2]);
            return 2;
        }

        CGImageRef first = loadPNG(frames[0]);
        if (!first) { fprintf(stderr, "could not read %s\n", [frames[0] UTF8String]); return 2; }
        size_t srcW = CGImageGetWidth(first), srcH = CGImageGetHeight(first);

        size_t outW = wantW > 0 ? (size_t)wantW : srcW;
        size_t outH = (size_t)llround((double)srcH * (double)outW / (double)srcW);
        outW &= ~(size_t)1;                 // H.264 needs even dimensions
        outH &= ~(size_t)1;
        CGImageRelease(first);

        [[NSFileManager defaultManager] removeItemAtPath:outPath error:nil];

        NSError *err = nil;
        AVAssetWriter *writer =
            [AVAssetWriter assetWriterWithURL:[NSURL fileURLWithPath:outPath]
                                     fileType:AVFileTypeMPEG4
                                        error:&err];
        if (!writer) { fprintf(stderr, "writer: %s\n", err.localizedDescription.UTF8String); return 2; }

        // ~12 Mbit at 1080-ish is plenty for flat UI graphics and keeps the
        // neon gradients from banding.
        NSDictionary *compression = @{
            AVVideoAverageBitRateKey            : @(12000000),
            AVVideoMaxKeyFrameIntervalKey       : @(fps * 2),
            AVVideoProfileLevelKey              : AVVideoProfileLevelH264HighAutoLevel,
            AVVideoAllowFrameReorderingKey      : @NO,
        };
        AVAssetWriterInput *input =
            [AVAssetWriterInput assetWriterInputWithMediaType:AVMediaTypeVideo
                                               outputSettings:@{
                AVVideoCodecKey                 : AVVideoCodecTypeH264,
                AVVideoWidthKey                 : @(outW),
                AVVideoHeightKey                : @(outH),
                AVVideoCompressionPropertiesKey : compression,
            }];
        input.expectsMediaDataInRealTime = NO;

        AVAssetWriterInputPixelBufferAdaptor *adaptor =
            [AVAssetWriterInputPixelBufferAdaptor
                assetWriterInputPixelBufferAdaptorWithAssetWriterInput:input
                                           sourcePixelBufferAttributes:@{
                (id)kCVPixelBufferPixelFormatTypeKey : @(kCVPixelFormatType_32BGRA),
                (id)kCVPixelBufferWidthKey           : @(outW),
                (id)kCVPixelBufferHeightKey          : @(outH),
            }];

        // ---- optional audio track ------------------------------------------
        AVAssetWriterInput *audioIn = nil;
        AVAssetReader *audioReader = nil;
        AVAssetReaderTrackOutput *audioOut = nil;
        const double videoSeconds = (double)frames.count / fps;

        if (audioPath) {
            AVURLAsset *aAsset = [AVURLAsset URLAssetWithURL:[NSURL fileURLWithPath:audioPath] options:nil];
            AVAssetTrack *aTrack = [[aAsset tracksWithMediaType:AVMediaTypeAudio] firstObject];
            if (!aTrack) { fprintf(stderr, "no audio track in %s\n", audioPath.UTF8String); return 2; }

            audioReader = [AVAssetReader assetReaderWithAsset:aAsset error:&err];
            audioReader.timeRange = CMTimeRangeMake(CMTimeMakeWithSeconds(audioStart, 600),
                                                    CMTimeMakeWithSeconds(videoSeconds, 600));
            audioOut = [AVAssetReaderTrackOutput assetReaderTrackOutputWithTrack:aTrack
                        outputSettings:@{
                            AVFormatIDKey            : @(kAudioFormatLinearPCM),
                            AVLinearPCMBitDepthKey   : @16,
                            AVLinearPCMIsFloatKey    : @NO,
                            AVLinearPCMIsBigEndianKey: @NO,
                            AVLinearPCMIsNonInterleaved: @NO,
                        }];
            [audioReader addOutput:audioOut];

            AudioChannelLayout stereo = {0};
            stereo.mChannelLayoutTag = kAudioChannelLayoutTag_Stereo;
            audioIn = [AVAssetWriterInput assetWriterInputWithMediaType:AVMediaTypeAudio
                       outputSettings:@{
                           AVFormatIDKey         : @(kAudioFormatMPEG4AAC),
                           AVNumberOfChannelsKey : @2,
                           AVSampleRateKey       : @48000,
                           AVEncoderBitRateKey   : @192000,
                           AVChannelLayoutKey    : [NSData dataWithBytes:&stereo length:sizeof(stereo)],
                       }];
            audioIn.expectsMediaDataInRealTime = NO;
            [writer addInput:audioIn];
        }

        [writer addInput:input];
        if (![writer startWriting]) {
            fprintf(stderr, "startWriting: %s\n", writer.error.localizedDescription.UTF8String);
            return 2;
        }
        [writer startSessionAtSourceTime:kCMTimeZero];

        CGColorSpaceRef cs = CGColorSpaceCreateDeviceRGB();
        int written = 0;

        for (NSUInteger i = 0; i < frames.count; ++i) {
            CGImageRef img = loadPNG(frames[i]);
            if (!img) continue;

            CVPixelBufferRef pb = NULL;
            if (CVPixelBufferPoolCreatePixelBuffer(NULL, adaptor.pixelBufferPool, &pb) != kCVReturnSuccess) {
                CGImageRelease(img);
                fprintf(stderr, "pixel buffer pool exhausted at frame %lu\n", (unsigned long)i);
                return 2;
            }

            CVPixelBufferLockBaseAddress(pb, 0);
            CGContextRef ctx = CGBitmapContextCreate(CVPixelBufferGetBaseAddress(pb),
                                                     outW, outH, 8,
                                                     CVPixelBufferGetBytesPerRow(pb), cs,
                                                     kCGImageAlphaNoneSkipFirst
                                                       | kCGBitmapByteOrder32Little);
            // Opaque black behind, so any alpha in the render composites the
            // same way it does on screen.
            CGContextSetRGBFillColor(ctx, 0, 0, 0, 1);
            CGContextFillRect(ctx, CGRectMake(0, 0, outW, outH));
            CGContextSetInterpolationQuality(ctx, kCGInterpolationHigh);
            CGContextDrawImage(ctx, CGRectMake(0, 0, outW, outH), img);
            CGContextRelease(ctx);
            CVPixelBufferUnlockBaseAddress(pb, 0);
            CGImageRelease(img);

            while (!input.isReadyForMoreMediaData)
                [NSThread sleepForTimeInterval:0.002];

            if (![adaptor appendPixelBuffer:pb withPresentationTime:CMTimeMake((int64_t)i, fps)]) {
                fprintf(stderr, "append failed at frame %lu: %s\n", (unsigned long)i,
                        writer.error.localizedDescription.UTF8String);
                CVPixelBufferRelease(pb);
                return 2;
            }
            CVPixelBufferRelease(pb);
            ++written;
        }

        CGColorSpaceRelease(cs);
        [input markAsFinished];

        // Audio is pumped after the picture. The writer is not real-time, so
        // the inputs do not have to be interleaved - only both finished before
        // the file is closed.
        if (audioIn) {
            int appended = 0;
            if ([audioReader startReading]) {
                while (1) {
                    CMSampleBufferRef sb = [audioOut copyNextSampleBuffer];
                    if (!sb) break;
                    while (!audioIn.isReadyForMoreMediaData)
                        [NSThread sleepForTimeInterval:0.002];
                    if (![audioIn appendSampleBuffer:sb]) { CFRelease(sb); break; }
                    CFRelease(sb);
                    ++appended;
                }
                printf("  muxed %d audio buffers from %s (from %.1fs)\n",
                       appended, [audioPath lastPathComponent].UTF8String, audioStart);
            } else {
                fprintf(stderr, "  WARNING: could not read audio (%s) - writing silent video\n",
                        audioReader.error.localizedDescription.UTF8String);
            }
            // ALWAYS finish this input. An input that is added to the writer
            // and never marked finished makes finishWriting hang forever, with
            // a zero-byte file and no error - which is exactly what happened.
            [audioIn markAsFinished];
        }

        __block BOOL done = NO;
        [writer finishWritingWithCompletionHandler:^{ done = YES; }];
        while (!done) [NSThread sleepForTimeInterval:0.01];

        if (writer.status != AVAssetWriterStatusCompleted) {
            fprintf(stderr, "finish: %s\n", writer.error.localizedDescription.UTF8String);
            return 2;
        }

        unsigned long long bytes =
            [[[NSFileManager defaultManager] attributesOfItemAtPath:outPath error:nil] fileSize];
        printf("wrote %s  (%d frames, %zux%zu, %d fps, %.1f s, %.1f MB%s)\n",
               outPath.UTF8String, written, outW, outH, fps,
               (double)written / fps, bytes / 1048576.0,
               audioPath ? ", with AAC audio" : ", silent");
    }
    return 0;
}
