//
//  mux.m - puts an audio track onto a finished video, without re-encoding it.
//
//  Kept out of the film renderer on purpose. An AVAssetWriter with both a
//  video and an audio input throttles whichever one is behind, so a renderer
//  that appends all its frames and then its audio wedges itself in a sleep
//  loop for ever; hand-interleaving them stalled too. One input never stalls,
//  so the film writes video only and the audio is composited on here.
//
//  Passthrough: neither track is re-encoded, which is why this takes seconds
//  rather than another full encode. The audio must therefore already be in a
//  form MPEG-4 accepts - AAC, not WAV. afconvert does that in one line:
//      afconvert -f m4af -d aac -b 192000 beat.wav beat.m4a
//
//  usage: mux out.mp4 video.mp4 audio.m4a [start=SEC]
//
#import <AVFoundation/AVFoundation.h>

int main(int argc, const char *argv[]) {
    @autoreleasepool {
        if (argc < 4) {
            fprintf(stderr, "usage: mux out.mp4 video.mp4 audio.m4a [start=SEC]\n");
            return 2;
        }
        NSString *outPath = [NSString stringWithUTF8String:argv[1]];
        NSString *vidPath = [NSString stringWithUTF8String:argv[2]];
        NSString *audPath = [NSString stringWithUTF8String:argv[3]];
        double start = 0.0;
        for (int i = 4; i < argc; ++i) {
            NSString *a = [NSString stringWithUTF8String:argv[i]];
            if ([a hasPrefix:@"start="]) start = [[a substringFromIndex:6] doubleValue];
        }

        AVURLAsset *video = [AVURLAsset URLAssetWithURL:[NSURL fileURLWithPath:vidPath] options:nil];
        AVURLAsset *audio = [AVURLAsset URLAssetWithURL:[NSURL fileURLWithPath:audPath] options:nil];
        AVAssetTrack *vt = [[video tracksWithMediaType:AVMediaTypeVideo] firstObject];
        AVAssetTrack *at = [[audio tracksWithMediaType:AVMediaTypeAudio] firstObject];
        if (!vt) { fprintf(stderr, "no video track in %s\n", argv[2]); return 2; }
        if (!at) { fprintf(stderr, "no audio track in %s\n", argv[3]); return 2; }

        CMTime dur = video.duration;
        AVMutableComposition *comp = [AVMutableComposition composition];
        NSError *err = nil;

        AVMutableCompositionTrack *cv =
            [comp addMutableTrackWithMediaType:AVMediaTypeVideo
                              preferredTrackID:kCMPersistentTrackID_Invalid];
        if (![cv insertTimeRange:CMTimeRangeMake(kCMTimeZero, dur) ofTrack:vt
                          atTime:kCMTimeZero error:&err]) {
            fprintf(stderr, "video: %s\n", err.localizedDescription.UTF8String); return 2;
        }

        // Trimmed to the video's length, and started at the same offset the
        // film read the material from - otherwise the music drifts against
        // the picture it was analysed from.
        CMTime aStart = CMTimeMakeWithSeconds(start, 600);
        CMTime aLen = CMTimeMinimum(dur, CMTimeSubtract(audio.duration, aStart));
        AVMutableCompositionTrack *ca =
            [comp addMutableTrackWithMediaType:AVMediaTypeAudio
                              preferredTrackID:kCMPersistentTrackID_Invalid];
        if (![ca insertTimeRange:CMTimeRangeMake(aStart, aLen) ofTrack:at
                          atTime:kCMTimeZero error:&err]) {
            fprintf(stderr, "audio: %s\n", err.localizedDescription.UTF8String); return 2;
        }

        [[NSFileManager defaultManager] removeItemAtPath:outPath error:nil];
        AVAssetExportSession *ex =
            [[AVAssetExportSession alloc] initWithAsset:comp
                                             presetName:AVAssetExportPresetPassthrough];
        ex.outputURL = [NSURL fileURLWithPath:outPath];
        ex.outputFileType = AVFileTypeMPEG4;

        __block BOOL done = NO;
        [ex exportAsynchronouslyWithCompletionHandler:^{ done = YES; }];
        while (!done) [NSThread sleepForTimeInterval:0.05];

        if (ex.status != AVAssetExportSessionStatusCompleted) {
            fprintf(stderr, "export failed: %s\n", ex.error.localizedDescription.UTF8String);
            return 2;
        }
        unsigned long long bytes =
            [[[NSFileManager defaultManager] attributesOfItemAtPath:outPath error:nil] fileSize];
        printf("wrote %s  (%.1f s, %.1f MB, video passthrough + AAC)\n",
               outPath.UTF8String, CMTimeGetSeconds(dur), bytes / 1048576.0);
    }
    return 0;
}
