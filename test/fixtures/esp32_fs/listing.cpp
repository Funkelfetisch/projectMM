// The real ESP32 filesystem implementation uses recording POSIX directory/stat calls.
#include "platform/platform.h"
#include "platform/esp32/platform_esp32_sdmmc.h"

#include <cassert>
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <string>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

namespace fs_fixture {
struct Input { const char* name; unsigned char type; mode_t mode; bool exists = true; };
std::vector<Input> input;
size_t next = 0;
unsigned stats = 0;
unsigned closes = 0;
dirent entry{};
DIR* open(const char*) { next = 0; return reinterpret_cast<DIR*>(&entry); }
dirent* read(DIR*) {
    if (next == input.size()) return nullptr;
    const auto& item = input[next++];
    std::snprintf(entry.d_name, sizeof(entry.d_name), "%s", item.name);
    entry.d_type = item.type;
    return &entry;
}
int close(DIR*) { ++closes; return 0; }
int statEntry(const char* path, struct stat* out) {
    ++stats;
    const char* name = std::strrchr(path, '/');
    for (const auto& item : input) {
        if (std::strcmp(name ? name + 1 : path, item.name)) continue;
        if (!item.exists) return -1;
        out->st_mode = item.mode;
        out->st_size = 37;
        return 0;
    }
    return -1;
}
} // namespace fs_fixture

#define opendir(...) fs_fixture::open(__VA_ARGS__)
#define readdir(...) fs_fixture::read(__VA_ARGS__)
#define closedir(...) fs_fixture::close(__VA_ARGS__)
#define stat(...) fs_fixture::statEntry(__VA_ARGS__)
#include "platform/esp32/platform_esp32_fs.cpp"
#undef opendir
#undef readdir
#undef closedir
#undef stat

namespace mm::platform {
bool sdCardMounted() { return true; }
void sdCardList(const char*, FsListCb cb, void* user, FsListMode mode) {
    cb("card", false, mode == FsListMode::NamesOnly ? 0 : 37, user);
}
} // namespace mm::platform

struct Output { std::string name; bool isDir; uint32_t size; };
int main(int argc, char** argv) {
    assert(argc == 2);
    using namespace mm::platform;
    const std::string scenario = argv[1];
    fs_fixture::input = {{"file", DT_REG, S_IFREG}, {"dir", DT_DIR, S_IFDIR},
                         {"unknown-file", DT_UNKNOWN, S_IFREG}, {"unknown-dir", DT_UNKNOWN, S_IFDIR},
                         {"link-dir", DT_LNK, S_IFDIR}, {"missing", DT_UNKNOWN, S_IFREG, false}};
    if (scenario == "known") fs_fixture::input.resize(2);
    assert(fsMount());
    std::vector<Output> output;
    const auto collect = [](const char* name, bool isDir, uint32_t size, void* user) {
        static_cast<std::vector<Output>*>(user)->push_back({name, isDir, size});
    };
    const bool metadata = scenario == "metadata";
    const bool root = scenario == "root";
    const bool sd = scenario == "sd";
    if (metadata) fsList("/presets", collect, &output); // Exercise the unchanged three-argument default.
    else fsList(sd ? "/sd" : root ? "/" : "/presets", collect, &output, FsListMode::NamesOnly);
    if (sd) {
        assert(output.size() == 1 && output[0].name == "card" && output[0].size == 0);
        assert(fs_fixture::stats == 0 && fs_fixture::closes == 0);
    } else {
        const size_t offset = root ? 1 : 0;
        assert(output.size() == fs_fixture::input.size() + offset);
        if (root) assert(output[0].name == "sd" && output[0].isDir && output[0].size == 0);
        for (size_t i = 0; i < fs_fixture::input.size(); ++i) {
            const auto& source = fs_fixture::input[i];
            const auto& result = output[i + offset];
            const bool directory = source.exists && S_ISDIR(source.mode);
            assert(result.name == source.name && result.isDir == directory);
            assert(result.size == (metadata && source.exists && !directory ? 37u : 0u));
        }
        assert(fs_fixture::stats == (metadata ? 6u : scenario == "known" ? 0u : 4u));
        assert(fs_fixture::closes == 1);
    }
    std::printf("%s passed: stat calls %u\n", argv[1], fs_fixture::stats);
}
