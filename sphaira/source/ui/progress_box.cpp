#include "ui/progress_box.hpp"
#include "ui/option_box.hpp"
#include "ui/nvg_util.hpp"
#include "app.hpp"
#include "defines.hpp"
#include "log.hpp"
#include "threaded_file_transfer.hpp"
#include "i18n.hpp"
#include <cstring>

namespace sphaira::ui {
namespace {

void threadFunc(void* arg) {
    auto d = static_cast<ProgressBox::ThreadData*>(arg);
    d->result = d->callback(d->pbox);
    d->pbox->RequestExit();
}

} // namespace

ProgressBox::ProgressBox(int image, const std::string& action, const std::string& title, ProgressBoxCallback callback, ProgressBoxDoneCallback done, int cpuid, int prio, int stack_size) {
    if (App::GetApp()->m_progress_boost_mode.Get()) {
        App::SetBoostMode(true);
    }

    SetAction(Button::B, Action{"Back"_i18n, [this](){
        App::Push(std::make_shared<OptionBox>("Are you sure you wish to cancel?"_i18n, "No"_i18n, "Yes"_i18n, 1, [this](auto op_index){
            if (op_index && *op_index) {
                RequestExit();
                SetPop();
            }
        }));
    }});

    m_pos.w = 770.f;
    m_pos.h = 295.f;
    m_pos.x = (SCREEN_WIDTH / 2.f) - (m_pos.w / 2.f);
    m_pos.y = (SCREEN_HEIGHT / 2.f) - (m_pos.h / 2.f);

    m_done = done;
    m_title = title;
    m_action = action;
    m_image = image;

    m_cpuid = cpuid;
    m_thread_data.pbox = this;
    m_thread_data.callback = callback;
    if (R_FAILED(threadCreate(&m_thread, threadFunc, &m_thread_data, nullptr, stack_size, prio, cpuid))) {
        log_write("failed to create thead\n");
    }
    if (R_FAILED(threadStart(&m_thread))) {
        log_write("failed to start thread\n");
    }
}

ProgressBox::~ProgressBox() {
    m_stop_source.request_stop();

    if (R_FAILED(threadWaitForExit(&m_thread))) {
        log_write("failed to join thread\n");
    }
    if (R_FAILED(threadClose(&m_thread))) {
        log_write("failed to close thread\n");
    }

    FreeImage();
    m_done(m_thread_data.result);

    App::SetBoostMode(false);
}

auto ProgressBox::Update(Controller* controller, TouchInfo* touch) -> void {
    Widget::Update(controller, touch);

    if (ShouldExit()) {
        SetPop();
    }
}

auto ProgressBox::Draw(NVGcontext* vg, Theme* theme) -> void {
    mutexLock(&m_mutex);
    std::vector<u8> image_data{};
    std::swap(m_image_data, image_data);
    if (m_timestamp.GetSeconds()) {
        m_timestamp.Update();
        m_speed = m_offset - m_last_offset;
        m_last_offset = m_offset;
    }

    const auto action = m_action;
    const auto title = m_title;
    const auto transfer = m_transfer;
    const auto size = m_size;
    const auto offset = m_offset;
    const auto speed = m_speed;
    const auto last_offset = m_last_offset;
    auto image = m_image;

    if (m_is_image_pending) {
        FreeImage();
        image = m_image = m_image_pending;
        m_image_pending = 0;
        m_is_image_pending = false;
    }
    mutexUnlock(&m_mutex);

    if (!image_data.empty()) {
        FreeImage();
        image = m_image = nvgCreateImageMem(vg, 0, image_data.data(), image_data.size());
        m_own_image = true;
    }

    gfx::dimBackground(vg);
    gfx::drawRect(vg, m_pos, theme->GetColour(ThemeEntryID_POPUP), 5);

    // The pop up shape.
    // const Vec4 box = { 255, 145, 770, 430 };
    const auto center_x = m_pos.x + m_pos.w/2;
    const auto end_y = m_pos.y + m_pos.h;
    const auto progress_bar_w = m_pos.w - 230;
    const Vec4 prog_bar = { center_x - progress_bar_w / 2, end_y - 100, progress_bar_w, 12 };

    nvgSave(vg);
    nvgIntersectScissor(vg, GetX(), GetY(), GetW(), GetH());

    if (image) {
        gfx::drawImage(vg, GetX() + 25, GetY() + 25, 120, 120, image, 5);
    }

    // shapes.
    if (offset && size) {
        const auto font_size = 18.F;
        const auto pad = 15.F;
        const float rounding = 5;

        gfx::drawRect(vg, prog_bar, theme->GetColour(ThemeEntryID_PROGRESSBAR_BACKGROUND), rounding);
        const u32 percentage = ((double)offset / (double)size) * 100.0;
        gfx::drawRect(vg, prog_bar.x, prog_bar.y, ((float)offset / (float)size) * prog_bar.w, prog_bar.h, theme->GetColour(ThemeEntryID_PROGRESSBAR), rounding);
        gfx::drawTextArgs(vg, prog_bar.x + prog_bar.w + pad, prog_bar.y, font_size, NVG_ALIGN_LEFT | NVG_ALIGN_TOP, theme->GetColour(ThemeEntryID_TEXT), "%u%%", percentage);

        const double speed_mb = (double)speed / (1024.0 * 1024.0);
        const double speed_kb = (double)speed / (1024.0);

        char speed_str[32];
        if (speed_mb >= 0.01) {
            std::snprintf(speed_str, sizeof(speed_str), "%.2f MiB/s", speed_mb);
        } else {
            std::snprintf(speed_str, sizeof(speed_str), "%.2f KiB/s", speed_kb);
        }

        const auto left = size - last_offset;
        const auto left_seconds = left / speed;
        const auto hours = left_seconds / (60 * 60);
        const auto minutes = left_seconds % (60 * 60) / 60;
        const auto seconds = left_seconds % 60;

        char time_str[64];
        if (hours) {
            std::snprintf(time_str, sizeof(time_str), "%zu hours %zu minutes remaining"_i18n.c_str(), hours, minutes);
        } else if (minutes) {
            std::snprintf(time_str, sizeof(time_str), "%zu minutes %zu seconds remaining"_i18n.c_str(), minutes, seconds);
        } else {
            std::snprintf(time_str, sizeof(time_str), "%zu seconds remaining"_i18n.c_str(), seconds);
        }

        gfx::drawTextArgs(vg, center_x, prog_bar.y + prog_bar.h + 30, 18, NVG_ALIGN_CENTER | NVG_ALIGN_TOP, theme->GetColour(ThemeEntryID_TEXT), "%s (%s)", time_str, speed_str);
    }

    gfx::drawTextArgs(vg, center_x, m_pos.y + 40, 24, NVG_ALIGN_CENTER | NVG_ALIGN_TOP, theme->GetColour(ThemeEntryID_TEXT), action.c_str());

    const auto draw_text = [&](ScrollingText& scroll, const std::string& txt, float y, float size, float pad, ThemeEntryID id){
        float bounds[4];
        nvgFontSize(vg, size);
        gfx::textBounds(vg, 0, 0, bounds, txt.c_str());

        const auto min_x = GetX() + pad;
        const auto title_x = std::max(min_x, center_x - (bounds[2] - bounds[0]) / 2);

        scroll.Draw(vg, true, title_x, y, GetW() - pad * 2, size, NVG_ALIGN_LEFT | NVG_ALIGN_TOP, theme->GetColour(id), txt.c_str());
    };

    draw_text(m_scroll_title, title, m_pos.y + 100, 22, 160, ThemeEntryID_TEXT);
    if (!transfer.empty()) {
        draw_text(m_scroll_transfer, transfer, m_pos.y + 160, 18, 30, ThemeEntryID_TEXT_INFO);
    }

    nvgRestore(vg);
}

auto ProgressBox::SetActionName(const std::string& action)  -> ProgressBox& {
    mutexLock(&m_mutex);
    m_action = action;
    mutexUnlock(&m_mutex);
    Yield();
    return *this;
}

auto ProgressBox::SetTitle(const std::string& title)  -> ProgressBox& {
    mutexLock(&m_mutex);
    m_title = title;
    mutexUnlock(&m_mutex);
    Yield();
    return *this;
}

auto ProgressBox::NewTransfer(const std::string& transfer)  -> ProgressBox& {
    mutexLock(&m_mutex);
    m_transfer = transfer;
    m_size = 0;
    m_offset = 0;
    m_last_offset = 0;
    m_timestamp.Update();
    mutexUnlock(&m_mutex);
    Yield();
    return *this;
}

auto ProgressBox::UpdateTransfer(s64 offset, s64 size)  -> ProgressBox& {
    mutexLock(&m_mutex);
    m_size = size;
    m_offset = offset;
    mutexUnlock(&m_mutex);
    Yield();
    return *this;
}

auto ProgressBox::SetImage(int image) -> ProgressBox& {
    mutexLock(&m_mutex);
    m_image_pending = image;
    m_is_image_pending = true;
    mutexUnlock(&m_mutex);
    return *this;
}

auto ProgressBox::SetImageData(std::vector<u8>& data) -> ProgressBox& {
    mutexLock(&m_mutex);
    std::swap(m_image_data, data);
    mutexUnlock(&m_mutex);
    return *this;
}

auto ProgressBox::SetImageDataConst(std::span<const u8> data) -> ProgressBox& {
    mutexLock(&m_mutex);
    m_image_data.resize(data.size());
    std::memcpy(m_image_data.data(), data.data(), m_image_data.size());
    mutexUnlock(&m_mutex);
    return *this;
}

void ProgressBox::RequestExit() {
    m_stop_source.request_stop();
}

auto ProgressBox::ShouldExit() -> bool {
    return m_stop_source.stop_requested();
}

auto ProgressBox::ShouldExitResult() -> Result {
    if (ShouldExit()) {
        R_THROW(Result_TransferCancelled);
    }
    R_SUCCEED();
}

auto ProgressBox::CopyFile(fs::Fs* fs_src, fs::Fs* fs_dst, const fs::FsPath& src_path, const fs::FsPath& dst_path, bool single_threaded) -> Result {
    const auto is_file_based_emummc = App::IsFileBaseEmummc();
    const auto is_both_native = fs_src->IsNative() && fs_dst->IsNative();

    fs::File src_file;
    R_TRY(fs_src->OpenFile(src_path, FsOpenMode_Read, &src_file));

    s64 src_size;
    R_TRY(src_file.GetSize(&src_size));

    // this can fail if it already exists so we ignore the result.
    // if the file actually failed to be created, the result is implicitly
    // handled when we try and open it for writing.
    fs_dst->CreateFile(dst_path, src_size, 0);

    fs::File dst_file;
    R_TRY(fs_dst->OpenFile(dst_path, FsOpenMode_Write, &dst_file));
    R_TRY(dst_file.SetSize(src_size));

    R_TRY(thread::Transfer(this, src_size,
        [&](void* data, s64 off, s64 size, u64* bytes_read) -> Result {
            const auto rc = src_file.Read(off, data, size, 0, bytes_read);

            if (is_both_native && is_file_based_emummc) {
                svcSleepThread(2e+6); // 2ms
            }

            return rc;
        },
        [&](const void* data, s64 off, s64 size) -> Result {
            const auto rc = dst_file.Write(off, data, size, 0);

            if (is_both_native && is_file_based_emummc) {
                svcSleepThread(2e+6); // 2ms
            }

            return rc;
        }, single_threaded ? thread::Mode::SingleThreaded : thread::Mode::MultiThreaded
    ));

    R_SUCCEED();
}

auto ProgressBox::CopyFile(fs::Fs* fs, const fs::FsPath& src_path, const fs::FsPath& dst_path, bool single_threaded) -> Result {
    return CopyFile(fs, fs, src_path, dst_path, single_threaded);
}

auto ProgressBox::CopyFile(const fs::FsPath& src_path, const fs::FsPath& dst_path, bool single_threaded) -> Result {
    fs::FsNativeSd fs;
    R_TRY(fs.GetFsOpenResult());
    return CopyFile(&fs, src_path, dst_path, single_threaded);
}

void ProgressBox::Yield() {
    svcSleepThread(YieldType_WithoutCoreMigration);
}

void ProgressBox::FreeImage() {
    if (m_image && m_own_image) {
        nvgDeleteImage(App::GetVg(), m_image);
    }

    m_image = 0;
    m_own_image = false;
}

} // namespace sphaira::ui
