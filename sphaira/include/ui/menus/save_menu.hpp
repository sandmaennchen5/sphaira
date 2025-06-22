#pragma once

#include "ui/menus/grid_menu_base.hpp"
#include "ui/list.hpp"
#include "title_info.hpp"
#include "fs.hpp"
#include "option.hpp"
#include "dumper.hpp"
#include <memory>
#include <vector>
#include <span>

namespace sphaira::ui::menu::save {

struct Entry final : FsSaveDataInfo {
    NacpLanguageEntry lang{};
    int image{};
    bool selected{};

    std::shared_ptr<title::ThreadResultData> info{};
    title::NacpLoadStatus status{title::NacpLoadStatus::None};

    auto GetName() const -> const char* {
        return lang.name;
    }

    auto GetAuthor() const -> const char* {
        return lang.author;
    }
};

enum SortType {
    SortType_Updated,
};

enum OrderType {
    OrderType_Descending,
    OrderType_Ascending,
};

using LayoutType = grid::LayoutType;

void SignalChange();

struct Menu final : grid::Menu {
    Menu(u32 flags);
    ~Menu();

    auto GetShortTitle() const -> const char* override { return "Saves"; };
    void Update(Controller* controller, TouchInfo* touch) override;
    void Draw(NVGcontext* vg, Theme* theme) override;
    void OnFocusGained() override;

private:
    void SetIndex(s64 index);
    void ScanHomebrew();
    void Sort();
    void SortAndFindLastFile(bool scan);
    void FreeEntries();
    void OnLayoutChange();

    auto GetSelectedEntries() const {
        std::vector<Entry> out;
        for (auto& e : m_entries) {
            if (e.selected) {
                out.emplace_back(e);
            }
        }

        if (!m_entries.empty() && out.empty()) {
            out.emplace_back(m_entries[m_index]);
        }

        return out;
    }

    void ClearSelection() {
        for (auto& e : m_entries) {
            e.selected = false;
        }

        m_selected_count = 0;
    }

    void BackupSaves(std::vector<std::reference_wrapper<Entry>>& entries);
    void RestoreSave();

    auto BuildSavePath(const Entry& e, bool is_auto) const -> fs::FsPath;
    Result RestoreSaveInternal(ProgressBox* pbox, const Entry& e, const fs::FsPath& path) const;
    Result BackupSaveInternal(ProgressBox* pbox, const dump::DumpLocation& location, const Entry& e, bool compressed, bool is_auto = false) const;

private:
    static constexpr inline const char* INI_SECTION = "saves";

    std::vector<Entry> m_entries{};
    s64 m_index{}; // where i am in the array
    s64 m_selected_count{};
    std::unique_ptr<List> m_list{};
    bool m_is_reversed{};
    bool m_dirty{};

    std::vector<AccountProfileBase> m_accounts{};
    s64 m_account_index{};
    u8 m_data_type{FsSaveDataType_Account};

    option::OptionLong m_sort{INI_SECTION, "sort", SortType::SortType_Updated};
    option::OptionLong m_order{INI_SECTION, "order", OrderType::OrderType_Descending};
    option::OptionLong m_layout{INI_SECTION, "layout", LayoutType::LayoutType_Grid};
    option::OptionBool m_auto_backup_on_restore{INI_SECTION, "auto_backup_on_restore", true};
    option::OptionBool m_compress_save_backup{INI_SECTION, "compress_save_backup", true};
};

} // namespace sphaira::ui::menu::save
