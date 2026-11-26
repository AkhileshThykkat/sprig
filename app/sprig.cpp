// sprig: the terminal viewer, plus the small commands agents call.
//
//   sprig                 watch: draw the mascot, react to events (q or Ctrl-C quits)
//   sprig demo            watch, cycling through every mood
//   sprig emit <mood>     tell a running viewer the mood (any agent, any script)
//   sprig hook            same, from a Claude Code / Codex hook payload on stdin
//   sprig path            print the pipe the viewer listens on
//
// Events travel over a per-user named pipe. Senders never block and never
// fail: no viewer running is the normal case, not an error.
#include <fcntl.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <termios.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "../core/sprig.hpp"
#include "events.hpp"

namespace {

using namespace std::chrono_literals;

// Owns one file descriptor and closes it exactly once.
class Fd {
 public:
  Fd() = default;
  explicit Fd(int fd) noexcept : fd_(fd) {}
  Fd(Fd&& other) noexcept : fd_(std::exchange(other.fd_, -1)) {}
  Fd& operator=(Fd&& other) noexcept {
    if (this != &other) {
      reset();
      fd_ = std::exchange(other.fd_, -1);
    }
    return *this;
  }
  Fd(const Fd&) = delete;
  Fd& operator=(const Fd&) = delete;
  ~Fd() { reset(); }

  [[nodiscard]] int get() const noexcept { return fd_; }
  explicit operator bool() const noexcept { return fd_ >= 0; }
  void reset() noexcept {
    if (fd_ >= 0) ::close(fd_);
    fd_ = -1;
  }

 private:
  int fd_ = -1;
};

bool write_all(int fd, std::string_view data) {
  while (!data.empty()) {
    const ssize_t n = ::write(fd, data.data(), data.size());
    if (n < 0) {
      if (errno == EINTR) continue;
      return false;
    }
    data.remove_prefix(static_cast<std::size_t>(n));
  }
  return true;
}

bool owned_fifo(int fd) {
  struct stat st {};
  return ::fstat(fd, &st) == 0 && S_ISFIFO(st.st_mode) && st.st_uid == ::getuid();
}

// $SPRIG_PIPE, else $XDG_RUNTIME_DIR (private to the user by spec), else a
// private directory in /tmp that we create and verify.
std::optional<std::string> pipe_path() {
  if (const char* p = std::getenv("SPRIG_PIPE"); p && *p) return p;
  if (const char* d = std::getenv("XDG_RUNTIME_DIR"); d && *d) return std::string(d) + "/sprig.pipe";
  const std::string dir = "/tmp/sprig-" + std::to_string(::getuid());
  if (::mkdir(dir.c_str(), 0700) != 0 && errno != EEXIST) return std::nullopt;
  struct stat st {};
  if (::lstat(dir.c_str(), &st) != 0 || !S_ISDIR(st.st_mode) || st.st_uid != ::getuid() ||
      (st.st_mode & 077) != 0)
    return std::nullopt;
  return dir + "/sprig.pipe";
}

// Hands a mood to the viewer if one is listening; otherwise does nothing.
void send(sprig::Mood mood) {
  const auto path = pipe_path();
  if (!path) return;
  const Fd fd{::open(path->c_str(), O_WRONLY | O_NONBLOCK | O_CLOEXEC | O_NOFOLLOW)};
  if (!fd || !owned_fifo(fd.get())) return;  // ENOENT / ENXIO: nobody is watching
  const std::string line = std::string(sprig::name(mood)) + '\n';  // < PIPE_BUF: one atomic write
  (void)write_all(fd.get(), line);
}

std::string read_stdin(std::size_t limit) {
  std::string data;
  std::array<char, 4096> buf{};
  while (data.size() < limit) {
    const ssize_t n = ::read(STDIN_FILENO, buf.data(), buf.size());
    if (n < 0 && errno == EINTR) continue;
    if (n <= 0) break;
    data.append(buf.data(), static_cast<std::size_t>(n));
  }
  return data;
}

// A grid of braille cells. Each cell holds 2 x 4 dots, which come out close
// to square in a normal terminal font.
class BrailleCanvas {
 public:
  void resize(int cols, int rows) {
    cols_ = std::max(cols, 1);
    rows_ = std::max(rows, 1);
    cells_.assign(static_cast<std::size_t>(cols_) * static_cast<std::size_t>(rows_), 0);
  }
  void clear() { std::ranges::fill(cells_, std::uint8_t{0}); }
  [[nodiscard]] int width() const { return cols_ * 2; }
  [[nodiscard]] int height() const { return rows_ * 4; }

  void dot(int x, int y) {
    if (x < 0 || y < 0 || x >= width() || y >= height()) return;
    static constexpr std::uint8_t kBits[2][4] = {{0x01, 0x02, 0x04, 0x40}, {0x08, 0x10, 0x20, 0x80}};
    cells_[static_cast<std::size_t>(y / 4) * static_cast<std::size_t>(cols_) + static_cast<std::size_t>(x / 2)] |=
        kBits[x % 2][y % 4];
  }

  void line(int x0, int y0, int x1, int y1) {  // Bresenham
    const int dx = std::abs(x1 - x0), dy = -std::abs(y1 - y0);
    const int sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;
    for (;;) {
      dot(x0, y0);
      if (x0 == x1 && y0 == y1) return;
      const int e2 = 2 * err;
      if (e2 >= dy) err += dy, x0 += sx;
      if (e2 <= dx) err += dx, y0 += sy;
    }
  }

  void append_to(std::string& out) const {
    for (int r = 0; r < rows_; ++r) {
      for (int c = 0; c < cols_; ++c) {
        const std::uint8_t bits = cells_[static_cast<std::size_t>(r) * static_cast<std::size_t>(cols_) + static_cast<std::size_t>(c)];
        if (bits == 0) {
          out += ' ';
          continue;
        }
        const unsigned cp = 0x2800u + bits;  // U+2800 braille block, as UTF-8
        out += static_cast<char>(0xE0 | (cp >> 12));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (cp & 0x3F));
      }
      out += "\r\n";
    }
  }

 private:
  int cols_ = 1, rows_ = 1;
  std::vector<std::uint8_t> cells_ = std::vector<std::uint8_t>(1);
};

void draw(const sprig::Frame& f, BrailleCanvas& canvas) {
  canvas.clear();
  const float w = static_cast<float>(canvas.width()), h = static_cast<float>(canvas.height());
  const float scale = std::min(w, h) / sprig::kViewBox;
  const float ox = (w - sprig::kViewBox * scale) / 2, oy = (h - sprig::kViewBox * scale) / 2;
  const auto at = [&](int i) {
    return std::pair{static_cast<int>(ox + f.xy[2 * i] * scale + 0.5f),
                     static_cast<int>(oy + f.xy[2 * i + 1] * scale + 0.5f)};
  };
  for (int s = 0; s < f.strokes; ++s) {
    const int first = f.start[s], n = f.count[s];
    for (int k = 1; k < n; ++k) {
      const auto [x0, y0] = at(first + k - 1);
      const auto [x1, y1] = at(first + k);
      canvas.line(x0, y0, x1, y1);
    }
    if (f.closed[s] && n > 2) {
      const auto [x0, y0] = at(first + n - 1);
      const auto [x1, y1] = at(first);
      canvas.line(x0, y0, x1, y1);
    }
  }
}

volatile std::sig_atomic_t g_stop = 0;
volatile std::sig_atomic_t g_resized = 1;

void on_stop(int) { g_stop = 1; }
void on_resize(int) { g_resized = 1; }

void install_signals() {
  struct sigaction sa {};
  sigemptyset(&sa.sa_mask);
  sa.sa_handler = on_stop;  // no SA_RESTART: poll() wakes up on the signal
  for (const int sig : {SIGINT, SIGTERM, SIGHUP}) ::sigaction(sig, &sa, nullptr);
  sa.sa_handler = on_resize;
  ::sigaction(SIGWINCH, &sa, nullptr);
}

// Switches the terminal into a drawing state and always switches it back.
class TerminalSession {
 public:
  TerminalSession() {
    if (::tcgetattr(STDIN_FILENO, &saved_) == 0) {
      termios quiet = saved_;
      quiet.c_lflag &= ~static_cast<tcflag_t>(ECHO | ICANON);  // keys arrive at once, unechoed
      restore_ = ::tcsetattr(STDIN_FILENO, TCSANOW, &quiet) == 0;
    }
    write_all(STDOUT_FILENO, "\x1b[?1049h\x1b[?25l\x1b[2J");  // alt screen, hide cursor
  }
  ~TerminalSession() {
    write_all(STDOUT_FILENO, "\x1b[0m\x1b[?25h\x1b[?1049l");
    if (restore_) ::tcsetattr(STDIN_FILENO, TCSANOW, &saved_);
  }
  TerminalSession(const TerminalSession&) = delete;
  TerminalSession& operator=(const TerminalSession&) = delete;

 private:
  termios saved_{};
  bool restore_ = false;
};

// Opens (creating if needed) the pipe for reading. Also keeps a write end of
// our own, so the pipe never reports hang-up between senders.
bool open_pipe(Fd& reader, Fd& keepalive) {
  const auto path = pipe_path();
  if (!path) {
    std::fputs("sprig: no private place for the pipe; set SPRIG_PIPE\n", stderr);
    return false;
  }
  if (::mkfifo(path->c_str(), 0600) != 0 && errno != EEXIST) {
    std::fprintf(stderr, "sprig: cannot create %s: %s\n", path->c_str(), std::strerror(errno));
    return false;
  }
  reader = Fd{::open(path->c_str(), O_RDONLY | O_NONBLOCK | O_CLOEXEC | O_NOFOLLOW)};
  if (!reader || !owned_fifo(reader.get())) {
    std::fprintf(stderr, "sprig: %s is not a pipe we own\n", path->c_str());
    return false;
  }
  keepalive = Fd{::open(path->c_str(), O_WRONLY | O_NONBLOCK | O_CLOEXEC | O_NOFOLLOW)};
  return true;
}

// Reads whatever senders wrote and applies each complete line.
void drain(int fd, std::string& inbox, sprig::Engine& engine) {
  std::array<char, 512> buf{};
  for (;;) {
    const ssize_t n = ::read(fd, buf.data(), buf.size());
    if (n < 0 && errno == EINTR) continue;
    if (n <= 0) break;
    inbox.append(buf.data(), static_cast<std::size_t>(n));
  }
  std::size_t nl;
  while ((nl = inbox.find('\n')) != std::string::npos) {
    std::string_view line{inbox.data(), nl};
    while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.remove_suffix(1);
    if (const auto mood = sprig::parse_mood(line)) engine.set_mood(*mood);
    inbox.erase(0, nl + 1);
  }
  if (inbox.size() > 4096) inbox.clear();  // junk with no newline: drop it
}

int watch(bool demo) {
  if (!::isatty(STDOUT_FILENO)) {
    std::fputs("sprig: run the viewer in a terminal\n", stderr);
    return 1;
  }
  Fd pipe, keepalive;
  if (!demo && !open_pipe(pipe, keepalive)) return 1;
  install_signals();
  const TerminalSession session;

  sprig::Engine engine{static_cast<sprig::u32>(::getpid())};
  BrailleCanvas canvas;
  std::string out, shown, inbox;
  bool read_keys = ::isatty(STDIN_FILENO);
  int demo_mood = 0;

  using Clock = std::chrono::steady_clock;
  constexpr auto kFrame = 33ms;  // ~30 fps
  auto last = Clock::now();
  auto next_demo = last;

  while (!g_stop) {
    if (g_resized) {
      g_resized = 0;
      winsize ws{};
      const bool ok = ::ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_col > 0 && ws.ws_row > 1;
      canvas.resize(ok ? ws.ws_col : 80, ok ? ws.ws_row - 1 : 23);  // last row: status
      shown.clear();
      write_all(STDOUT_FILENO, "\x1b[2J");
    }

    std::array<pollfd, 2> fds{};
    nfds_t nfds = 0;
    if (pipe) fds[nfds++] = {pipe.get(), POLLIN, 0};
    const nfds_t key_slot = nfds;
    if (read_keys) fds[nfds++] = {STDIN_FILENO, POLLIN, 0};
    const auto wait = std::chrono::duration_cast<std::chrono::milliseconds>(last + kFrame - Clock::now());
    if (::poll(fds.data(), nfds, static_cast<int>(std::max<std::int64_t>(wait.count(), 0))) > 0) {
      if (pipe && (fds[0].revents & POLLIN)) drain(pipe.get(), inbox, engine);
      if (read_keys && fds[key_slot].revents) {
        char key = 0;
        const ssize_t n = ::read(STDIN_FILENO, &key, 1);
        if (n <= 0) read_keys = false;
        else if (key == 'q') break;
      }
    }

    const auto now = Clock::now();
    if (now - last < kFrame) continue;
    const float dt = std::chrono::duration<float>(now - last).count();
    last = now;
    if (demo && now >= next_demo) {
      engine.set_mood(static_cast<sprig::Mood>(demo_mood++ % sprig::kMoodCount));
      next_demo = now + 3s;
    }
    engine.step(dt);
    draw(engine.frame(), canvas);

    const sprig::u32 rgb = sprig::color(engine.mood());
    out.clear();
    out += "\x1b[H\x1b[38;2;" + std::to_string(rgb >> 16) + ';' + std::to_string((rgb >> 8) & 0xFF) +
           ';' + std::to_string(rgb & 0xFF) + 'm';
    canvas.append_to(out);
    out += "\x1b[0;2m sprig \xC2\xB7 ";
    out += sprig::name(engine.mood());
    out += "\x1b[K\x1b[0m";
    if (out != shown) {  // asleep and still: nothing to send
      write_all(STDOUT_FILENO, out);
      shown.swap(out);
    }
  }
  return 0;
}

void usage(std::FILE* to) {
  std::fputs(
      "usage: sprig [demo | emit <mood> | hook | path]\n"
      "  sprig              show the mascot and react to events (q quits)\n"
      "  sprig demo         show every mood in turn\n"
      "  sprig emit <mood>  set the mood of a running viewer\n"
      "  sprig hook         set it from a Claude Code / Codex hook payload on stdin\n"
      "  sprig path         print the pipe the viewer listens on\n"
      "moods:",
      to);
  for (const char* m : sprig::kMoodNames) std::fprintf(to, " %s", m);
  std::fputc('\n', to);
}

}  // namespace

int main(int argc, char** argv) {
  std::signal(SIGPIPE, SIG_IGN);  // a viewer quitting mid-write must not kill a sender
  const std::string_view cmd = argc > 1 ? argv[1] : "";

  if (cmd.empty()) return watch(false);
  if (cmd == "demo") return watch(true);
  if (cmd == "emit" && argc == 3) {
    const auto mood = sprig::parse_mood(argv[2]);
    if (!mood) {
      std::fprintf(stderr, "sprig: unknown mood '%s'\n", argv[2]);
      usage(stderr);
      return 2;
    }
    send(*mood);
    return 0;
  }
  if (cmd == "hook") {  // always succeeds and prints nothing: hooks must never get in the way
    if (const auto mood = sprig::mood_from_hook(read_stdin(1 << 20))) send(*mood);
    return 0;
  }
  if (cmd == "path") {
    const auto path = pipe_path();
    if (!path) return 1;
    std::puts(path->c_str());
    return 0;
  }
  if (cmd == "help" || cmd == "-h" || cmd == "--help") {
    usage(stdout);
    return 0;
  }
  usage(stderr);
  return 2;
}
