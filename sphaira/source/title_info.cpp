#include "title_info.hpp"
#include "defines.hpp"
#include "ui/types.hpp"
#include "log.hpp"

#include "yati/nx/nca.hpp"
#include "yati/nx/ncm.hpp"

#include <cstring>
#include <atomic>
#include <ranges>
#include <algorithm>

#include <nxtc.h>
#include <minIni.h>

namespace sphaira::title {
namespace {

constexpr int THREAD_PRIO = PRIO_PREEMPTIVE;
constexpr int THREAD_CORE = 1;

struct ThreadData {
    ThreadData(bool title_cache);

    void Run();
    void Close();
    void Clear();

    void PushAsync(u64 id);
    auto GetAsync(u64 app_id) -> std::shared_ptr<ThreadResultData>;
    auto Get(u64 app_id, bool* cached = nullptr) -> std::shared_ptr<ThreadResultData>;

    auto IsRunning() const -> bool {
        return m_running;
    }

    auto IsTitleCacheEnabled() const {
        return m_title_cache;
    }

private:
    fs::FsNativeSd m_fs{};
    UEvent m_uevent{};
    Mutex m_mutex_id{};
    Mutex m_mutex_result{};
    bool m_title_cache{};

    // app_ids pushed to the queue, signal uevent when pushed.
    std::vector<u64> m_ids{};
    // control data pushed to the queue.
    std::vector<std::shared_ptr<ThreadResultData>> m_result{};

    std::atomic_bool m_running{};
};

Mutex g_mutex{};
Thread g_thread{};
u32 g_ref_count{};
std::unique_ptr<ThreadData> g_thread_data{};

struct NcmEntry {
    const NcmStorageId storage_id;
    NcmContentStorage cs{};
    NcmContentMetaDatabase db{};

    void Open() {
        if (R_FAILED(ncmOpenContentMetaDatabase(std::addressof(db), storage_id))) {
            log_write("\tncmOpenContentMetaDatabase() failed. storage_id: %u\n", storage_id);
        } else {
            log_write("\tncmOpenContentMetaDatabase() success. storage_id: %u\n", storage_id);
        }

        if (R_FAILED(ncmOpenContentStorage(std::addressof(cs), storage_id))) {
            log_write("\tncmOpenContentStorage() failed. storage_id: %u\n", storage_id);
        } else {
            log_write("\tncmOpenContentStorage() success. storage_id: %u\n", storage_id);
        }
    }

    void Close() {
        ncmContentMetaDatabaseClose(std::addressof(db));
        ncmContentStorageClose(std::addressof(cs));

        db = {};
        cs = {};
    }
};

constinit NcmEntry ncm_entries[] = {
    // on memory, will become invalid on the gamecard being inserted / removed.
    { NcmStorageId_GameCard },
    // normal (save), will remain valid.
    { NcmStorageId_BuiltInUser },
    { NcmStorageId_SdCard },
};

auto& GetNcmEntry(u8 storage_id) {
    auto it = std::ranges::find_if(ncm_entries, [storage_id](auto& e){
        return storage_id == e.storage_id;
    });

    if (it == std::end(ncm_entries)) {
        log_write("unable to find valid ncm entry: %u\n", storage_id);
        return ncm_entries[0];
    }

    return *it;
}

// also sets the status to error.
void FakeNacpEntry(std::shared_ptr<ThreadResultData>& e) {
    e->status = NacpLoadStatus::Error;
    // fake the nacp entry
    std::strcpy(e->lang.name, "Corrupted");
    std::strcpy(e->lang.author, "Corrupted");
}

Result LoadControlManual(u64 id, NacpStruct& nacp, std::shared_ptr<ThreadResultData>& data) {
    TimeStamp ts;

    MetaEntries entries;
    R_TRY(GetMetaEntries(id, entries, ContentFlag_Nacp));
    R_UNLESS(!entries.empty(), Result_GameEmptyMetaEntries);

    u64 program_id;
    fs::FsPath path;
    R_TRY(GetControlPathFromStatus(entries.back(), &program_id, &path));
    R_TRY(nca::ParseControl(path, program_id, &nacp, sizeof(nacp), &data->icon));

    log_write("\t\t[manual control] time taken: %.2fs %zums\n", ts.GetSecondsD(), ts.GetMs());
    R_SUCCEED();
}

ThreadData::ThreadData(bool title_cache) : m_title_cache{title_cache} {
    ueventCreate(&m_uevent, true);
    mutexInit(&m_mutex_id);
    mutexInit(&m_mutex_result);
    m_running = true;
}

void ThreadData::Run() {
    TimeStamp ts{};
    bool cached{true};
    const auto waiter = waiterForUEvent(&m_uevent);

    while (IsRunning()) {
        const auto rc = waitSingle(waiter, 3e+9);

        // if we timed out, flush the cache and poll again.
        if (R_FAILED(rc)) {
            nxtcFlushCacheFile();
            continue;
        }

        if (!IsRunning()) {
            return;
        }

        std::vector<u64> ids;
        {
            SCOPED_MUTEX(&m_mutex_id);
            std::swap(ids, m_ids);
        }

        for (u64 i = 0; i < std::size(ids); i++) {
            if (!IsRunning()) {
                return;
            }

            // sleep after every other entry loaded.
            const auto elapsed = (s64)2e+6 - (s64)ts.GetNs();
            if (!cached && elapsed > 0) {
                svcSleepThread(elapsed);
            }

            // loads new entry into cache.
            std::ignore = Get(ids[i], &cached);
            ts.Update();
        }
    }
}

void ThreadData::Close() {
    m_running = false;
    ueventSignal(&m_uevent);
}

void ThreadData::Clear() {
    SCOPED_MUTEX(&m_mutex_id);
    SCOPED_MUTEX(&m_mutex_result);
    m_result.clear();
    nxtcWipeCache();
}

void ThreadData::PushAsync(u64 id) {
    SCOPED_MUTEX(&m_mutex_id);
    SCOPED_MUTEX(&m_mutex_result);

    const auto it_id = std::ranges::find(m_ids, id);
    const auto it_result = std::ranges::find_if(m_result, [id](auto& e){
        return id == e->id;
    });

    if (it_id == m_ids.end() && it_result == m_result.end()) {
        m_ids.emplace_back(id);
        ueventSignal(&m_uevent);
    }
}

auto ThreadData::GetAsync(u64 app_id) -> std::shared_ptr<ThreadResultData> {
    SCOPED_MUTEX(&m_mutex_result);

    for (s64 i = 0; i < std::size(m_result); i++) {
        if (app_id == m_result[i]->id) {
            return m_result[i];
        }
    }

    return {};
}

auto ThreadData::Get(u64 app_id, bool* cached) -> std::shared_ptr<ThreadResultData> {
    // try and fetch from results first, before manually loading.
    if (auto data = GetAsync(app_id)) {
        if (cached) {
            *cached = true;
        }
        return data;
    }

    TimeStamp ts;
    auto result = std::make_shared<ThreadResultData>(app_id);
    result->status = NacpLoadStatus::Error;

    if (auto data = nxtcGetApplicationMetadataEntryById(app_id)) {
        log_write("[NXTC] loaded from cache time taken: %.2fs %zums %zuns\n", ts.GetSecondsD(), ts.GetMs(), ts.GetNs());
        ON_SCOPE_EXIT(nxtcFreeApplicationMetadata(&data));

        if (cached) {
            *cached = true;
        }

        result->status = NacpLoadStatus::Loaded;
        std::strcpy(result->lang.name, data->name);
        std::strcpy(result->lang.author, data->publisher);
        result->icon.resize(data->icon_size);
        std::memcpy(result->icon.data(), data->icon_data, result->icon.size());
    } else {
        if (cached) {
            *cached = false;
        }

        bool manual_load = true;
        u64 actual_size{};
        auto control = std::make_unique<NsApplicationControlData>();

        if (hosversionBefore(20,0,0)) {
            TimeStamp ts;
            if (R_SUCCEEDED(nsGetApplicationControlData(NsApplicationControlSource_CacheOnly, app_id, control.get(), sizeof(NsApplicationControlData), &actual_size))) {
                manual_load = false;
                log_write("\t\t[ns control cache] time taken: %.2fs %zums\n", ts.GetSecondsD(), ts.GetMs());
            }
        }

        if (manual_load) {
            manual_load = R_SUCCEEDED(LoadControlManual(app_id, control->nacp, result));
        }

        Result rc{};
        if (!manual_load) {
            TimeStamp ts;
            if (R_SUCCEEDED(rc = nsGetApplicationControlData(NsApplicationControlSource_Storage, app_id, control.get(), sizeof(NsApplicationControlData), &actual_size))) {
                log_write("\t\t[ns control storage] time taken: %.2fs %zums\n", ts.GetSecondsD(), ts.GetMs());
            }
        }

        if (R_FAILED(rc)) {
            FakeNacpEntry(result);
        } else {
            bool valid = true;
            NacpLanguageEntry* lang;
            if (R_SUCCEEDED(nsGetApplicationDesiredLanguage(&control->nacp, &lang))) {
                result->lang = *lang;
            } else {
                FakeNacpEntry(result);
                valid = false;
            }

            if (!manual_load) {
                const auto jpeg_size = actual_size - sizeof(NacpStruct);
                result->icon.resize(jpeg_size);
                std::memcpy(result->icon.data(), control->icon, result->icon.size());
            }

            // add new entry to cache, if valid.
            if (valid) {
                nxtcAddEntry(app_id, &control->nacp, result->icon.size(), result->icon.data(), true);
            }

            result->status = NacpLoadStatus::Loaded;
        }
    }

    // load override from sys-tweak.
    if (result->status == NacpLoadStatus::Loaded) {
        const auto tweak_path = GetContentsPath(app_id);
        if (m_fs.DirExists(tweak_path)) {
            log_write("[TITLE] found contents path: %s\n", tweak_path.s);

            std::vector<u8> icon;
            m_fs.read_entire_file(fs::AppendPath(tweak_path, "icon.jpg"), icon);

            struct Overrides {
                std::string name;
                std::string author;
            } overrides;

            static const auto cb = [](const mTCHAR *Section, const mTCHAR *Key, const mTCHAR *Value, void *UserData) -> int {
                auto e = static_cast<Overrides*>(UserData);

                if (!std::strcmp(Section, "override_nacp")) {
                    if (!std::strcmp(Key, "name")) {
                        e->name = Value;
                    } else if (!std::strcmp(Key, "author")) {
                        e->author = Value;
                    }
                }

                return 1;
            };

            ini_browse(cb, &overrides, fs::AppendPath(tweak_path, "config.ini"));

            if (!icon.empty() && icon.size() < sizeof(NsApplicationControlData::icon)) {
                log_write("[TITLE] overriding icon: %zu -> %zu\n", result->icon.size(), icon.size());
                result->icon = icon;
            }

            if (!overrides.name.empty() && overrides.name.length() < sizeof(result->lang.name)) {
                log_write("[TITLE] overriding name: %s -> %s\n", result->lang.name, overrides.name.c_str());
                std::strcpy(result->lang.name, overrides.name.c_str());
            }

            if (!overrides.author.empty() && overrides.author.length() < sizeof(result->lang.author)) {
                log_write("[TITLE] overriding author: %s -> %s\n", result->lang.author, overrides.author.c_str());
                std::strcpy(result->lang.author, overrides.author.c_str());
            }
        }
    }

    SCOPED_MUTEX(&m_mutex_result);
    m_result.emplace_back(result);
    return result;
}

void ThreadFunc(void* user) {
    auto data = static_cast<ThreadData*>(user);

    if (data->IsTitleCacheEnabled() && !nxtcInitialize()) {
        log_write("[NXTC] failed to init cache\n");
    }
    ON_SCOPE_EXIT(nxtcExit());

    while (data->IsRunning()) {
        data->Run();
    }
}

} // namespace

// starts background thread.
Result Init() {
    SCOPED_MUTEX(&g_mutex);

    if (!g_ref_count) {
        R_TRY(nsInitialize());
        R_TRY(ncmInitialize());

        for (auto& e : ncm_entries) {
            e.Open();
        }

        g_thread_data = std::make_unique<ThreadData>(true);
        R_TRY(threadCreate(&g_thread, ThreadFunc, g_thread_data.get(), nullptr, 1024*32, THREAD_PRIO, THREAD_CORE));
        svcSetThreadCoreMask(g_thread.handle, THREAD_CORE, THREAD_AFFINITY_DEFAULT(THREAD_CORE));
        R_TRY(threadStart(&g_thread));
    }

    g_ref_count++;
    R_SUCCEED();
}

void Exit() {
    SCOPED_MUTEX(&g_mutex);

    if (!g_ref_count) {
        return;
    }

    g_ref_count--;
    if (!g_ref_count) {
        g_thread_data->Close();

        threadWaitForExit(&g_thread);
        threadClose(&g_thread);
        g_thread_data.reset();

        for (auto& e : ncm_entries) {
            e.Close();
        }

        nsExit();
        ncmExit();
    }
}

void Clear() {
    SCOPED_MUTEX(&g_mutex);
    if (g_thread_data) {
        g_thread_data->Clear();
    }
}

void PushAsync(u64 app_id) {
    SCOPED_MUTEX(&g_mutex);
    if (g_thread_data) {
        g_thread_data->PushAsync(app_id);
    }
}

auto GetAsync(u64 app_id) -> std::shared_ptr<ThreadResultData> {
    SCOPED_MUTEX(&g_mutex);
    if (g_thread_data) {
        return g_thread_data->GetAsync(app_id);
    }
    return {};
}

auto Get(u64 app_id, bool* cached) -> std::shared_ptr<ThreadResultData> {
    SCOPED_MUTEX(&g_mutex);
    if (g_thread_data) {
        return g_thread_data->Get(app_id, cached);
    }
    return {};
}

auto GetNcmCs(u8 storage_id) -> NcmContentStorage& {
    return GetNcmEntry(storage_id).cs;
}

auto GetNcmDb(u8 storage_id) -> NcmContentMetaDatabase& {
    return GetNcmEntry(storage_id).db;
}

Result GetMetaEntries(u64 id, MetaEntries& out, u32 flags) {
    for (s32 i = 0; ; i++) {
        s32 count;
        NsApplicationContentMetaStatus status;
        R_TRY(nsListApplicationContentMetaStatus(id, i, &status, 1, &count));

        if (!count) {
            break;
        }

        if (flags & ContentMetaTypeToContentFlag(status.meta_type)) {
            out.emplace_back(status);
        }
    }

    R_SUCCEED();
}

Result GetControlPathFromStatus(const NsApplicationContentMetaStatus& status, u64* out_program_id, fs::FsPath* out_path) {
    const auto& ee = status;
    if (ee.storageID != NcmStorageId_SdCard && ee.storageID != NcmStorageId_BuiltInUser && ee.storageID != NcmStorageId_GameCard) {
        return 0x1;
    }

    auto& db = GetNcmDb(ee.storageID);
    auto& cs = GetNcmCs(ee.storageID);

    NcmContentMetaKey key;
    R_TRY(ncmContentMetaDatabaseGetLatestContentMetaKey(&db, &key, ee.application_id));

    NcmContentId content_id;
    R_TRY(ncmContentMetaDatabaseGetContentIdByType(&db, &content_id, &key, NcmContentType_Control));

    R_TRY(ncmContentStorageGetProgramId(&cs, out_program_id, &content_id, FsContentAttributes_All));

    R_TRY(ncmContentStorageGetPath(&cs, out_path->s, sizeof(*out_path), &content_id));
    R_SUCCEED();
}

// taken from nxdumptool.
void utilsReplaceIllegalCharacters(char *str, bool ascii_only)
{
    static const char g_illegalFileSystemChars[] = "\\/:*?\"<>|";

    size_t str_size = 0, cur_pos = 0;

    if (!str || !(str_size = strlen(str))) return;

    u8 *ptr1 = (u8*)str, *ptr2 = ptr1;
    ssize_t units = 0;
    u32 code = 0;
    bool repl = false;

    while(cur_pos < str_size)
    {
        units = decode_utf8(&code, ptr1);
        if (units < 0) break;

        if (code < 0x20 || (!ascii_only && code == 0x7F) || (ascii_only && code >= 0x7F) || \
            (units == 1 && memchr(g_illegalFileSystemChars, (int)code, std::size(g_illegalFileSystemChars))))
        {
            if (!repl)
            {
                *ptr2++ = '_';
                repl = true;
            }
        } else {
            if (ptr2 != ptr1) memmove(ptr2, ptr1, (size_t)units);
            ptr2 += units;
            repl = false;
        }

        ptr1 += units;
        cur_pos += (size_t)units;
    }

    *ptr2 = '\0';
}

auto GetContentsPath(u64 app_id) -> fs::FsPath {
    fs::FsPath path;
    std::snprintf(path, sizeof(path), "/atmosphere/contents/%016lX", app_id);
    return path;
}

} // namespace sphaira::title
