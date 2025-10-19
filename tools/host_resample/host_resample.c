/* Host-side resampler index checker
 * This program reproduces mapping/index math from resample_audio and
 * searches for parameter combinations that would cause the interpolation
 * code to access srcFrame+1 out of range. Run on host to iterate fast.
 */
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <stdbool.h>

static void check_case(int src_frames, int channels, double ratio) {
    if (src_frames <= 0 || channels <= 0) return;
    int dst_frames = (int)floor(src_frames * ratio);
    if (dst_frames <= 0) dst_frames = 1;

    double mapping_ratio = ratio;
    // mapping_ratio sometimes adjusted when truncated; emulate both
    for (int use_adjust = 0; use_adjust < 2; ++use_adjust) {
        double map = mapping_ratio;
        if (use_adjust) {
            map = (double)dst_frames / (double)src_frames;
        }

        for (int dst = 0; dst < dst_frames; ++dst) {
            double srcF = dst / map;
            int srcFrame = (int)srcF;
            double frac = srcF - srcFrame;

            // For linear interpolation we will read srcFrame and srcFrame+1
            int idx0 = srcFrame;
            int idx1 = srcFrame + 1;

            if (idx0 < 0 || idx0 >= src_frames) {
                printf("OOB idx0: src_frames=%d channels=%d ratio=%.6f dst_frames=%d dst=%d srcF=%.6f srcFrame=%d\n",
                       src_frames, channels, ratio, dst_frames, dst, srcF, srcFrame);
                return;
            }
            if (idx1 < 0 || idx1 >= src_frames) {
                printf("OOB idx1: src_frames=%d channels=%d ratio=%.6f dst_frames=%d dst=%d srcF=%.6f srcFrame=%d idx1=%d map=%.6f use_adjust=%d\n",
                       src_frames, channels, ratio, dst_frames, dst, srcF, srcFrame, idx1, map, use_adjust);
                return;
            }
        }
    }
}

int main(int argc, char** argv) {
    (void)argc; (void)argv;
    int max_src_frames = 5000;
    const int channels_list[] = {1, 2};
    const double ratios[] = {0.5, 1.0, 1.5, 2.0, 3.0, 6.0, 12.0};

    bool found = false;
    for (int ch_i = 0; ch_i < (int)(sizeof(channels_list)/sizeof(channels_list[0])); ++ch_i) {
        int ch = channels_list[ch_i];
        for (int r = 0; r < (int)(sizeof(ratios)/sizeof(ratios[0])); ++r) {
            double ratio = ratios[r];
            for (int sf = 1; sf <= max_src_frames; ++sf) {
                int dst_frames = (int)floor((double)sf * ratio);
                if (dst_frames <= 0) dst_frames = 1;
                // Skip tiny frames where resampler would bypass interpolation
                if (sf < 2) continue;
                // Check mapping
                for (int use_adjust=0; use_adjust<2; ++use_adjust) {
                    double map = (use_adjust) ? ((double)dst_frames / (double)sf) : ratio;
                    for (int dst=0; dst<dst_frames; ++dst) {
                        double srcF = dst / map;
                        int srcFrame = (int)srcF;
                        int idx1 = srcFrame + 1;
                        if (srcFrame < 0 || srcFrame >= sf || idx1 < 0 || idx1 >= sf) {
                            printf("FOUND OOB: src_frames=%d channels=%d ratio=%.6f dst_frames=%d dst=%d use_adjust=%d srcF=%.6f srcFrame=%d idx1=%d map=%.6f\n",
                                   sf, ch, ratio, dst_frames, dst, use_adjust, srcF, srcFrame, idx1, map);
                            found = true;
                            goto done;
                        }
                    }
                }
            }
        }
    }
done:
    if (!found) {
        printf("No OOB cases found within tested ranges. Increase ranges to explore further.\n");
        return 0;
    }
    return 0;
}
