#include "ambiance/app.hpp"
#include "ambiance/render.hpp"
#include <algorithm>
#include <array>
#include <cerrno>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <poll.h>
#include <spawn.h>
#include <sstream>
#include <stdexcept>
#include <sys/stat.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>
extern char **environ;
namespace ambiance {
volatile std::sig_atomic_t cancelled = 0;
namespace fs = std::filesystem;
namespace {
void checkCancel() {
  if (cancelled)
    throw std::runtime_error("Export cancelled; incomplete temporary files removed");
}
std::string content(const fs::path &p) {
  std::ifstream f(p);
  return {std::istreambuf_iterator<char>(f), {}};
}
std::string executable(const char *name, const char *env) {
  if (auto p = std::getenv(env)) {
    if (access(p, X_OK))
      throw std::runtime_error(std::string(env) + " is not executable");
    return fs::absolute(p).string();
  }
  std::istringstream paths(std::getenv("PATH") ? std::getenv("PATH") : "");
  std::string dir;
  while (std::getline(paths, dir, ':')) {
    auto p = fs::path(dir.empty() ? "." : dir) / name;
    if (!access(p.c_str(), X_OK))
      return fs::absolute(p).string();
  }
  for (auto dir : {"/opt/homebrew/bin", "/usr/local/bin"}) {
    auto p = fs::path(dir) / name;
    if (!access(p.c_str(), X_OK))
      return p.string();
  }
  throw std::runtime_error(std::string("Missing ") + name + ". Install with: brew install ffmpeg");
}
struct Process {
  pid_t pid = -1;
  int input = -1;
  fs::path log;
  Process(const std::vector<std::string> &args, const fs::path &logPath, bool pipeInput)
      : log(logPath) {
    int fds[2]{-1, -1};
    if (pipeInput && pipe(fds))
      throw std::runtime_error("Cannot create encoder pipe");
    int fd = open(log.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if (fd < 0) {
      if (pipeInput) {
        close(fds[0]);
        close(fds[1]);
      }
      throw std::runtime_error("Cannot create process log");
    }
    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);
    if (pipeInput) {
      posix_spawn_file_actions_adddup2(&actions, fds[0], STDIN_FILENO);
      posix_spawn_file_actions_addclose(&actions, fds[0]);
      posix_spawn_file_actions_addclose(&actions, fds[1]);
    } else
      posix_spawn_file_actions_addopen(&actions, STDIN_FILENO, "/dev/null", O_RDONLY, 0);
    posix_spawn_file_actions_adddup2(&actions, fd, STDOUT_FILENO);
    posix_spawn_file_actions_adddup2(&actions, fd, STDERR_FILENO);
    posix_spawn_file_actions_addclose(&actions, fd);
    std::vector<char *> argv;
    for (auto &a : args)
      argv.push_back(const_cast<char *>(a.c_str()));
    argv.push_back(nullptr);
    int rc = posix_spawn(&pid, args[0].c_str(), &actions, nullptr, argv.data(), environ);
    posix_spawn_file_actions_destroy(&actions);
    close(fd);
    if (pipeInput) {
      close(fds[0]);
      input = fds[1];
      fcntl(input, F_SETFL, O_NONBLOCK);
    }
    if (rc) {
      if (input >= 0)
        close(input);
      input = -1;
      pid = -1;
      throw std::runtime_error("Cannot launch encoder: " + std::string(std::strerror(rc)));
    }
  }
  ~Process() {
    if (input >= 0)
      close(input);
    if (pid > 0) {
      kill(pid, SIGKILL);
      while (waitpid(pid, nullptr, 0) < 0 && errno == EINTR) {
      }
    }
  }
  void write(const void *data, size_t size) {
    auto *p = static_cast<const char *>(data);
    auto last = std::chrono::steady_clock::now();
    while (size) {
      checkCancel();
      ssize_t n = ::write(input, p, size);
      if (n > 0) {
        p += n;
        size -= n;
        last = std::chrono::steady_clock::now();
      } else if (n < 0 && (errno == EAGAIN || errno == EINTR)) {
        pollfd f{input, POLLOUT, 0};
        poll(&f, 1, 100);
        if (std::chrono::steady_clock::now() - last > std::chrono::seconds(60))
          throw std::runtime_error("Encoder pipe stalled for 60s");
      } else
        throw std::runtime_error("Encoder pipe closed: " + content(log));
    }
  }
  std::string wait() {
    if (input >= 0) {
      close(input);
      input = -1;
    }
    auto start = std::chrono::steady_clock::now();
    for (;;) {
      checkCancel();
      int status;
      auto rc = waitpid(pid, &status, WNOHANG);
      if (rc == pid) {
        pid = -1;
        auto msg = content(log);
        if (!WIFEXITED(status) || WEXITSTATUS(status))
          throw std::runtime_error("Encoder/probe failed: " + msg);
        return msg;
      }
      if (rc < 0 && errno != EINTR)
        throw std::runtime_error("Cannot wait for encoder");
      if (std::chrono::steady_clock::now() - start > std::chrono::seconds(60))
        throw std::runtime_error("Encoder/probe stalled for 60s after input closed");
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
  }
};
void put(std::ofstream &f, uint32_t value, int bytes) {
  for (int i = 0; i < bytes; i++)
    f.put(char((value >> (8 * i)) & 255));
}
class Wav {
  std::ofstream f;

public:
  Digest hash;
  Wav(const fs::path &p, uint64_t frames) : f(p, std::ios::binary) {
    if (!f)
      throw std::runtime_error("Cannot create WAV");
    f.write("RIFF", 4);
    put(f, uint32_t(frames * 6 + 36), 4);
    f.write("WAVEfmt ", 8);
    put(f, 16, 4);
    put(f, 1, 2);
    put(f, 2, 2);
    put(f, 48000, 4);
    put(f, 288000, 4);
    put(f, 6, 2);
    put(f, 24, 2);
    f.write("data", 4);
    put(f, uint32_t(frames * 6), 4);
  }
  void append(float *b, size_t n, uint64_t offset, uint64_t total) {
    std::array<char, 2400> bytes{};
    for (size_t i = 0; i < n; i++) {
      double fade = std::min(1.0, double(total - offset - i - 1) / 240);
      for (int ch = 0; ch < 2; ch++) {
        int32_t v =
            int32_t(std::llround(std::clamp(double(b[2 * i + ch]) * fade, -1.0, 1.0) * 8388607));
        for (int k = 0; k < 3; k++)
          bytes[i * 6 + ch * 3 + k] = char((uint32_t(v) >> (8 * k)) & 255);
      }
    }
    f.write(bytes.data(), n * 6);
    hash.bytes(bytes.data(), n * 6);
    if (!f)
      throw std::runtime_error("WAV write failed (check free disk space)");
  }
  void finish() {
    f.flush();
    if (!f)
      throw std::runtime_error("Cannot flush WAV");
    f.close();
    if (!f)
      throw std::runtime_error("Cannot close WAV");
  }
};
struct Temp {
  fs::path path;
  explicit Temp(const fs::path &parent) {
    auto pattern = (parent / ".ambiance-XXXXXX").string();
    std::vector<char> b(pattern.begin(), pattern.end());
    b.push_back(0);
    auto *p = mkdtemp(b.data());
    if (!p)
      throw std::runtime_error("Cannot create temporary directory in output folder");
    path = p;
  }
  ~Temp() {
    std::error_code ec;
    fs::remove_all(path, ec);
  }
};
void destination(const fs::path &p, bool overwrite) {
  if (fs::exists(p) && (!overwrite || !fs::is_regular_file(p)))
    throw std::runtime_error("Output exists (use --overwrite for regular files): " + p.string());
}
void promote(const fs::path &a, const fs::path &b, bool overwrite) {
  if (overwrite) {
    if (rename(a.c_str(), b.c_str()))
      throw std::runtime_error("Cannot replace output: " + b.string());
  } else {
    if (link(a.c_str(), b.c_str()))
      throw std::runtime_error("Cannot publish output (destination appeared or is unwritable): " +
                               b.string());
    unlink(a.c_str());
  }
}
std::string json(const std::string &s) {
  std::string r = "\"";
  for (unsigned char c : s) {
    if (c == '"' || c == '\\') {
      r += '\\';
      r += char(c);
    } else if (c < 32) {
      const char *h = "0123456789abcdef";
      r += "\\u00";
      r += h[c >> 4];
      r += h[c & 15];
    } else
      r += char(c);
  }
  return r + '"';
}
} // namespace
uint64_t durationSamples(const std::string &s, int fps) {
  if (s.empty() || s.size() > 20)
    throw std::runtime_error("Invalid duration; use decimal seconds");
  uint64_t num = 0, den = 1;
  bool dot = false;
  int digits = 0;
  for (char c : s) {
    if (c == '.' && !dot) {
      dot = true;
      continue;
    }
    if (c < '0' || c > '9' || (dot && ++digits > 6))
      throw std::runtime_error("Duration requires a decimal with at most six fractional digits");
    num = num * 10 + uint64_t(c - '0');
    if (num > 3600ULL * 1000000)
      throw std::runtime_error("Duration exceeds one hour");
    if (dot)
      den *= 10;
  }
  if (num == 0 || num > 3600 * den || num * sampleRate % den || (fps && num * fps % den))
    throw std::runtime_error("Duration must give whole audio sample frames and whole video frames, "
                             "maximum 3600 seconds");
  return num * sampleRate / den;
}
void exportScene(const Options &o) {
  auto start = std::chrono::steady_clock::now();
  auto source = readScript(o.script);
  Session audio(source, o.script, o.seed);
  bool video = fs::path(o.output).extension() == ".mp4";
  if (!video && fs::path(o.output).extension() != ".wav")
    throw std::runtime_error("Output must end in .wav or .mp4");
  std::ostringstream defaultDuration;
  defaultDuration << std::fixed << std::setprecision(6) << audio.config().duration;
  auto seconds = o.duration.empty() ? defaultDuration.str() : o.duration;
  uint64_t samples = durationSamples(seconds, video ? o.fps : 0),
           frames = video ? samples * o.fps / sampleRate : 0;
  if (o.width < 64 || o.height < 64 || o.width > 4096 || o.height > 4096 || (o.width % 2) ||
      (o.height % 2))
    throw std::runtime_error("Output dimensions must be even integers from 64 through 4096");
  if (samples * 6 + 36 > UINT32_MAX)
    throw std::runtime_error("Classic WAV size limit exceeded");
  auto output = fs::absolute(o.output);
  auto parent = output.parent_path();
  if (!fs::is_directory(parent))
    throw std::runtime_error("Output directory does not exist: " + parent.string());
  auto kept = output;
  kept.replace_extension(".wav");
  auto manifest = fs::path(output.string() + ".json");
  destination(output, o.overwrite);
  destination(manifest, o.overwrite);
  if (video && o.keepWav)
    destination(kept, o.overwrite);
  if (fs::weakly_canonical(o.script) == fs::weakly_canonical(output) ||
      fs::weakly_canonical(o.script) == fs::weakly_canonical(manifest) ||
      (video && o.keepWav && fs::weakly_canonical(o.script) == fs::weakly_canonical(kept)))
    throw std::runtime_error("Output must not replace the scene script");
  uint64_t required = samples * 6 * (video && o.keepWav ? 2 : 1) + 16 * 1024 * 1024;
  if (fs::space(parent).available < required)
    throw std::runtime_error("Insufficient free space for source WAV and working output");
  Temp temp(parent);
  std::string ffmpeg, ffprobe, version;
  if (video) {
    ffmpeg = executable("ffmpeg", "AMBIANCE_FFMPEG");
    ffprobe = executable("ffprobe", "AMBIANCE_FFPROBE");
    Process caps({ffmpeg, "-hide_banner", "-encoders"}, temp.path / "caps.log", false);
    auto list = caps.wait();
    if (list.find("libx264") == std::string::npos || list.find(" aac ") == std::string::npos)
      throw std::runtime_error("FFmpeg needs libx264 and AAC encoders");
    Process v({ffmpeg, "-version"}, temp.path / "version.log", false);
    version = v.wait();
    version = version.substr(0, version.find('\n'));
  }
  std::cerr << "Audio pass: " << samples << " stereo sample frames\n";
  Wav wav(temp.path / "audio.wav", samples);
  std::array<float, 800> block;
  for (uint64_t at = 0; at < samples; at += 400) {
    checkCancel();
    audio.block(block.data());
    wav.append(block.data(), std::min<uint64_t>(400, samples - at), at, samples);
    if (at && at % (48000 * 30) == 0)
      std::cerr << "  audio " << at / 48000 << "s\n";
  }
  wav.finish();
  auto eventDigest = audio.digest().hex();
  Digest frameDigest;
  if (video) {
    Session visual(source, o.script, o.seed);
    Canvas canvas(o.width, o.height);
    auto encoded = temp.path / "video.mp4";
    std::vector<std::string> args{ffmpeg,
                                  "-nostdin",
                                  "-hide_banner",
                                  "-loglevel",
                                  "error",
                                  "-y",
                                  "-f",
                                  "rawvideo",
                                  "-pixel_format",
                                  "rgba",
                                  "-video_size",
                                  std::to_string(o.width) + "x" + std::to_string(o.height),
                                  "-framerate",
                                  std::to_string(o.fps),
                                  "-i",
                                  "pipe:0",
                                  "-i",
                                  (temp.path / "audio.wav").string(),
                                  "-map",
                                  "0:v:0",
                                  "-map",
                                  "1:a:0",
                                  "-c:v",
                                  "libx264",
                                  "-preset",
                                  "veryfast",
                                  "-crf",
                                  "18",
                                  "-pix_fmt",
                                  "yuv420p",
                                  "-colorspace",
                                  "bt709",
                                  "-color_primaries",
                                  "bt709",
                                  "-color_trc",
                                  "bt709",
                                  "-c:a",
                                  "aac",
                                  "-b:a",
                                  "192k",
                                  "-ar",
                                  "48000",
                                  "-t",
                                  seconds,
                                  "-movflags",
                                  "+faststart",
                                  encoded.string()};
    Process encoder(args, temp.path / "encoder.log", true);
    std::cerr << "Video pass: " << frames << " frames\n";
    for (uint64_t i = 0; i < frames; i++) {
      checkCancel();
      uint64_t sample = i * sampleRate / o.fps;
      visual.advanceTo(sample + 1);
      canvas.draw(visual.snapshot(), sample);
      if (canvas.pitch() == o.width * 4) {
        frameDigest.bytes(canvas.pixels(), size_t(o.width) * o.height * 4);
        encoder.write(canvas.pixels(), size_t(o.width) * o.height * 4);
      } else
        for (int y = 0; y < o.height; y++) {
          auto *p = static_cast<const char *>(canvas.pixels()) + y * canvas.pitch();
          frameDigest.bytes(p, o.width * 4);
          encoder.write(p, o.width * 4);
        }
      if (i && i % uint64_t(o.fps * 10) == 0)
        std::cerr << "  video " << i / o.fps << "s / " << seconds << "s\n";
    }
    visual.advanceTo(samples);
    if (visual.digest().hex() != eventDigest)
      throw std::runtime_error("Determinism check failed between audio/video passes");
    encoder.wait();
    Process probe({ffprobe, "-v", "error", "-count_frames", "-show_entries",
                   "stream=codec_name,codec_type,width,height,sample_rate,r_frame_rate,nb_read_frames,duration:"
                   "format=duration",
                   "-of", "flat", encoded.string()},
                  temp.path / "probe.log", false);
    auto report = probe.wait();
    std::ofstream(temp.path / "probe.txt") << report;
    auto has = [&](const std::string &s) { return report.find(s) != std::string::npos; };
    auto numericField = [&](const std::string &key) {
      auto pos = report.find(key + "=\"");
      if (pos == std::string::npos)
        throw std::runtime_error("Missing ffprobe field: " + key);
      auto value = std::stod(report.substr(pos + key.size() + 2));
      return value;
    };
    double expectedDuration = double(samples) / sampleRate;
    if (std::abs(numericField("streams.stream.0.duration") - expectedDuration) > .00001 ||
        std::abs(numericField("streams.stream.1.duration") - expectedDuration) > .0001)
      throw std::runtime_error("Encoded stream duration verification failed: " + report);

    if (!has("codec_name=\"h264\"") || !has("codec_name=\"aac\"") ||
        !has("width=" + std::to_string(o.width) + "\n") ||
        !has("height=" + std::to_string(o.height) + "\n") || !has("sample_rate=\"48000\"") ||
        !has("streams.stream.0.r_frame_rate=\"" + std::to_string(o.fps) + "/1\"") ||
        !has("streams.stream.0.nb_read_frames=\"" + std::to_string(frames) + "\""))
      throw std::runtime_error("Encoded output verification failed: " + report);
  }
  Digest scriptHash;
  scriptHash.bytes(source.data(), source.size());
  auto mpath = temp.path / "manifest.json";
  std::ofstream m(mpath);
  m << "{\n  \"engine\": \"Ambiance Generator 1.0.0\",\n  \"compiler\": " << json(__VERSION__)
    << ",\n  \"build_type\": " << json(AMBIANCE_BUILD_TYPE)
    << ",\n  \"build_source_sha256\": " << json(AMBIANCE_BUILD_ID)
    << ",\n  \"platform\": \"macOS arm64\",\n  \"dependencies\": \"SDL 3.2.10; Box2D 3.1.1; Lua "
       "5.4.8; miniaudio 0.11.22\",\n  \"script\": "
    << json(fs::absolute(o.script).string())
    << ",\n  \"script_fnv1a64\": " << json(scriptHash.hex())
    << ",\n  \"seed\": " << audio.config().seed
    << ",\n  \"tick_rate\": 120,\n  \"sample_rate\": 48000,\n  \"sample_frames\": " << samples
    << ",\n  \"video_frames\": " << frames << ",\n  \"fps\": " << (video ? o.fps : 0)
    << ",\n  \"width\": " << o.width << ",\n  \"height\": " << o.height
    << ",\n  \"event_digest\": " << json(eventDigest)
    << ",\n  \"pcm24_digest\": " << json(wav.hash.hex())
    << ",\n  \"rgba_digest\": " << json(frameDigest.hex()) << ",\n  \"ffmpeg\": " << json(version)
    << ",\n  \"aac_note\": \"AAC uses priming/padding; source WAV length is exact.\",\n  "
       "\"elapsed_seconds\": "
    << std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count() << "\n}\n";
  m.close();
  if (!m)
    throw std::runtime_error("Cannot write render manifest");
  checkCancel();
  // Every artifact is complete before any requested destination is promoted.
  if (video && o.keepWav)
    promote(temp.path / "audio.wav", kept, o.overwrite);
  promote(mpath, manifest, o.overwrite);
  promote(temp.path / (video ? "video.mp4" : "audio.wav"), output, o.overwrite);
  std::cerr << "Saved " << output << "; events " << eventDigest << "; PCM " << wav.hash.hex()
            << "\n";
}
} // namespace ambiance
