// The HDD cache (z:) as hard links to the game's own files.

#include <cstdint>
#include <filesystem>
#include <string>
#include <system_error>

#include "core/log.h"
#include "kernel/kernel.h"
#include "xdk/hle.h"

using namespace kernel;
namespace fs = std::filesystem;

X86_EXTERN_FUNC(__imp__sub_000F6CC0);

namespace {

constexpr uint32_t kCacheState = 0x8A9908;  // what the check found: 0 the cache is good
constexpr const char* kCached[] = {"voice.afs", "datahdd.afs", "datahdd2.afs"};

struct Pair {
    fs::path disc, cache;
};

bool PathsOf(const char* name, Pair& p) {
    p.disc = ResolvePath(std::string("\\Device\\CdRom0\\doa2\\") + name, 0);
    p.cache = ResolvePath(std::string("\\Device\\Harddisk0\\Partition5\\") + name, 0);
    return !p.disc.empty() && !p.cache.empty();
}

void LinkCache() {
    int linked = 0, kept = 0;
    for (const char* name : kCached) {
        Pair p;
        if (!PathsOf(name, p)) return;  // the game on a disc image: no host file to link
        std::error_code ec;
        if (!fs::is_regular_file(p.disc, ec)) return;
        if (fs::exists(p.cache, ec) && fs::equivalent(p.disc, p.cache, ec)) {
            kept++;
            continue;
        }
        fs::create_directories(p.cache.parent_path(), ec);
        fs::remove(p.cache, ec);  // a copy from an earlier start, dated or not
        fs::create_hard_link(p.disc, p.cache, ec);
        if (ec) {
            LOG_FIRST(4, "hdd_cache: no hard link for %s (%s), the game copies it", name, ec.message().c_str());
            return;
        }
        linked++;
    }
    if (linked) LOG_FIRST(4, "hdd_cache: z: holds hard links to the game's files (%d new, %d kept)", linked, kept);
}

// a cache the check turned down was deleted by it; a link left over would have the copy thread write into the
// game's own file, so none stays
void DropLinksIfRejected() {
    if (R32(kCacheState) == 0) return;
    for (const char* name : kCached) {
        Pair p;
        if (!PathsOf(name, p)) return;
        std::error_code ec;
        if (fs::exists(p.cache, ec) && fs::equivalent(p.disc, p.cache, ec)) {
            fs::remove(p.cache, ec);
            LOG_FIRST(4, "hdd_cache: the check turned down the cache, link %s removed", name);
        }
    }
}

}  // namespace

// void CheckHddCache(void), cdecl
X86_FUNC(sub_000F6CC0) {
    LinkCache();
    __imp__sub_000F6CC0(ctx, base);
    DropLinksIfRejected();
}
