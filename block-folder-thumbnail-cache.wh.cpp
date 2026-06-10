// ==WindhawkMod==
// @id              block-folder-thumbnail-cache
// @name            Block Folder Thumbnail Cache
// @description     Prevents File Explorer from generating preview thumbnails for folders, while preserving file thumbnails and themed folder icons.
// @version         0.1
// @author          Szymon
// @include         explorer.exe
// @compilerOptions -lole32 -lshell32 -lshlwapi
// @license         MIT
// ==/WindhawkMod==

// ==WindhawkModReadme==
/*
# Block Folder Thumbnail Cache

This mod prevents Windows File Explorer from generating thumbnail previews for folders,
while keeping normal thumbnails enabled for files such as images, videos, PDFs, and other supported formats.

## Purpose

Windows 11 uses folder preview thumbnails for directories that contain images or other media files.
This can interfere with custom icon themes, icon replacement tools, and visual consistency,
because media folders may display generated preview thumbnails instead of the normal themed folder icon.

This mod is designed to:

- keep file thumbnails enabled,
- block generated thumbnail previews for folders,
- preserve normal folder icons from the current Windows icon theme,
- avoid forcing a specific folder icon such as the default yellow Windows folder,
- remove the old `Logo` registry workaround if enabled in settings.

## How it works

The mod hooks `CoCreateInstance` and wraps the `IThumbnailCache` interface used by File Explorer.
When Explorer requests a thumbnail for a folder, the mod blocks that request.
For regular files, the original thumbnail behavior is left untouched.

As a result, folders should fall back to their normal icon, while files continue to show thumbnails.

## Notes

This mod is experimental.
File Explorer uses several internal paths for icons, thumbnails, and cached folder previews,
so some cached folder thumbnails may remain visible until the thumbnail/icon cache is cleared.

For best results, keep the cache cleanup option enabled after installing or updating the mod.
*/
// ==/WindhawkModReadme==

// ==WindhawkModSettings==
/*
- enabled: true
  $name: Enable mod
  $description: Blocks generated thumbnail previews for folders while keeping file thumbnails enabled.

- removeOldLogoHack: true
  $name: Remove old Logo registry hack
  $description: Removes the old folder Logo registry value that could force a specific folder icon, such as the default yellow Windows folder.

- enableFileThumbnails: true
  $name: Keep file thumbnails enabled
  $description: Sets IconsOnly to 0 so that thumbnails for files such as images, videos, and PDFs remain enabled.

- clearCacheOnce: true
  $name: Clear thumbnail cache once
  $description: Clears File Explorer thumbnail and icon cache once for this mod version, then restarts Explorer.
*/
// ==/WindhawkModSettings==

#include <windows.h>
#include <unknwn.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <strsafe.h>

#define MOD_VERSION_MARKER L"0.1"

struct {
    bool enabled;
    bool removeOldLogoHack;
    bool enableFileThumbnails;
    bool clearCacheOnce;
} settings;

const IID IID_IUnknown_Local = {
    0x00000000,
    0x0000,
    0x0000,
    {0xC0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46}
};

const IID IID_IThumbnailCache_Local = {
    0xF676C15D,
    0x596A,
    0x4CE2,
    {0x82, 0x34, 0x33, 0x99, 0x6F, 0x44, 0x5D, 0xB1}
};

const CLSID CLSID_LocalThumbnailCache_Local = {
    0x50EF4544,
    0xAC9F,
    0x4A8E,
    {0xB2, 0x1B, 0x8A, 0x26, 0x18, 0x0D, 0xB1, 0x3F}
};

typedef enum WTS_FLAGS_LOCAL {
    WTS_EXTRACT = 0x00000000,
    WTS_INCACHEONLY = 0x00000001,
    WTS_FASTEXTRACT = 0x00000002,
    WTS_FORCEEXTRACTION = 0x00000004,
    WTS_SLOWRECLAIM = 0x00000008,
    WTS_EXTRACTDONOTCACHE = 0x00000020,
    WTS_SCALETOREQUESTEDSIZE = 0x00000040,
    WTS_SKIPFASTEXTRACT = 0x00000080,
    WTS_EXTRACTINPROC = 0x00000100
} WTS_FLAGS_LOCAL;

typedef enum WTS_CACHEFLAGS_LOCAL {
    WTS_DEFAULT = 0x00000000,
    WTS_LOWQUALITY = 0x00000001,
    WTS_CACHED = 0x00000002
} WTS_CACHEFLAGS_LOCAL;

typedef struct WTS_THUMBNAILID_LOCAL {
    BYTE rgbKey[16];
} WTS_THUMBNAILID_LOCAL;

struct ISharedBitmap;
struct IThumbnailCache_Local : public IUnknown {
    virtual HRESULT STDMETHODCALLTYPE GetThumbnail(
        IShellItem* pShellItem,
        UINT cxyRequestedThumbSize,
        WTS_FLAGS_LOCAL flags,
        ISharedBitmap** ppvThumb,
        WTS_CACHEFLAGS_LOCAL* pOutFlags,
        WTS_THUMBNAILID_LOCAL* pThumbnailID
    ) = 0;

    virtual HRESULT STDMETHODCALLTYPE GetThumbnailByID(
        WTS_THUMBNAILID_LOCAL thumbnailID,
        UINT cxyRequestedThumbSize,
        ISharedBitmap** ppvThumb,
        WTS_CACHEFLAGS_LOCAL* pOutFlags
    ) = 0;
};

void LoadSettings() {
    settings.enabled = Wh_GetIntSetting(L"enabled");
    settings.removeOldLogoHack = Wh_GetIntSetting(L"removeOldLogoHack");
    settings.enableFileThumbnails = Wh_GetIntSetting(L"enableFileThumbnails");
    settings.clearCacheOnce = Wh_GetIntSetting(L"clearCacheOnce");
}

bool IsShellItemFolder(IShellItem* item) {
    if (!item) {
        return false;
    }

    SFGAOF attrs = 0;

    HRESULT hr = item->GetAttributes(
        SFGAO_FOLDER | SFGAO_FILESYSTEM,
        &attrs
    );

    if (FAILED(hr)) {
        return false;
    }

    return (attrs & SFGAO_FOLDER) != 0;
}

bool SetRegDword(HKEY root, LPCWSTR subKey, LPCWSTR valueName, DWORD data) {
    HKEY hKey = nullptr;

    LONG result = RegCreateKeyExW(
        root,
        subKey,
        0,
        nullptr,
        REG_OPTION_NON_VOLATILE,
        KEY_SET_VALUE,
        nullptr,
        &hKey,
        nullptr
    );

    if (result != ERROR_SUCCESS) {
        return false;
    }

    result = RegSetValueExW(
        hKey,
        valueName,
        0,
        REG_DWORD,
        reinterpret_cast<const BYTE*>(&data),
        sizeof(data)
    );

    RegCloseKey(hKey);

    return result == ERROR_SUCCESS;
}

bool SetRegString(HKEY root, LPCWSTR subKey, LPCWSTR valueName, LPCWSTR data) {
    HKEY hKey = nullptr;

    LONG result = RegCreateKeyExW(
        root,
        subKey,
        0,
        nullptr,
        REG_OPTION_NON_VOLATILE,
        KEY_SET_VALUE,
        nullptr,
        &hKey,
        nullptr
    );

    if (result != ERROR_SUCCESS) {
        return false;
    }

    result = RegSetValueExW(
        hKey,
        valueName,
        0,
        REG_SZ,
        reinterpret_cast<const BYTE*>(data),
        static_cast<DWORD>((wcslen(data) + 1) * sizeof(WCHAR))
    );

    RegCloseKey(hKey);

    return result == ERROR_SUCCESS;
}

bool GetRegString(
    HKEY root,
    LPCWSTR subKey,
    LPCWSTR valueName,
    LPWSTR buffer,
    DWORD bufferChars
) {
    HKEY hKey = nullptr;

    LONG result = RegOpenKeyExW(
        root,
        subKey,
        0,
        KEY_QUERY_VALUE,
        &hKey
    );

    if (result != ERROR_SUCCESS) {
        return false;
    }

    DWORD type = 0;
    DWORD size = bufferChars * sizeof(WCHAR);

    result = RegQueryValueExW(
        hKey,
        valueName,
        nullptr,
        &type,
        reinterpret_cast<LPBYTE>(buffer),
        &size
    );

    RegCloseKey(hKey);

    return result == ERROR_SUCCESS && type == REG_SZ;
}

void RemoveOldLogoHack() {
    HKEY hKey = nullptr;

    LONG result = RegOpenKeyExW(
        HKEY_CURRENT_USER,
        L"Software\\Classes\\Local Settings\\Software\\Microsoft\\Windows\\Shell\\Bags\\AllFolders\\Shell",
        0,
        KEY_SET_VALUE,
        &hKey
    );

    if (result == ERROR_SUCCESS) {
        RegDeleteValueW(hKey, L"Logo");
        RegCloseKey(hKey);
        Wh_Log(L"Removed old Logo registry hack");
    }
}

void ApplyBasicSettings() {
    if (settings.enableFileThumbnails) {
        SetRegDword(
            HKEY_CURRENT_USER,
            L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\Advanced",
            L"IconsOnly",
            0
        );

        Wh_Log(L"IconsOnly = 0");
    }

    if (settings.removeOldLogoHack) {
        RemoveOldLogoHack();
    }
}

class ThumbnailCacheWrapper : public IThumbnailCache_Local {
private:
    LONG refCount;
    IThumbnailCache_Local* original;

public:
    ThumbnailCacheWrapper(IThumbnailCache_Local* originalCache)
        : refCount(1), original(originalCache) {
    }

    virtual ~ThumbnailCacheWrapper() {
        if (original) {
            original->Release();
            original = nullptr;
        }
    }

    HRESULT STDMETHODCALLTYPE QueryInterface(
        REFIID riid,
        void** ppvObject
    ) override {
        if (!ppvObject) {
            return E_POINTER;
        }

        if (
            IsEqualIID(riid, IID_IUnknown_Local) ||
            IsEqualIID(riid, IID_IThumbnailCache_Local)
        ) {
            *ppvObject = static_cast<IThumbnailCache_Local*>(this);
            AddRef();
            return S_OK;
        }

        *ppvObject = nullptr;
        return E_NOINTERFACE;
    }

    ULONG STDMETHODCALLTYPE AddRef() override {
        return InterlockedIncrement(&refCount);
    }

    ULONG STDMETHODCALLTYPE Release() override {
        ULONG count = InterlockedDecrement(&refCount);

        if (count == 0) {
            delete this;
        }

        return count;
    }

    HRESULT STDMETHODCALLTYPE GetThumbnail(
        IShellItem* pShellItem,
        UINT cxyRequestedThumbSize,
        WTS_FLAGS_LOCAL flags,
        ISharedBitmap** ppvThumb,
        WTS_CACHEFLAGS_LOCAL* pOutFlags,
        WTS_THUMBNAILID_LOCAL* pThumbnailID
    ) override {
        if (settings.enabled && IsShellItemFolder(pShellItem)) {
            Wh_Log(L"Blocked folder thumbnail from IThumbnailCache");

            if (ppvThumb) {
                *ppvThumb = nullptr;
            }

            if (pOutFlags) {
                *pOutFlags = WTS_DEFAULT;
            }

            if (pThumbnailID) {
                ZeroMemory(pThumbnailID, sizeof(*pThumbnailID));
            }

            return E_FAIL;
        }

        return original->GetThumbnail(
            pShellItem,
            cxyRequestedThumbSize,
            flags,
            ppvThumb,
            pOutFlags,
            pThumbnailID
        );
    }

    HRESULT STDMETHODCALLTYPE GetThumbnailByID(
        WTS_THUMBNAILID_LOCAL thumbnailID,
        UINT cxyRequestedThumbSize,
        ISharedBitmap** ppvThumb,
        WTS_CACHEFLAGS_LOCAL* pOutFlags
    ) override {
        return original->GetThumbnailByID(
            thumbnailID,
            cxyRequestedThumbSize,
            ppvThumb,
            pOutFlags
        );
    }
};

using CoCreateInstance_t = HRESULT (WINAPI*)(
    REFCLSID rclsid,
    LPUNKNOWN pUnkOuter,
    DWORD dwClsContext,
    REFIID riid,
    LPVOID* ppv
);

CoCreateInstance_t CoCreateInstance_Original = nullptr;

HRESULT WINAPI CoCreateInstance_Hook(
    REFCLSID rclsid,
    LPUNKNOWN pUnkOuter,
    DWORD dwClsContext,
    REFIID riid,
    LPVOID* ppv
) {
    HRESULT hr = CoCreateInstance_Original(
        rclsid,
        pUnkOuter,
        dwClsContext,
        riid,
        ppv
    );

    if (
        SUCCEEDED(hr) &&
        ppv &&
        *ppv &&
        settings.enabled &&
        IsEqualCLSID(rclsid, CLSID_LocalThumbnailCache_Local) &&
        IsEqualIID(riid, IID_IThumbnailCache_Local)
    ) {
        Wh_Log(L"Wrapping LocalThumbnailCache");

        IThumbnailCache_Local* originalCache =
            static_cast<IThumbnailCache_Local*>(*ppv);

        *ppv = static_cast<IThumbnailCache_Local*>(
            new ThumbnailCacheWrapper(originalCache)
        );
    }

    return hr;
}

bool WasCleanupAlreadyDoneForThisVersion() {
    WCHAR version[64] = {};

    bool exists = GetRegString(
        HKEY_CURRENT_USER,
        L"Software\\WindhawkMods\\BlockFolderThumbnailCache",
        L"LastCleanupVersion",
        version,
        ARRAYSIZE(version)
    );

    if (!exists) {
        return false;
    }

    return wcscmp(version, MOD_VERSION_MARKER) == 0;
}

void MarkCleanupDoneForThisVersion() {
    SetRegString(
        HKEY_CURRENT_USER,
        L"Software\\WindhawkMods\\BlockFolderThumbnailCache",
        L"LastCleanupVersion",
        MOD_VERSION_MARKER
    );
}

void ClearCacheAndRestartExplorerOnce() {
    if (!settings.clearCacheOnce) {
        return;
    }

    if (WasCleanupAlreadyDoneForThisVersion()) {
        Wh_Log(L"Cache already cleaned for this version");
        return;
    }

    MarkCleanupDoneForThisVersion();

    WCHAR commandLine[] =
        L"powershell.exe -NoProfile -ExecutionPolicy Bypass -WindowStyle Hidden "
        L"-Command \""
        L"Start-Sleep -Seconds 2; "
        L"Stop-Process -Name explorer -Force -ErrorAction SilentlyContinue; "
        L"Remove-Item $env:LOCALAPPDATA\\Microsoft\\Windows\\Explorer\\thumbcache_*.db -Force -ErrorAction SilentlyContinue; "
        L"Remove-Item $env:LOCALAPPDATA\\Microsoft\\Windows\\Explorer\\iconcache_*.db -Force -ErrorAction SilentlyContinue; "
        L"Start-Sleep -Seconds 1; "
        L"Start-Process explorer.exe"
        L"\"";

    STARTUPINFOW si = {};
    PROCESS_INFORMATION pi = {};

    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;

    BOOL ok = CreateProcessW(
        nullptr,
        commandLine,
        nullptr,
        nullptr,
        FALSE,
        CREATE_NO_WINDOW,
        nullptr,
        nullptr,
        &si,
        &pi
    );

    if (ok) {
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
        Wh_Log(L"Cache cleanup started");
    } else {
        Wh_Log(L"CreateProcessW failed: %lu", GetLastError());
    }
}

BOOL Wh_ModInit() {
    Wh_Log(L"Init");

    LoadSettings();
    ApplyBasicSettings();

    Wh_SetFunctionHook(
        reinterpret_cast<void*>(CoCreateInstance),
        reinterpret_cast<void*>(CoCreateInstance_Hook),
        reinterpret_cast<void**>(&CoCreateInstance_Original)
    );

    Wh_Log(L"Hooked CoCreateInstance");

    ClearCacheAndRestartExplorerOnce();

    return TRUE;
}

void Wh_ModUninit() {
    Wh_Log(L"Uninit");
}

void Wh_ModSettingsChanged() {
    Wh_Log(L"SettingsChanged");

    LoadSettings();
    ApplyBasicSettings();
    ClearCacheAndRestartExplorerOnce();
}
