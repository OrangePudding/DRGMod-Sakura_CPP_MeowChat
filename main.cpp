#define NOMINMAX
#include <windows.h>

#include <string>
#include <cstdio>
#include <cstdarg>
#include <cwchar>
#include <cstring>
#include <cwctype>

// =====================================================================
// Minimal ABI-compatible declaration of RC::CppUserModBase.
// Matches the UE4SS build installed in the game folder:
//   v3.0.1 Beta #0 - Git SHA #486806a
// Keep the virtual ORDER exact, or UE4SS will call through the wrong
// vtable slot and crash.
// =====================================================================
namespace RC
{
    namespace ModShim
    {
        struct Lua;
        struct LuaVec;
    }

    class CppUserModBase
    {
    public:
        virtual ~CppUserModBase() {}
        virtual void on_update() {}
        virtual void on_unreal_init() {}
        virtual void on_ui_init() {}
        virtual void on_program_start() {}
        virtual void on_lua_start(std::wstring_view, ModShim::Lua*, ModShim::Lua*, ModShim::Lua*, ModShim::LuaVec*) {}
        virtual void on_lua_start(ModShim::Lua*, ModShim::Lua*, ModShim::Lua*, ModShim::LuaVec*) {}
        virtual void on_lua_stop(std::wstring_view, ModShim::Lua*, ModShim::Lua*, ModShim::Lua*, ModShim::LuaVec*) {}
        virtual void on_lua_stop(ModShim::Lua*, ModShim::Lua*, ModShim::Lua*, ModShim::LuaVec*) {}
        virtual void on_dll_load(std::wstring_view) {}
        virtual void render_tab() {}
        virtual void on_lua_start(std::wstring_view, ModShim::Lua*, ModShim::Lua*, ModShim::Lua*, ModShim::Lua*) {}
        virtual void on_lua_start(ModShim::Lua*, ModShim::Lua*, ModShim::Lua*, ModShim::Lua*) {}
        virtual void on_lua_stop(std::wstring_view, ModShim::Lua*, ModShim::Lua*, ModShim::Lua*, ModShim::Lua*) {}
        virtual void on_lua_stop(ModShim::Lua*, ModShim::Lua*, ModShim::Lua*, ModShim::Lua*) {}
        virtual void on_cpp_mods_loaded() {}

    protected:
        void* m_gui_tabs_storage[2];

    public:
        std::wstring ModName;
        std::wstring ModVersion;
        std::wstring ModDescription;
        std::wstring ModAuthors;
        std::wstring ModIntendedSDKVersion;
    };
}

#include <Unreal/Common.hpp>
#include <Unreal/NameTypes.hpp>
#include <Unreal/UObjectGlobals.hpp>
#include <Unreal/UObject.hpp>
#include <Unreal/UClass.hpp>
#include <Unreal/UFunction.hpp>
#include <Unreal/UFunctionStructs.hpp>
#include <Unreal/UStruct.hpp>
#include <Unreal/UScriptStruct.hpp>
#include <Unreal/FProperty.hpp>
#include <Unreal/FField.hpp>
#include <Unreal/FText.hpp>
#include <Unreal/FMemory.hpp>
#include <Unreal/FFrame.hpp>
#include <Unreal/Hooks.hpp>

namespace MeowChat
{
    using namespace RC::Unreal;

    // =====================================================================
    // Raw ABI-compatible view of UE4 FString / UE4SS FText (24 bytes).
    // The installed UE4SS.dll (v3.0.1) does not export every SDK helper,
    // so we only rely on the raw layouts plus exported primitives.
    // =====================================================================
    struct RawFString
    {
        wchar_t* Data;
        int32_t Num;   // includes trailing null
        int32_t Max;   // capacity
    };

    static HMODULE g_hmod = nullptr;
    static HMODULE g_ue4ss = nullptr;
    static std::wstring g_log_path;
    static std::wstring g_cfg_path;              // <mod dir>\config.txt
    static bool g_cfg_enabled = true;            // master switch
    static bool g_cfg_sender_enabled = true;     // also meow the sender name
    static std::wstring g_cfg_suffix = L"\u55B5"; // appended text (default: meow)
    static ULONGLONG g_cfg_check_tick = 0;       // reload throttle
    static bool g_cfg_had_file = false;
    static uint64_t g_cfg_last_write = 0;

    // DLL-exported helpers that changed ABI between the SDK submodule and
    // the installed UE4SS.dll; resolved once at runtime.
    static uint8_t*& (*g_fnLocals)(void*) = nullptr;                 // FFrame::Locals() -> uint8_t*&
    static FField*& (*g_fnGetNext)(void*) = nullptr;             // FField::GetNext() -> FField*&
    static void (*g_fnFTextSetString)(void*, const void*) = nullptr; // FText::SetString(FString const&&)

    static bool g_procsResolved = false;

    static void Log(const wchar_t* fmt, ...);

    static void ResolveProcs()
    {
        if (g_procsResolved) return;
        g_ue4ss = GetModuleHandleW(L"UE4SS.dll");
        if (!g_ue4ss) return;
        g_fnLocals = (uint8_t*& (*)(void*))GetProcAddress(g_ue4ss, "?Locals@FFrame@Unreal@RC@@QEAAAEAPEAEXZ");
        g_fnGetNext = (FField*& (*)(void*))GetProcAddress(g_ue4ss, "?GetNext@FField@Unreal@RC@@AEAAAEAPEAV123@XZ");
        g_fnFTextSetString = (void (*)(void*, const void*))GetProcAddress(g_ue4ss, "?SetString@FText@Unreal@RC@@QEAAX$$QEBVFString@23@@Z");
        g_procsResolved = true;
        Log(L"proc: Locals=%p GetNext=%p FTextSetString=%p",
            (void*)g_fnLocals, (void*)g_fnGetNext, (void*)g_fnFTextSetString);
    }

    static void InitLogPath()
    {
        wchar_t buf[MAX_PATH] = {0};
        DWORD n = GetModuleFileNameW(g_hmod, buf, MAX_PATH);
        std::wstring p(buf, n);
        auto pos = p.rfind(L'\\');
        if (pos != std::wstring::npos) p = p.substr(0, pos);
        pos = p.rfind(L'\\');
        if (pos != std::wstring::npos) p = p.substr(0, pos);
        g_log_path = p + L"\\meowchat.log";
        g_cfg_path = p + L"\\config.txt";
        DeleteFileW(g_log_path.c_str());   // fresh log per game start
    }

    static void Log(const wchar_t* fmt, ...)
    {
        if (g_log_path.empty()) return;
        wchar_t buf[1024] = {0};
        va_list args;
        va_start(args, fmt);
        _vsnwprintf_s(buf, 1024, _TRUNCATE, fmt, args);
        va_end(args);
        FILE* f = nullptr;
        if (_wfopen_s(&f, g_log_path.c_str(), L"a, ccs=UTF-8") == 0 && f)
        {
            SYSTEMTIME st;
            GetLocalTime(&st);
            fwprintf(f, L"[%02u:%02u:%02u.%03u] %s\n",
                     (unsigned)st.wHour, (unsigned)st.wMinute, (unsigned)st.wSecond, (unsigned)st.wMilliseconds, buf);
            fclose(f);
        }
    }

    // =====================================================================
    // Runtime config: <mod dir>\config.txt (UTF-8), hot-reloaded every second.
    //   Enabled       = true    # master switch (true/false, 1/0)
    //   Suffix        = \u55B5   # text appended to messages / sender names
    //   SenderEnabled = true    # also append to the sender name
    // =====================================================================
    static std::wstring TrimW(const std::wstring& s)
    {
        size_t b = 0, e = s.size();
        while (b < e && iswspace(s[b])) ++b;
        while (e > b && iswspace(s[e - 1])) --e;
        return s.substr(b, e - b);
    }

    static std::wstring Utf8ToWide(const std::string& s)
    {
        if (s.empty()) return {};
        int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
        if (n <= 0) return {};
        std::wstring w((size_t)n, 0);
        MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), &w[0], n);
        return w;
    }

    static bool ParseBoolValue(const std::wstring& v, bool fallback)
    {
        if (v.empty()) return fallback;
        if (_wcsicmp(v.c_str(), L"true") == 0 || v == L"1" ||
            _wcsicmp(v.c_str(), L"yes") == 0 || _wcsicmp(v.c_str(), L"on") == 0) return true;
        if (_wcsicmp(v.c_str(), L"false") == 0 || v == L"0" ||
            _wcsicmp(v.c_str(), L"no") == 0 || _wcsicmp(v.c_str(), L"off") == 0) return false;
        return fallback;
    }

    static void ResetConfigDefaults()
    {
        g_cfg_enabled = true;
        g_cfg_sender_enabled = true;
        g_cfg_suffix = L"\u55B5";
    }

    static void LoadConfig()
    {
        ResetConfigDefaults();
        HANDLE h = CreateFileW(g_cfg_path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                               nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (h == INVALID_HANDLE_VALUE)
        {
            Log(L"config: no config file, using defaults");
            return;
        }
        DWORD size = GetFileSize(h, nullptr);
        std::string buf(size ? size : 1, 0);
        DWORD read = 0;
        BOOL ok = (size > 0) && ReadFile(h, &buf[0], size, &read, nullptr);
        CloseHandle(h);
        if (!ok) return;

        // strip UTF-8 BOM if present
        size_t off = (buf.size() >= 3 && (unsigned char)buf[0] == 0xEF &&
                      (unsigned char)buf[1] == 0xBB && (unsigned char)buf[2] == 0xBF) ? 3 : 0;
        std::wstring text = Utf8ToWide(buf.substr(off));

        size_t pos = 0;
        while (pos <= text.size())
        {
            size_t nl = text.find(L'\n', pos);
            std::wstring line = TrimW(text.substr(pos, nl == std::wstring::npos ? std::wstring::npos : nl - pos));
            pos = (nl == std::wstring::npos) ? text.size() + 1 : nl + 1;
            if (line.empty() || line[0] == L'#' || line[0] == L';') continue;
            if (line[0] == 0xFEFF) line = line.substr(1);
            size_t eq = line.find(L'=');
            if (eq == std::wstring::npos) continue;
            std::wstring key = TrimW(line.substr(0, eq));
            std::wstring val = TrimW(line.substr(eq + 1));
            if (_wcsicmp(key.c_str(), L"Enabled") == 0) g_cfg_enabled = ParseBoolValue(val, g_cfg_enabled);
            else if (_wcsicmp(key.c_str(), L"Suffix") == 0) g_cfg_suffix = val;
            else if (_wcsicmp(key.c_str(), L"SenderEnabled") == 0) g_cfg_sender_enabled = ParseBoolValue(val, g_cfg_sender_enabled);
        }
        Log(L"config: enabled=%d sender=%d suffix=%ls", g_cfg_enabled ? 1 : 0,
            g_cfg_sender_enabled ? 1 : 0, g_cfg_suffix.c_str());
    }

    // Called every frame from on_update; only re-reads the file when its
    // modification time changes, so config edits take effect within ~1s.
    static void TryReloadConfig()
    {
        ULONGLONG now = GetTickCount64();
        if (now - g_cfg_check_tick < 1000) return;
        g_cfg_check_tick = now;

        WIN32_FILE_ATTRIBUTE_DATA fad;
        if (!GetFileAttributesExW(g_cfg_path.c_str(), GetFileExInfoStandard, &fad))
        {
            if (g_cfg_had_file)
            {
                g_cfg_had_file = false;
                ResetConfigDefaults();
                Log(L"config: config file removed, using defaults");
            }
            return;
        }
        uint64_t ft = ((uint64_t)fad.ftLastWriteTime.dwHighDateTime << 32) | fad.ftLastWriteTime.dwLowDateTime;
        if (g_cfg_had_file && ft == g_cfg_last_write) return;
        g_cfg_had_file = true;
        g_cfg_last_write = ft;
        LoadConfig();
    }

    // =====================================================================
    // Meow logic, kept identical to the reference Lua mod:
    //   * strip trailing whitespace
    //   * walk backwards over trailing punctuation/space, insert ? right
    //     after the last text char (punctuation stays at the tail)
    //   * idempotent: skip if the text part already ends with ?
    //   * skip pure placeholder templates like "{0}"
    // =====================================================================
    static bool IsMiaoPunc(wchar_t c)
    {
        switch (c)
        {
        case 0xFF01: /* ! */ case 0xFF1F: /* ? */
        case 0x3002: /* ? */ case 0xFF0C: /* ? */ case 0xFF5E: /* ? */
        case L'!': case L'?': case L'.': case L',': case L'~':
        case 0x2026: /* ... */ case 0x3001: /* ? */
            return true;
        default:
            return false;
        }
    }

    // Equivalent to Lua: str:gsub("{%w+}", ""):gsub("%s+", "") == ""
    static bool IsPurePlaceholders(const std::wstring& s)
    {
        std::wstring out;
        out.reserve(s.size());
        size_t i = 0;
        while (i < s.size())
        {
            if (s[i] == L'{')
            {
                size_t j = i + 1;
                while (j < s.size() && (iswalnum(s[j]) || s[j] == L'_')) ++j;
                if (j > i + 1 && j < s.size() && s[j] == L'}')
                {
                    i = j + 1;   // skip the whole {xxx} placeholder
                    continue;
                }
            }
            out.push_back(s[i]);
            ++i;
        }
        for (wchar_t c : out)
        {
            if (!iswspace(c)) return false;
        }
        return true;
    }

    static std::wstring EnsureMiao(const std::wstring& str)
    {
        if (!g_cfg_enabled || g_cfg_suffix.empty()) return str;
        if (str.empty()) return str;

        // strip trailing whitespace
        std::wstring work = str;
        while (!work.empty() && iswspace(work.back())) work.pop_back();
        if (work.empty()) return str;   // whitespace-only: leave untouched

        // find the last text char (skip trailing space/punctuation)
        size_t pos = work.size();
        while (pos > 0)
        {
            wchar_t ch = work[pos - 1];
            if (ch == L' ' || IsMiaoPunc(ch))
            {
                --pos;
            }
            else
            {
                break;
            }
        }

        std::wstring main = work.substr(0, pos);
        std::wstring tail = work.substr(pos);

        // idempotent: text part already ends with the configured suffix
        if (!main.empty() && main.size() >= g_cfg_suffix.size() &&
            main.compare(main.size() - g_cfg_suffix.size(), g_cfg_suffix.size(), g_cfg_suffix) == 0) return str;

        // skip pure placeholder templates
        if (IsPurePlaceholders(work)) return str;

        return main + g_cfg_suffix + tail;
    }

    // =====================================================================
    // Raw FString helpers (no SDK class dependency)
    // =====================================================================
    static std::wstring ReadRawFString(const RawFString& fs)
    {
        if (!fs.Data || fs.Num <= 0) return {};
        return std::wstring(fs.Data, (size_t)(fs.Num - 1));
    }

    // Replace the contents of an in-place FString with 'text'.
    // The new buffer is allocated with UE's own allocator (exported from
    // UE4SS), so the engine frees it correctly when it destroys the param.
    // The OLD buffer is deliberately NOT freed here: some other mods inject
    // hand-rolled FStrings (e.g. WelcomeMod uses new[]), so freeing with the
    // UE allocator could corrupt the heap. Leaking a few hundred bytes per
    // modified message is far safer than a heap mismatch.
    static void WriteRawFString(RawFString& fs, const std::wstring& text)
    {
        int32_t len = (int32_t)text.size();
        int32_t newNum = len + 1;
        wchar_t* buf = (wchar_t*)FMemory::Malloc((SIZE_T)newNum * sizeof(wchar_t), alignof(wchar_t));
        if (!buf) return;
        if (len > 0) memcpy(buf, text.c_str(), (size_t)len * sizeof(wchar_t));
        buf[len] = 0;
        fs.Data = buf;
        fs.Num = newNum;
        fs.Max = newNum;
    }

    static void ApplyMeowFString(const wchar_t* tag, const std::wstring& raw, const std::wstring& miaoed, RawFString& out)
    {
        Log(L"meow[%s]: %ls -> %ls", tag, raw.c_str(), miaoed.c_str());
        WriteRawFString(out, miaoed);
    }

    static void ApplyMeowFText(const wchar_t* tag, const std::wstring& raw, const std::wstring& miaoed, FText& msg)
    {
        Log(L"meow[%s]: %ls -> %ls", tag, raw.c_str(), miaoed.c_str());
        RawFString newFs{nullptr, 0, 0};
        WriteRawFString(newFs, miaoed);
        if (!newFs.Data) return;
        g_fnFTextSetString(&msg, &newFs);
        // If the engine moved the buffer into its FTextData, newFs.Data is
        // null and the engine owns it; if it copied, we free our own buffer.
        if (newFs.Data) FMemory::Free(newFs.Data);
    }

    // Convenience: append meow to an in-place FString if the text changed.
    static void ApplyMeowFStringIfNeeded(const wchar_t* tag, RawFString& fs)
    {
        std::wstring raw = ReadRawFString(fs);
        if (raw.empty()) return;
        std::wstring miaoed = EnsureMiao(raw);
        if (miaoed != raw) ApplyMeowFString(tag, raw, miaoed, fs);
    }

    // Convenience: append meow to an in-place FText if the text changed.
    static void ApplyMeowFTextIfNeeded(const wchar_t* tag, FText& msg)
    {
        std::wstring tpl = msg.ToString();
        if (tpl.empty()) return;
        std::wstring miaoed = EnsureMiao(tpl);
        if (miaoed != tpl) ApplyMeowFText(tag, tpl, miaoed, msg);
    }

    // =====================================================================
    // Resolved hook state (all used on the game thread)
    // =====================================================================
    static bool g_hooksReady = false;

    static CallbackId g_serverHookId = 0;
    static CallbackId g_clientHookId = 0;
    static CallbackId g_localizedHookId = 0;
    static CallbackId g_postGameHookId = 0;
    static CallbackId g_postLocalizedHookId = 0;
    static bool g_triedServer = false;
    static bool g_triedClient = false;
    static bool g_triedLocalized = false;
    static bool g_triedChatStruct = false;
    static bool g_triedLocStruct = false;
    static bool g_triedPostGame = false;
    static bool g_triedPostLocalized = false;

    static UFunction* g_fnServerNewMessage = nullptr;
    static UFunction* g_fnClientNewMessage = nullptr;
    static UFunction* g_fnClientNewLocalized = nullptr;
    static UFunction* g_fnPostGameMessage = nullptr;
    static UFunction* g_fnPostLocalizedGameMessage = nullptr;

    // =====================================================================
    // Broadcast rewrite (see TryMeowMulticastParms below).
    //
    // ClientNewMessage / Client_NewLocalizedMessage are native NetMulticast
    // RPCs on FSDGameState. On the host, UObject::ProcessEvent calls
    // CallRemoteFunction(Function, Parms, ...) - which serializes the
    // multicast payload to every client - BEFORE the function body runs.
    // UE4SS's ProcessEvent pre-callback fires at the very start of
    // ProcessEvent, i.e. before that send, so rewriting Parms there makes
    // the meowed sender/text reach EVERY client, with or without the mod.
    //
    // The previous approach patched the CallRemoteFunction vtable slot
    // directly, but the slot read from FSDGameState failed the in-module
    // sanity check in this game build (vtable layout differs from stock
    // UE 4.27), so that hook was permanently dead and only host-local
    // display was ever meowed. The ProcessEvent hook needs no vtable
    // offsets at all.
    // =====================================================================

    static int32_t g_offServerText = -1;         // Server_NewMessage::Text (FString)
    static int32_t g_offClientMsgStruct = -1;    // ClientNewMessage::Msg (FFSDChatMessage)
    static int32_t g_offClientMsgField = -1;     // FFSDChatMessage::Msg (FString)
    static int32_t g_offClientSenderField = -1;  // FFSDChatMessage::Sender (FString)
    static int32_t g_offLocalizedMsgStruct = -1; // Client_NewLocalizedMessage::Msg (FFSDLocalizedChatMessage)
    static int32_t g_offLocalizedMsgField = -1;  // FFSDLocalizedChatMessage::Msg (FText)
    static int32_t g_offLocalizedSenderField = -1; // FFSDLocalizedChatMessage::Sender (FString)
    static int32_t g_offPostGameMsg = -1;         // PostGameMessage::Msg (FString)
    static int32_t g_offPostLocalizedMsg = -1;    // PostLocalizedGameMessage::Msg (FText)

    static int32_t FindPropOffset(UStruct* s, const wchar_t* name)
    {
        if (!s) return -1;
        FField* child = s->GetChildProperties();
        while (child)
        {
            if (child->GetName() == name)
            {
                FProperty* prop = static_cast<FProperty*>(child);
                return prop->GetOffset_Internal();
            }
            if (!g_fnGetNext) return -1;
            child = g_fnGetNext(child);
        }
        return -1;
    }

    // =====================================================================
    // Host-side broadcast rewrite: called from the ProcessEvent pre-callback
    // with the raw RPC Parms buffer, BEFORE the engine serializes + sends the
    // multicast payload (UObject::ProcessEvent calls CallRemoteFunction
    // before the function body). Runs on the host so every client receives
    // the meowed sender + text. Also fires on clients when they receive the
    // RPC - idempotent, so a payload already meowed by a mod host is left
    // alone.
    // =====================================================================
    static void TryMeowMulticastParms(UFunction* fn, void* parms)
    {
        if (!fn || !parms || !g_fnFTextSetString) return;
        uint8_t* p = (uint8_t*)parms;
        if (fn == g_fnClientNewMessage && g_offClientMsgStruct >= 0 && g_offClientMsgField >= 0)
        {
            uint8_t* msgStruct = p + g_offClientMsgStruct;
            if (g_cfg_sender_enabled && g_offClientSenderField >= 0)
            {
                ApplyMeowFStringIfNeeded(L"SendSender", *(RawFString*)(msgStruct + g_offClientSenderField));
            }
            ApplyMeowFStringIfNeeded(L"SendMsg", *(RawFString*)(msgStruct + g_offClientMsgField));
        }
        else if (fn == g_fnClientNewLocalized && g_offLocalizedMsgStruct >= 0 && g_offLocalizedMsgField >= 0)
        {
            uint8_t* msgStruct = p + g_offLocalizedMsgStruct;
            if (g_cfg_sender_enabled && g_offLocalizedSenderField >= 0)
            {
                ApplyMeowFStringIfNeeded(L"SendLocSender", *(RawFString*)(msgStruct + g_offLocalizedSenderField));
            }
            ApplyMeowFTextIfNeeded(L"SendLocMsg", *(FText*)(msgStruct + g_offLocalizedMsgField));
        }
    }

    static void ChatRewriteCallbackImpl(UObject*, UFunction* fn, void* parms)
    {
        TryMeowMulticastParms(fn, parms);
    }

    // SEH wrapper: an AV here must never take the game down.
    static void ChatRewriteCallback(UObject* ctx, UFunction* fn, void* parms)
    {
        __try { ChatRewriteCallbackImpl(ctx, fn, parms); }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            Log(L"hook: ProcessEvent rewrite exception 0x%08X", GetExceptionCode());
        }
    }

    // =====================================================================
    // Host side: append ? to player chat before it is broadcast.
    // Server_NewMessage is a Server RPC, so this pre-hook only runs on the
    // host machine (including the host's own local player messages).
    // =====================================================================
    static void OnServerNewMessageImpl(UnrealScriptFunctionCallableContext& ctx)
    {
        if (!g_fnLocals || g_offServerText < 0) return;
        uint8_t*& localsRef = g_fnLocals(&ctx.TheStack);
        uint8_t* parms = localsRef;
        if (!parms) return;
        RawFString& text = *(RawFString*)(parms + g_offServerText);
        std::wstring raw = ReadRawFString(text);
        if (raw.empty()) return;
        std::wstring miaoed = EnsureMiao(raw);
        if (miaoed != raw) ApplyMeowFString(L"ServerNewMessage", raw, miaoed, text);
    }

    // =====================================================================
    // Every client (including host): append ? to received player chat
    // (both the sender name and the message text).
    // Idempotent, so it never double-appends.
    // =====================================================================
    static void OnClientNewMessageImpl(UnrealScriptFunctionCallableContext& ctx)
    {
        if (!g_fnLocals || g_offClientMsgStruct < 0 || g_offClientMsgField < 0) return;
        uint8_t*& localsRef = g_fnLocals(&ctx.TheStack);
        uint8_t* parms = localsRef;
        if (!parms) return;
        uint8_t* msgStruct = parms + g_offClientMsgStruct;
        if (g_cfg_sender_enabled && g_offClientSenderField >= 0)
        {
            ApplyMeowFStringIfNeeded(L"ClientSender", *(RawFString*)(msgStruct + g_offClientSenderField));
        }
        RawFString& msg = *(RawFString*)(msgStruct + g_offClientMsgField);
        std::wstring raw = ReadRawFString(msg);
        if (raw.empty()) return;
        std::wstring miaoed = EnsureMiao(raw);
        if (miaoed != raw) ApplyMeowFString(L"ClientNewMessage", raw, miaoed, msg);
    }

    // =====================================================================
    // Every client (including host): localized/system messages.
    // The Msg (FText) template and the Sender (FString) are meowified.
    // Arguments are left intact so FText::Format keeps its original
    // semantics (apostrophes in argument values are never re-escaped).
    // =====================================================================
    static void OnClientNewLocalizedImpl(UnrealScriptFunctionCallableContext& ctx)
    {
        if (!g_fnLocals || !g_fnFTextSetString || g_offLocalizedMsgStruct < 0 || g_offLocalizedMsgField < 0) return;
        uint8_t*& localsRef = g_fnLocals(&ctx.TheStack);
        uint8_t* parms = localsRef;
        if (!parms) return;
        uint8_t* msgStruct = parms + g_offLocalizedMsgStruct;
        if (g_cfg_sender_enabled && g_offLocalizedSenderField >= 0)
        {
            ApplyMeowFStringIfNeeded(L"LocSender", *(RawFString*)(msgStruct + g_offLocalizedSenderField));
        }
        FText& msg = *(FText*)(msgStruct + g_offLocalizedMsgField);
        std::wstring tpl = msg.ToString();
        if (tpl.empty()) return;
        std::wstring miaoed = EnsureMiao(tpl);
        if (miaoed != tpl) ApplyMeowFText(L"ClientNewLocalized", tpl, miaoed, msg);
    }

    // ---- SEH wrappers: no exception (AV / C++ exception) can crash the game ----
    static void OnServerNewMessage(UnrealScriptFunctionCallableContext& ctx, void*)
    {
        __try { OnServerNewMessageImpl(ctx); }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            Log(L"hook: Server_NewMessage exception 0x%08X", GetExceptionCode());
        }
    }

    static void OnClientNewMessage(UnrealScriptFunctionCallableContext& ctx, void*)
    {
        __try { OnClientNewMessageImpl(ctx); }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            Log(L"hook: ClientNewMessage exception 0x%08X", GetExceptionCode());
        }
    }

    static void OnClientNewLocalized(UnrealScriptFunctionCallableContext& ctx, void*)
    {
        __try { OnClientNewLocalizedImpl(ctx); }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            Log(L"hook: Client_NewLocalizedMessage exception 0x%08X", GetExceptionCode());
        }
    }

    // =====================================================================
    // Local fallback: PostGameMessage / PostLocalizedGameMessage. These did
    // not fire in the 18:12 test session (system messages go through the
    // Client_NewLocalizedMessage multicast instead), but are kept for
    // coverage of any locally-posted message that skips the multicast RPCs.
    // =====================================================================
    static void OnPostGameMessageImpl(UnrealScriptFunctionCallableContext& ctx)
    {
        if (!g_fnLocals || g_offPostGameMsg < 0) return;
        uint8_t*& localsRef = g_fnLocals(&ctx.TheStack);
        uint8_t* parms = localsRef;
        if (!parms) return;
        RawFString& msg = *(RawFString*)(parms + g_offPostGameMsg);
        std::wstring raw = ReadRawFString(msg);
        if (raw.empty()) return;
        std::wstring miaoed = EnsureMiao(raw);
        if (miaoed != raw) ApplyMeowFString(L"PostGameMessage", raw, miaoed, msg);
    }

    static void OnPostLocalizedGameMessageImpl(UnrealScriptFunctionCallableContext& ctx)
    {
        if (!g_fnLocals || !g_fnFTextSetString || g_offPostLocalizedMsg < 0) return;
        uint8_t*& localsRef = g_fnLocals(&ctx.TheStack);
        uint8_t* parms = localsRef;
        if (!parms) return;
        FText& msg = *(FText*)(parms + g_offPostLocalizedMsg);
        std::wstring tpl = msg.ToString();
        if (tpl.empty()) return;
        std::wstring miaoed = EnsureMiao(tpl);
        if (miaoed != tpl) ApplyMeowFText(L"PostLocalizedGameMessage", tpl, miaoed, msg);
    }

    static void OnPostGameMessage(UnrealScriptFunctionCallableContext& ctx, void*)
    {
        __try { OnPostGameMessageImpl(ctx); }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            Log(L"hook: PostGameMessage exception 0x%08X", GetExceptionCode());
        }
    }

    static void OnPostLocalizedGameMessage(UnrealScriptFunctionCallableContext& ctx, void*)
    {
        __try { OnPostLocalizedGameMessageImpl(ctx); }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            Log(L"hook: PostLocalizedGameMessage exception 0x%08X", GetExceptionCode());
        }
    }

    // =====================================================================
    // Register hooks lazily (retried from on_update until everything is up)
    // =====================================================================
    static void TryRegisterHooks()
    {
        if (g_hooksReady) return;
        ResolveProcs();

        if (!g_triedServer)
        {
            g_triedServer = true;
            g_fnServerNewMessage = UObjectGlobals::StaticFindObject<UFunction*>(nullptr, nullptr, L"/Script/FSD.FSDPlayerController:Server_NewMessage");
            if (g_fnServerNewMessage)
            {
                g_offServerText = FindPropOffset(g_fnServerNewMessage, L"Text");
                if (g_offServerText >= 0)
                {
                    g_serverHookId = g_fnServerNewMessage->RegisterPreHook(OnServerNewMessage);
                    Log(L"hook: Server_NewMessage registered (Text@%d)", g_offServerText);
                }
                else
                {
                    Log(L"hook: Server_NewMessage has no Text param, skipped");
                }
            }
        }

        if (!g_triedClient)
        {
            g_triedClient = true;
            g_fnClientNewMessage = UObjectGlobals::StaticFindObject<UFunction*>(nullptr, nullptr, L"/Script/FSD.FSDGameState:ClientNewMessage");
            if (g_fnClientNewMessage)
            {
                g_offClientMsgStruct = FindPropOffset(g_fnClientNewMessage, L"Msg");
                if (g_offClientMsgStruct >= 0)
                {
                    g_clientHookId = g_fnClientNewMessage->RegisterPreHook(OnClientNewMessage);
                    Log(L"hook: ClientNewMessage registered (Msg@%d)", g_offClientMsgStruct);
                }
                else
                {
                    Log(L"hook: ClientNewMessage has no Msg param, skipped");
                }
            }
        }

        if (!g_triedLocalized)
        {
            g_triedLocalized = true;
            g_fnClientNewLocalized = UObjectGlobals::StaticFindObject<UFunction*>(nullptr, nullptr, L"/Script/FSD.FSDGameState:Client_NewLocalizedMessage");
            if (g_fnClientNewLocalized)
            {
                g_offLocalizedMsgStruct = FindPropOffset(g_fnClientNewLocalized, L"Msg");
                if (g_offLocalizedMsgStruct >= 0)
                {
                    g_localizedHookId = g_fnClientNewLocalized->RegisterPreHook(OnClientNewLocalized);
                    Log(L"hook: Client_NewLocalizedMessage registered (Msg@%d)", g_offLocalizedMsgStruct);
                }
                else
                {
                    Log(L"hook: Client_NewLocalizedMessage has no Msg param, skipped");
                }
            }
        }

        if (!g_triedPostGame)
        {
            g_triedPostGame = true;
            g_fnPostGameMessage = UObjectGlobals::StaticFindObject<UFunction*>(nullptr, nullptr, L"/Script/FSD.FSDGameState:PostGameMessage");
            if (g_fnPostGameMessage)
            {
                g_offPostGameMsg = FindPropOffset(g_fnPostGameMessage, L"Msg");
                if (g_offPostGameMsg >= 0)
                {
                    g_postGameHookId = g_fnPostGameMessage->RegisterPreHook(OnPostGameMessage);
                    Log(L"hook: PostGameMessage registered (Msg@%d)", g_offPostGameMsg);
                }
                else
                {
                    Log(L"hook: PostGameMessage has no Msg param, skipped");
                }
            }
        }

        if (!g_triedPostLocalized)
        {
            g_triedPostLocalized = true;
            g_fnPostLocalizedGameMessage = UObjectGlobals::StaticFindObject<UFunction*>(nullptr, nullptr, L"/Script/FSD.FSDGameState:PostLocalizedGameMessage");
            if (g_fnPostLocalizedGameMessage)
            {
                g_offPostLocalizedMsg = FindPropOffset(g_fnPostLocalizedGameMessage, L"Msg");
                if (g_offPostLocalizedMsg >= 0)
                {
                    g_postLocalizedHookId = g_fnPostLocalizedGameMessage->RegisterPreHook(OnPostLocalizedGameMessage);
                    Log(L"hook: PostLocalizedGameMessage registered (Msg@%d)", g_offPostLocalizedMsg);
                }
                else
                {
                    Log(L"hook: PostLocalizedGameMessage has no Msg param, skipped");
                }
            }
        }
        // FFSDChatMessage::Msg field offset
        if (!g_triedChatStruct)
        {
            g_triedChatStruct = true;
            UScriptStruct* chatStruct = UObjectGlobals::StaticFindObject<UScriptStruct*>(nullptr, nullptr, L"/Script/FSD.FSDChatMessage");
            if (chatStruct)
            {
                g_offClientMsgField = FindPropOffset(chatStruct, L"Msg");
                Log(L"hook: FFSDChatMessage.Msg@%d", g_offClientMsgField);
                g_offClientSenderField = FindPropOffset(chatStruct, L"Sender");
                Log(L"hook: FFSDChatMessage.Sender@%d", g_offClientSenderField);
            }
            else
            {
                Log(L"hook: FSDChatMessage struct not found");
            }
        }

        // FFSDLocalizedChatMessage::Msg field offset
        if (!g_triedLocStruct)
        {
            g_triedLocStruct = true;
            UScriptStruct* locStruct = UObjectGlobals::StaticFindObject<UScriptStruct*>(nullptr, nullptr, L"/Script/FSD.FSDLocalizedChatMessage");
            if (locStruct)
            {
                g_offLocalizedMsgField = FindPropOffset(locStruct, L"Msg");
                Log(L"hook: FFSDLocalizedChatMessage.Msg@%d", g_offLocalizedMsgField);
                g_offLocalizedSenderField = FindPropOffset(locStruct, L"Sender");
                Log(L"hook: FFSDLocalizedChatMessage.Sender@%d", g_offLocalizedSenderField);
            }
            else
            {
                Log(L"hook: FSDLocalizedChatMessage struct not found");
            }
        }

        bool serverOk = g_fnServerNewMessage && g_offServerText >= 0;
        bool clientOk = g_fnClientNewMessage && g_offClientMsgStruct >= 0 && g_offClientMsgField >= 0;
        bool locOk = g_fnClientNewLocalized && g_offLocalizedMsgStruct >= 0 && g_offLocalizedMsgField >= 0;
        bool postOk = g_fnPostGameMessage && g_offPostGameMsg >= 0;
        bool postLocOk = g_fnPostLocalizedGameMessage && g_offPostLocalizedMsg >= 0;
        bool procsOk = g_fnLocals && g_fnGetNext && g_fnFTextSetString;
        if (serverOk && clientOk && locOk && postOk && postLocOk && procsOk)
        {
            g_hooksReady = true;
            Log(L"hook: all hooks ready");
        }
    }

    // =====================================================================
    // Game-thread setup driver.
    //
    // UE4SS invokes on_update on its OWN background thread
    // ("UE4SS-UpdateThread"), which races the engine's GC/async loading
    // during level transitions (the WelcomeMod black-screen root cause).
    // All UE reflection + config reloading therefore runs from a ProcessEvent
    // pre-callback - the same mechanism Lua ExecuteInGameThread uses - on the
    // GAME thread, in sync with the engine.  Config is read/written on the
    // game thread too, which also removes the old cross-thread data race on
    // g_cfg_* (a background-thread std::wstring write could tear while the
    // game-thread hooks read it).
    // =====================================================================
    static bool g_inSetup = false;       // re-entrancy guard
    static uint64_t g_lastSetupTick = 0;

    static void GameThreadSetupImpl()
    {
        TryReloadConfig();
        if (!g_hooksReady) TryRegisterHooks();
    }

    static void GameThreadSetupCallback(UObject*, UFunction*, void*)
    {
        uint64_t now = GetTickCount64();
        if (now - g_lastSetupTick < 500) return;
        if (g_inSetup) return;
        g_inSetup = true;
        g_lastSetupTick = now;
        __try
        {
            GameThreadSetupImpl();
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            Log(L"setup: exception 0x%08X", GetExceptionCode());
        }
        g_inSetup = false;
    }

    // =====================================================================
    // Mod class
    // =====================================================================
    class MyMod : public RC::CppUserModBase
    {
    public:
        MyMod()
        {
            ModName = L"Sakura_CPP_MeowChat";
            ModVersion = L"0.1";
            ModDescription = L"Chat gets a meow suffix (crash-safe C++ port)";
            ModAuthors = L"Sakura";
            InitLogPath();
            Log(L"=== Sakura_CPP_MeowChat loaded ===");
            TryReloadConfig();   // load config.txt right away
        }

        ~MyMod() override
        {
            Log(L"mod destroyed, unregistering hooks");
            // Unregister so UE4SS hot-reload (Ctrl+R) never leaves dangling callbacks.
            if (g_serverHookId && g_fnServerNewMessage) g_fnServerNewMessage->UnregisterHook(g_serverHookId);
            if (g_clientHookId && g_fnClientNewMessage) g_fnClientNewMessage->UnregisterHook(g_clientHookId);
            if (g_localizedHookId && g_fnClientNewLocalized) g_fnClientNewLocalized->UnregisterHook(g_localizedHookId);
            if (g_postGameHookId && g_fnPostGameMessage) g_fnPostGameMessage->UnregisterHook(g_postGameHookId);
            if (g_postLocalizedHookId && g_fnPostLocalizedGameMessage) g_fnPostLocalizedGameMessage->UnregisterHook(g_postLocalizedHookId);
        }

        void on_program_start() override
        {
            Log(L"on_program_start called");
            RC::Unreal::Hook::RegisterProcessEventPreCallback(GameThreadSetupCallback);
            RC::Unreal::Hook::RegisterProcessEventPreCallback(ChatRewriteCallback);
            Log(L"setup: game-thread ProcessEvent hook registered");
        }

        void on_unreal_init() override
        {
            // All UE work now runs from the game-thread ProcessEvent callback;
            // doing reflection here can race the engine (WelcomeMod freeze).
        }

        void on_update() override
        {
            // Intentionally empty: setup/config moved to the game thread.
        }
    };
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH)
    {
        MeowChat::g_hmod = hModule;
    }
    return TRUE;
}

extern "C" __declspec(dllexport) RC::CppUserModBase* start_mod()
{
    return new MeowChat::MyMod();
}

extern "C" __declspec(dllexport) void uninstall_mod(RC::CppUserModBase* mod)
{
    delete mod;
}









