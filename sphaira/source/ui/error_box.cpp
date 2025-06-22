#include "ui/error_box.hpp"
#include "ui/nvg_util.hpp"
#include "app.hpp"
#include "i18n.hpp"

namespace sphaira::ui {
namespace {

auto GetCodeMessage(Result rc) -> const char* {
    switch (rc) {
        case FsError_PathNotFound: return "FsError_PathNotFound";
        case FsError_PathAlreadyExists: return "FsError_PathAlreadyExists";
        case FsError_TargetLocked: return "FsError_TargetLocked";

        case Result_TransferCancelled: return "SphairaError_TransferCancelled";
        case Result_StreamBadSeek: return "SphairaError_StreamBadSeek";
        case Result_FsTooManyEntries: return "SphairaError_FsTooManyEntries";
        case Result_FsNewPathTooLarge: return "SphairaError_FsNewPathTooLarge";
        case Result_FsInvalidType: return "SphairaError_FsInvalidType";
        case Result_FsEmpty: return "SphairaError_FsEmpty";
        case Result_FsAlreadyRoot: return "SphairaError_FsAlreadyRoot";
        case Result_FsNoCurrentPath: return "SphairaError_FsNoCurrentPath";
        case Result_FsBrokenCurrentPath: return "SphairaError_FsBrokenCurrentPath";
        case Result_FsIndexOutOfBounds: return "SphairaError_FsIndexOutOfBounds";
        case Result_FsFsNotActive: return "SphairaError_FsFsNotActive";
        case Result_FsNewPathEmpty: return "SphairaError_FsNewPathEmpty";
        case Result_FsLoadingCancelled: return "SphairaError_FsLoadingCancelled";
        case Result_FsBrokenRoot: return "SphairaError_FsBrokenRoot";
        case Result_FsUnknownStdioError: return "SphairaError_FsUnknownStdioError";
        case Result_FsReadOnly: return "SphairaError_FsReadOnly";
        case Result_FsNotActive: return "SphairaError_FsNotActive";
        case Result_FsFailedStdioStat: return "SphairaError_FsFailedStdioStat";
        case Result_FsFailedStdioOpendir: return "SphairaError_FsFailedStdioOpendir";
        case Result_NroBadMagic: return "SphairaError_NroBadMagic";
        case Result_NroBadSize: return "SphairaError_NroBadSize";
        case Result_AppFailedMusicDownload: return "SphairaError_AppFailedMusicDownload";
        case Result_CurlFailedEasyInit: return "SphairaError_CurlFailedEasyInit";
        case Result_DumpFailedNetworkUpload: return "SphairaError_DumpFailedNetworkUpload";
        case Result_UnzOpen2_64: return "SphairaError_UnzOpen2_64";
        case Result_UnzGetGlobalInfo64: return "SphairaError_UnzGetGlobalInfo64";
        case Result_UnzLocateFile: return "SphairaError_UnzLocateFile";
        case Result_UnzGoToFirstFile: return "SphairaError_UnzGoToFirstFile";
        case Result_UnzGoToNextFile: return "SphairaError_UnzGoToNextFile";
        case Result_UnzOpenCurrentFile: return "SphairaError_UnzOpenCurrentFile";
        case Result_UnzGetCurrentFileInfo64: return "SphairaError_UnzGetCurrentFileInfo64";
        case Result_UnzReadCurrentFile: return "SphairaError_UnzReadCurrentFile";
        case Result_ZipOpen2_64: return "SphairaError_ZipOpen2_64";
        case Result_ZipOpenNewFileInZip: return "SphairaError_ZipOpenNewFileInZip";
        case Result_ZipWriteInFileInZip: return "SphairaError_ZipWriteInFileInZip";
        case Result_FileBrowserFailedUpload: return "SphairaError_FileBrowserFailedUpload";
        case Result_FileBrowserDirNotDaybreak: return "SphairaError_FileBrowserDirNotDaybreak";
        case Result_AppstoreFailedZipDownload: return "SphairaError_AppstoreFailedZipDownload";
        case Result_AppstoreFailedMd5: return "SphairaError_AppstoreFailedMd5";
        case Result_AppstoreFailedParseManifest: return "SphairaError_AppstoreFailedParseManifest";
        case Result_GameBadReadForDump: return "SphairaError_GameBadReadForDump";
        case Result_GameEmptyMetaEntries: return "SphairaError_GameEmptyMetaEntries";
        case Result_GameMultipleKeysFound: return "SphairaError_GameMultipleKeysFound";
        case Result_GameNoNspEntriesBuilt: return "SphairaError_GameNoNspEntriesBuilt";
        case Result_KeyMissingNcaKeyArea: return "SphairaError_KeyMissingNcaKeyArea";
        case Result_KeyMissingTitleKek: return "SphairaError_KeyMissingTitleKek";
        case Result_KeyMissingMasterKey: return "SphairaError_KeyMissingMasterKey";
        case Result_KeyFailedDecyptETicketDeviceKey: return "SphairaError_KeyFailedDecyptETicketDeviceKey";
        case Result_NcaFailedNcaHeaderHashVerify: return "SphairaError_NcaFailedNcaHeaderHashVerify";
        case Result_NcaBadSigKeyGen: return "SphairaError_NcaBadSigKeyGen";
        case Result_GcBadReadForDump: return "SphairaError_GcBadReadForDump";
        case Result_GcEmptyGamecard: return "SphairaError_GcEmptyGamecard";
        case Result_GcBadXciMagic: return "SphairaError_GcBadXciMagic";
        case Result_GcBadXciRomSize: return "SphairaError_GcBadXciRomSize";
        case Result_GcFailedToGetSecurityInfo: return "SphairaError_GcFailedToGetSecurityInfo";
        case Result_GhdlEmptyAsset: return "SphairaError_GhdlEmptyAsset";
        case Result_GhdlFailedToDownloadAsset: return "SphairaError_GhdlFailedToDownloadAsset";
        case Result_GhdlFailedToDownloadAssetJson: return "SphairaError_GhdlFailedToDownloadAssetJson";
        case Result_ThemezerFailedToDownloadThemeMeta: return "SphairaError_ThemezerFailedToDownloadThemeMeta";
        case Result_ThemezerFailedToDownloadTheme: return "SphairaError_ThemezerFailedToDownloadTheme";
        case Result_MainFailedToDownloadUpdate: return "SphairaError_MainFailedToDownloadUpdate";
        case Result_UsbDsBadDeviceSpeed: return "SphairaError_UsbDsBadDeviceSpeed";
        case Result_NspBadMagic: return "SphairaError_NspBadMagic";
        case Result_XciBadMagic: return "SphairaError_XciBadMagic";
        case Result_EsBadTitleKeyType: return "SphairaError_EsBadTitleKeyType";
        case Result_EsPersonalisedTicketDeviceIdMissmatch: return "SphairaError_EsPersonalisedTicketDeviceIdMissmatch";
        case Result_EsFailedDecryptPersonalisedTicket: return "SphairaError_EsFailedDecryptPersonalisedTicket";
        case Result_EsBadDecryptedPersonalisedTicketSize: return "SphairaError_EsBadDecryptedPersonalisedTicketSize";
        case Result_EsInvalidTicketBadRightsId: return "SphairaError_EsInvalidTicketBadRightsId";
        case Result_EsInvalidTicketFromatVersion: return "SphairaError_EsInvalidTicketFromatVersion";
        case Result_EsInvalidTicketKeyType: return "SphairaError_EsInvalidTicketKeyType";
        case Result_EsInvalidTicketKeyRevision: return "SphairaError_EsInvalidTicketKeyRevision";
        case Result_OwoBadArgs: return "SphairaError_OwoBadArgs";
        case Result_UsbCancelled: return "SphairaError_UsbCancelled";
        case Result_UsbBadMagic: return "SphairaError_UsbBadMagic";
        case Result_UsbBadVersion: return "SphairaError_UsbBadVersion";
        case Result_UsbBadCount: return "SphairaError_UsbBadCount";
        case Result_UsbBadTransferSize: return "SphairaError_UsbBadTransferSize";
        case Result_UsbBadTotalSize: return "SphairaError_UsbBadTotalSize";
        case Result_UsbUploadBadMagic: return "SphairaError_UsbUploadBadMagic";
        case Result_UsbUploadExit: return "SphairaError_UsbUploadExit";
        case Result_UsbUploadBadCount: return "SphairaError_UsbUploadBadCount";
        case Result_UsbUploadBadTransferSize: return "SphairaError_UsbUploadBadTransferSize";
        case Result_UsbUploadBadTotalSize: return "SphairaError_UsbUploadBadTotalSize";
        case Result_UsbUploadBadCommand: return "SphairaError_UsbUploadBadCommand";
        case Result_YatiContainerNotFound: return "SphairaError_YatiContainerNotFound";
        case Result_YatiNcaNotFound: return "SphairaError_YatiNcaNotFound";
        case Result_YatiInvalidNcaReadSize: return "SphairaError_YatiInvalidNcaReadSize";
        case Result_YatiInvalidNcaSigKeyGen: return "SphairaError_YatiInvalidNcaSigKeyGen";
        case Result_YatiInvalidNcaMagic: return "SphairaError_YatiInvalidNcaMagic";
        case Result_YatiInvalidNcaSignature0: return "SphairaError_YatiInvalidNcaSignature0";
        case Result_YatiInvalidNcaSignature1: return "SphairaError_YatiInvalidNcaSignature1";
        case Result_YatiInvalidNcaSha256: return "SphairaError_YatiInvalidNcaSha256";
        case Result_YatiNczSectionNotFound: return "SphairaError_YatiNczSectionNotFound";
        case Result_YatiInvalidNczSectionCount: return "SphairaError_YatiInvalidNczSectionCount";
        case Result_YatiNczBlockNotFound: return "SphairaError_YatiNczBlockNotFound";
        case Result_YatiInvalidNczBlockVersion: return "SphairaError_YatiInvalidNczBlockVersion";
        case Result_YatiInvalidNczBlockType: return "SphairaError_YatiInvalidNczBlockType";
        case Result_YatiInvalidNczBlockTotal: return "SphairaError_YatiInvalidNczBlockTotal";
        case Result_YatiInvalidNczBlockSizeExponent: return "SphairaError_YatiInvalidNczBlockSizeExponent";
        case Result_YatiInvalidNczZstdError: return "SphairaError_YatiInvalidNczZstdError";
        case Result_YatiTicketNotFound: return "SphairaError_YatiTicketNotFound";
        case Result_YatiInvalidTicketBadRightsId: return "SphairaError_YatiInvalidTicketBadRightsId";
        case Result_YatiCertNotFound: return "SphairaError_YatiCertNotFound";
        case Result_YatiNcmDbCorruptHeader: return "SphairaError_YatiNcmDbCorruptHeader";
        case Result_YatiNcmDbCorruptInfos: return "SphairaError_YatiNcmDbCorruptInfos";
    }

    return "";
}

} // namespace

ErrorBox::ErrorBox(const std::string& message) : m_message{message} {
    log_write("[ERROR] %s\n", m_message.c_str());

    m_pos.w = 770.f;
    m_pos.h = 430.f;
    m_pos.x = 255;
    m_pos.y = 145;

    SetAction(Button::A, Action{[this](){
        SetPop();
    }});

    App::PlaySoundEffect(SoundEffect::SoundEffect_Error);
}

ErrorBox::ErrorBox(Result code, const std::string& message) : ErrorBox{message} {
    m_code = code;
    m_code_message = GetCodeMessage(code);
    log_write("[ERROR] Code: 0x%X Module: %u Description: %u\n", R_VALUE(code), R_MODULE(code), R_DESCRIPTION(code));
}

auto ErrorBox::Update(Controller* controller, TouchInfo* touch) -> void {
    Widget::Update(controller, touch);
}

auto ErrorBox::Draw(NVGcontext* vg, Theme* theme) -> void {
    gfx::dimBackground(vg);
    gfx::drawRect(vg, m_pos, theme->GetColour(ThemeEntryID_POPUP));

    const Vec4 box = { 455, 470, 365, 65 };
    const auto center_x = m_pos.x + m_pos.w/2;

    gfx::drawTextArgs(vg, center_x, 180, 63, NVG_ALIGN_CENTER | NVG_ALIGN_TOP, theme->GetColour(ThemeEntryID_ERROR), "\uE140");
    if (m_code.has_value()) {
        const auto code = m_code.value();
        if (m_code_message.empty()) {
            gfx::drawTextArgs(vg, center_x, 270, 25, NVG_ALIGN_CENTER | NVG_ALIGN_TOP, theme->GetColour(ThemeEntryID_TEXT), "Code: 0x%X Module: %u Description: 0x%X", R_VALUE(code), R_MODULE(code), R_DESCRIPTION(code));
        } else {
            gfx::drawTextArgs(vg, center_x, 270, 25, NVG_ALIGN_CENTER | NVG_ALIGN_TOP, theme->GetColour(ThemeEntryID_TEXT), "%s", m_code_message.c_str());
        }
    } else {
        gfx::drawTextArgs(vg, center_x, 270, 25, NVG_ALIGN_CENTER | NVG_ALIGN_TOP, theme->GetColour(ThemeEntryID_TEXT), "An error occurred"_i18n.c_str());
    }
    gfx::drawTextArgs(vg, center_x, 325, 23, NVG_ALIGN_CENTER | NVG_ALIGN_TOP, theme->GetColour(ThemeEntryID_TEXT), "%s", m_message.c_str());
    gfx::drawTextArgs(vg, center_x, 380, 20, NVG_ALIGN_CENTER | NVG_ALIGN_TOP, theme->GetColour(ThemeEntryID_TEXT_INFO), "If this message appears repeatedly, please open an issue."_i18n.c_str());
    gfx::drawTextArgs(vg, center_x, 415, 20, NVG_ALIGN_CENTER | NVG_ALIGN_TOP, theme->GetColour(ThemeEntryID_TEXT_INFO), "https://github.com/ITotalJustice/sphaira/issues");
    gfx::drawRectOutline(vg, theme, 4.f, box);
    gfx::drawTextArgs(vg, center_x, box.y + box.h/2, 23, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE, theme->GetColour(ThemeEntryID_TEXT_SELECTED), "OK"_i18n.c_str());
}

} // namespace sphaira::ui
