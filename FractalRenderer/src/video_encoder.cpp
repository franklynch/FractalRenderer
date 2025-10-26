#include "video_encoder.h"
#include <fmt/core.h>
#include <filesystem>
#include <sstream>
#include <fstream>
#include <regex>
#include <iostream>

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#include <sys/wait.h>
#endif

VideoEncoder::VideoEncoder() {
}

VideoEncoder::~VideoEncoder() {
    cancel();
}

bool VideoEncoder::is_ffmpeg_available() {
#ifdef _WIN32
    // Try to execute ffmpeg -version
    FILE* pipe = _popen("ffmpeg -version 2>&1", "r");
    if (!pipe) return false;

    char buffer[128];
    bool found = false;
    while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
        std::string line(buffer);
        if (line.find("ffmpeg version") != std::string::npos) {
            found = true;
            break;
        }
    }
    _pclose(pipe);
    return found;
#else
    FILE* pipe = popen("ffmpeg -version 2>&1", "r");
    if (!pipe) return false;

    char buffer[128];
    bool found = false;
    while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
        std::string line(buffer);
        if (line.find("ffmpeg version") != std::string::npos) {
            found = true;
            break;
        }
    }
    pclose(pipe);
    return found;
#endif
}

std::string VideoEncoder::get_ffmpeg_version() {
#ifdef _WIN32
    FILE* pipe = _popen("ffmpeg -version 2>&1", "r");
#else
    FILE* pipe = popen("ffmpeg -version 2>&1", "r");
#endif

    if (!pipe) return "FFmpeg not found";

    char buffer[256];
    std::string result;
    if (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
        result = buffer;
    }

#ifdef _WIN32
    _pclose(pipe);
#else
    pclose(pipe);
#endif

    return result;
}

std::string VideoEncoder::get_codec_params(const VideoEncodeSettings& settings) const {
    std::ostringstream params;

    switch (settings.codec) {
    case VideoCodec::H264:
        params << "-c:v libx264";

        // Preset based on quality
        switch (settings.quality) {
        case VideoQuality::Draft:
            params << " -preset veryfast -crf 28";
            break;
        case VideoQuality::Good:
            params << " -preset medium -crf 23";
            break;
        case VideoQuality::High:
            params << " -preset slow -crf " << settings.crf;
            break;
        case VideoQuality::Lossless:
            params << " -preset medium -crf 0";
            break;
        }

        params << " -pix_fmt yuv420p";  // Compatible with most players
        break;

    case VideoCodec::H265:
        params << "-c:v libx265";

        switch (settings.quality) {
        case VideoQuality::Draft:
            params << " -preset veryfast -crf 28";
            break;
        case VideoQuality::Good:
            params << " -preset medium -crf 28";
            break;
        case VideoQuality::High:
            params << " -preset slow -crf " << settings.crf;
            break;
        case VideoQuality::Lossless:
            params << " -preset medium -x265-params lossless=1";
            break;
        }

        params << " -pix_fmt yuv420p";
        break;

    case VideoCodec::VP9:
        params << "-c:v libvpx-vp9";

        switch (settings.quality) {
        case VideoQuality::Draft:
            params << " -crf 40 -b:v 0";
            break;
        case VideoQuality::Good:
            params << " -crf 31 -b:v 0";
            break;
        case VideoQuality::High:
            params << " -crf 15 -b:v 0";
            break;
        case VideoQuality::Lossless:
            params << " -lossless 1";
            break;
        }

        params << " -row-mt 1";  // Multi-threading
        break;

    case VideoCodec::ProRes:
        // ProRes profiles: 0=Proxy, 1=LT, 2=Standard, 3=HQ
        params << "-c:v prores_ks";

        switch (settings.quality) {
        case VideoQuality::Draft:
            params << " -profile:v 0";  // Proxy
            break;
        case VideoQuality::Good:
            params << " -profile:v 2";  // Standard
            break;
        case VideoQuality::High:
            params << " -profile:v 3";  // HQ
            break;
        case VideoQuality::Lossless:
            params << " -profile:v 3 -qscale:v 0";
            break;
        }

        params << " -pix_fmt yuv422p10le";
        break;

    case VideoCodec::AV1:
        params << "-c:v libaom-av1";

        switch (settings.quality) {
        case VideoQuality::Draft:
            params << " -crf 40 -b:v 0 -cpu-used 8";
            break;
        case VideoQuality::Good:
            params << " -crf 30 -b:v 0 -cpu-used 4";
            break;
        case VideoQuality::High:
            params << " -crf 20 -b:v 0 -cpu-used 2";
            break;
        case VideoQuality::Lossless:
            params << " -crf 0 -b:v 0";
            break;
        }
        break;
    }

    return params.str();
}

std::string VideoEncoder::build_ffmpeg_command(
    const std::string& frames_folder,
    const VideoEncodeSettings& settings
) const {
    std::ostringstream cmd;

    // Input frames
    cmd << "ffmpeg -y";  // Overwrite output
    cmd << " -framerate " << settings.fps;
    cmd << " -i \"" << frames_folder << "/frame_%06d.png\"";

    // Audio (if provided)
    if (!settings.audio_file.empty() && std::filesystem::exists(settings.audio_file)) {
        cmd << " -i \"" << settings.audio_file << "\"";
        cmd << " -c:a aac -b:a 192k";
        cmd << " -shortest";  // End when shortest stream ends
    }

    // Video codec and quality
    cmd << " " << get_codec_params(settings);

    // Output
    cmd << " \"" << settings.output_filename << "\"";

    // Progress output
    cmd << " -progress pipe:1";
    cmd << " -loglevel warning";

    return cmd.str();
}

bool VideoEncoder::encode(
    const std::string& frames_folder,
    const VideoEncodeSettings& settings
) {
    // Check if FFmpeg is available
    if (!is_ffmpeg_available()) {
        progress.error = true;
        progress.status = "FFmpeg not found! Please install FFmpeg.";
        if (on_error) {
            on_error("FFmpeg not found. Please install FFmpeg and add it to PATH.");
        }
        return false;
    }

    // Check if frames folder exists
    if (!std::filesystem::exists(frames_folder)) {
        progress.error = true;
        progress.status = "Frames folder not found!";
        if (on_error) {
            on_error("Frames folder not found: " + frames_folder);
        }
        return false;
    }

    // Count frames
    int frame_count = 0;
    for (const auto& entry : std::filesystem::directory_iterator(frames_folder)) {
        if (entry.path().extension() == ".png") {
            frame_count++;
        }
    }

    if (frame_count == 0) {
        progress.error = true;
        progress.status = "No frames found!";
        if (on_error) {
            on_error("No PNG frames found in: " + frames_folder);
        }
        return false;
    }

    // Reset progress
    progress = EncodeProgress();
    progress.is_encoding = true;
    progress.total_frames = frame_count;
    progress.status = "Starting FFmpeg...";
    cancel_requested = false;

    // Build command
    std::string command = build_ffmpeg_command(frames_folder, settings);

    fmt::print("\n=== VIDEO ENCODING STARTED ===\n");
    fmt::print("Codec: ");
    switch (settings.codec) {
    case VideoCodec::H264: fmt::print("H.264 (libx264)\n"); break;
    case VideoCodec::H265: fmt::print("H.265 (libx265)\n"); break;
    case VideoCodec::VP9: fmt::print("VP9 (libvpx-vp9)\n"); break;
    case VideoCodec::ProRes: fmt::print("ProRes\n"); break;
    case VideoCodec::AV1: fmt::print("AV1 (libaom-av1)\n"); break;
    }
    fmt::print("Quality: ");
    switch (settings.quality) {
    case VideoQuality::Draft: fmt::print("Draft\n"); break;
    case VideoQuality::Good: fmt::print("Good\n"); break;
    case VideoQuality::High: fmt::print("High\n"); break;
    case VideoQuality::Lossless: fmt::print("Lossless\n"); break;
    }
    fmt::print("FPS: {}\n", settings.fps);
    fmt::print("Frames: {}\n", frame_count);
    fmt::print("Output: {}\n", settings.output_filename);
    fmt::print("Command: {}\n\n", command);

    // Execute FFmpeg
    bool success = execute_ffmpeg(command, frame_count);

    if (success && !cancel_requested) {
        progress.completed = true;
        progress.is_encoding = false;
        progress.status = "Encoding complete!";
        progress.progress = 1.0f;

        // Get file size
        if (std::filesystem::exists(settings.output_filename)) {
            auto file_size = std::filesystem::file_size(settings.output_filename);
            float size_mb = file_size / (1024.0f * 1024.0f);

            fmt::print("\n=== ENCODING COMPLETE ===\n");
            fmt::print("Output: {}\n", settings.output_filename);
            fmt::print("File size: {:.2f} MB\n", size_mb);
            fmt::print("Duration: {:.2f} seconds\n", frame_count / (float)settings.fps);
            fmt::print("=========================\n\n");

            if (on_complete) {
                on_complete(settings.output_filename);
            }

            // Clean up frames if requested
            if (settings.delete_frames_after) {
                fmt::print("Cleaning up frames...\n");
                try {
                    std::filesystem::remove_all(frames_folder);
                    fmt::print("Frames deleted: {}\n", frames_folder);
                }
                catch (const std::exception& e) {
                    fmt::print("Warning: Could not delete frames: {}\n", e.what());
                }
            }
        }
    }
    else if (cancel_requested) {
        progress.status = "Encoding cancelled";
        fmt::print("Encoding cancelled by user\n");
    }
    else {
        progress.error = true;
        progress.status = "Encoding failed";
        fmt::print("Encoding failed!\n");

        if (on_error) {
            on_error("FFmpeg encoding failed");
        }
    }

    progress.is_encoding = false;
    return success;
}

void VideoEncoder::cancel() {
    if (progress.is_encoding) {
        cancel_requested = true;
        fmt::print("Cancelling encoding...\n");
    }
}

bool VideoEncoder::execute_ffmpeg(const std::string& command, int total_frames) {
#ifdef _WIN32
    // Windows implementation with progress tracking
    HANDLE hReadPipe, hWritePipe;
    SECURITY_ATTRIBUTES sa = { sizeof(SECURITY_ATTRIBUTES), NULL, TRUE };

    if (!CreatePipe(&hReadPipe, &hWritePipe, &sa, 0)) {
        return false;
    }

    STARTUPINFOA si = { sizeof(STARTUPINFOA) };
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdOutput = hWritePipe;
    si.hStdError = hWritePipe;
    si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);

    PROCESS_INFORMATION pi = {};

    if (!CreateProcessA(NULL, const_cast<char*>(command.c_str()),
        NULL, NULL, TRUE, CREATE_NO_WINDOW,
        NULL, NULL, &si, &pi)) {
        CloseHandle(hReadPipe);
        CloseHandle(hWritePipe);
        return false;
    }

    CloseHandle(hWritePipe);

    // Read output
    char buffer[4096];
    DWORD bytesRead;
    std::string output_buffer;

    auto start_time = std::chrono::high_resolution_clock::now();

    while (ReadFile(hReadPipe, buffer, sizeof(buffer) - 1, &bytesRead, NULL) && bytesRead > 0) {
        buffer[bytesRead] = '\0';
        output_buffer += buffer;

        // Process complete lines
        size_t pos;
        while ((pos = output_buffer.find('\n')) != std::string::npos) {
            std::string line = output_buffer.substr(0, pos);
            output_buffer = output_buffer.substr(pos + 1);

            parse_ffmpeg_output(line);

            // Calculate encoding speed
            auto current_time = std::chrono::high_resolution_clock::now();
            float elapsed = std::chrono::duration<float>(current_time - start_time).count();
            if (elapsed > 0) {
                progress.fps_encoding = progress.frames_encoded / elapsed;
            }

            if (on_progress) {
                on_progress(progress.frames_encoded, total_frames);
            }
        }

        if (cancel_requested) {
            TerminateProcess(pi.hProcess, 1);
            break;
        }
    }

    WaitForSingleObject(pi.hProcess, INFINITE);

    DWORD exitCode;
    GetExitCodeProcess(pi.hProcess, &exitCode);

    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    CloseHandle(hReadPipe);

    return (exitCode == 0 && !cancel_requested);

#else
    // Linux/Mac implementation
    FILE* pipe = popen((command + " 2>&1").c_str(), "r");
    if (!pipe) return false;

    char buffer[256];
    auto start_time = std::chrono::high_resolution_clock::now();

    while (fgets(buffer, sizeof(buffer), pipe) != nullptr && !cancel_requested) {
        std::string line(buffer);
        parse_ffmpeg_output(line);

        auto current_time = std::chrono::high_resolution_clock::now();
        float elapsed = std::chrono::duration<float>(current_time - start_time).count();
        if (elapsed > 0) {
            progress.fps_encoding = progress.frames_encoded / elapsed;
        }

        if (on_progress) {
            on_progress(progress.frames_encoded, total_frames);
        }
    }

    int status = pclose(pipe);
    return (WEXITSTATUS(status) == 0 && !cancel_requested);
#endif
}

void VideoEncoder::parse_ffmpeg_output(const std::string& line) {
    // Parse FFmpeg progress output
    // Format: frame= 123 fps= 45 ...

    std::regex frame_regex("frame=\\s*(\\d+)");
    std::regex fps_regex("fps=\\s*([\\d.]+)");

    std::smatch match;

    if (std::regex_search(line, match, frame_regex)) {
        progress.frames_encoded = std::stoi(match[1].str());
        if (progress.total_frames > 0) {
            progress.progress = static_cast<float>(progress.frames_encoded) / progress.total_frames;
        }

        progress.status = fmt::format("Encoding frame {}/{}...",
            progress.frames_encoded, progress.total_frames);

        fmt::print("\rEncoding: {}/{} frames ({:.1f}%) @ {:.1f} fps",
            progress.frames_encoded, progress.total_frames,
            progress.progress * 100.0f, progress.fps_encoding);
        std::cout.flush();
    }
}