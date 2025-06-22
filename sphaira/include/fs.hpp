#pragma once

#include <switch.h>
#include <dirent.h>
#include <cstring>
#include <vector>
#include <string>
#include <string_view>
#include "defines.hpp"

namespace fs {

struct FsPath {
    FsPath() = default;

    constexpr FsPath(const FsPath& p) { From(p); }
    constexpr FsPath(const char* str) { From(str); }
    constexpr FsPath(const std::string& str) { From(str); }
    constexpr FsPath(const std::string_view& str) { From(str); }

    constexpr void From(const FsPath& p) {
        From(p.s);
    }

    constexpr void From(const char* str) {
        if consteval {
            for (u32 i = 0; str[i] != '\0'; i++) {
                s[i] = str[i];
            }
        } else {
            std::strcpy(s, str);
        }
    }

    constexpr void From(const std::string& str) {
        std::copy(str.cbegin(), str.cend(), std::begin(s));
    }

    constexpr void From(const std::string_view& str) {
        std::copy(str.cbegin(), str.cend(), std::begin(s));
    }

    constexpr auto toString() const -> std::string {
        return s;
    }

    constexpr auto starts_with(std::string_view str) const -> bool {
        return !strncasecmp(s, str.data(), str.length());
    }

    constexpr auto empty() const {
        return s[0] == '\0';
    }

    constexpr auto size() const {
        return std::strlen(s);
    }

    constexpr auto length() const {
        return std::strlen(s);
    }

    constexpr void clear() {
        s[0] = '\0';
    }

    constexpr operator const char*() const { return s; }
    constexpr operator char*() { return s; }
    constexpr operator std::string() { return s; }
    constexpr operator std::string_view() { return s; }
    constexpr operator std::string() const { return s; }
    constexpr operator std::string_view() const { return s; }
    constexpr char& operator[](std::size_t idx) { return s[idx]; }
    constexpr const char& operator[](std::size_t idx) const { return s[idx]; }

    constexpr FsPath& operator=(const FsPath& p) noexcept {
        From(p.s);
        return *this;
    }

    constexpr FsPath operator+(const FsPath& v) const noexcept {
        FsPath r{*this};
        return r += v;
    }

    constexpr FsPath operator+(const char* v) const noexcept {
        FsPath r{*this};
        return r += v;
    }

    constexpr FsPath operator+(const std::string& v) const noexcept {
        FsPath r{*this};
        return r += v;
    }

    constexpr FsPath operator+(const std::string_view v) const noexcept {
        FsPath r{*this};
        return r += v;
    }

    constexpr const char* operator+(std::size_t v) const noexcept {
        return this->s + v;
    }

    constexpr FsPath& operator+=(const FsPath& v) noexcept {
        std::strcat(*this, v);
        return *this;
    }

    constexpr FsPath& operator+=(const char* v) noexcept {
        std::strcat(*this, v);
        return *this;
    }

    constexpr FsPath& operator+=(const std::string& v) noexcept {
        std::strncat(*this, v.data(), v.length());
        return *this;
    }

    constexpr FsPath& operator+=(const std::string_view& v) noexcept {
        std::strncat(*this, v.data(), v.length());
        return *this;
    }

    constexpr FsPath& operator+=(char v) noexcept {
        const auto sz = size();
        s[sz + 0] = v;
        s[sz + 1] = '\0';
        return *this;
    }

    constexpr bool operator==(const FsPath& v) const noexcept {
        return !strcasecmp(*this, v);
    }

    constexpr bool operator==(const char* v) const noexcept {
        return !strcasecmp(*this, v);
    }

    constexpr bool operator==(const std::string& v) const noexcept {
        return !strncasecmp(*this, v.data(), v.length());
    }

    constexpr bool operator==(const std::string_view v) const noexcept {
        return !strncasecmp(*this, v.data(), v.length());
    }

    static consteval bool Test(const auto& str) {
        FsPath path{str};
        return path[0] == str[0];
    }
    static consteval bool TestFrom(const auto& str) {
        FsPath path;
        path.From(str);
        return path[0] == str[0];
    }

    char s[FS_MAX_PATH]{};
};

inline FsPath operator+(const char* v, const FsPath& fp) {
    FsPath r{v};
    return r += fp;
}

inline FsPath operator+(const std::string& v, const FsPath& fp) {
    FsPath r{v};
    return r += fp;
}

inline FsPath operator+(const std::string_view& v, const FsPath& fp) {
    FsPath r{v};
    return r += fp;
}

static_assert(FsPath::Test("abc"));
static_assert(FsPath::Test(std::string_view{"abc"}));
static_assert(FsPath::Test(std::string{"abc"}));
static_assert(FsPath::Test(FsPath{"abc"}));

static_assert(FsPath::TestFrom("abc"));
static_assert(FsPath::TestFrom(std::string_view{"abc"}));
static_assert(FsPath::TestFrom(std::string{"abc"}));
static_assert(FsPath::TestFrom(FsPath{"abc"}));

// fwd
struct Fs;

struct File {
    ~File();

    Result Read(s64 off, void* buf, u64 read_size, u32 option, u64* bytes_read);
    Result Write(s64 off, const void* buf, u64 write_size, u32 option);
    Result SetSize(s64 sz);
    Result GetSize(s64* out);
    void Close();

    fs::Fs* m_fs{};
    FsFile m_native{};
    std::FILE* m_stdio{};
    s64 m_stdio_off{};
    u32 m_mode{};
};

struct Dir {
    ~Dir();

    Result GetEntryCount(s64* out);
    Result Read(s64 *total_entries, size_t max_entries, FsDirectoryEntry *buf);
    Result ReadAll(std::vector<FsDirectoryEntry>& buf);
    void Close();

    fs::Fs* m_fs{};
    FsDir m_native{};
    DIR* m_stdio{};
    u32 m_mode{};
};

FsPath AppendPath(const fs::FsPath& root_path, const fs::FsPath& file_path);

Result CreateFile(FsFileSystem* fs, const FsPath& path, u64 size = 0, u32 option = 0, bool ignore_read_only = true);
Result CreateDirectory(FsFileSystem* fs, const FsPath& path, bool ignore_read_only = true);
Result CreateDirectoryRecursively(FsFileSystem* fs, const FsPath& path, bool ignore_read_only = true);
Result CreateDirectoryRecursivelyWithPath(FsFileSystem* fs, const FsPath& path, bool ignore_read_only = true);
Result DeleteFile(FsFileSystem* fs, const FsPath& path, bool ignore_read_only = true);
Result DeleteDirectory(FsFileSystem* fs, const FsPath& path, bool ignore_read_only = true);
Result DeleteDirectoryRecursively(FsFileSystem* fs, const FsPath& path, bool ignore_read_only = true);
Result RenameFile(FsFileSystem* fs, const FsPath& src, const FsPath& dst, bool ignore_read_only = true);
Result RenameDirectory(FsFileSystem* fs, const FsPath& src, const FsPath& dst, bool ignore_read_only = true);
Result GetEntryType(FsFileSystem* fs, const FsPath& path, FsDirEntryType* out);
Result GetFileTimeStampRaw(FsFileSystem* fs, const FsPath& path, FsTimeStampRaw *out);
Result SetTimestamp(FsFileSystem* fs, const FsPath& path, const FsTimeStampRaw* ts);
bool FileExists(FsFileSystem* fs, const FsPath& path);
bool DirExists(FsFileSystem* fs, const FsPath& path);
Result read_entire_file(FsFileSystem* fs, const FsPath& path, std::vector<u8>& out);
Result write_entire_file(FsFileSystem* fs, const FsPath& path, const std::vector<u8>& in, bool ignore_read_only = true);
Result copy_entire_file(FsFileSystem* fs, const FsPath& dst, const FsPath& src, bool ignore_read_only = true);

Result CreateFile(const FsPath& path, u64 size = 0, u32 option = 0, bool ignore_read_only = true);
Result CreateDirectory(const FsPath& path, bool ignore_read_only = true);
Result CreateDirectoryRecursively(const FsPath& path, bool ignore_read_only = true);
Result CreateDirectoryRecursivelyWithPath(const FsPath& path, bool ignore_read_only = true);
Result DeleteFile(const FsPath& path, bool ignore_read_only = true);
Result DeleteDirectory(const FsPath& path, bool ignore_read_only = true);
Result DeleteDirectoryRecursively(const FsPath& path, bool ignore_read_only = true);
Result RenameFile(const FsPath& src, const FsPath& dst, bool ignore_read_only = true);
Result RenameDirectory(const FsPath& src, const FsPath& dst, bool ignore_read_only = true);
Result GetEntryType(const FsPath& path, FsDirEntryType* out);
Result GetFileTimeStampRaw(const FsPath& path, FsTimeStampRaw *out);
Result SetTimestamp(const FsPath& path, const FsTimeStampRaw* ts);
bool FileExists(const FsPath& path);
bool DirExists(const FsPath& path);
Result read_entire_file(const FsPath& path, std::vector<u8>& out);
Result write_entire_file(const FsPath& path, const std::vector<u8>& in, bool ignore_read_only = true);
Result copy_entire_file(const FsPath& dst, const FsPath& src, bool ignore_read_only = true);

Result OpenFile(fs::Fs* fs, const fs::FsPath& path, u32 mode, File* f);
Result OpenDirectory(fs::Fs* fs, const fs::FsPath& path, u32 mode, Dir* d);

// opens dir, fetches count for all entries.
// NOTE: this function will be slow on non-native fs, due to multiple
// readdir() functions being needed!
Result DirGetEntryCount(fs::Fs* fs, const fs::FsPath& path, s64* count, u32 mode);
// same as the above, but fetches file and folder count in a single pass
// this is faster when using native, and *much* faster for stdio.
Result DirGetEntryCount(fs::Fs* fs, const fs::FsPath& path, s64* file_count, s64* dir_count, u32 mode = FsDirOpenMode_ReadDirs|FsDirOpenMode_ReadFiles);

// optimised for stdio calls as stat returns size and timestamp in a single call.
// whereas for native, this is 2 function calls.
// however if you need both, you will need 2 calls for native anyway,
// but can avoid the second (expensive) stat call.
Result FileGetSizeAndTimestamp(fs::Fs* fs, const FsPath& path, FsTimeStampRaw* ts, s64* size);
Result IsDirEmpty(fs::Fs* m_fs, const fs::FsPath& path, bool* out);

struct Fs {
    Fs(bool ignore_read_only = true) : m_ignore_read_only{ignore_read_only} {}
    virtual ~Fs() = default;

    virtual Result CreateFile(const FsPath& path, u64 size = 0, u32 option = 0) = 0;
    virtual Result CreateDirectory(const FsPath& path) = 0;
    virtual Result CreateDirectoryRecursively(const FsPath& path) = 0;
    virtual Result CreateDirectoryRecursivelyWithPath(const FsPath& path) = 0;
    virtual Result DeleteFile(const FsPath& path) = 0;
    virtual Result DeleteDirectory(const FsPath& path) = 0;
    virtual Result DeleteDirectoryRecursively(const FsPath& path) = 0;
    virtual Result RenameFile(const FsPath& src, const FsPath& dst) = 0;
    virtual Result RenameDirectory(const FsPath& src, const FsPath& dst) = 0;
    virtual Result GetEntryType(const FsPath& path, FsDirEntryType* out) = 0;
    virtual Result GetFileTimeStampRaw(const FsPath& path, FsTimeStampRaw *out) = 0;
    virtual Result SetTimestamp(const FsPath& path, const FsTimeStampRaw* ts) = 0;
    virtual Result Commit() = 0;
    virtual bool FileExists(const FsPath& path) = 0;
    virtual bool DirExists(const FsPath& path) = 0;
    virtual bool IsNative() const = 0;
    virtual FsPath Root() const { return "/"; }
    virtual Result read_entire_file(const FsPath& path, std::vector<u8>& out) = 0;
    virtual Result write_entire_file(const FsPath& path, const std::vector<u8>& in) = 0;
    virtual Result copy_entire_file(const FsPath& dst, const FsPath& src) = 0;

    Result OpenFile(const fs::FsPath& path, u32 mode, File* f) {
        return fs::OpenFile(this, path, mode, f);
    }
    Result OpenDirectory(const fs::FsPath& path, u32 mode, Dir* d) {
        return fs::OpenDirectory(this, path, mode, d);
    }
    Result DirGetEntryCount(const fs::FsPath& path, s64* count, u32 mode) {
        return fs::DirGetEntryCount(this, path, count, mode);
    }
    Result DirGetEntryCount(const fs::FsPath& path, s64* file_count, s64* dir_count, u32 mode = FsDirOpenMode_ReadDirs|FsDirOpenMode_ReadFiles) {
        return fs::DirGetEntryCount(this, path, file_count, dir_count, mode);
    }
    Result FileGetSizeAndTimestamp(const FsPath& path, FsTimeStampRaw* ts, s64* size) {
        return fs::FileGetSizeAndTimestamp(this, path, ts, size);
    }
    Result IsDirEmpty(const fs::FsPath& path, bool* out) {
        return fs::IsDirEmpty(this, path, out);
    }

    void SetIgnoreReadOnly(bool enable) {
        m_ignore_read_only = enable;
    }

protected:
    bool m_ignore_read_only;
};

struct FsStdio : Fs {
    FsStdio(bool ignore_read_only = true, const FsPath& root = "/") : Fs{ignore_read_only}, m_root{root} {}
    virtual ~FsStdio() = default;

    Result CreateFile(const FsPath& path, u64 size = 0, u32 option = 0) override {
        return fs::CreateFile(path, size, option, m_ignore_read_only);
    }
    Result CreateDirectory(const FsPath& path) override {
        return fs::CreateDirectory(path, m_ignore_read_only);
    }
    Result CreateDirectoryRecursively(const FsPath& path) override {
        return fs::CreateDirectoryRecursively(path, m_ignore_read_only);
    }
    Result CreateDirectoryRecursivelyWithPath(const FsPath& path) override {
        return fs::CreateDirectoryRecursivelyWithPath(path, m_ignore_read_only);
    }
    Result DeleteFile(const FsPath& path) override {
        return fs::DeleteFile(path, m_ignore_read_only);
    }
    Result DeleteDirectory(const FsPath& path) override {
        return fs::DeleteDirectory(path, m_ignore_read_only);
    }
    Result DeleteDirectoryRecursively(const FsPath& path) override {
        return fs::DeleteDirectoryRecursively(path, m_ignore_read_only);
    }
    Result RenameFile(const FsPath& src, const FsPath& dst) override {
        return fs::RenameFile(src, dst, m_ignore_read_only);
    }
    Result RenameDirectory(const FsPath& src, const FsPath& dst) override {
        return fs::RenameDirectory(src, dst, m_ignore_read_only);
    }
    Result GetEntryType(const FsPath& path, FsDirEntryType* out) override {
        return fs::GetEntryType(path, out);
    }
    Result GetFileTimeStampRaw(const FsPath& path, FsTimeStampRaw *out) override {
        return fs::GetFileTimeStampRaw(path, out);
    }
    Result SetTimestamp(const FsPath& path, const FsTimeStampRaw *ts) override {
        return fs::SetTimestamp(path, ts);
    }
    Result Commit() override {
        R_SUCCEED();
    }
    bool FileExists(const FsPath& path) override {
        return fs::FileExists(path);
    }
    bool DirExists(const FsPath& path) override {
        return fs::DirExists(path);
    }
    bool IsNative() const override {
        return false;
    }
    FsPath Root() const override {
        return m_root;
    }
    Result read_entire_file(const FsPath& path, std::vector<u8>& out) override {
        return fs::read_entire_file(path, out);
    }
    Result write_entire_file(const FsPath& path, const std::vector<u8>& in) override {
        return fs::write_entire_file(path, in, m_ignore_read_only);
    }
    Result copy_entire_file(const FsPath& dst, const FsPath& src) override {
        return fs::copy_entire_file(dst, src, m_ignore_read_only);
    }

    const FsPath m_root;
};

struct FsNative : Fs {
    explicit FsNative(bool ignore_read_only = true) : Fs{ignore_read_only} {}
    explicit FsNative(FsFileSystem* fs, bool own, bool ignore_read_only = true) : Fs{ignore_read_only}, m_fs{*fs}, m_own{own} {}

    virtual ~FsNative() {
        if (m_own) {
            fsFsClose(&m_fs);
        }
    }

    Result GetFreeSpace(const FsPath& path, s64* out) {
        return fsFsGetFreeSpace(&m_fs, path, out);
    }

    Result GetTotalSpace(const FsPath& path, s64* out) {
        return fsFsGetTotalSpace(&m_fs, path, out);
    }

    virtual bool IsFsActive() {
        return serviceIsActive(&m_fs.s);
    }

    virtual Result GetFsOpenResult() const {
        return m_open_result;
    }

    Result CreateFile(const FsPath& path, u64 size = 0, u32 option = 0) override {
        return fs::CreateFile(&m_fs, path, size, option, m_ignore_read_only);
    }
    Result CreateDirectory(const FsPath& path) override {
        return fs::CreateDirectory(&m_fs, path, m_ignore_read_only);
    }
    Result CreateDirectoryRecursively(const FsPath& path) override {
        return fs::CreateDirectoryRecursively(&m_fs, path, m_ignore_read_only);
    }
    Result CreateDirectoryRecursivelyWithPath(const FsPath& path) override {
        return fs::CreateDirectoryRecursivelyWithPath(&m_fs, path, m_ignore_read_only);
    }
    Result DeleteFile(const FsPath& path) override {
        return fs::DeleteFile(&m_fs, path, m_ignore_read_only);
    }
    Result DeleteDirectory(const FsPath& path) override {
        return fs::DeleteDirectory(&m_fs, path, m_ignore_read_only);
    }
    Result DeleteDirectoryRecursively(const FsPath& path) override {
        return fs::DeleteDirectoryRecursively(&m_fs, path, m_ignore_read_only);
    }
    Result RenameFile(const FsPath& src, const FsPath& dst) override {
        return fs::RenameFile(&m_fs, src, dst, m_ignore_read_only);
    }
    Result RenameDirectory(const FsPath& src, const FsPath& dst) override {
        return fs::RenameDirectory(&m_fs, src, dst, m_ignore_read_only);
    }
    Result GetEntryType(const FsPath& path, FsDirEntryType* out) override {
        return fs::GetEntryType(&m_fs, path, out);
    }
    Result GetFileTimeStampRaw(const FsPath& path, FsTimeStampRaw *out) override {
        return fs::GetFileTimeStampRaw(&m_fs, path, out);
    }
    Result SetTimestamp(const FsPath& path, const FsTimeStampRaw *ts) override {
        return fs::SetTimestamp(&m_fs, path, ts);
    }
    Result Commit() override {
        return fsFsCommit(&m_fs);
    }
    bool FileExists(const FsPath& path) override {
        return fs::FileExists(&m_fs, path);
    }
    bool DirExists(const FsPath& path) override {
        return fs::DirExists(&m_fs, path);
    }
    bool IsNative() const override {
        return true;
    }
    Result read_entire_file(const FsPath& path, std::vector<u8>& out) override {
        return fs::read_entire_file(&m_fs, path, out);
    }
    Result write_entire_file(const FsPath& path, const std::vector<u8>& in) override {
        return fs::write_entire_file(&m_fs, path, in, m_ignore_read_only);
    }
    Result copy_entire_file(const FsPath& dst, const FsPath& src) override {
        return fs::copy_entire_file(&m_fs, dst, src, m_ignore_read_only);
    }

    FsFileSystem m_fs{};
    Result m_open_result{};
    bool m_own{true};
};

#if 0
struct FsNativeSd final : FsNative {
    FsNativeSd() {
        m_open_result = fsOpenSdCardFileSystem(&m_fs);
    }
};
#else
struct FsNativeSd final : FsNative {
    FsNativeSd(bool ignore_read_only = true) : FsNative{fsdevGetDeviceFileSystem("sdmc:"), false, ignore_read_only} {
        m_open_result = 0;
    }
};
#endif

struct FsNativeBis final : FsNative {
    FsNativeBis(FsBisPartitionId id, const FsPath& string) {
        m_open_result = fsOpenBisFileSystem(&m_fs, id, string);
    }
};

struct FsNativeImage final : FsNative {
    FsNativeImage(FsImageDirectoryId id) {
        m_open_result = fsOpenImageDirectoryFileSystem(&m_fs, id);
    }
};

struct FsNativeContentStorage final : FsNative {
    FsNativeContentStorage(FsContentStorageId id) {
        m_open_result = fsOpenContentStorageFileSystem(&m_fs, id);
    }
};

struct FsNativeGameCard final : FsNative {
    FsNativeGameCard(const FsGameCardHandle* handle, FsGameCardPartition partition) {
        m_open_result = fsOpenGameCardFileSystem(&m_fs, handle, partition);
    }
};

struct FsNativeSave final : FsNative {
    FsNativeSave(FsSaveDataType data_type, FsSaveDataSpaceId save_data_space_id, const FsSaveDataAttribute *attr, bool read_only) {
        if (data_type == FsSaveDataType_System || data_type == FsSaveDataType_SystemBcat) {
            m_open_result = fsOpenSaveDataFileSystemBySystemSaveDataId(&m_fs, FsSaveDataSpaceId_System, attr);
        } else {
            if (read_only) {
                m_open_result = fsOpenReadOnlySaveDataFileSystem(&m_fs, save_data_space_id, attr);
            } else {
                m_open_result = fsOpenSaveDataFileSystem(&m_fs, save_data_space_id, attr);
            }
        }
    }
};

} // namespace fs
