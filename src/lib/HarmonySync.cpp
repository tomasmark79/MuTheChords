#include <MuTheChordsLib/HarmonySync.hpp>
#include <MuTheChordsLib/Scales.hpp>
#include <Utils/UtilsFactory.hpp>
#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <nlohmann/json.hpp>
#include <random>
#include <sstream>
#include <thread>
#include <vector>

#if defined(_WIN32)
#include <Utils/Platform/WindowsHeaders.hpp>
#elif !defined(__EMSCRIPTEN__)
#include <fcntl.h>
#include <signal.h>
#include <sys/file.h>
#include <unistd.h>
#endif

namespace muthechords {
  namespace {
    bool valid(const HarmonyState &s) {
      return s.root >= 0 && s.root <= 11 && s.scale >= 0 && s.scale < scaleCount;
    }

    int processId() {
#if defined(_WIN32)
      return static_cast<int>(GetCurrentProcessId());
#elif !defined(__EMSCRIPTEN__)
      return static_cast<int>(getpid());
#else
      return 1;
#endif
    }

    std::string generateUuidHex() {
      std::random_device rd;
      std::mt19937_64 gen(rd());
      std::uniform_int_distribution<uint64_t> dis;
      std::ostringstream oss;
      oss << std::hex << std::setfill('0') << std::setw(16) << dis(gen) << std::setw(16)
          << dis(gen);
      return oss.str();
    }

    bool isValidUuidHex(const std::string &s) {
      if (s.size() != 32) {
        return false;
      }
      return std::all_of(s.begin(), s.end(), [](unsigned char c) {
        return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
      });
    }

    std::string fnv1aHex(const std::string &str) {
      uint64_t hash = 14695981039346656037ULL;
      for (unsigned char c : str) {
        hash ^= c;
        hash *= 1099511628211ULL;
      }
      std::ostringstream oss;
      oss << std::hex << hash;
      return oss.str();
    }

    std::filesystem::path userAppDataDirectory() {
#if defined(_WIN32)
      if (const char *appdata = std::getenv("APPDATA"); appdata && *appdata) {
        return std::filesystem::path(appdata);
      }
#elif defined(__APPLE__)
      if (const char *home = std::getenv("HOME"); home && *home) {
        return std::filesystem::path(home) / "Library" / "Application Support";
      }
#else
      if (const char *xdg = std::getenv("XDG_CONFIG_HOME"); xdg && *xdg) {
        return std::filesystem::path(xdg);
      }
      if (const char *home = std::getenv("HOME"); home && *home) {
        return std::filesystem::path(home) / ".config";
      }
#endif
      std::error_code ec;
      auto tmp = std::filesystem::temp_directory_path(ec);
      return ec ? std::filesystem::current_path() : tmp;
    }

    // Empty means definitely dead. '?' means inaccessible: retain conservatively.
    std::string processIdentity(int pid) {
#if defined(_WIN32)
      const auto handle = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | SYNCHRONIZE, FALSE,
                                      static_cast<DWORD>(pid));
      if (handle == nullptr) {
        return GetLastError() == ERROR_INVALID_PARAMETER ? "" : "?";
      }
      FILETIME created{}, exited{}, kernel{}, user{};
      std::string result = "?";
      if (WaitForSingleObject(handle, 0) == WAIT_OBJECT_0) {
        result.clear();
      } else if (GetProcessTimes(handle, &created, &exited, &kernel, &user)) {
        result = std::to_string(static_cast<int64_t>(created.dwHighDateTime)) + ":" +
                 std::to_string(static_cast<int64_t>(created.dwLowDateTime));
      }
      CloseHandle(handle);
      return result;
#elif defined(__linux__)
      std::ifstream statFile("/proc/" + std::to_string(pid) + "/stat");
      std::ostringstream ss;
      ss << statFile.rdbuf();
      const std::string stat = ss.str();
      if (stat.empty()) {
        return kill(pid, 0) == 0 || errno == EPERM ? "?" : "";
      }
      const auto rparen = stat.rfind(')');
      if (rparen == std::string::npos || rparen + 2 >= stat.size()) {
        return "?";
      }
      std::istringstream iss(stat.substr(rparen + 2));
      std::vector<std::string> fields;
      std::string token;
      while (iss >> token) {
        fields.push_back(token);
      }
      if (fields.size() < 20) {
        return "?";
      }
      if (fields[0] == "Z" || fields[0] == "X") {
        return {};
      }
      std::ifstream bootFile("/proc/sys/kernel/random/boot_id");
      std::string boot;
      std::getline(bootFile, boot);
      while (!boot.empty() && (boot.back() == '\n' || boot.back() == '\r' || boot.back() == ' ')) {
        boot.pop_back();
      }
      return boot.empty() ? "?" : boot + ":" + fields[19];
#elif !defined(__EMSCRIPTEN__)
      return kill(pid, 0) == 0 || errno == EPERM ? std::to_string(pid) : "";
#else
      (void)pid;
      return "wasm:1";
#endif
    }

    // Never unlink this file: lock identity must survive data-file replacement and
    // the last member leaving. Native locks also serialize instances in one
    // process.
    class FileLock {
    public:
      explicit FileLock(const std::filesystem::path &file, double timeoutMs = 20.0) {
        const auto start = std::chrono::steady_clock::now();
        do {
#if defined(_WIN32)
          handle = CreateFileW(file.wstring().c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr,
                               OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
          if (handle != INVALID_HANDLE_VALUE) {
            acquired = true;
            return;
          }
          const auto err = GetLastError();
          if (err != ERROR_SHARING_VIOLATION && err != ERROR_LOCK_VIOLATION) {
            return;
          }
#elif !defined(__EMSCRIPTEN__)
          if (handle < 0) {
            handle = open(file.string().c_str(), O_CREAT | O_RDWR | O_CLOEXEC, 0600);
          }
          if (handle < 0) {
            return;
          }
          if (flock(handle, LOCK_EX | LOCK_NB) == 0) {
            acquired = true;
            return;
          }
          if (errno != EWOULDBLOCK && errno != EAGAIN && errno != EINTR) {
            return;
          }
#else
          (void)file;
          acquired = true;
          return;
#endif
          if (timeoutMs > 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
          }
        } while (std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start)
                     .count() < timeoutMs);
      }

      ~FileLock() {
#if defined(_WIN32)
        if (handle != INVALID_HANDLE_VALUE) {
          CloseHandle(handle);
        }
#elif !defined(__EMSCRIPTEN__)
        if (handle >= 0) {
          close(handle);
        }
#endif
      }

      bool acquired = false;

    private:
#if defined(_WIN32)
      HANDLE handle = INVALID_HANDLE_VALUE;
#elif !defined(__EMSCRIPTEN__)
      int handle = -1;
#endif
    };

    struct Member {
      std::string token;
      std::string identity;
      int pid = 0;
    };

    struct Bus {
      HarmonyState state;
      std::string revision;
      std::vector<Member> members;
    };

    bool integerField(const nlohmann::json &object, const char *key, int &value) {
      if (!object.is_object() || !object.contains(key)) {
        return false;
      }
      const auto &v = object.at(key);
      if (!v.is_number_integer() || v.is_boolean()) {
        return false;
      }
      value = v.get<int>();
      return true;
    }

    bool stringField(const nlohmann::json &object, const char *key, std::string &value) {
      if (!object.is_object() || !object.contains(key)) {
        return false;
      }
      const auto &v = object.at(key);
      if (!v.is_string()) {
        return false;
      }
      const auto s = v.get<std::string>();
      if (s.empty() || s.size() > 200) {
        return false;
      }
      value = s;
      return true;
    }
  } // namespace

  struct HarmonySync::Impl {
    explicit Impl(const std::string &channel)
        : fileReader(utils::UtilsFactory::createFileReader()),
          fileWriter(utils::UtilsFactory::createFileWriter()),
          directoryManager(utils::UtilsFactory::createDirectoryManager()),
          jsonSerializer(utils::UtilsFactory::createJsonSerializer()) {
      directory = userAppDataDirectory() / "MuTheChords" / "Harmony Sync";
      const std::string name =
          channel.empty() ? std::string("global-v3") : ("test-" + fnv1aHex(channel));
      data = directory / (name + ".json");
      lock = directory / (name + ".lock");
      self = {generateUuidHex(), processIdentity(processId()), processId()};
    }

    std::shared_ptr<utils::IFileReader> fileReader;
    std::shared_ptr<utils::IFileWriter> fileWriter;
    std::shared_ptr<utils::IDirectoryManager> directoryManager;
    std::shared_ptr<utils::IJsonSerializer> jsonSerializer;

    std::filesystem::path directory;
    std::filesystem::path data;
    std::filesystem::path lock;
    Member self;
    std::unique_ptr<FileLock> lease;
    bool active = false;
    std::string seen;
    std::string failure;

    bool fail(const char *message) {
      failure = message;
      return false;
    }

    bool prepare() {
      failure.clear();
      return directoryManager->createDirectory(directory).hasValue() ||
             fail("Cannot create harmony sync directory");
    }

    bool load(Bus &bus) {
      std::error_code ec;
      if (!std::filesystem::exists(data, ec)) {
        return true;
      }
      if (!fileReader->exists(data)) {
        return fail("Invalid harmony sync file");
      }
      auto sizeRes = fileReader->getSize(data);
      if (!sizeRes || sizeRes.value() > 1024ULL * 1024ULL) {
        return fail("Invalid harmony sync file");
      }
      auto docRes = jsonSerializer->loadFromFile(data);
      if (!docRes || !docRes.value().is_object()) {
        return fail("Cannot read harmony sync data");
      }
      const auto &doc = docRes.value();
      int version = 0;
      if (!integerField(doc, "version", version) || version != 2 ||
          !stringField(doc, "revision", bus.revision) ||
          !integerField(doc, "root", bus.state.root) ||
          !integerField(doc, "scale", bus.state.scale) || !valid(bus.state)) {
        return fail("Invalid harmony sync state");
      }
      if (!doc.contains("members") || !doc["members"].is_array()) {
        return fail("Invalid harmony sync membership");
      }
      const auto &array = doc["members"];
      if (array.size() > 4096) {
        return fail("Invalid harmony sync membership");
      }
      for (const auto &item : array) {
        Member m;
        if (!item.is_object() || !stringField(item, "token", m.token) ||
            !stringField(item, "identity", m.identity) || !isValidUuidHex(m.token) ||
            !integerField(item, "pid", m.pid) || m.pid <= 0 ||
            std::any_of(bus.members.begin(), bus.members.end(),
                        [&](const auto &n) { return n.token == m.token; })) {
          return fail("Invalid harmony sync member");
        }
        bus.members.push_back(m);
      }
      bus.members.erase(std::remove_if(bus.members.begin(), bus.members.end(),
                                       [&](const auto &m) {
        const auto identity = processIdentity(m.pid);
        bool stale = identity.empty() || (identity != "?" && identity != m.identity);
        const auto leaseFile = directory / ("member-" + m.token + ".lock");
        if (!stale) {
          FileLock probe(leaseFile, 0);
          stale = probe.acquired;
        }
        if (stale) {
          std::error_code remEc;
          std::filesystem::remove(leaseFile, remEc);
        }
        return stale;
      }),
                        bus.members.end());
      return true;
    }

    bool save(const Bus &bus) {
      std::error_code ec;
      if (bus.members.empty()) {
        std::filesystem::remove(data, ec);
        return !ec || fail("Cannot remove inactive harmony sync state");
      }
      nlohmann::json doc = nlohmann::json::object();
      doc["version"] = 2;
      doc["revision"] = bus.revision;
      doc["root"] = bus.state.root;
      doc["scale"] = bus.state.scale;
      nlohmann::json members = nlohmann::json::array();
      for (const auto &m : bus.members) {
        members.push_back({{"token", m.token}, {"identity", m.identity}, {"pid", m.pid}});
      }
      doc["members"] = std::move(members);

      const auto tmpFile =
          directory / (data.filename().string() + "." + generateUuidHex() + ".tmp");
      if (!jsonSerializer->saveToFile(tmpFile, doc).hasValue()) {
        std::filesystem::remove(tmpFile, ec);
        return fail("Cannot write harmony sync state");
      }
#if defined(_WIN32)
      if (!MoveFileExW(tmpFile.wstring().c_str(), data.wstring().c_str(),
                       MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        std::filesystem::remove(tmpFile, ec);
        return fail("Cannot write harmony sync state");
      }
#else
      std::filesystem::rename(tmpFile, data, ec);
      if (ec) {
        std::filesystem::remove(tmpFile, ec);
        return fail("Cannot write harmony sync state");
      }
#endif
      return true;
    }

    [[nodiscard]] bool contains(const Bus &bus) const {
      return std::any_of(bus.members.begin(), bus.members.end(),
                         [&](const auto &m) { return m.token == self.token; });
    }
  };

  HarmonySync::HarmonySync(std::string channel) : impl(std::make_unique<Impl>(channel)) {}
  HarmonySync::~HarmonySync() { leave(); }
  bool HarmonySync::joined() const { return impl->active; }
  std::string HarmonySync::error() const { return impl->failure; }

  bool HarmonySync::join(HarmonyState &state) {
    auto &p = *impl;
    if (p.active) {
      p.failure.clear();
      return true;
    }
    if (!valid(state)) {
      return p.fail("Invalid local harmony state");
    }
    if (p.self.identity.empty() || p.self.identity == "?") {
      return p.fail("Cannot identify harmony sync process");
    }
    if (!p.prepare()) {
      return false;
    }
    FileLock lock(p.lock);
    if (!lock.acquired) {
      return p.fail("Harmony sync lock unavailable");
    }
    Bus bus;
    if (!p.load(bus)) {
      return false;
    }
    if (bus.members.empty()) {
      bus.state = state;
      bus.revision = generateUuidHex();
    }
    p.lease = std::make_unique<FileLock>(p.directory / ("member-" + p.self.token + ".lock"));
    if (!p.lease->acquired) {
      p.lease.reset();
      return p.fail("Cannot acquire harmony sync membership lease");
    }
    if (!p.contains(bus)) {
      bus.members.push_back(p.self);
    }
    if (!p.save(bus)) {
      p.lease.reset();
      return false;
    }
    state = bus.state;
    p.seen = bus.revision;
    p.active = true;
    return true;
  }

  void HarmonySync::leave() {
    auto &p = *impl;
    if (!p.active) {
      return;
    }
    p.active = false;
    p.lease.reset();
    if (!p.prepare()) {
      return;
    }
    FileLock lock(p.lock);
    if (!lock.acquired) {
      p.fail("Harmony sync lock unavailable on leave");
      return;
    }
    Bus bus;
    if (!p.load(bus)) {
      return;
    }
    bus.members.erase(std::remove_if(bus.members.begin(), bus.members.end(),
                                     [&](const auto &m) { return m.token == p.self.token; }),
                      bus.members.end());
    p.save(bus);
    std::error_code ec;
    std::filesystem::remove(p.directory / ("member-" + p.self.token + ".lock"), ec);
  }

  bool HarmonySync::publish(const HarmonyState &state) {
    auto &p = *impl;
    p.failure.clear();
    if (!p.active) {
      return p.fail("Harmony sync is not joined");
    }
    if (!valid(state)) {
      return p.fail("Invalid local harmony state");
    }
    if (!p.prepare()) {
      return false;
    }
    FileLock lock(p.lock);
    if (!lock.acquired) {
      return p.fail("Harmony sync lock unavailable");
    }
    Bus bus;
    if (!p.load(bus)) {
      return false;
    }
    if (!p.contains(bus)) {
      p.active = false;
      p.lease.reset();
      return p.fail("Harmony sync membership lost");
    }
    if (state != bus.state) {
      bus.state = state;
      bus.revision = generateUuidHex();
      if (!p.save(bus)) {
        return false;
      }
    }
    p.seen = bus.revision;
    return true;
  }

  bool HarmonySync::read(HarmonyState &state) {
    auto &p = *impl;
    p.failure.clear();
    if (!p.active) {
      return false;
    }
    if (!p.prepare()) {
      return false;
    }
    FileLock lock(p.lock);
    if (!lock.acquired) {
      return p.fail("Harmony sync lock unavailable");
    }
    Bus bus;
    if (!p.load(bus)) {
      return false;
    }
    if (!p.contains(bus)) {
      p.active = false;
      p.lease.reset();
      return p.fail("Harmony sync membership lost");
    }
    if (p.seen == bus.revision) {
      return false;
    }
    state = bus.state;
    p.seen = bus.revision;
    return true;
  }

} // namespace muthechords
