#include "threaded_file_transfer.hpp"
#include "log.hpp"
#include "defines.hpp"
#include "app.hpp"
#include "minizip_helper.hpp"

#include <vector>
#include <algorithm>
#include <cstring>
#include <minizip/unzip.h>
#include <minizip/zip.h>

namespace sphaira::thread {
namespace {

// used for file based emummc and zip/unzip.
constexpr u64 SMALL_BUFFER_SIZE = 1024 * 512;
// used for everything else.
constexpr u64 NORMAL_BUFFER_SIZE = 1024*1024*4;

struct ThreadBuffer {
    ThreadBuffer() {
        buf.reserve(NORMAL_BUFFER_SIZE);
    }

    std::vector<u8> buf;
    s64 off;
};

template<std::size_t Size>
struct RingBuf {
private:
    ThreadBuffer buf[Size]{};
    unsigned r_index{};
    unsigned w_index{};

    static_assert((sizeof(RingBuf::buf) & (sizeof(RingBuf::buf) - 1)) == 0, "Must be power of 2!");

public:
    void ringbuf_reset() {
        this->r_index = this->w_index;
    }

    unsigned ringbuf_capacity() const {
        return sizeof(this->buf) / sizeof(this->buf[0]);
    }

    unsigned ringbuf_size() const {
        return (this->w_index - this->r_index) % (ringbuf_capacity() * 2U);
    }

    unsigned ringbuf_free() const {
        return ringbuf_capacity() - ringbuf_size();
    }

    void ringbuf_push(std::vector<u8>& buf_in, s64 off_in) {
        auto& value = this->buf[this->w_index % ringbuf_capacity()];
        value.off = off_in;
        std::swap(value.buf, buf_in);

        this->w_index = (this->w_index + 1U) % (ringbuf_capacity() * 2U);
    }

    void ringbuf_pop(std::vector<u8>& buf_out, s64& off_out) {
        auto& value = this->buf[this->r_index % ringbuf_capacity()];
        off_out = value.off;
        std::swap(value.buf, buf_out);

        this->r_index = (this->r_index + 1U) % (ringbuf_capacity() * 2U);
    }
};

struct ThreadData {
    ThreadData(ui::ProgressBox* _pbox, s64 size, ReadCallback _rfunc, WriteCallback _wfunc, u64 buffer_size);

    auto GetResults() -> Result;
    void WakeAllThreads();

    void SetReadResult(Result result) {
        read_result = result;
    }

    void SetWriteResult(Result result) {
        write_result = result;
    }

    void SetPullResult(Result result) {
        pull_result = result;
    }

    auto GetWriteOffset() const {
        return write_offset;
    }

    auto GetWriteSize() const {
        return write_size;
    }

    Result Pull(void* data, s64 size, u64* bytes_read);
    Result readFuncInternal();
    Result writeFuncInternal();

private:
    Result SetWriteBuf(std::vector<u8>& buf, s64 size);
    Result GetWriteBuf(std::vector<u8>& buf_out, s64& off_out);
    Result SetPullBuf(std::vector<u8>& buf, s64 size);
    Result GetPullBuf(void* data, s64 size, u64* bytes_read);

    Result Read(void* buf, s64 size, u64* bytes_read);

private:
    // these need to be copied
    ui::ProgressBox* const pbox;
    const ReadCallback rfunc;
    const WriteCallback wfunc;

    // these need to be created
    Mutex mutex{};
    Mutex pull_mutex{};

    CondVar can_read{};
    CondVar can_write{};
    CondVar can_pull{};
    CondVar can_pull_write{};

    RingBuf<2> write_buffers{};
    std::vector<u8> pull_buffer{};
    s64 pull_buffer_offset{};

    const u64 read_buffer_size;
    const s64 write_size;

    // these are shared between threads
    volatile s64 read_offset{};
    volatile s64 write_offset{};

    volatile Result read_result{};
    volatile Result write_result{};
    volatile Result pull_result{};
};

ThreadData::ThreadData(ui::ProgressBox* _pbox, s64 size, ReadCallback _rfunc, WriteCallback _wfunc, u64 buffer_size)
: pbox{_pbox}
, rfunc{_rfunc}
, wfunc{_wfunc}
, read_buffer_size{buffer_size}
, write_size{size} {
    mutexInit(std::addressof(mutex));
    mutexInit(std::addressof(pull_mutex));

    condvarInit(std::addressof(can_read));
    condvarInit(std::addressof(can_write));
    condvarInit(std::addressof(can_pull));
    condvarInit(std::addressof(can_pull_write));
}

auto ThreadData::GetResults() -> Result {
    R_UNLESS(!pbox->ShouldExit(), Result_TransferCancelled);
    R_TRY(read_result);
    R_TRY(write_result);
    R_TRY(pull_result);
    R_SUCCEED();
}

void ThreadData::WakeAllThreads() {
    condvarWakeAll(std::addressof(can_read));
    condvarWakeAll(std::addressof(can_write));
    condvarWakeAll(std::addressof(can_pull));
    condvarWakeAll(std::addressof(can_pull_write));

    mutexUnlock(std::addressof(mutex));
    mutexUnlock(std::addressof(pull_mutex));
}

Result ThreadData::SetWriteBuf(std::vector<u8>& buf, s64 size) {
    buf.resize(size);

    mutexLock(std::addressof(mutex));
    if (!write_buffers.ringbuf_free()) {
        R_TRY(condvarWait(std::addressof(can_read), std::addressof(mutex)));
    }

    ON_SCOPE_EXIT(mutexUnlock(std::addressof(mutex)));
    R_TRY(GetResults());
    write_buffers.ringbuf_push(buf, 0);
    return condvarWakeOne(std::addressof(can_write));
}

Result ThreadData::GetWriteBuf(std::vector<u8>& buf_out, s64& off_out) {
    mutexLock(std::addressof(mutex));
    if (!write_buffers.ringbuf_size()) {
        R_TRY(condvarWait(std::addressof(can_write), std::addressof(mutex)));
    }

    ON_SCOPE_EXIT(mutexUnlock(std::addressof(mutex)));
    R_TRY(GetResults());
    write_buffers.ringbuf_pop(buf_out, off_out);
    return condvarWakeOne(std::addressof(can_read));
}

Result ThreadData::SetPullBuf(std::vector<u8>& buf, s64 size) {
    buf.resize(size);

    mutexLock(std::addressof(pull_mutex));
    if (!pull_buffer.empty()) {
        R_TRY(condvarWait(std::addressof(can_pull_write), std::addressof(pull_mutex)));
    }

    ON_SCOPE_EXIT(mutexUnlock(std::addressof(pull_mutex)));
    R_TRY(GetResults());

    pull_buffer.swap(buf);
    return condvarWakeOne(std::addressof(can_pull));
}

Result ThreadData::GetPullBuf(void* data, s64 size, u64* bytes_read) {
    mutexLock(std::addressof(pull_mutex));
    if (pull_buffer.empty()) {
        R_TRY(condvarWait(std::addressof(can_pull), std::addressof(pull_mutex)));
    }

    ON_SCOPE_EXIT(mutexUnlock(std::addressof(pull_mutex)));
    R_TRY(GetResults());

    *bytes_read = size = std::min<s64>(size, pull_buffer.size() - pull_buffer_offset);
    std::memcpy(data, pull_buffer.data() + pull_buffer_offset, size);
    pull_buffer_offset += size;

    if (pull_buffer_offset == pull_buffer.size()) {
        pull_buffer_offset = 0;
        pull_buffer.clear();
        return condvarWakeOne(std::addressof(can_pull_write));
    } else {
        R_SUCCEED();
    }
}

Result ThreadData::Read(void* buf, s64 size, u64* bytes_read) {
    size = std::min<s64>(size, write_size - read_offset);
    const auto rc = rfunc(buf, read_offset, size, bytes_read);
    read_offset += *bytes_read;
    return rc;
}

Result ThreadData::Pull(void* data, s64 size, u64* bytes_read) {
    return GetPullBuf(data, size, bytes_read);
}

// read thread reads all data from the source
Result ThreadData::readFuncInternal() {
    // the main buffer which data is read into.
    std::vector<u8> buf;
    buf.reserve(this->read_buffer_size);

    while (this->read_offset < this->write_size && R_SUCCEEDED(this->GetResults())) {
        // read more data
        s64 read_size = this->read_buffer_size;

        u64 bytes_read{};
        buf.resize(read_size);
        R_TRY(this->Read(buf.data(), read_size, std::addressof(bytes_read)));
        auto buf_size = bytes_read;

        R_TRY(this->SetWriteBuf(buf, buf_size));
    }

    log_write("finished read thread success!\n");
    R_SUCCEED();
}

// write thread writes data to the nca placeholder.
Result ThreadData::writeFuncInternal() {
    std::vector<u8> buf;
    buf.reserve(this->read_buffer_size);

    while (this->write_offset < this->write_size && R_SUCCEEDED(this->GetResults())) {
        s64 dummy_off;
        R_TRY(this->GetWriteBuf(buf, dummy_off));
        const auto size = buf.size();

        if (!this->wfunc) {
            R_TRY(this->SetPullBuf(buf, buf.size()));
        } else {
            R_TRY(this->wfunc(buf.data(), this->write_offset, buf.size()));
        }

        this->write_offset += size;
    }

    log_write("finished write thread success!\n");
    R_SUCCEED();
}

void readFunc(void* d) {
    auto t = static_cast<ThreadData*>(d);
    t->SetReadResult(t->readFuncInternal());
    log_write("read thread returned now\n");
}

void writeFunc(void* d) {
    auto t = static_cast<ThreadData*>(d);
    t->SetWriteResult(t->writeFuncInternal());
    log_write("write thread returned now\n");
}

auto GetAlternateCore(int id) {
    return id == 1 ? 2 : 1;
}

Result TransferInternal(ui::ProgressBox* pbox, s64 size, ReadCallback rfunc, WriteCallback wfunc, StartCallback2 sfunc, Mode mode, u64 buffer_size = NORMAL_BUFFER_SIZE) {
    const auto is_file_based_emummc = App::IsFileBaseEmummc();

    if (is_file_based_emummc) {
        buffer_size = SMALL_BUFFER_SIZE;
    }

    if (mode == Mode::SingleThreadedIfSmaller) {
        if (size <= buffer_size) {
            mode = Mode::SingleThreaded;
        } else {
            mode = Mode::MultiThreaded;
        }
    }

    // single threaded pull buffer is not supported.
    R_UNLESS(mode != Mode::MultiThreaded || !sfunc, 0x1);

    // todo: support single threaded pull buffer.
    if (mode == Mode::SingleThreaded) {
        std::vector<u8> buf(buffer_size);

        s64 offset{};
        while (offset < size) {
            R_TRY(pbox->ShouldExitResult());

            u64 bytes_read;
            const auto rsize = std::min<s64>(buf.size(), size - offset);
            R_TRY(rfunc(buf.data(), offset, rsize, &bytes_read));
            R_TRY(wfunc(buf.data(), offset, bytes_read));

            offset += bytes_read;
            pbox->UpdateTransfer(offset, size);
        }

        R_SUCCEED();
    }
    else {
        const auto WRITE_THREAD_CORE = sfunc ? pbox->GetCpuId() : GetAlternateCore(pbox->GetCpuId());
        const auto READ_THREAD_CORE = GetAlternateCore(WRITE_THREAD_CORE);

        ThreadData t_data{pbox, size, rfunc, wfunc, buffer_size};

        Thread t_read{};
        R_TRY(threadCreate(&t_read, readFunc, std::addressof(t_data), nullptr, 1024*256, 0x3B, READ_THREAD_CORE));
        ON_SCOPE_EXIT(threadClose(&t_read));

        Thread t_write{};
        R_TRY(threadCreate(&t_write, writeFunc, std::addressof(t_data), nullptr, 1024*256, 0x3B, WRITE_THREAD_CORE));
        ON_SCOPE_EXIT(threadClose(&t_write));

        const auto start_threads = [&]() -> Result {
            log_write("starting threads\n");
            R_TRY(threadStart(std::addressof(t_read)));
            R_TRY(threadStart(std::addressof(t_write)));
            R_SUCCEED();
        };

        ON_SCOPE_EXIT(threadWaitForExit(std::addressof(t_read)));
        ON_SCOPE_EXIT(threadWaitForExit(std::addressof(t_write)));

        if (sfunc) {
            log_write("[THREAD] doing sfuncn\n");
            t_data.SetPullResult(sfunc(start_threads, [&](void* data, s64 size, u64* bytes_read) -> Result {
                R_TRY(t_data.GetResults());
                return t_data.Pull(data, size, bytes_read);
            }));
        }
        else {
            log_write("[THREAD] doing normal\n");
            R_TRY(start_threads());
            log_write("[THREAD] started threads\n");

            while (t_data.GetWriteOffset() != t_data.GetWriteSize() && R_SUCCEEDED(t_data.GetResults())) {
                pbox->UpdateTransfer(t_data.GetWriteOffset(), t_data.GetWriteSize());
                svcSleepThread(1e+6);
            }
        }

        // wait for all threads to close.
        log_write("waiting for threads to close\n");
        for (;;) {
            t_data.WakeAllThreads();
            pbox->Yield();

            if (R_FAILED(waitSingleHandle(t_read.handle, 1000))) {
                continue;
            } else if (R_FAILED(waitSingleHandle(t_write.handle, 1000))) {
                continue;
            }
            break;
        }
        log_write("threads closed\n");

        // if any of the threads failed, wake up all threads so they can exit.
        if (R_FAILED(t_data.GetResults())) {
            log_write("some reads failed, waking threads\n");
            log_write("returning due to fail\n");
            return t_data.GetResults();
        }

        log_write("returning from thread func\n");
        return t_data.GetResults();
    }
}

} // namespace

Result Transfer(ui::ProgressBox* pbox, s64 size, ReadCallback rfunc, WriteCallback wfunc, Mode mode) {
    return TransferInternal(pbox, size, rfunc, wfunc, nullptr, mode);
}

Result TransferPull(ui::ProgressBox* pbox, s64 size, ReadCallback rfunc, StartCallback sfunc, Mode mode) {
    return TransferInternal(pbox, size, rfunc, nullptr, [sfunc](StartThreadCallback start, PullCallback pull) -> Result {
        R_TRY(start());
        return sfunc(pull);
    }, mode);
}

Result TransferPull(ui::ProgressBox* pbox, s64 size, ReadCallback rfunc, StartCallback2 sfunc, Mode mode) {
    return TransferInternal(pbox, size, rfunc, nullptr, sfunc, mode);
}

Result TransferUnzip(ui::ProgressBox* pbox, void* zfile, fs::Fs* fs, const fs::FsPath& path, s64 size, u32 crc32, Mode mode) {
    Result rc;
    if (R_FAILED(rc = fs->CreateDirectoryRecursivelyWithPath(path)) && rc != FsError_PathAlreadyExists) {
        log_write("failed to create folder: %s 0x%04X\n", path.s, rc);
        R_THROW(rc);
    }

    if (R_FAILED(rc = fs->CreateFile(path, size, 0)) && rc != FsError_PathAlreadyExists) {
        log_write("failed to create file: %s 0x%04X\n", path.s, rc);
        R_THROW(rc);
    }

    fs::File f;
    R_TRY(fs->OpenFile(path, FsOpenMode_Write, &f));

    // only update the size if this is an existing file.
    if (rc == FsError_PathAlreadyExists) {
        R_TRY(f.SetSize(size));
    }

    // NOTES: do not use temp file with rename / delete after as it massively slows
    // down small file transfers (RA 21s -> 50s).
    u32 crc32_out{};
    R_TRY(thread::TransferInternal(pbox, size,
        [&](void* data, s64 off, s64 size, u64* bytes_read) -> Result {
            const auto result = unzReadCurrentFile(zfile, data, size);
            if (result <= 0) {
                log_write("failed to read zip file: %s %d\n", path.s, result);
                R_THROW(Result_UnzReadCurrentFile);
            }

            if (crc32) {
                crc32_out = crc32CalculateWithSeed(crc32_out, data, result);
            }

            *bytes_read = result;
            R_SUCCEED();
        },
        [&](const void* data, s64 off, s64 size) -> Result {
            return f.Write(off, data, size, FsWriteOption_None);
        },
        nullptr, mode, SMALL_BUFFER_SIZE
    ));

    // validate crc32 (if set in the info).
    R_UNLESS(!crc32 || crc32 == crc32_out, 0x8);

    R_SUCCEED();
}

Result TransferZip(ui::ProgressBox* pbox, void* zfile, fs::Fs* fs, const fs::FsPath& path, u32* crc32, Mode mode) {
    fs::File f;
    R_TRY(fs->OpenFile(path, FsOpenMode_Read, &f));

    s64 file_size;
    R_TRY(f.GetSize(&file_size));

    if (crc32) {
        *crc32 = 0;
    }

    return thread::TransferInternal(pbox, file_size,
        [&](void* data, s64 off, s64 size, u64* bytes_read) -> Result {
            const auto rc = f.Read(off, data, size, FsReadOption_None, bytes_read);
            if (R_SUCCEEDED(rc) && crc32) {
                *crc32 = crc32CalculateWithSeed(*crc32, data, *bytes_read);
            }
            return rc;
        },
        [&](const void* data, s64 off, s64 size) -> Result {
            if (ZIP_OK != zipWriteInFileInZip(zfile, data, size)) {
                log_write("failed to write zip file: %s\n", path.s);
                R_THROW(Result_ZipWriteInFileInZip);
            }
            R_SUCCEED();
        },
        nullptr, mode, SMALL_BUFFER_SIZE
    );
}

Result TransferUnzipAll(ui::ProgressBox* pbox, void* zfile, fs::Fs* fs, const fs::FsPath& base_path, UnzipAllFilter filter, Mode mode) {
    unz_global_info64 ginfo;
    if (UNZ_OK != unzGetGlobalInfo64(zfile, &ginfo)) {
        R_THROW(Result_UnzGetGlobalInfo64);
    }

    if (UNZ_OK != unzGoToFirstFile(zfile)) {
        R_THROW(Result_UnzGoToFirstFile);
    }

    for (s64 i = 0; i < ginfo.number_entry; i++) {
        R_TRY(pbox->ShouldExitResult());

        if (i > 0) {
            if (UNZ_OK != unzGoToNextFile(zfile)) {
                log_write("failed to unzGoToNextFile\n");
                R_THROW(Result_UnzGoToNextFile);
            }
        }

        if (UNZ_OK != unzOpenCurrentFile(zfile)) {
            log_write("failed to open current file\n");
            R_THROW(Result_UnzOpenCurrentFile);
        }
        ON_SCOPE_EXIT(unzCloseCurrentFile(zfile));

        unz_file_info64 info;
        fs::FsPath name;
        if (UNZ_OK != unzGetCurrentFileInfo64(zfile, &info, name, sizeof(name), 0, 0, 0, 0)) {
            log_write("failed to get current info\n");
            R_THROW(Result_UnzGetCurrentFileInfo64);
        }

        // check if we should skip this file.
        // don't make const as to allow the function to modify the path
        // this function is used for the updater to change sphaira.nro to exe path.
        auto path = fs::AppendPath(base_path, name);
        if (filter && !filter(name, path)) {
            continue;
        }

        pbox->NewTransfer(name);

        if (path[std::strlen(path) -1] == '/') {
            Result rc;
            if (R_FAILED(rc = fs->CreateDirectoryRecursively(path)) && rc != FsError_PathAlreadyExists) {
                log_write("failed to create folder: %s 0x%04X\n", path.s, rc);
                R_THROW(rc);
            }
        } else {
            R_TRY(TransferUnzip(pbox, zfile, fs, path, info.uncompressed_size, info.crc, mode));
        }
    }

    R_SUCCEED();
}

Result TransferUnzipAll(ui::ProgressBox* pbox, const fs::FsPath& zip_out, fs::Fs* fs, const fs::FsPath& base_path, UnzipAllFilter filter, Mode mode) {
    zlib_filefunc64_def file_func;
    mz::FileFuncStdio(&file_func);

    auto zfile = unzOpen2_64(zip_out, &file_func);
    R_UNLESS(zfile, Result_UnzOpen2_64);
    ON_SCOPE_EXIT(unzClose(zfile));

    return TransferUnzipAll(pbox, zfile, fs, base_path, filter, mode);
}

} // namespace::thread
