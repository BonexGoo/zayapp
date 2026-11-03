#pragma once

#include <string>
#include <cstddef>

class VisaApi
{
public:
    // Factory (matches existing usage)
    static VisaApi* Create();
    static void Destroy(VisaApi*& p);

public:
    VisaApi();
    ~VisaApi();

    VisaApi(const VisaApi&) = delete;
    VisaApi& operator=(const VisaApi&) = delete;

public:
    // State / diagnostics
    bool IsAvailable() const;   // VISA DLL loaded & symbols resolved (Windows only)
    bool IsOpen() const;        // Instrument session open
    const std::string& LastError() const;

public:
    // Connection
    // Open() with no args tries, in order:
    // 1) the resource previously set via SetResource()
    // 2) environment variable WEERO_VISA_RESOURCE
    // 3) environment variable VISA_RESOURCE
    bool Open();
    bool Open(const char* resource);
    void Close();

    void SetResource(const char* resource);
    const std::string& GetResource() const;

public:
    // I/O
    bool Write(const void* data, size_t size);
    bool Write(const char* text); // convenience (matches existing usage)

    bool Read(void* out, size_t cap, size_t& out_read);
    bool ReadText(std::string& out, size_t max_bytes = 4096);

    // Base query into std::string (implementation in .cpp)
    bool QueryText(const char* cmd, std::string& out, size_t max_bytes = 4096);

    // Compatibility: Query(cmd, outVar) where outVar can be std::string or BOSS::String etc.
    // Requirement: outVar must be assignable from const char* (outVar = "...").
    template<typename OutStringLike>
    bool Query(const char* cmd, OutStringLike& out, size_t max_bytes = 4096)
    {
        std::string tmp;
        if(!QueryText(cmd, tmp, max_bytes))
            return false;
        out = tmp.c_str();
        return true;
    }

private:
    struct Impl;
    Impl* m;
};
