// Live log and screenshots over the network when the NRO was sent with nxlink (tools/nxlink.sh): libnx sets
// __nxlink_host from the launch arguments, stdout/stderr then go to the `nxlink -s` server on the PC, and the log
// gets a stdout sink next to its file one. Started normally from hbmenu, nothing changes (no sockets are opened).
//
// Screenshots (caps:sc, JPEG of the whole screen with the overlays) go down the same stream as base64 lines,
// "@@MGRR_SHOT <name> <part> <parts> <crc32 of the part's base64> <base64>", which tools/nxlink_recv.py puts back
// together next to the log. Every stdout write, log lines included (LockedStdoutSink), takes g_stdout_lock: the
// FILE lock of newlib did not keep a line whole (on console a log line landed inside a screenshot line now and then).
// The parts and their crc still name any part that goes wrong.
// Taken on L+R+Minus and every --mgrr_nxlink_shot_s seconds.

#include "mgrr_nxlink.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include <netinet/in.h>

#include <spdlog/pattern_formatter.h>
#include <spdlog/sinks/sink.h>

#include <rex/cvar.h>
#include <rex/logging.h>

#include <switch.h>

REXCVAR_DEFINE_INT32(mgrr_nxlink_shot_s, 30, "MGRR",
                     "With nxlink: send a screenshot every N seconds (0: only on L+R+Minus)");

namespace mgrr {
namespace {

std::mutex g_stdout_lock;

void WriteStdout(const char* data, size_t size, bool& ok) {
  ok = std::fwrite(data, 1, size, stdout) == size && std::fflush(stdout) == 0;
  if (!ok) std::clearerr(stdout);
}

class LockedStdoutSink final : public spdlog::sinks::sink {
 public:
  LockedStdoutSink() : formatter_(std::make_unique<spdlog::pattern_formatter>()) {}
  void log(const spdlog::details::log_msg& msg) override {
    std::lock_guard<std::mutex> guard(g_stdout_lock);
    spdlog::memory_buf_t text;
    formatter_->format(msg, text);
    bool ok;
    WriteStdout(text.data(), text.size(), ok);
  }
  void flush() override {
    std::lock_guard<std::mutex> guard(g_stdout_lock);
    std::fflush(stdout);
  }
  void set_pattern(const std::string& pattern) override {
    std::lock_guard<std::mutex> guard(g_stdout_lock);
    formatter_ = std::make_unique<spdlog::pattern_formatter>(pattern);
  }
  void set_formatter(std::unique_ptr<spdlog::formatter> formatter) override {
    std::lock_guard<std::mutex> guard(g_stdout_lock);
    formatter_ = std::move(formatter);
  }

 private:
  std::unique_ptr<spdlog::formatter> formatter_;
};

std::string Base64(const uint8_t* data, size_t size) {
  static constexpr char kDigits[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  std::string out;
  out.reserve((size + 2) / 3 * 4);
  size_t i = 0;
  for (; i + 2 < size; i += 3) {
    const uint32_t v = (data[i] << 16) | (data[i + 1] << 8) | data[i + 2];
    out += kDigits[v >> 18];
    out += kDigits[(v >> 12) & 63];
    out += kDigits[(v >> 6) & 63];
    out += kDigits[v & 63];
  }
  if (i < size) {
    const uint32_t v = (data[i] << 16) | (i + 1 < size ? data[i + 1] << 8 : 0);
    out += kDigits[v >> 18];
    out += kDigits[(v >> 12) & 63];
    out += i + 1 < size ? kDigits[(v >> 6) & 63] : '=';
    out += '=';
  }
  return out;
}

void SendScreenshot(std::vector<uint8_t>& jpeg, unsigned number, const char* why) {
  u64 size = 0;
  const Result rc = capsscCaptureJpegScreenShot(&size, jpeg.data(), jpeg.size(), ViLayerStack_Default, 1000000000);
  if (R_FAILED(rc) || size == 0 || size > jpeg.size()) {
    REXLOG_WARN("[nxlink] screenshot failed: {:08X}", rc);
    return;
  }
  char name[48];
  std::snprintf(name, sizeof(name), "shot_%03u_%s.jpg", number, why);
  const std::string b64 = Base64(jpeg.data(), size);
  constexpr size_t kPart = 2048;
  const size_t parts = (b64.size() + kPart - 1) / kPart;
  unsigned failed = 0;
  for (size_t i = 0; i < parts; ++i) {
    const std::string_view data = std::string_view(b64).substr(i * kPart, kPart);
    char head[96];
    const int n = std::snprintf(head, sizeof(head), "@@MGRR_SHOT %s %zu %zu %08X ", name, i, parts,
                                unsigned(crc32Calculate(data.data(), data.size())));
    std::string line(head, size_t(n));
    line.append(data);
    line += '\n';
    bool ok;
    {
      std::lock_guard<std::mutex> guard(g_stdout_lock);
      WriteStdout(line.data(), line.size(), ok);
    }
    if (!ok) ++failed;
  }
  REXLOG_INFO("[nxlink] screenshot {} ({} KB, {} parts, {} failed writes)", name, size / 1024, parts, failed);
}

void ScreenshotThread() {
  if (R_FAILED(capsscInitialize())) {
    REXLOG_WARN("[nxlink] caps:sc is not available, no screenshots");
    return;
  }
  std::vector<uint8_t> jpeg(0x80000);
  PadState pad{};
  padInitializeAny(&pad);
  const int period_s = REXCVAR_GET(mgrr_nxlink_shot_s);
  auto next = std::chrono::steady_clock::now() + std::chrono::seconds(period_s > 0 ? period_s : 1);
  unsigned number = 0;
  constexpr u64 kCombo = HidNpadButton_L | HidNpadButton_R | HidNpadButton_Minus;
  for (;;) {
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    padUpdate(&pad);
    const u64 held = padGetButtons(&pad);
    if ((held & kCombo) == kCombo && (padGetButtonsDown(&pad) & kCombo)) {
      SendScreenshot(jpeg, ++number, "button");
    } else if (period_s > 0 && std::chrono::steady_clock::now() >= next) {
      next = std::chrono::steady_clock::now() + std::chrono::seconds(period_s);
      SendScreenshot(jpeg, ++number, "auto");
    }
  }
}

}  // namespace

void StartNxlinkLog() {
  if (__nxlink_host.s_addr == 0) return;
  const Result rc = socketInitializeDefault();
  if (R_FAILED(rc)) {
    REXLOG_WARN("[nxlink] socketInitializeDefault failed: {:08X}", rc);
    return;
  }
  if (nxlinkStdio() < 0) {
    REXLOG_WARN("[nxlink] could not connect to the host (is `nxlink -s` running?)");
    return;
  }
  std::setvbuf(stdout, nullptr, _IOLBF, 0);
  auto sink = std::make_shared<LockedStdoutSink>();
  sink->set_pattern("[%H:%M:%S.%e] [%l] [%n] %v");
  rex::ReplaceConsoleSink(sink);
  REXLOG_INFO("[nxlink] log goes to the host too; screenshots on L+R+Minus and every {} s",
              REXCVAR_GET(mgrr_nxlink_shot_s));
  // HID is shared with the game's input driver: libnx counts hidInitialize, and each reader keeps its own PadState.
  hidInitialize();
  // Not detach(): with the SDK's thread wrappers it throws std::system_error (seen on console). The thread lives
  // until the process ends (windowed_app_main_switch.cpp), so the object is simply never destroyed.
  new std::thread(ScreenshotThread);
}

}  // namespace mgrr
