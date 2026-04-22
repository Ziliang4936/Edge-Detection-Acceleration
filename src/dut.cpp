#include "dut.hpp"

#include <cmath>
#include <cstdint>

namespace {

using Pixel = std::uint8_t;
using Direction = std::uint8_t;
using BlurPixel = std::uint16_t;
using Magnitude = float;

constexpr Pixel kNoEdgeLocal = 0;
constexpr Pixel kWeakEdgeLocal = 75;
constexpr Pixel kStrongEdgeLocal = 255;
constexpr int kBlurScale = 256;

// FIFO depth between DATAFLOW stages. Two rows is enough to tolerate small
// imbalances between producer and consumer throughput.
constexpr int kStreamDepth = kMaxImageWidth * 2;

constexpr float kGaussianKernel2D[kGaussianKernelSize][kGaussianKernelSize] = {
    {0.012146124730090273f, 0.026108117663175317f, 0.03369625012825844f, 0.026108117663175317f, 0.012146124730090273f},
    {0.026108117663175317f, 0.056127299040522325f, 0.07244443355700187f, 0.056127299040522325f, 0.026108117663175317f},
    {0.03369625012825844f, 0.07244443355700187f, 0.09348737760471192f, 0.07244443355700187f, 0.03369625012825844f},
    {0.026108117663175317f, 0.056127299040522325f, 0.07244443355700187f, 0.056127299040522325f, 0.026108117663175317f},
    {0.012146124730090273f, 0.026108117663175317f, 0.03369625012825844f, 0.026108117663175317f, 0.012146124730090273f},
};

constexpr int kSobelX[3][3] = {
    {-1, 0, 1},
    {-2, 0, 2},
    {-1, 0, 1},
};

constexpr int kSobelY[3][3] = {
    {1, 2, 1},
    {0, 0, 0},
    {-1, -2, -1},
};

int clamp_int(int value, int low, int high) {
    if (value < low) {
        return low;
    }
    if (value > high) {
        return high;
    }
    return value;
}

Direction quantize_direction(float gy, float gx) {
    const float angle_degrees = std::atan2(gy, gx) * 57.29577951308232f;
    float wrapped = angle_degrees;
    if (wrapped < 0.0f) {
        wrapped += 180.0f;
    }

    if ((wrapped >= 0.0f && wrapped < 22.5f) || (wrapped >= 157.5f && wrapped <= 180.0f)) {
        return 0;
    }
    if (wrapped < 67.5f) {
        return 45;
    }
    if (wrapped < 112.5f) {
        return 90;
    }
    return 135;
}

void read_header(DutStream& strmIn, int& width, int& height, float& low_threshold, float& high_threshold) {
    const DutWord version = strmIn.read();
    (void)version;
    width = static_cast<int>(strmIn.read());
    height = static_cast<int>(strmIn.read());
    low_threshold = static_cast<float>(static_cast<std::uint32_t>(strmIn.read()));
    high_threshold = static_cast<float>(static_cast<std::uint32_t>(strmIn.read()));
}

// Stage 1 (DATAFLOW): copy header-less pixel words from the AXI stream into an
// internal FIFO. Isolates downstream stages from the AXI protocol type.
void read_input_stream(DutStream& strmIn, hls::stream<Pixel>& pix_out, int width, int height) {
read_rows:
    for (int y = 0; y < height; ++y) {
    read_cols:
        for (int x = 0; x < width; ++x) {
#pragma HLS PIPELINE II=1
            pix_out.write(static_cast<Pixel>(static_cast<std::uint32_t>(strmIn.read()) & 0xffu));
        }
    }
}

// Stage 2 (DATAFLOW): 5x5 Gaussian blur using a 5-row line buffer. Output is
// delayed by (+2 rows, +2 cols) so that every sample in the 5x5 window has
// been committed to the line buffer before it is read back for convolution.
void gaussian_stage(hls::stream<Pixel>& pix_in, hls::stream<BlurPixel>& blur_out, int width, int height) {
    static Pixel line_buf[kGaussianKernelSize][kMaxImageWidth];
#pragma HLS ARRAY_PARTITION variable=line_buf dim=1 complete

gauss_y:
    for (int y_in = 0; y_in < height + 2; ++y_in) {
    gauss_x:
        for (int x = 0; x < width + 2; ++x) {
#pragma HLS PIPELINE II=1
            if (x < width) {
                Pixel new_p;
                if (y_in < height) {
                    new_p = static_cast<Pixel>(pix_in.read());
                } else {
                    // Replay last image row so the clamp-to-border reference
                    // behavior is preserved without extra ports on line_buf.
                    new_p = line_buf[(height - 1) % kGaussianKernelSize][x];
                }
                line_buf[y_in % kGaussianKernelSize][x] = new_p;
            }

            const int y_out = y_in - 2;
            const int x_out = x - 2;
            if (y_out >= 0 && y_out < height && x_out >= 0 && x_out < width) {
                float sum = 0.0f;
            gauss_ky:
                for (int ky = 0; ky < kGaussianKernelSize; ++ky) {
#pragma HLS UNROLL
                gauss_kx:
                    for (int kx = 0; kx < kGaussianKernelSize; ++kx) {
#pragma HLS UNROLL
                        const int sy = clamp_int(y_out + ky - 2, 0, height - 1);
                        const int sx = clamp_int(x_out + kx - 2, 0, width - 1);
                        sum += static_cast<float>(line_buf[sy % kGaussianKernelSize][sx]) * kGaussianKernel2D[ky][kx];
                    }
                }
                const float scaled = sum * static_cast<float>(kBlurScale);
                blur_out.write(static_cast<BlurPixel>(scaled + 0.5f));
            }
        }
    }
}

// Stage 3 (DATAFLOW): 3x3 Sobel gradients using a 3-row blur line buffer.
// Output is delayed by (+1 row, +1 col) for the same RAW-hazard reason as the
// Gaussian stage.
void sobel_stage(hls::stream<BlurPixel>& blur_in,
                 hls::stream<Magnitude>& mag_out,
                 hls::stream<Direction>& dir_out,
                 int width, int height) {
    static BlurPixel blur_buf[3][kMaxImageWidth];
#pragma HLS ARRAY_PARTITION variable=blur_buf dim=1 complete

sobel_y:
    for (int y_in = 0; y_in < height + 1; ++y_in) {
    sobel_x:
        for (int x = 0; x < width + 1; ++x) {
#pragma HLS PIPELINE II=1
            if (x < width) {
                BlurPixel new_blur;
                if (y_in < height) {
                    new_blur = static_cast<BlurPixel>(blur_in.read());
                } else {
                    new_blur = blur_buf[(height - 1) % 3][x];
                }
                blur_buf[y_in % 3][x] = new_blur;
            }

            const int y_out = y_in - 1;
            const int x_out = x - 1;
            if (y_out >= 0 && y_out < height && x_out >= 0 && x_out < width) {
                float gx = 0.0f;
                float gy = 0.0f;
            sobel_ky:
                for (int ky = 0; ky < 3; ++ky) {
#pragma HLS UNROLL
                sobel_kx:
                    for (int kx = 0; kx < 3; ++kx) {
#pragma HLS UNROLL
                        const int sy = clamp_int(y_out + ky - 1, 0, height - 1);
                        const int sx = clamp_int(x_out + kx - 1, 0, width - 1);
                        const float sample = static_cast<float>(blur_buf[sy % 3][sx]) / static_cast<float>(kBlurScale);
                        gx += sample * static_cast<float>(kSobelX[ky][kx]);
                        gy += sample * static_cast<float>(kSobelY[ky][kx]);
                    }
                }
                mag_out.write(std::sqrt((gx * gx) + (gy * gy)));
                dir_out.write(quantize_direction(gy, gx));
            }
        }
    }
}

Pixel nms_threshold_pixel(const Magnitude prev_mag[kMaxImageWidth],
                          const Magnitude curr_mag[kMaxImageWidth],
                          const Magnitude next_mag[kMaxImageWidth],
                          const Direction curr_dir[kMaxImageWidth],
                          int x,
                          float low_threshold,
                          float high_threshold) {
    const float center = curr_mag[x];
    float neighbor_a = 0.0f;
    float neighbor_b = 0.0f;

    switch (curr_dir[x]) {
        case 0:
            neighbor_a = curr_mag[x - 1];
            neighbor_b = curr_mag[x + 1];
            break;
        case 45:
            neighbor_a = next_mag[x - 1];
            neighbor_b = prev_mag[x + 1];
            break;
        case 90:
            neighbor_a = prev_mag[x];
            neighbor_b = next_mag[x];
            break;
        default:
            neighbor_a = prev_mag[x - 1];
            neighbor_b = next_mag[x + 1];
            break;
    }

    float suppressed = 0.0f;
    if (center >= neighbor_a && center >= neighbor_b) {
        suppressed = center;
    }

    if (suppressed >= high_threshold) {
        return kStrongEdgeLocal;
    }
    if (suppressed >= low_threshold) {
        return kWeakEdgeLocal;
    }
    return kNoEdgeLocal;
}

// Stage 4 (DATAFLOW): fused NMS + double threshold using a 3-row magnitude/
// direction line buffer. Output is delayed by (+1 row, +1 col) to guarantee
// that the right-hand neighbor column has been written before it is read.
void nms_threshold_stage(hls::stream<Magnitude>& mag_in,
                         hls::stream<Direction>& dir_in,
                         hls::stream<Pixel>& thresh_out,
                         int width, int height,
                         float low_threshold, float high_threshold) {
    static Magnitude mag_buf[3][kMaxImageWidth];
#pragma HLS ARRAY_PARTITION variable=mag_buf dim=1 complete
    static Direction dir_buf[3][kMaxImageWidth];
#pragma HLS ARRAY_PARTITION variable=dir_buf dim=1 complete

nms_y:
    for (int y_in = 0; y_in < height + 1; ++y_in) {
    nms_x:
        for (int x = 0; x < width + 1; ++x) {
#pragma HLS PIPELINE II=1
            if (x < width) {
                Magnitude new_mag;
                Direction new_dir;
                if (y_in < height) {
                    new_mag = mag_in.read();
                    new_dir = static_cast<Direction>(dir_in.read());
                } else {
                    new_mag = mag_buf[(height - 1) % 3][x];
                    new_dir = dir_buf[(height - 1) % 3][x];
                }
                mag_buf[y_in % 3][x] = new_mag;
                dir_buf[y_in % 3][x] = new_dir;
            }

            const int y_out = y_in - 1;
            const int x_out = x - 1;
            if (y_out >= 0 && y_out < height && x_out >= 0 && x_out < width) {
                Pixel pixel = kNoEdgeLocal;
                if (y_out > 0 && y_out < height - 1 && x_out > 0 && x_out < width - 1) {
                    pixel = nms_threshold_pixel(mag_buf[(y_out - 1) % 3],
                                                mag_buf[y_out % 3],
                                                mag_buf[(y_out + 1) % 3],
                                                dir_buf[y_out % 3],
                                                x_out,
                                                low_threshold,
                                                high_threshold);
                }
                thresh_out.write(pixel);
            }
        }
    }
}

// Stage 5 (DATAFLOW): write header + thresholded image to the output AXI stream.
void write_output_stream(hls::stream<Pixel>& thresh_in, DutStream& strmOut, int width, int height) {
    strmOut.write(static_cast<DutWord>(kDutProtocolVersion));
    strmOut.write(static_cast<DutWord>(width));
    strmOut.write(static_cast<DutWord>(height));

write_rows:
    for (int y = 0; y < height; ++y) {
    write_cols:
        for (int x = 0; x < width; ++x) {
#pragma HLS PIPELINE II=1
            strmOut.write(static_cast<DutWord>(static_cast<std::uint32_t>(thresh_in.read()) & 0xffu));
        }
    }
}

void dataflow_pipeline(DutStream& strmIn, DutStream& strmOut,
                       int width, int height,
                       float low_threshold, float high_threshold) {
#pragma HLS DATAFLOW

    hls::stream<Pixel> pix_stream;
#pragma HLS STREAM variable=pix_stream depth=kStreamDepth
    hls::stream<BlurPixel> blur_stream;
#pragma HLS STREAM variable=blur_stream depth=kStreamDepth
    hls::stream<Magnitude> mag_stream;
#pragma HLS STREAM variable=mag_stream depth=kStreamDepth
    hls::stream<Direction> dir_stream;
#pragma HLS STREAM variable=dir_stream depth=kStreamDepth
    hls::stream<Pixel> thresh_stream;
#pragma HLS STREAM variable=thresh_stream depth=kStreamDepth

    read_input_stream(strmIn, pix_stream, width, height);
    gaussian_stage(pix_stream, blur_stream, width, height);
    sobel_stage(blur_stream, mag_stream, dir_stream, width, height);
    nms_threshold_stage(mag_stream, dir_stream, thresh_stream, width, height, low_threshold, high_threshold);
    write_output_stream(thresh_stream, strmOut, width, height);
}

}  // namespace

void dut(DutStream& strmIn, DutStream& strmOut) {
#pragma HLS INTERFACE ap_ctrl_hs port=return
#pragma HLS INTERFACE axis port=strmIn
#pragma HLS INTERFACE axis port=strmOut

    int width = 0;
    int height = 0;
    float low_threshold = 0.0f;
    float high_threshold = 0.0f;

    read_header(strmIn, width, height, low_threshold, high_threshold);
    if (width <= 0 || width > kMaxImageWidth || height <= 0 || height > kMaxImageHeight) {
        strmOut.write(static_cast<DutWord>(0));
        strmOut.write(static_cast<DutWord>(0));
        strmOut.write(static_cast<DutWord>(0));
        return;
    }

    dataflow_pipeline(strmIn, strmOut, width, height, low_threshold, high_threshold);
}
