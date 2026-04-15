#include <chrono>
#include <exception>
#include <iostream>
#include <string>

#include "gaussian.hpp"
#include "hysteresis.hpp"
#include "image_io.hpp"
#include "nms.hpp"
#include "sobel.hpp"
#include "threshold.hpp"

namespace {

struct Options {
    std::string input_path;
    std::string output_path;
    float low_threshold = 40.0f;
    float high_threshold = 100.0f;
    int kernel_size = 5;
    float sigma = 1.4f;
    std::string dump_prefix;
};

void print_usage() {
    std::cout
        << "Usage:\n"
        << "  edge_detector <input.pgm> <output.pgm> [--low value] [--high value] [--kernel odd] [--sigma value] [--dump-prefix prefix]\n";
}

Options parse_args(int argc, char* argv[]) {
    if (argc < 3) {
        print_usage();
        throw std::runtime_error("Missing required arguments.");
    }

    Options options;
    options.input_path = argv[1];
    options.output_path = argv[2];

    for (int i = 3; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--low" && i + 1 < argc) {
            options.low_threshold = std::stof(argv[++i]);
        } else if (arg == "--high" && i + 1 < argc) {
            options.high_threshold = std::stof(argv[++i]);
        } else if (arg == "--kernel" && i + 1 < argc) {
            options.kernel_size = std::stoi(argv[++i]);
        } else if (arg == "--sigma" && i + 1 < argc) {
            options.sigma = std::stof(argv[++i]);
        } else if (arg == "--dump-prefix" && i + 1 < argc) {
            options.dump_prefix = argv[++i];
        } else {
            print_usage();
            throw std::runtime_error("Unknown or incomplete argument: " + arg);
        }
    }

    return options;
}

void maybe_dump_image(const std::string& prefix, const std::string& suffix, const ImageU8& image) {
    if (!prefix.empty()) {
        write_pgm(prefix + "_" + suffix + ".pgm", image);
    }
}

}

int main(int argc, char* argv[]) {
    try {
        const Options options = parse_args(argc, argv);

        const ImageU8 input_u8 = read_pgm(options.input_path);
        const ImageF32 input_f32 = u8_to_f32(input_u8);
        // const ImageF32 blurred = gaussian_blur(input_f32, options.kernel_size, options.sigma);
        // const GradientData gradients = compute_sobel_gradients(blurred);
        // const ImageF32 suppressed = non_maximum_suppression(gradients.magnitude, gradients.direction);
        // const ImageU8 thresholded = double_threshold(suppressed, options.low_threshold, options.high_threshold);
        // const ImageU8 edges = hysteresis(thresholded);
        constexpr int kBenchmarkIterations = 100;
        using Clock = std::chrono::high_resolution_clock;

        ImageF32 blurred;
        GradientData gradients;
        ImageF32 suppressed;
        ImageU8 thresholded;
        ImageU8 edges;

        long long gaussian_total = 0, sobel_total = 0, nms_total = 0, thresh_total = 0, hyst_total = 0;

        for (int iter = 0; iter < kBenchmarkIterations; ++iter) {
            const auto t1 = Clock::now();
            blurred = gaussian_blur(input_f32, options.kernel_size, options.sigma);
            const auto t2 = Clock::now();
            gradients = compute_sobel_gradients(blurred);
            const auto t3 = Clock::now();
            suppressed = non_maximum_suppression(gradients.magnitude, gradients.direction);
            const auto t4 = Clock::now();
            thresholded = double_threshold(suppressed, options.low_threshold, options.high_threshold);
            const auto t5 = Clock::now();
            edges = hysteresis(thresholded);
            const auto t6 = Clock::now();


            gaussian_total += std::chrono::duration_cast<std::chrono::microseconds>(t2 - t1).count();
            sobel_total    += std::chrono::duration_cast<std::chrono::microseconds>(t3 - t2).count();
            nms_total      += std::chrono::duration_cast<std::chrono::microseconds>(t4 - t3).count();
            thresh_total   += std::chrono::duration_cast<std::chrono::microseconds>(t5 - t4).count();
            hyst_total     += std::chrono::duration_cast<std::chrono::microseconds>(t6 - t5).count();
        }

        const double gaussian_avg = static_cast<double>(gaussian_total) / kBenchmarkIterations;
        const double sobel_avg    = static_cast<double>(sobel_total)    / kBenchmarkIterations;
        const double nms_avg    = static_cast<double>(nms_total)    / kBenchmarkIterations;
        const double thresh_avg = static_cast<double>(thresh_total) / kBenchmarkIterations;
        const double hyst_avg   = static_cast<double>(hyst_total)   / kBenchmarkIterations;
        const double total_avg  = gaussian_avg + sobel_avg + nms_avg + thresh_avg + hyst_avg;

        std::cout << "Image: " << options.input_path
                  << " (" << input_u8.width << "x" << input_u8.height << ")\n";
        std::cout << "--- Baseline timing (stages 1-5, avg over "
                  << kBenchmarkIterations << " iterations) ---\n";
        std::cout << "  Stage 1  Gaussian Blur:   " << gaussian_avg << " us\n";
        std::cout << "  Stage 2  Sobel Gradients: " << sobel_avg    << " us\n";
        std::cout << "  Stage 3  NMS:             " << nms_avg      << " us\n";
        std::cout << "  Stage 4  Double Thresh:   " << thresh_avg   << " us\n";
        std::cout << "  Stage 5  Hysteresis:      " << hyst_avg     << " us\n";
        std::cout << "  Total:                    " << total_avg    << " us\n";

        write_pgm(options.output_path, edges);
        maybe_dump_image(options.dump_prefix, "blur", f32_to_u8_clamped(blurred));
        maybe_dump_image(options.dump_prefix, "gradient", f32_to_u8_normalized(gradients.magnitude));
        maybe_dump_image(options.dump_prefix, "nms", f32_to_u8_normalized(suppressed));
        maybe_dump_image(options.dump_prefix, "threshold", thresholded);

        std::cout << "Edge detection complete.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Error: " << error.what() << "\n";
        return 1;
    }
}
