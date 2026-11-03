#include <boss.hpp>
#include "visaapi.hpp"

#include <cstdlib>
#include <cstring>

#if BOSS_WINDOWS
  #define NOMINMAX
  #include <windows.h>
  #include <mutex>
  #include <vector>
#endif

#if BOSS_WINDOWS
using ViStatus  = long;
using ViSession = unsigned long;
using ViUInt32  = unsigned long;
using ViBuf     = unsigned char*;
using ViPUInt32 = ViUInt32*;
static constexpr ViStatus VI_SUCCESS = 0;
#endif

struct VisaApi::Impl
{
    bool available = false;
    std::string last_error;
    std::string resource;

    void SetErr(const char* msg) { last_error = (msg ? msg : ""); }

    static const char* GetEnvOrNull(const char* key)
    {
        const char* v = std::getenv(key);
        return (v && v[0]) ? v : nullptr;
    }

#if BOSS_WINDOWS
    HMODULE visa = nullptr;
    ViSession rm = 0;
    ViSession inst = 0;

    using PFN_viOpenDefaultRM = ViStatus (WINAPI*)(ViSession*);
    using PFN_viOpen          = ViStatus (WINAPI*)(ViSession, const char*, ViUInt32, ViUInt32, ViSession*);
    using PFN_viClose         = ViStatus (WINAPI*)(ViSession);
    using PFN_viWrite         = ViStatus (WINAPI*)(ViSession, ViBuf, ViUInt32, ViPUInt32);
    using PFN_viRead          = ViStatus (WINAPI*)(ViSession, ViBuf, ViUInt32, ViPUInt32);

    PFN_viOpenDefaultRM viOpenDefaultRM = nullptr;
    PFN_viOpen          viOpen          = nullptr;
    PFN_viClose         viClose         = nullptr;
    PFN_viWrite         viWrite         = nullptr;
    PFN_viRead          viRead          = nullptr;

    std::once_flag load_once;
    std::mutex io_mtx;

    static bool LoadVisaOnce(Impl* m);
#endif
};

#if BOSS_WINDOWS

static FARPROC GetSym(HMODULE h, const char* name)
{
    return (h ? GetProcAddress(h, name) : nullptr);
}

static HMODULE TryLoadVisaDll()
{
    HMODULE h = LoadLibraryW(L"visa64.dll");
    if(!h) h = LoadLibraryW(L"VISA64.dll");
    return h;
}

bool VisaApi::Impl::LoadVisaOnce(Impl* m)
{
    std::call_once(m->load_once, [&]()->void
    {
        m->visa = TryLoadVisaDll();
        if(!m->visa)
        {
            m->available = false;
            m->SetErr("visa64.dll not found");
            return;
        }

        m->viOpenDefaultRM = (PFN_viOpenDefaultRM)GetSym(m->visa, "viOpenDefaultRM");
        m->viOpen          = (PFN_viOpen)GetSym(m->visa, "viOpen");
        m->viClose         = (PFN_viClose)GetSym(m->visa, "viClose");
        m->viWrite         = (PFN_viWrite)GetSym(m->visa, "viWrite");
        m->viRead          = (PFN_viRead)GetSym(m->visa, "viRead");

        if(!m->viOpenDefaultRM || !m->viOpen || !m->viClose || !m->viWrite || !m->viRead)
        {
            m->SetErr("VISA symbols missing (GetProcAddress failed)");
            FreeLibrary(m->visa);
            m->visa = nullptr;
            m->available = false;
            return;
        }

        m->available = true;
        m->last_error.clear();
    });
    return m->available;
}

#endif // BOSS_WINDOWS

VisaApi::VisaApi() : m(new Impl)
{
#if !BOSS_WINDOWS
    m->available = false;
    m->SetErr("VISA disabled (non-Windows build)");
#endif
}

VisaApi::~VisaApi()
{
    Close();

#if BOSS_WINDOWS
    if(m && m->visa)
    {
        FreeLibrary(m->visa);
        m->visa = nullptr;
    }
#endif

    delete m;
    m = nullptr;
}

VisaApi* VisaApi::Create()
{
    VisaApi* api = new (std::nothrow) VisaApi();
    if(!api) return nullptr;

#if BOSS_WINDOWS
    (void)Impl::LoadVisaOnce(api->m);
#endif
    return api;
}

void VisaApi::Destroy(VisaApi*& p)
{
    delete p;
    p = nullptr;
}

bool VisaApi::IsAvailable() const
{
#if BOSS_WINDOWS
    return m && m->available;
#else
    return false;
#endif
}

bool VisaApi::IsOpen() const
{
#if BOSS_WINDOWS
    return m && (m->inst != 0);
#else
    return false;
#endif
}

const std::string& VisaApi::LastError() const
{
    static const std::string empty;
    return m ? m->last_error : empty;
}

void VisaApi::SetResource(const char* resource)
{
    if(!m) return;
    m->resource = (resource ? resource : "");
}

const std::string& VisaApi::GetResource() const
{
    static const std::string empty;
    return m ? m->resource : empty;
}

bool VisaApi::Open()
{
    const char* r = nullptr;

    if(m && !m->resource.empty())
        r = m->resource.c_str();
    if(!r) r = Impl::GetEnvOrNull("WEERO_VISA_RESOURCE");
    if(!r) r = Impl::GetEnvOrNull("VISA_RESOURCE");

    if(!r)
    {
        if(m) m->SetErr("resource not set (SetResource or env WEERO_VISA_RESOURCE/VISA_RESOURCE)");
        return false;
    }

    return Open(r);
}

bool VisaApi::Open(const char* resource)
{
#if BOSS_WINDOWS
    if(!m) return false;
    if(!Impl::LoadVisaOnce(m)) return false;

    if(!resource || !resource[0])
    {
        m->SetErr("resource is empty");
        return false;
    }

    m->resource = resource;

    std::lock_guard<std::mutex> lk(m->io_mtx);

    if(m->inst)
    {
        m->viClose(m->inst);
        m->inst = 0;
    }

    if(m->rm == 0)
    {
        ViStatus st = m->viOpenDefaultRM(&m->rm);
        if(st < VI_SUCCESS || m->rm == 0)
        {
            m->SetErr("viOpenDefaultRM failed");
            return false;
        }
    }

    ViStatus st = m->viOpen(m->rm, resource, 0, 0, &m->inst);
    if(st < VI_SUCCESS || m->inst == 0)
    {
        m->SetErr("viOpen failed");
        return false;
    }

    m->last_error.clear();
    return true;
#else
    (void)resource;
    if(m) m->SetErr("VISA disabled (non-Windows build)");
    return false;
#endif
}

void VisaApi::Close()
{
#if BOSS_WINDOWS
    if(!m) return;

    std::lock_guard<std::mutex> lk(m->io_mtx);

    if(m->inst)
    {
        if(m->viClose) m->viClose(m->inst);
        m->inst = 0;
    }
    if(m->rm)
    {
        if(m->viClose) m->viClose(m->rm);
        m->rm = 0;
    }
#endif
}

bool VisaApi::Write(const void* data, size_t size)
{
#if BOSS_WINDOWS
    if(!m) return false;
    if(!Impl::LoadVisaOnce(m)) return false;
    if(!m->inst) { m->SetErr("instrument not open"); return false; }
    if(!data || size == 0) { m->SetErr("write buffer empty"); return false; }

    std::lock_guard<std::mutex> lk(m->io_mtx);

    ViUInt32 wrote = 0;
    ViStatus st = m->viWrite(m->inst, (ViBuf)const_cast<void*>(data), (ViUInt32)size, &wrote);
    if(st < VI_SUCCESS)
    {
        m->SetErr("viWrite failed");
        return false;
    }

    m->last_error.clear();
    return true;
#else
    (void)data; (void)size;
    if(m) m->SetErr("VISA disabled (non-Windows build)");
    return false;
#endif
}

bool VisaApi::Write(const char* text)
{
    if(!text) { if(m) m->SetErr("text is null"); return false; }
    return Write(text, std::strlen(text));
}

bool VisaApi::Read(void* out, size_t cap, size_t& out_read)
{
#if BOSS_WINDOWS
    out_read = 0;
    if(!m) return false;
    if(!Impl::LoadVisaOnce(m)) return false;
    if(!m->inst) { m->SetErr("instrument not open"); return false; }
    if(!out || cap == 0) { m->SetErr("read buffer empty"); return false; }

    std::lock_guard<std::mutex> lk(m->io_mtx);

    ViUInt32 readn = 0;
    ViStatus st = m->viRead(m->inst, (ViBuf)out, (ViUInt32)cap, &readn);
    if(st < VI_SUCCESS)
    {
        m->SetErr("viRead failed");
        return false;
    }

    out_read = (size_t)readn;
    m->last_error.clear();
    return true;
#else
    (void)out; (void)cap;
    out_read = 0;
    if(m) m->SetErr("VISA disabled (non-Windows build)");
    return false;
#endif
}

bool VisaApi::ReadText(std::string& out, size_t max_bytes)
{
    out.clear();
#if BOSS_WINDOWS
    if(max_bytes == 0) { if(m) m->SetErr("max_bytes is 0"); return false; }

    std::vector<unsigned char> buf(max_bytes);
    size_t n = 0;
    if(!Read(buf.data(), buf.size(), n)) return false;

    out.assign((const char*)buf.data(), (const char*)buf.data() + n);
    return true;
#else
    (void)max_bytes;
    if(m) m->SetErr("VISA disabled (non-Windows build)");
    return false;
#endif
}

bool VisaApi::QueryText(const char* cmd, std::string& out, size_t max_bytes)
{
    out.clear();
#if BOSS_WINDOWS
    if(!cmd || !cmd[0]) { if(m) m->SetErr("cmd is empty"); return false; }

    std::string w = cmd;
    if(w.back() != '\n') w.push_back('\n');

    if(!Write(w.data(), w.size())) return false;
    return ReadText(out, max_bytes);
#else
    (void)cmd; (void)max_bytes;
    if(m) m->SetErr("VISA disabled (non-Windows build)");
    return false;
#endif
}
