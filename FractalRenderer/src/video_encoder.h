#pragma once

#include <string>
#include <functional>
#include <atomic>

enum class VideoCodec {
    H264,      // Most compatible (libx264)
    H265,      // Better compression (libx265)
    VP9,       // Open source (libvpx-vp9)
    ProRes,    // Professional (prores_ks)
    AV1        // Future-proof (libaom-av1)
};

enum class VideoQuality {
    Draft,     // Fast encoding, lower quality
    Good,      // Balanced
    High,      // Slow encoding, high quality
    Lossless   // Huge files, perfect quality
};

struct VideoEncodeSettings {
    VideoCodec codec = VideoCodec::H264;
    VideoQuality quality = VideoQuality::High;
    int fps = 60;
    int crf = 18;  // Lower = better quality (0-51 for H.264/H.265)
    std::string audio_file;  // Optional background music
    bool delete_frames_after = true;  // Clean up PNGs after encoding

    std::string output_filename = "fractal_animation.mp4";
};

struct EncodeProgress {
    int frames_encoded = 0;
    int total_frames = 0;
    float progress = 0.0f;  // 0.0 to 1.0
    float fps_encoding = 0.0f;  // Encoding speed
    std::string status;
    bool is_encoding = false;
    bool completed = false;
    bool error = false;
};

class VideoEncoder {
public:
    VideoEncoder();
    ~VideoEncoder();

    // Start encoding frames to video
    bool encode(
        const std::string& frames_folder,
        const VideoEncodeSettings& settings
    );

    // Cancel ongoing encode
    void cancel();

    // Get current progress
    const EncodeProgress& get_progress() const { return progress; }

    // Check if FFmpeg is available
    static bool is_ffmpeg_available();

    // Get FFmpeg version string
    static std::string get_ffmpeg_version();

    // Callbacks
    std::function<void(const std::string&)> on_complete;
    std::function<void(const std::string&)> on_error;
    std::function<void(int, int)> on_progress;  // frames_done, total_frames

private:
    EncodeProgress progress;
    std::atomic<bool> cancel_requested{ false };

    // Build FFmpeg command line
    std::string build_ffmpeg_command(
        const std::string& frames_folder,
        const VideoEncodeSettings& settings
    ) const;

    // Execute FFmpeg and track progress
    bool execute_ffmpeg(
        const std::string& command,
        int total_frames
    );

    // Parse FFmpeg output for progress
    void parse_ffmpeg_output(const std::string& line);

    // Get codec parameters
    std::string get_codec_params(const VideoEncodeSettings& settings) const;
};
