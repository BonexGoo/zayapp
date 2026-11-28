#include "file_system_engine.hpp"

#if defined(__EMSCRIPTEN__) || defined(BOSS_FILE_SYSTEM_DISABLE_NATIVE)
namespace BOSS::FileSystem {
struct Engine::Impl {};
Engine::Engine(std::string, std::string) : p_(new Impl) {}
Engine::~Engine() = default;
std::string Engine::Capabilities() { return "{\"available\":0,\"platform\":\"wasm\",\"version\":2,\"error_code\":\"UNSUPPORTED_PLATFORM\"}"; }
std::string Engine::Drives() { return "{\"count\":0,\"items\":[]}"; }
std::string Engine::PickDirectory() { return {}; }
#define BOSS_FS_STUB(name,args) std::string Engine::name args { return "{\"ok\":0,\"error_code\":\"UNSUPPORTED_PLATFORM\"}"; }
BOSS_FS_STUB(Start,(const std::string&)) BOSS_FS_STUB(Control,(const std::string&))
BOSS_FS_STUB(Poll,() const) BOSS_FS_STUB(Items,(const std::string&,int,int) const)
BOSS_FS_STUB(Item,(const std::string&) const) BOSS_FS_STUB(Preview,(const std::string&))
BOSS_FS_STUB(Issues,(int,int) const)
BOSS_FS_STUB(PreviewPoll,() const) BOSS_FS_STUB(PreviewRelease,())
BOSS_FS_STUB(Reveal,(const std::string&) const) BOSS_FS_STUB(SetRules,(int,double))
BOSS_FS_STUB(SaveSnapshot,()) BOSS_FS_STUB(Snapshots,() const) BOSS_FS_STUB(LoadSnapshot,(const std::string&))
BOSS_FS_STUB(ExportReport,()) BOSS_FS_STUB(TaskPoll,() const) BOSS_FS_STUB(TaskCancel,())
#undef BOSS_FS_STUB
}
#else
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdint>
#include <ctime>
#include <cstdio>
#include <cstdlib>
#include <cwctype>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <limits>
#include <map>
#include <mutex>
#include <set>
#include <sstream>
#include <stdexcept>
#include <thread>
#include <vector>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shlobj.h>
#include <shellapi.h>
#include <objbase.h>
#include <wincodec.h>
#ifdef _MSC_VER
#pragma comment(lib,"Shell32.lib")
#pragma comment(lib,"Ole32.lib")
#pragma comment(lib,"Windowscodecs.lib")
#endif
#else
#include <sys/stat.h>
#endif

namespace BOSS::FileSystem {
namespace fs = std::filesystem;
using Clock = std::chrono::steady_clock;
namespace {
const uint64_t GiB = uint64_t(1024) * 1024 * 1024;
const size_t NoParent = std::numeric_limits<size_t>::max();
std::string cleanUtf8(const std::string& text) {
    std::string clean;clean.reserve(text.size());
    for(size_t i=0;i<text.size();) {
        const auto c=static_cast<unsigned char>(text[i]);
        const size_t n=c<0x80?1:((c>=0xC2&&c<=0xDF)?2:((c>=0xE0&&c<=0xEF)?3:((c>=0xF0&&c<=0xF4)?4:0)));
        bool valid=n&&i+n<=text.size();
        for(size_t j=1;valid&&j<n;++j)valid=(static_cast<unsigned char>(text[i+j])&0xC0)==0x80;
        if(valid&&n>=3){const auto c1=static_cast<unsigned char>(text[i+1]);if((c==0xE0&&c1<0xA0)||(c==0xED&&c1>=0xA0)||(c==0xF0&&c1<0x90)||(c==0xF4&&c1>=0x90))valid=false;}
        if(valid){clean.append(text,i,n);i+=n;}else{clean+="\xEF\xBF\xBD";++i;}
    }
    return clean;
}
std::string quote(const std::string& s) {
    std::string out = "\"";
    static const char* hex = "0123456789abcdef";
    for (unsigned char c : cleanUtf8(s)) {
        if (c == '"' || c == '\\') { out += '\\'; out += char(c); }
        else if (c < 32) { out += "\\u00"; out += hex[c >> 4]; out += hex[c & 15]; }
        else out += char(c);
    }
    return out + '"';
}
std::string number(uint64_t n) { return quote(std::to_string(n)); }
std::string fixed(double n, int places=1) {
    std::ostringstream o; o.imbue(std::locale::classic());
    o << std::fixed << std::setprecision(places) << n; return o.str();
}
std::string amount(uint64_t bytes) { return fixed(double(bytes) / double(GiB)); }
std::string countText(uint64_t n) {
    auto s=std::to_string(n);
    for (int i=int(s.size())-3; i>0; i-=3) s.insert(size_t(i),",");
    return s;
}
std::string err(const char* code, const std::string& message={}) {
    return "{\"ok\":0,\"state\":\"failed\",\"error_code\":"+quote(code)+",\"message\":"+quote(message)+"}";
}
std::string ok() { return "{\"ok\":1,\"error_code\":\"\"}"; }
bool boundedQuoted(std::istream& in,std::string& out,size_t maximum) {
    in>>std::ws;out.clear();char c=0;if(!in.get(c)||c!='"')return false;
    while(in.get(c)){
        if(c=='"')return true;
        if(c=='\\'&&!in.get(c))return false;
        if(out.size()>=maximum)return false;
        out+=c;
    }
    return false;
}
std::string utf8(const fs::path& p) { return p.u8string(); }
fs::path path(const std::string& s) { return fs::u8path(s); }
fs::path nativePath(fs::path p) {
#ifdef _WIN32
    p.make_preferred();
    auto w=p.wstring();
    if (w.rfind(L"\\\\?\\",0)==0) return p;
    if (w.rfind(L"\\\\",0)==0) return fs::path(L"\\\\?\\UNC\\"+w.substr(2));
    if (p.is_absolute()) return fs::path(L"\\\\?\\"+w);
#endif
    return p;
}
// A Windows root must not end with a backslash in a ZaySon string literal.
// The public path form also avoids JSON/Solver double escaping on every file row.
#ifdef _WIN32
std::string windowsDisplayPath(std::string s) {
    if (s.rfind("\\\\?\\UNC\\",0)==0) s="\\\\"+s.substr(8);
    else if (s.rfind("\\\\?\\",0)==0) s=s.substr(4);
    std::replace(s.begin(),s.end(),'\\','/');
    return s;
}
#endif
std::string displayPath(const fs::path& p) {
#ifdef _WIN32
    return windowsDisplayPath(utf8(p));
#else
    return utf8(p);
#endif
}
bool inside(const fs::path& p, const fs::path& root) {
    auto a=p.begin(),b=root.begin();
    for (; b!=root.end(); ++b,++a) {
        if (a==p.end()) return false;
#ifdef _WIN32
        auto x=a->wstring(),y=b->wstring();
        std::transform(x.begin(),x.end(),x.begin(),::towlower);
        std::transform(y.begin(),y.end(),y.begin(),::towlower);
        if (x!=y) return false;
#else
        if (*a!=*b) return false;
#endif
    }
    return true;
}
int64_t nowSeconds() { return int64_t(std::time(nullptr)); }
int64_t modified(const fs::path& p, std::error_code& ec) {
    auto t=fs::last_write_time(p,ec); if(ec) return 0;
    auto system = std::chrono::time_point_cast<std::chrono::seconds>(
        t-fs::file_time_type::clock::now()+std::chrono::system_clock::now());
    return system.time_since_epoch().count();
}
std::string dateText(int64_t t) {
    if(t<=0) return "—";
    auto time=std::time_t(t); std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm,&time);
#else
    localtime_r(&time,&tm);
#endif
    char b[32]; std::strftime(b,sizeof(b),"%Y.%m.%d",&tm); return b;
}
std::string uniqueName() {
    auto us=std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    static std::atomic<uint64_t> serial{0}; return std::to_string(us)+"_"+std::to_string(++serial);
}
std::string lower(std::string s) { for(auto& c:s) if(c>='A'&&c<='Z') c=char(c+32); return s; }
int typeOf(const fs::path& p) {
    auto x=lower(utf8(p.extension()));
    const std::array<std::string,4> ext={{
        ".mp4 .mov .mkv .avi .wmv .webm .m4v .mpeg .mpg .mts ",
        ".zip .7z .rar .gz .bz2 .tar .xz .iso .cab .zst ",
        ".cpp .c .h .hpp .cc .cxx .py .js .ts .jsx .tsx .java .cs .go .rs .swift .zay .json .yaml .yml .xml .html .css .sql .sh .obj .o .lib .a .pdb ",
        ".png .jpg .jpeg .webp .bmp .gif .tif .tiff .heic .avif .svg .psd .raw .dng "
    }};
    for(int i=0;i<4;++i) if(!x.empty()&&ext[size_t(i)].find(x+" ")!=std::string::npos) return i;
    return 4;
}
bool reparse(const fs::path& p) {
#ifdef _WIN32
    DWORD a=GetFileAttributesW(p.c_str()); return a!=INVALID_FILE_ATTRIBUTES&&(a&FILE_ATTRIBUTE_REPARSE_POINT);
#else
    std::error_code ec; return fs::is_symlink(fs::symlink_status(p,ec));
#endif
}
bool offline(const fs::path& p) {
#ifdef _WIN32
    DWORD a=GetFileAttributesW(p.c_str()); if(a==INVALID_FILE_ATTRIBUTES) return false;
    DWORD flags=FILE_ATTRIBUTE_OFFLINE;
#ifdef FILE_ATTRIBUTE_RECALL_ON_OPEN
    flags |= FILE_ATTRIBUTE_RECALL_ON_OPEN;
#endif
#ifdef FILE_ATTRIBUTE_RECALL_ON_DATA_ACCESS
    flags |= FILE_ATTRIBUTE_RECALL_ON_DATA_ACCESS;
#endif
    return (a&flags)!=0;
#else
    (void)p; return false;
#endif
}
struct Identity { std::string key; uint64_t allocation=0; int64_t created=0; bool allocationKnown=false; };
Identity identity(const fs::path& p, bool directory) {
    Identity out;
#ifdef _WIN32
    HANDLE h=CreateFileW(p.c_str(),FILE_READ_ATTRIBUTES,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,
        nullptr,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT|FILE_FLAG_BACKUP_SEMANTICS,nullptr);
    if(h==INVALID_HANDLE_VALUE) return out;
    BY_HANDLE_FILE_INFORMATION i{};
    if(GetFileInformationByHandle(h,&i)) {
        out.key=std::to_string(i.dwVolumeSerialNumber)+":"+std::to_string(i.nFileIndexHigh)+":"+std::to_string(i.nFileIndexLow);
        ULARGE_INTEGER created{}; created.LowPart=i.ftCreationTime.dwLowDateTime; created.HighPart=i.ftCreationTime.dwHighDateTime;
        if(created.QuadPart>=116444736000000000ULL) out.created=int64_t((created.QuadPart-116444736000000000ULL)/10000000ULL);
    }
    FILE_STANDARD_INFO standard{};
    if(!directory&&GetFileInformationByHandleEx(h,FileStandardInfo,&standard,sizeof(standard))) {
        out.allocation=uint64_t(standard.AllocationSize.QuadPart);out.allocationKnown=true;
    }
    CloseHandle(h);
#else
    (void)directory; struct stat s{};
    if(::lstat(p.c_str(),&s)==0) {
        out.key=std::to_string(uint64_t(s.st_dev))+":"+std::to_string(uint64_t(s.st_ino));
        out.allocation=uint64_t(s.st_blocks)*512;out.allocationKnown=true;
    }
#endif
    return out;
}
std::string csv(const std::string& s) {
    std::string clean=cleanUtf8(s);
    // Spreadsheet formula injection: exported paths and names are text.
    if(!clean.empty()&&(clean[0]=='='||clean[0]=='+'||clean[0]=='-'||clean[0]=='@'||clean[0]=='\t'||clean[0]=='\r')) clean="'"+clean;
    std::string o="\"";for(char c:clean){if(c=='"')o+='"';o+=c;}return o+'"';
}
#ifdef _WIN32
template<typename T> struct ComPointer {
    T* p=nullptr;~ComPointer(){if(p)p->Release();}
    T** out(){return &p;}T* operator->()const{return p;}
};
struct ComApartment {
    HRESULT result=CoInitializeEx(nullptr,COINIT_MULTITHREADED);
    ~ComApartment(){if(SUCCEEDED(result))CoUninitialize();}
};
std::string thumbnail(const fs::path& input,const fs::path& output,UINT& width,UINT& height) {
    ComApartment apartment;if(FAILED(apartment.result))return "PREVIEW_FAILED";
    ComPointer<IWICImagingFactory> factory;
    if(FAILED(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_IWICImagingFactory,reinterpret_cast<void**>(factory.out()))))return "PREVIEW_UNSUPPORTED";
    ComPointer<IWICBitmapDecoder> decoder;ComPointer<IWICBitmapFrameDecode> frame;
    if(FAILED(factory->CreateDecoderFromFilename(input.c_str(),&GUID_VendorMicrosoft,GENERIC_READ,WICDecodeMetadataCacheOnDemand,decoder.out()))||FAILED(decoder->GetFrame(0,frame.out())))return "PREVIEW_UNSUPPORTED";
    UINT originalW=0,originalH=0;
    if(FAILED(frame->GetSize(&originalW,&originalH))||originalW==0||originalH==0)return "PREVIEW_FAILED";
    if(uint64_t(originalW)*originalH>40000000ULL)return "PREVIEW_LIMIT_EXCEEDED";
    const double scale=std::min(1.0,1024.0/double(std::max(originalW,originalH)));
    width=std::max(1u,UINT(std::floor(originalW*scale)));height=std::max(1u,UINT(std::floor(originalH*scale)));
    ComPointer<IWICBitmapScaler> scaler;ComPointer<IWICFormatConverter> converter;
    if(FAILED(factory->CreateBitmapScaler(scaler.out()))||FAILED(scaler->Initialize(frame.p,width,height,WICBitmapInterpolationModeFant))||FAILED(factory->CreateFormatConverter(converter.out()))||FAILED(converter->Initialize(scaler.p,GUID_WICPixelFormat32bppBGRA,WICBitmapDitherTypeNone,nullptr,0,WICBitmapPaletteTypeCustom)))return "PREVIEW_FAILED";
    std::vector<BYTE> pixels(size_t(width)*height*4);
    if(FAILED(converter->CopyPixels(nullptr,width*4,UINT(pixels.size()),pixels.data())))return "PREVIEW_FAILED";
    ComPointer<IWICStream> stream;ComPointer<IWICBitmapEncoder> encoder;ComPointer<IWICBitmapFrameEncode> encodedFrame;
    if(FAILED(factory->CreateStream(stream.out()))||FAILED(stream->InitializeFromFilename(output.c_str(),GENERIC_WRITE))||FAILED(factory->CreateEncoder(GUID_ContainerFormatPng,&GUID_VendorMicrosoft,encoder.out()))||FAILED(encoder->Initialize(stream.p,WICBitmapEncoderNoCache))||FAILED(encoder->CreateNewFrame(encodedFrame.out(),nullptr))||FAILED(encodedFrame->Initialize(nullptr))||FAILED(encodedFrame->SetSize(width,height)))return "PREVIEW_FAILED";
    WICPixelFormatGUID format=GUID_WICPixelFormat32bppBGRA;
    if(FAILED(encodedFrame->SetPixelFormat(&format))||!IsEqualGUID(format,GUID_WICPixelFormat32bppBGRA)||FAILED(encodedFrame->WritePixels(height,width*4,UINT(pixels.size()),pixels.data()))||FAILED(encodedFrame->Commit())||FAILED(encoder->Commit()))return "PREVIEW_FAILED";
    return {};
}
#endif
}

struct Engine::Impl {
    struct Entry {
        fs::path location; std::string id, name, identityKey;
        size_t parent=NoParent; bool directory=false, complete=false, uncertain=false, local=true;
        uint64_t bytes=0, allocation=0, files=0; bool allocationKnown=true;
        int64_t mtime=0, created=0; int type=4;
    };
    struct Issue { std::string code,path; };
    mutable std::mutex mutex; std::condition_variable wake;
    std::thread worker,taskWorker,previewWorker;
    std::atomic<bool> cancel{false},pause{false},taskCancel{false};
    std::vector<Entry> entries;std::vector<size_t> folderIndices;
    std::vector<Issue> issues;uint64_t issueTotal=0;
    std::set<std::string> allocatedIds;
    std::array<uint64_t,5> typeBytes{},typeCounts{};
    fs::path root,cache;
    std::string assetPrefix;
    std::string state="idle",sourceState,sourceRoot,currentPath,lastError;
    std::string taskState="idle",taskKind,taskResult,taskError;
    std::condition_variable previewWake;bool previewStop=false,previewPending=false;
    uint64_t previewSequence=0;Entry previewEntry;fs::path previewFile;
    std::string previewJson="{\"state\":\"idle\",\"kind\":\"none\"}";
    uint64_t fileCount=0,folderCount=0,logical=0,allocated=0,unknownAllocation=0,revision=0;
    int unchangedDays=0;uint64_t minimumBytes=0;
    uint64_t candidateVersion=0;
    mutable uint64_t cachedCandidateVersion=std::numeric_limits<uint64_t>::max();
    mutable std::vector<size_t> cachedCandidates;
    mutable uint64_t cachedCandidateBytes=0;
    bool history=false;std::string generation=uniqueName();
    Clock::time_point started=Clock::now(),ended=started;
    int64_t observed=nowSeconds();
    Impl(std::string cachePath,std::string prefix):assetPrefix(std::move(prefix)) {
        if(cachePath.empty())throw std::invalid_argument("cacheDirectory must not be empty");
        cache=nativePath(fs::absolute(path(cachePath)));
        std::replace(assetPrefix.begin(),assetPrefix.end(),'\\','/');
        if(!assetPrefix.empty()) {
            if(assetPrefix.front()=='/'||assetPrefix.find(':')!=std::string::npos)
                throw std::invalid_argument("assetPrefix must be a relative resource prefix");
            for(const auto& part:path(assetPrefix))if(part=="..")
                throw std::invalid_argument("assetPrefix must not contain parent traversal");
            if(assetPrefix.back()!='/')assetPrefix+='/';
        }
    }
    ~Impl(){cancel=true;pause=false;taskCancel=true;{std::lock_guard<std::mutex>lock(mutex);previewStop=true;++previewSequence;}wake.notify_all();previewWake.notify_all();if(worker.joinable())worker.join();if(taskWorker.joinable())taskWorker.join();if(previewWorker.joinable())previewWorker.join();std::error_code ec;if(!previewFile.empty())fs::remove(previewFile,ec);}
    bool active()const {return state=="running"||state=="paused"||state=="pausing"||state=="cancelling";}
    void issue(const std::string& code,const fs::path& location,size_t parent=NoParent) {
        ++issueTotal;if(issues.size()<4096)issues.push_back({code,displayPath(location)});
        for(size_t p=parent;p!=NoParent;p=entries[p].parent){entries[p].complete=false;entries[p].uncertain=true;}
        ++candidateVersion;
    }
    bool candidate(const Entry& e) const {
        const auto cutoff=observed-int64_t(unchangedDays)*86400;
        return e.directory&&e.parent!=NoParent&&e.complete&&e.files>0&&e.bytes>=minimumBytes&&e.mtime>0&&e.mtime<cutoff&&e.created<cutoff;
    }
    const std::vector<size_t>& candidates()const {
        if(cachedCandidateVersion==candidateVersion)return cachedCandidates;
        std::vector<size_t> result;
        for(auto i:folderIndices) if(candidate(entries[i])) {
            bool ancestor=false;
            for(size_t p=entries[i].parent;p!=NoParent;p=entries[p].parent) if(candidate(entries[p])) {ancestor=true;break;}
            if(!ancestor)result.push_back(i);
        }
        std::sort(result.begin(),result.end(),[&](size_t a,size_t b){return entries[a].bytes!=entries[b].bytes?entries[a].bytes>entries[b].bytes:a<b;});
        cachedCandidates=std::move(result);cachedCandidateBytes=0;
        for(auto i:cachedCandidates)cachedCandidateBytes+=entries[i].bytes;
        cachedCandidateVersion=candidateVersion;
        return cachedCandidates;
    }
    std::string entryJson(const Entry& e,bool detail=false)const {
        const auto age=e.mtime>0?std::max<int64_t>(0,(observed-e.mtime)/86400):0;
        std::string s="{\"id\":"+quote(e.id)+",\"name\":"+quote(e.name)+",\"path\":"+quote(displayPath(e.location))+
            ",\"kind\":"+quote(e.directory?"folder":"file")+",\"gib\":"+quote(amount(e.bytes))+
            ",\"bytes\":"+number(e.bytes)+",\"files\":"+number(e.files)+",\"files_text\":"+quote(countText(e.files))+
            ",\"mtime_text\":"+quote(dateText(e.mtime))+",\"mtime_epoch\":"+std::to_string(e.mtime)+
            ",\"age_days\":"+std::to_string(age)+",\"coverage\":"+quote(e.complete?"complete":"partial")+
            ",\"local\":"+(e.local?"1":"0");
        if(detail) s+=",\"reason_code\":"+quote(candidate(e)?"SUBTREE_UNCHANGED":"INSPECT_CONTENT")+
            ",\"reason\":"+quote(candidate(e)?"SUBTREE_UNCHANGED":"INSPECT_CONTENT")+
            ",\"reclaimable_bytes\":null,\"actual_last_use_known\":0";
        return s+"}";
    }
    bool lookup(const std::string& id,Entry& e)const {
        // Index IDs include a per-scan generation. A stale ID never aliases a new scan.
        const auto prefix=generation+"_";if(id.rfind(prefix,0)!=0)return false;
        try {size_t used=0;auto i=std::stoull(id.substr(prefix.size()),&used);if(used!=id.size()-prefix.size()||i>=entries.size())return false;e=entries[size_t(i)];return true;}catch(...){return false;}
    }
    std::string validate(const Entry& e)const {
        if(history)return "HISTORY_READ_ONLY";
        std::error_code ec;
        if(reparse(e.location))return "ITEM_CHANGED";
        const auto current=fs::weakly_canonical(e.location,ec);if(ec||!inside(current,root))return "SCOPE_REVOKED";
        auto status=fs::symlink_status(e.location,ec);if(ec||!fs::exists(status))return "ITEM_NOT_FOUND";
        auto info=identity(e.location,e.directory);
        if(e.identityKey.empty()||info.key.empty()||info.key!=e.identityKey)return "ITEM_CHANGED";
        if(!e.directory) {
            const auto size=fs::file_size(e.location,ec);if(ec||size!=e.bytes)return "ITEM_CHANGED";
            auto time=modified(e.location,ec);if(ec||std::llabs(time-e.mtime)>1)return "ITEM_CHANGED";
        }
        return {};
    }
    void run() {
        struct Frame {size_t index;fs::directory_iterator it,end;bool valid=true;};
        std::vector<Frame> stack;
        auto open=[&](size_t idx) {
            std::error_code ec;fs::directory_iterator it(entries[idx].location,fs::directory_options::none,ec);
            if(ec){std::lock_guard<std::mutex>lock(mutex);issue("PERMISSION_DENIED",entries[idx].location,idx);return false;}
            stack.push_back({idx,it,{},true});return true;
        };
        open(0);
        while(!stack.empty()&&!cancel) {
            if(pause) {
                std::unique_lock<std::mutex>lock(mutex);state="paused";++revision;
                wake.wait(lock,[&]{return !pause||cancel;});if(cancel)break;state="running";++revision;
            }
            auto& f=stack.back();
            if(f.it==f.end) {
                std::lock_guard<std::mutex>lock(mutex);auto& e=entries[f.index];
                e.complete=!e.uncertain;
                if(e.parent!=NoParent)entries[e.parent].uncertain=entries[e.parent].uncertain||!e.complete;
                ++candidateVersion;
                stack.pop_back();++revision;continue;
            }
            const auto item=f.it->path();const size_t parent=f.index;
            std::error_code ec;f.it.increment(ec);
            if(ec){std::lock_guard<std::mutex>lock(mutex);issue("ENUMERATION_FAILED",entries[parent].location,parent);f.it={};}
            if(reparse(item)) {std::lock_guard<std::mutex>lock(mutex);issue("LINK_TARGET_EXCLUDED",item,parent);continue;}
            auto status=fs::symlink_status(item,ec);
            if(ec){std::lock_guard<std::mutex>lock(mutex);issue("ITEM_NOT_FOUND",item,parent);continue;}
            const bool dir=fs::is_directory(status);if(!dir&&!fs::is_regular_file(status))continue;
            Entry e;e.location=item;e.parent=parent;e.name=utf8(item.filename());e.directory=dir;e.complete=!dir;e.local=!offline(item);e.type=typeOf(item);
            auto identityInfo=identity(item,dir);e.identityKey=identityInfo.key;e.created=identityInfo.created;
            if(!dir) {
                e.bytes=fs::file_size(item,ec);
                if(ec){std::lock_guard<std::mutex>lock(mutex);issue("METADATA_UNAVAILABLE",item,parent);continue;}
                e.mtime=modified(item,ec);if(ec){e.mtime=0;e.complete=false;}
                e.files=1;e.allocation=identityInfo.allocation;e.allocationKnown=identityInfo.allocationKnown&&!identityInfo.key.empty();
            }
            size_t idx;
            {
                std::lock_guard<std::mutex>lock(mutex);idx=entries.size();e.id=generation+"_"+std::to_string(idx);
                if(dir){folderIndices.push_back(idx);++folderCount;
                    for(size_t parentIndex=parent;parentIndex!=NoParent;parentIndex=entries[parentIndex].parent)
                        entries[parentIndex].created=std::max(entries[parentIndex].created,e.created);
                }
                else {
                    ++fileCount;logical+=e.bytes;typeBytes[size_t(e.type)]+=e.bytes;++typeCounts[size_t(e.type)];
                    if(e.allocationKnown) {if(!e.identityKey.empty()&&allocatedIds.insert(e.identityKey).second)allocated+=e.allocation;}
                    else ++unknownAllocation;
                    // Propagate each observed file immediately. Partial/cancelled trees
                    // retain correct known totals even if directory frames never close.
                    for(size_t parentIndex=parent;parentIndex!=NoParent;parentIndex=entries[parentIndex].parent) {
                        auto& p=entries[parentIndex];p.bytes+=e.bytes;p.files+=1;p.allocation+=e.allocation;
                        p.allocationKnown=p.allocationKnown&&e.allocationKnown;p.mtime=std::max(p.mtime,e.mtime);
                        p.created=std::max(p.created,e.created);p.uncertain=p.uncertain||!e.complete;
                    }
                }
                currentPath=displayPath(item);entries.push_back(e);++revision;
            }
            if(dir)open(idx);
        }
        {
            std::lock_guard<std::mutex>lock(mutex);
            if(cancel) {for(auto& f:stack)entries[f.index].complete=false;state="cancelled";}
            else state=lastError.empty()?"completed":"failed";
            ended=Clock::now();++revision;++candidateVersion;
        }
    }
    std::string taskJson()const {
        return "{\"ok\":"+(taskState=="failed"?std::string("0"):std::string("1"))+",\"state\":"+quote(taskState)+",\"kind\":"+quote(taskKind)+",\"path\":"+quote(taskResult)+",\"error_code\":"+quote(taskError)+"}";
    }
    void previewLoop() {
#ifdef _WIN32
        for(;;) {
            Entry e;uint64_t sequence;
            {std::unique_lock<std::mutex>lock(mutex);previewWake.wait(lock,[&]{return previewStop||previewPending;});if(previewStop)return;e=previewEntry;sequence=previewSequence;previewPending=false;}
            std::string failure;{std::lock_guard<std::mutex>lock(mutex);failure=validate(e);}
            if(failure.empty()&&offline(e.location))failure="CONTENT_NOT_LOCAL";
            const auto token=uniqueName();auto output=cache/"preview"/(token+".png");auto temporary=output;temporary+=".tmp";
            std::error_code ec;UINT width=0,height=0;const auto begin=Clock::now();
            if(failure.empty()){fs::create_directories(output.parent_path(),ec);if(ec)failure="IO_ERROR";}
            if(failure.empty())failure=thumbnail(e.location,temporary,width,height);
            if(failure.empty()&&Clock::now()-begin>std::chrono::seconds(5))failure="PREVIEW_TIMEOUT";
            {std::lock_guard<std::mutex>lock(mutex);if(failure.empty())failure=validate(e);
                if(previewStop||sequence!=previewSequence){fs::remove(temporary,ec);continue;}
                if(failure.empty()){fs::rename(temporary,output,ec);if(ec)failure="IO_ERROR";}
                if(failure.empty()){previewFile=output;previewJson="{\"ok\":1,\"state\":\"ready\",\"kind\":\"image\",\"item_id\":"+quote(e.id)+",\"filename\":"+quote(e.name)+",\"asset\":"+quote(assetPrefix+"preview/"+token+".png")+",\"width\":"+std::to_string(width)+",\"height\":"+std::to_string(height)+",\"text\":\"\"}";}
                else {fs::remove(temporary,ec);previewJson=err(failure.c_str());}
            }
        }
#endif
    }
};

Engine::Engine(std::string cache,std::string assetPrefix):p_(new Impl(std::move(cache),std::move(assetPrefix))){}
Engine::~Engine()=default;
std::string Engine::Capabilities() {
#ifdef _WIN32
    return "{\"available\":1,\"platform\":\"windows\",\"version\":2,\"read_only\":1,\"reveal\":1,\"folder_picker\":1,\"preview_image\":1,\"preview_text\":1,\"snapshots\":1,\"report_export\":1,\"max_page\":200}";
#else
    return "{\"available\":1,\"platform\":\"native\",\"version\":2,\"read_only\":1,\"reveal\":0,\"folder_picker\":0,\"preview_image\":0,\"preview_text\":1,\"snapshots\":1,\"report_export\":1,\"max_page\":200}";
#endif
}
std::string Engine::Drives() {
    std::vector<fs::path> roots;
#ifdef _WIN32
    const DWORD mask=GetLogicalDrives();
    if(mask==0)return err("VOLUME_ENUMERATION_FAILED");
    for(int i=0;i<26;++i)if(mask&(1u<<i)){std::wstring w=L"A:\\";w[0]=wchar_t(L'A'+i);auto type=GetDriveTypeW(w.c_str());if(type==DRIVE_FIXED||type==DRIVE_REMOVABLE)roots.emplace_back(w);}
#else
    roots.emplace_back("/");
#endif
    std::string s="{\"ok\":1,\"count\":"+std::to_string(roots.size())+",\"items\":[";bool first=true;
    for(auto& root:roots){std::error_code ec;auto v=fs::space(root,ec);if(!first)s+=',';first=false;
        s+="{\"path\":"+quote(displayPath(root))+",\"name\":"+quote(displayPath(root))+",\"label\":"+quote(displayPath(root))+",\"ready\":"+(ec?"0":"1")+
        ",\"total_bytes\":"+(ec?std::string("null"):number(v.capacity))+",\"free_bytes\":"+(ec?std::string("null"):number(v.free))+
        ",\"total_gib\":"+quote(ec?"—":amount(v.capacity))+",\"free_gib\":"+quote(ec?"—":amount(v.free))+"}";}
    return s+"]}";
}
std::string Engine::PickDirectory() {
#ifdef _WIN32
    const HRESULT initialized=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
    BROWSEINFOW info{};info.ulFlags=BIF_RETURNONLYFSDIRS|BIF_NEWDIALOGSTYLE;info.lpszTitle=L"Choose a folder";
    auto pidl=SHBrowseForFolderW(&info);std::string result;
    if(pidl){wchar_t b[32768]{};if(SHGetPathFromIDListEx(pidl,b,32768,GPFIDL_DEFAULT))result=displayPath(fs::path(b));CoTaskMemFree(pidl);}
    if(SUCCEEDED(initialized))CoUninitialize();return result;
#else
    return {};
#endif
}
std::string Engine::Start(const std::string& requested) {
    {std::lock_guard<std::mutex>lock(p_->mutex);if(p_->active())return err("BUSY");}
    PreviewRelease();
    if(p_->worker.joinable())p_->worker.join();
    std::error_code ec;auto root=nativePath(fs::absolute(path(requested),ec));
    if(requested.empty()||ec)return err("INVALID_PATH");
    if(!fs::is_directory(root,ec)||ec)return err("VOLUME_UNAVAILABLE");
    root=fs::weakly_canonical(root,ec);if(ec)return err("PERMISSION_DENIED");
    {
        std::lock_guard<std::mutex>lock(p_->mutex);p_->root=root;p_->entries.clear();p_->folderIndices.clear();p_->issues.clear();p_->issueTotal=0;p_->allocatedIds.clear();
        p_->fileCount=p_->folderCount=p_->logical=p_->allocated=p_->unknownAllocation=0;p_->typeBytes.fill(0);p_->typeCounts.fill(0);
        p_->cancel=false;p_->pause=false;p_->history=false;p_->generation=uniqueName();p_->state="running";p_->lastError.clear();p_->sourceState.clear();p_->sourceRoot.clear();++p_->candidateVersion;
        p_->currentPath=displayPath(root);p_->started=p_->ended=Clock::now();p_->observed=nowSeconds();++p_->revision;
        Impl::Entry r;r.location=root;r.name=displayPath(root);r.directory=true;r.complete=false;r.id=p_->generation+"_0";r.identityKey=identity(root,true).key;p_->entries.push_back(r);p_->folderIndices.push_back(0);
    }
    p_->worker=std::thread([impl=p_.get()]{try{impl->run();}catch(const std::exception&){std::lock_guard<std::mutex>lock(impl->mutex);impl->state="failed";impl->lastError="IO_ERROR";impl->ended=Clock::now();++impl->revision;}});
    return Poll();
}
std::string Engine::Control(const std::string& action) {
    std::lock_guard<std::mutex>lock(p_->mutex);
    if(p_->history)return err("HISTORY_READ_ONLY");
    if(action=="pause"&&p_->state=="running"){p_->pause=true;p_->state="pausing";}
    else if(action=="resume"&&(p_->state=="paused"||p_->state=="pausing")){p_->pause=false;p_->state="running";p_->wake.notify_all();}
    else if(action=="cancel"&&p_->active()){p_->cancel=true;p_->pause=false;p_->state="cancelling";p_->wake.notify_all();}
    else return err("INVALID_STATE");
    ++p_->revision;return "{\"ok\":1,\"state\":"+quote(p_->state)+"}";
}
std::string Engine::Poll()const {
    std::lock_guard<std::mutex>lock(p_->mutex);
    const auto elapsed=std::chrono::duration_cast<std::chrono::seconds>((p_->active()?Clock::now():p_->ended)-p_->started).count();
    char duration[48];std::snprintf(duration,sizeof(duration),"%02lld:%02lld",static_cast<long long>(elapsed/60),static_cast<long long>(elapsed%60));
    const auto& candidates=p_->candidates();const uint64_t candidateBytes=p_->cachedCandidateBytes;
    std::string s="{\"ok\":1,\"state\":"+quote(p_->state)+",\"scan_id\":"+quote(p_->entries.empty()?std::string():p_->generation)+",\"revision\":"+number(p_->revision)+",\"root_path\":"+quote(displayPath(p_->root))+
        ",\"current_path\":"+quote(p_->currentPath)+",\"files_seen\":"+number(p_->fileCount)+",\"directories_seen\":"+number(p_->folderCount)+
        ",\"logical_bytes\":"+number(p_->logical)+",\"gib_text\":"+quote(amount(p_->logical))+",\"files_text\":"+quote(countText(p_->fileCount))+
        ",\"folders_text\":"+quote(countText(p_->folderCount))+",\"allocated_bytes\":"+(p_->unknownAllocation?std::string("null"):number(p_->allocated))+
        ",\"allocation_status\":"+quote(p_->unknownAllocation?"partial":"known")+",\"issue_count\":"+number(p_->issueTotal)+
        ",\"coverage\":"+quote((p_->issueTotal||p_->state!="completed"||p_->entries.empty()||!p_->entries.front().complete)?"partial":"complete")+",\"elapsed_seconds\":"+std::to_string(elapsed)+
        ",\"elapsed_text\":"+quote(duration)+",\"progress_known\":0,\"progress_percent\":null,\"eta_seconds\":null"+
        ",\"candidates_count\":"+std::to_string(candidates.size())+",\"candidates_gib\":"+quote(amount(candidateBytes))+
        ",\"history\":"+(p_->history?"1":"0")+",\"source_scan_state\":"+quote(p_->sourceState)+",\"error_code\":"+quote(p_->lastError)+",\"type_stats\":[";
    const char*keys[]={"video","archive","code","image","other"};
    for(size_t i=0;i<5;++i){if(i)s+=',';s+="{\"key\":"+quote(keys[i])+",\"bytes\":"+number(p_->typeBytes[i])+",\"gib\":"+quote(amount(p_->typeBytes[i]))+
        ",\"pct\":"+fixed(p_->logical?double(p_->typeBytes[i])*100/double(p_->logical):0)+",\"count\":"+number(p_->typeCounts[i])+"}";}
    return s+"]}";
}
std::string Engine::Items(const std::string& mode,int offset,int limit)const {
    int typeFilter=-1;const char* keys[]={"video","archive","code","image","other"};
    const auto filter=mode.rfind("type:",0)==0?mode.substr(5):mode;
    for(int i=0;i<5;++i)if(filter==keys[i])typeFilter=i;
    if(mode!="all"&&mode!="largest"&&mode!="candidates"&&typeFilter<0)return err("INVALID_ARGUMENT");
    offset=std::max(0,offset);limit=std::max(1,std::min(200,limit));
    std::lock_guard<std::mutex>lock(p_->mutex);std::vector<size_t> chosen;
    if(mode=="candidates")chosen=p_->candidates();
    else {
        for(size_t i=1;i<p_->entries.size();++i){const auto&e=p_->entries[i];
            if(mode=="largest"&&!e.directory)continue;
            if(typeFilter>=0&&(e.directory||e.type!=typeFilter))continue;
            chosen.push_back(i);}
        // Partition to the requested window, then sort at most max_page entries.
        // A late page does not repeatedly sort every preceding row.
        const size_t end=std::min(chosen.size(),size_t(offset)+size_t(limit));
        if(size_t(offset)<end){
            const auto less=[&](size_t a,size_t b){return p_->entries[a].bytes!=p_->entries[b].bytes?p_->entries[a].bytes>p_->entries[b].bytes:a<b;};
            if(offset)std::nth_element(chosen.begin(),chosen.begin()+offset,chosen.end(),less);
            if(end<chosen.size())std::nth_element(chosen.begin()+offset,chosen.begin()+end,chosen.end(),less);
            std::sort(chosen.begin()+offset,chosen.begin()+end,less);
        }
    }
    const size_t end=std::min(chosen.size(),size_t(offset)+size_t(limit));
    std::string s="{\"ok\":1,\"count\":"+std::to_string(end>size_t(offset)?end-size_t(offset):0)+",\"total\":"+std::to_string(chosen.size())+
        ",\"offset\":"+std::to_string(offset)+",\"scan_id\":"+quote(p_->entries.empty()?std::string():p_->generation)+",\"revision\":"+number(p_->revision)+",\"items\":[";
    for(size_t i=size_t(offset);i<end;++i){if(i>size_t(offset))s+=',';s+=p_->entryJson(p_->entries[chosen[i]]);}
    return s+"]}";
}
std::string Engine::Item(const std::string& id)const {
    std::lock_guard<std::mutex>lock(p_->mutex);Impl::Entry e;if(!p_->lookup(id,e))return err("ITEM_NOT_FOUND");return p_->entryJson(e,true);
}
std::string Engine::Issues(int offset,int limit)const {
    offset=std::max(0,offset);limit=std::max(1,std::min(200,limit));
    std::lock_guard<std::mutex>lock(p_->mutex);const auto end=std::min(p_->issues.size(),size_t(offset)+size_t(limit));
    std::string s="{\"total\":"+number(p_->issueTotal)+",\"count\":"+std::to_string(end>size_t(offset)?end-size_t(offset):0)+",\"truncated\":"+(p_->issueTotal>p_->issues.size()?"1":"0")+",\"items\":[";
    for(size_t i=size_t(offset);i<end;++i){if(i>size_t(offset))s+=',';s+="{\"code\":"+quote(p_->issues[i].code)+",\"path\":"+quote(p_->issues[i].path)+"}";}
    return s+"]}";
}
std::string Engine::Preview(const std::string& id) {
    PreviewRelease();
    const auto publish=[&](std::string result){std::lock_guard<std::mutex>lock(p_->mutex);p_->previewJson=std::move(result);return p_->previewJson;};
    Impl::Entry e;std::string failure;
    {std::lock_guard<std::mutex>lock(p_->mutex);if(!p_->lookup(id,e))failure="ITEM_NOT_FOUND";else failure=p_->validate(e);}
    if(!failure.empty())return publish(err(failure.c_str()));
    if(e.directory){std::lock_guard<std::mutex>lock(p_->mutex);p_->previewJson="{\"ok\":1,\"state\":\"ready\",\"kind\":\"folder\",\"filename\":"+quote(e.name)+",\"item\":"+p_->entryJson(e,true)+",\"text\":\"\"}";return p_->previewJson;}
    if(!e.local||offline(e.location))return publish(err("CONTENT_NOT_LOCAL"));
    const auto ext=lower(utf8(e.location.extension()));
#ifdef _WIN32
    if(ext==".png"||ext==".jpg"||ext==".jpeg"||ext==".bmp") {
        if(e.bytes>16*1024*1024)return publish(err("PREVIEW_LIMIT_EXCEEDED"));
        {std::lock_guard<std::mutex>lock(p_->mutex);p_->previewEntry=e;p_->previewPending=true;
            p_->previewJson="{\"ok\":1,\"state\":\"loading\",\"kind\":\"image\",\"item_id\":"+quote(e.id)+",\"filename\":"+quote(e.name)+",\"text\":\"\"}";
            if(!p_->previewWorker.joinable())p_->previewWorker=std::thread([impl=p_.get()]{impl->previewLoop();});}
        p_->previewWake.notify_one();return PreviewPoll();
    }
#endif
    const std::string texts=".txt .md .csv .json .log .cpp .c .h .hpp .py .js .ts .java .cs .go .rs .yaml .yml .xml .html .css .ini .zay ";
    if(ext.empty()||texts.find(ext+" ")==std::string::npos)return publish("{\"ok\":1,\"state\":\"unsupported\",\"kind\":\"unsupported\",\"error_code\":\"PREVIEW_UNSUPPORTED\",\"text\":\"\",\"filename\":"+quote(e.name)+"}");
    constexpr size_t limit=32*1024;std::ifstream input(e.location,std::ios::binary);if(!input)return publish(err("PERMISSION_DENIED"));
    std::string text(limit,'\0');input.read(&text[0],std::streamsize(limit));text.resize(size_t(input.gcount()));
    // Reject UTF-16/binary. Normalize malformed UTF-8 below to avoid invalid JSON strings.
    if(text.find('\0')!=std::string::npos)return publish(err("PREVIEW_ENCODING_UNSUPPORTED"));
    const auto clean=cleanUtf8(text);
    {std::lock_guard<std::mutex>lock(p_->mutex);failure=p_->validate(e);}
    if(!failure.empty())return publish(err(failure.c_str()));
    return publish("{\"ok\":1,\"state\":\"ready\",\"kind\":\"text\",\"filename\":"+quote(e.name)+",\"text\":"+quote(clean)+",\"truncated\":"+(e.bytes>limit?"1":"0")+",\"encoding\":\"UTF-8\"}");
}
std::string Engine::PreviewPoll()const {std::lock_guard<std::mutex>lock(p_->mutex);return p_->previewJson;}
std::string Engine::PreviewRelease() {
    fs::path old;
    {std::lock_guard<std::mutex>lock(p_->mutex);++p_->previewSequence;p_->previewPending=false;p_->previewJson="{\"ok\":1,\"state\":\"idle\",\"kind\":\"none\"}";old=p_->previewFile;p_->previewFile.clear();}
    std::error_code ec;if(!old.empty())fs::remove(old,ec);return ok();
}
std::string Engine::Reveal(const std::string& id)const {
    Impl::Entry e;{std::lock_guard<std::mutex>lock(p_->mutex);if(!p_->lookup(id,e))return err("ITEM_NOT_FOUND");auto failure=p_->validate(e);if(!failure.empty())return err(failure.c_str());}
#ifdef _WIN32
    const auto initialized=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
    auto shellPath=path(displayPath(e.location));shellPath.make_preferred();
    PIDLIST_ABSOLUTE pidl=nullptr;const auto hr=SHParseDisplayName(shellPath.c_str(),nullptr,&pidl,0,nullptr);
    HRESULT opened=E_FAIL;if(SUCCEEDED(hr)){opened=SHOpenFolderAndSelectItems(pidl,0,nullptr,0);CoTaskMemFree(pidl);}
    if(SUCCEEDED(initialized))CoUninitialize();return SUCCEEDED(opened)?ok():err("IO_ERROR");
#else
    return err("UNSUPPORTED_PLATFORM");
#endif
}
std::string Engine::SetRules(int days,double minimum) {
    if(days<0||days>36500||!std::isfinite(minimum)||minimum<0||minimum>1000000)return err("INVALID_ARGUMENT");
    std::lock_guard<std::mutex>lock(p_->mutex);p_->unchangedDays=days;p_->minimumBytes=uint64_t(minimum*double(GiB));++p_->revision;++p_->candidateVersion;return ok();
}

std::string Engine::SaveSnapshot() {
    std::vector<Impl::Entry> entries;fs::path destination;std::string sourceState;int64_t observed;uint64_t issueCount;
    {std::lock_guard<std::mutex>lock(p_->mutex);if(p_->taskState=="running")return err("BUSY");if(p_->entries.empty())return err("NO_SCAN");entries=p_->entries;sourceState=p_->state;observed=p_->observed;issueCount=p_->issueTotal;}
    if(p_->taskWorker.joinable())p_->taskWorker.join();
    destination=p_->cache/"snapshots"/(uniqueName()+".fssnapshot");
    {std::lock_guard<std::mutex>lock(p_->mutex);p_->taskState="running";p_->taskKind="snapshot_save";p_->taskResult.clear();p_->taskError.clear();p_->taskCancel=false;}
    p_->taskWorker=std::thread([impl=p_.get(),entries=std::move(entries),destination,sourceState,observed,issueCount]{
        std::error_code ec;fs::create_directories(destination.parent_path(),ec);auto temporary=destination;temporary+=".tmp";
        std::ofstream o(temporary,std::ios::binary);o<<"BOSS_FILE_SYSTEM_SNAPSHOT 1\n"<<std::quoted(sourceState)<<' '<<observed<<' '<<issueCount<<' '<<entries.size()<<'\n';
        for(const auto&e:entries){if(impl->taskCancel)break;o<<std::quoted(displayPath(e.location))<<' '<<std::quoted(e.name)<<' '<<std::quoted(e.identityKey)<<' '<<e.parent<<' '<<e.directory<<' '<<e.complete<<' '<<e.local<<' '<<e.bytes<<' '<<e.allocation<<' '<<e.allocationKnown<<' '<<e.files<<' '<<e.mtime<<' '<<e.created<<' '<<e.type<<'\n';}
        o.flush();bool success=bool(o)&&!impl->taskCancel;o.close();if(success){fs::rename(temporary,destination,ec);success=!ec;}if(!success)fs::remove(temporary,ec);
        std::lock_guard<std::mutex>lock(impl->mutex);impl->taskState=success?"completed":(impl->taskCancel?"cancelled":"failed");impl->taskError=success?"":(impl->taskCancel?"CANCELLED":"IO_ERROR");if(success)impl->taskResult=displayPath(destination);
    });
    return TaskPoll();
}
std::string Engine::Snapshots()const {
    std::error_code ec;auto folder=p_->cache/"snapshots";std::vector<fs::path> files;
    for(fs::directory_iterator it(folder,ec),end;!ec&&it!=end;it.increment(ec))if(it->path().extension()==".fssnapshot"&&!reparse(it->path()))files.push_back(it->path());
    std::sort(files.begin(),files.end(),std::greater<fs::path>());std::string s="{\"count\":"+std::to_string(files.size())+",\"items\":[";
    for(size_t i=0;i<std::min<size_t>(100,files.size());++i){if(i)s+=',';s+="{\"id\":"+quote(utf8(files[i].filename()))+",\"name\":"+quote(utf8(files[i].filename()))+"}";}return s+"]}";
}
std::string Engine::LoadSnapshot(const std::string& name) {
    if(name.empty()||name.find_first_of("/\\:")!=std::string::npos||name.find('\0')!=std::string::npos)return err("INVALID_ARGUMENT");
    const auto basename=path(name);
    if(basename.has_root_name()||basename.has_root_directory()||basename.filename()!=basename||basename.extension()!=".fssnapshot")return err("INVALID_ARGUMENT");
    {std::lock_guard<std::mutex>lock(p_->mutex);if(p_->active()||p_->taskState=="running")return err("BUSY");}
    if(p_->worker.joinable())p_->worker.join();
    const auto source=p_->cache/"snapshots"/basename;if(reparse(source))return err("SCOPE_REVOKED");
    std::error_code ec;const auto sourceBytes=fs::file_size(source,ec);
    // Import budgets apply before allocating an index or reading quoted fields.
    if(ec||sourceBytes>256ULL*1024*1024)return err("SNAPSHOT_INVALID");
    std::ifstream in(source,std::ios::binary);char header[64]{};in.getline(header,sizeof(header));
    if(!in||std::string(header)!="BOSS_FILE_SYSTEM_SNAPSHOT 1")return err("SNAPSHOT_INVALID");
    std::string oldState;int64_t observed=0;uint64_t issueCount=0;size_t count=0;
    if(!boundedQuoted(in,oldState,16))return err("SNAPSHOT_INVALID");
    in>>observed>>issueCount>>count;
    if(!in||count==0||count>2000000||count>sourceBytes/32)return err("SNAPSHOT_INVALID");
    const std::set<std::string> states={"running","paused","pausing","cancelling","cancelled","completed","failed"};
    if(states.find(oldState)==states.end())return err("SNAPSHOT_INVALID");
    std::vector<Impl::Entry> entries;const auto generation=uniqueName();
    try {
        entries.reserve(std::min<size_t>(count,4096));
        for(size_t i=0;i<count;++i){Impl::Entry e;std::string location;
            if(!boundedQuoted(in,location,131072)||!boundedQuoted(in,e.name,16384)||!boundedQuoted(in,e.identityKey,256))return err("SNAPSHOT_INVALID");
            in>>e.parent>>e.directory>>e.complete>>e.local>>e.bytes>>e.allocation>>e.allocationKnown>>e.files>>e.mtime>>e.created>>e.type;
            if(!in||location.empty()||location.find('\0')!=std::string::npos||(i==0&&(e.parent!=NoParent||!e.directory))||(i>0&&(e.parent>=i||!entries[e.parent].directory))||e.type<0||e.type>4)return err("SNAPSHOT_INVALID");
            e.location=path(location);e.id=generation+"_"+std::to_string(i);e.allocationKnown=e.allocationKnown&&!e.identityKey.empty();e.uncertain=!e.complete;entries.push_back(std::move(e));
        }
    }catch(const std::exception&){return err("SNAPSHOT_INVALID");}
    in>>std::ws;if(!in.eof())return err("SNAPSHOT_INVALID");
    PreviewRelease();
    {
        std::lock_guard<std::mutex>lock(p_->mutex);p_->entries=std::move(entries);p_->generation=generation;p_->root=p_->entries.front().location;p_->currentPath=displayPath(p_->root);p_->history=true;p_->sourceState=oldState;p_->state="completed";p_->lastError.clear();p_->sourceRoot.clear();p_->cancel=false;p_->pause=false;p_->observed=observed;p_->issueTotal=issueCount;p_->issues.clear();p_->fileCount=p_->folderCount=p_->logical=p_->allocated=p_->unknownAllocation=0;p_->typeBytes.fill(0);p_->typeCounts.fill(0);p_->folderIndices.clear();p_->allocatedIds.clear();
        for(size_t i=0;i<p_->entries.size();++i){auto&e=p_->entries[i];if(e.directory){p_->folderIndices.push_back(i);if(i)++p_->folderCount;}else{++p_->fileCount;p_->logical+=e.bytes;p_->typeBytes[size_t(e.type)]+=e.bytes;++p_->typeCounts[size_t(e.type)];if(!e.allocationKnown)++p_->unknownAllocation;else if(p_->allocatedIds.insert(e.identityKey).second)p_->allocated+=e.allocation;}}
        if(oldState!="completed")for(auto&e:p_->entries)if(e.directory)e.complete=false;
        p_->started=p_->ended=Clock::now();++p_->revision;++p_->candidateVersion;
    }
    return Poll();
}
std::string Engine::ExportReport() {
    std::vector<Impl::Entry> entries;fs::path destination;
    {std::lock_guard<std::mutex>lock(p_->mutex);if(p_->taskState=="running")return err("BUSY");if(p_->entries.empty())return err("NO_SCAN");entries=p_->entries;}
    if(p_->taskWorker.joinable())p_->taskWorker.join();
    destination=p_->cache/"reports"/(uniqueName()+".csv");
    {std::lock_guard<std::mutex>lock(p_->mutex);p_->taskState="running";p_->taskKind="report_export";p_->taskResult.clear();p_->taskError.clear();p_->taskCancel=false;}
    p_->taskWorker=std::thread([impl=p_.get(),entries=std::move(entries),destination]{
        std::error_code ec;fs::create_directories(destination.parent_path(),ec);auto temporary=destination;temporary+=".tmp";std::ofstream o(temporary,std::ios::binary);
        o<<"\xEF\xBB\xBF"<<"path,kind,logical_bytes,file_count,latest_modified_epoch,coverage\r\n";
        for(const auto&e:entries){if(impl->taskCancel)break;o<<csv(displayPath(e.location))<<','<<(e.directory?"directory":"file")<<','<<e.bytes<<','<<e.files<<','<<e.mtime<<','<<(e.complete?"complete":"partial")<<"\r\n";}
        o.flush();bool success=bool(o)&&!impl->taskCancel;o.close();if(success){fs::rename(temporary,destination,ec);success=!ec;}if(!success)fs::remove(temporary,ec);
        std::lock_guard<std::mutex>lock(impl->mutex);impl->taskState=success?"completed":(impl->taskCancel?"cancelled":"failed");impl->taskError=success?"":(impl->taskCancel?"CANCELLED":"IO_ERROR");if(success)impl->taskResult=displayPath(destination);
    });return TaskPoll();
}
std::string Engine::TaskPoll()const {std::lock_guard<std::mutex>lock(p_->mutex);return p_->taskJson();}
std::string Engine::TaskCancel(){p_->taskCancel=true;return TaskPoll();}
} // namespace BOSS::FileSystem
#endif
