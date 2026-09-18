// camera_hls.cpp
//
// Low-latency webcam -> HLS streamer.
//
// Input:
//   /dev/video0
//
// Output:
//   /tmp/camera/stream.m3u8
//   /tmp/camera/seg*.ts
//
// Build:
//   g++ -std=c++17 -O2 -Wall -Wextra camera_hls.cpp -o camera_hls
//
// Run:
//   ./camera_hls
//
// Notes:
// - HLS cannot have literally zero latency: the browser must receive media
//   after it has been encoded and put into HLS segments.
// - This uses very short 200 ms segments and a 200 ms GOP.
// - The camera is expected to output MJPEG, so the MJPEG frames are decoded
//   and re-encoded to H.264 for browser-compatible HLS.
// - For genuinely sub-200-ms glass-to-glass latency, use WebRTC instead.

#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>

namespace fs = std::filesystem;

int main()
{
    constexpr const char* camera = "/dev/video0";
    constexpr const char* output_dir = "/tmp/camera";

    // 30 FPS, 1920x1080, MJPEG camera input.
    constexpr int fps = 30;
    constexpr int gop = 6; // 6 frames = 200 ms at 30 FPS

    if (!fs::exists(camera)) {
        std::cerr << "Camera not found: " << camera << '\n';
        return 1;
    }

    std::error_code ec;
    fs::create_directories(output_dir, ec);
    if (ec) {
        std::cerr << "Cannot create " << output_dir << ": "
                  << ec.message() << '\n';
        return 1;
    }

    // Remove old HLS files. This avoids starting with an old playlist.
    for (const auto& entry : fs::directory_iterator(output_dir, ec)) {
        if (ec)
            break;

        const auto name = entry.path().filename().string();

        if (name == "stream.m3u8" ||
            name.rfind("seg", 0) == 0 ||
            name.rfind("init", 0) == 0) {
            fs::remove(entry.path(), ec);
        }
    }

    //
    // Important low-latency settings:
    //
    // -fflags nobuffer
    //     Don't intentionally buffer the V4L2 input.
    //
    // -flags low_delay
    //     Tell the codec pipeline to prefer low-delay operation.
    //
    // -tune zerolatency
    //     Disables x264 features such as B-frame reordering that add latency.
    //
    // -preset ultrafast
    //     Spend less CPU time encoding. This matters for camera latency.
    //
    // -g 6 / -keyint_min 6 / -sc_threshold 0
    //     Force an IDR frame every 6 frames. With 30 FPS this gives
    //     approximately 200 ms HLS segments.
    //
    // -bf 0
    //     No B frames => no frame reordering delay.
    //
    // -hls_time 0.2
    //     Target 200 ms HLS segments.
    //
    // -hls_list_size 3
    //     Keep only the latest 3 segments in the playlist.
    //
    // -hls_flags delete_segments+independent_segments+omit_endlist
    //     Keep the playlist live and remove old media files.
    //
    // -flush_packets 1
    //     Flush muxed packets as soon as possible.
    //
    // The bitrate here is intentionally moderate for 1080p30. Adjust it
    // according to the scene and network/client requirements.
    //
    const std::string cmd =
        "exec ffmpeg "
        "-hide_banner "
        "-loglevel info "
        "-f v4l2 "
        "-input_format mjpeg "
        "-video_size 1920x1080 "
        "-framerate 30 "
        "-fflags nobuffer "
        "-flags low_delay "
        "-i " + std::string(camera) + " "
        "-an "
        "-c:v libx264 "
        "-preset ultrafast "
        "-tune zerolatency "
        "-pix_fmt yuv420p "
        "-b:v 6M "
        "-maxrate 6M "
        "-bufsize 1M "
        "-g " + std::to_string(gop) + " "
        "-keyint_min " + std::to_string(gop) + " "
        "-sc_threshold 0 "
        "-bf 0 "
        "-flush_packets 1 "
        "-f hls "
        "-hls_time 0.2 "
        "-hls_list_size 3 "
        "-hls_flags delete_segments+independent_segments+omit_endlist "
        "-hls_segment_filename " + std::string(output_dir) + "/seg%05d.ts "
        "-y "
        + std::string(output_dir) + "/stream.m3u8";

    std::cout
        << "Starting low-latency camera stream\n"
        << "  Input : " << camera << '\n'
        << "  Output: " << output_dir << "/stream.m3u8\n"
        << "  HLS   : ~200 ms segments\n"
        << "  GOP   : " << gop << " frames\n\n";

    std::cout << cmd << "\n\n";

    const int result = std::system(cmd.c_str());

    if (result == -1) {
        std::cerr << "Failed to start ffmpeg.\n";
        return 1;
    }

    return WEXITSTATUS(result);
}
