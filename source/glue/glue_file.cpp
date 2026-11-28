#include <boss.hpp>
#include "glue_file.hpp"
#include "file_system_engine.hpp"
#include <service/boss_zay.hpp>
#include <algorithm>
#include <atomic>
#include <exception>
#include <map>
#include <memory>
#include <string>

// Per-widget services. Workers belong to Engine and never call DOM or Host APIs.
namespace
{
    std::string FileError(const char* code)
    {
        return std::string("{\"ok\":0,\"state\":\"failed\",\"error_code\":\"") + code + "\"}";
    }

    bool FileDomTarget(const String& name)
    {
        return name.Left(2) == "d." && name.Length() > 2 && name.Right(1) != ".";
    }

    bool FileCacheKey(const std::string& key)
    {
        if(key.empty() || key.size() > 64) return false;
        for(const unsigned char c : key)
            if(!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                 (c >= '0' && c <= '9') || c == '_' || c == '-')) return false;
        return true;
    }

    std::string FileJsonString(const Context& value, const char* name)
    {
        const chars text = value(name).GetText();
        return text ? std::string(text) : std::string();
    }

    int FileJsonInteger(const Context& value, const char* name, int fallback = 0)
    {
        const chars text = value(name).GetText();
        return text && *text ? Parser::GetInt(text) : fallback;
    }

    bool NativeFilesAvailable(const char* capability = "available")
    {
        const auto json = BOSS::FileSystem::Engine::Capabilities();
        const Context value(ST_Json, SO_OnlyReference, json.c_str());
        return FileJsonInteger(value, capability) == 1;
    }

    struct FileSession
    {
        BOSS::FileSystem::Engine engine;
        const std::string prefix;
        std::string previewAsset, loadedAsset;
        int width = 0, height = 0;
        std::unique_ptr<Image> image;

        FileSession(const std::string& directory, const std::string& assetPrefix)
            : engine(directory, assetPrefix), prefix(assetPrefix + "preview/") {}

        void ClearImage()
        {
            image.reset();
            previewAsset.clear();
            loadedAsset.clear();
            width = height = 0;
        }

        void ObservePreview(const std::string& json)
        {
            const Context value(ST_Json, SO_OnlyReference, json.c_str());
            const std::string state = FileJsonString(value, "state");
            const std::string kind = FileJsonString(value, "kind");
            if(state != "ready" || kind != "image") { ClearImage(); return; }
            const std::string asset = FileJsonString(value, "asset");
            const int w = FileJsonInteger(value, "width"), h = FileJsonInteger(value, "height");
            if(asset.rfind(prefix, 0) != 0 || asset.size() < prefix.size() + 5 ||
               asset.substr(asset.size() - 4) != ".png" ||
               w < 1 || h < 1 || w > 1024 || h > 1024) { ClearImage(); return; }
            const auto token = asset.substr(prefix.size(), asset.size() - prefix.size() - 4);
            for(const char c : token)
                if(!((c >= '0' && c <= '9') || c == '_')) { ClearImage(); return; }
            if(asset != previewAsset) { image.reset(); loadedAsset.clear(); }
            previewAsset = asset; width = w; height = h;
        }

        void Draw(ZayPanel& panel)
        {
            if(previewAsset.empty() || panel.w() <= 0 || panel.h() <= 0) return;
            if(loadedAsset != previewAsset)
            {
                if(!Asset::Exist(previewAsset.c_str())) return;
                auto candidate = std::unique_ptr<Image>(new Image);
                candidate->SetName(previewAsset.c_str()).Load();
                if(!candidate->HasBitmap()) return;
                image = std::move(candidate);
                loadedAsset = previewAsset;
            }
            if(!image) return;
            const double ratio = std::min(double(panel.w()) / width, double(panel.h()) / height);
            const float w = float(width * ratio), h = float(height * ratio);
            ZAY_XYWH(panel, (panel.w() - w) / 2, (panel.h() - h) / 2, w, h)
                panel.stretch(*image, Image::Build::Force);
        }
    };

    struct FileServices
    {
        ZayGlue::Host host;
        ZayObject* view;
        std::map<std::string, std::shared_ptr<FileSession>> sessions;
        std::map<std::string, std::string> published;

        explicit FileServices(const ZayGlue::Bindings& context)
            : host(context.host), view(context.view) {}

        bool Publish(const String& target, const std::string& json, bool force = false)
        {
            if(!host.IsAlive()) return false;
            const Context value(ST_Json, SO_OnlyReference, json.c_str());
            const bool success = FileJsonInteger(value, "ok", 1) != 0;
            if(target.Length() == 0) return success;
            if(!FileDomTarget(target)) return false;
            const std::string key = (chars) target;
            const auto before = published.find(key);
            // Result subtrees are output-only. Avoid rebuilding an unchanged page.
            if(!force && before != published.end() && before->second == json) return success;
            const String header = target.Offset(2) + '.';
            ZayWidgetDOM::RemoveVariables(header);
            ZayWidgetDOM::SetJson(value, header);
            if(published.size() >= 128 && before == published.end()) published.erase(published.begin());
            published[key] = json;
            Invalidate();
            return success;
        }

        bool PublishDrives(const String& target)
        {
            if(!host.IsAlive() || !FileDomTarget(target)) return false;
            if(!NativeFilesAvailable()) return Publish(target, FileError("UNSUPPORTED_PLATFORM"), true);
            const auto json = BOSS::FileSystem::Engine::Drives();
            const Context value(ST_Json, SO_OnlyReference, json.c_str());
            if(FileJsonInteger(value, "ok", 1) == 0) return Publish(target, json, true);
            const auto& items = value("items");
            const sint32 count = items.LengthOfIndexable();
            if(count != FileJsonInteger(value, "count", -1))
            {
                BOSS_TRACE("[file] Drive result count does not match items");
                return Publish(target, FileError("VOLUME_DOM_FAILED"), true);
            }
            for(sint32 i = 0; i < count; ++i)
            {
                const auto root = FileJsonString(items[i], "path");
                // Engine::Drives returns only POSIX / or Windows X:/ volume roots.
                const bool valid = root == "/" || (root.size() == 3 &&
                    ((root[0] >= 'A' && root[0] <= 'Z') || (root[0] >= 'a' && root[0] <= 'z')) &&
                    root[1] == ':' && root[2] == '/');
                if(!valid)
                {
                    BOSS_TRACE("[file] Drive result contains an invalid or missing root");
                    return Publish(target, FileError("VOLUME_DOM_FAILED"), true);
                }
            }
            const String header = target.Offset(2) + '.';
            ZayWidgetDOM::RemoveVariables(header);
            for(sint32 i = 0; i < count; ++i)
            {
                const auto& item = items[i];
                const String row = header + String::Format("items.%d.", i);
                // Do not rely on a recursive array expansion to create the drive row.
                // The engine root has no quotes or backslashes, so these are safe literals.
                const auto root = FileJsonString(item, "path");
                const String rootFormula = String::Format("'%s'", root.c_str());
                ZayWidgetDOM::SetValue(row + "path", rootFormula);
                ZayWidgetDOM::SetValue(row + "name", rootFormula);
                ZayWidgetDOM::SetValue(row + "label", rootFormula);
                ZayWidgetDOM::SetValue(row + "ready", FileJsonInteger(item, "ready") == 1 ? "1" : "0");
                // These fields are bounded decimal strings (or null/em dash).
                const char* fields[] = {"total_bytes", "free_bytes", "total_gib", "free_gib"};
                for(const auto* field : fields)
                {
                    const auto text = FileJsonString(item, field);
                    ZayWidgetDOM::SetValue(row + field, String::Format("'%s'", text.c_str()));
                }
            }
            // Publish count last: a count always refers to already-created rows.
            ZayWidgetDOM::SetValue(header + "items.count", String::FromInteger(count));
            ZayWidgetDOM::SetValue(header + "count", String::FromInteger(count));
            ZayWidgetDOM::SetValue(header + "ok", "1");
            published.erase((chars) target);
            Invalidate();
            return true;
        }

        void Invalidate()
        {
            if(host.IsAlive() && view) view->invalidate();
        }

        std::shared_ptr<FileSession> Find(const String& token) const
        {
            if(!host.IsAlive()) return {};
            const auto found = sessions.find((chars) token);
            return found == sessions.end() ? std::shared_ptr<FileSession>() : found->second;
        }

        bool Open(const String& cacheNamespace, const String& target, bool resetNamespace = false)
        {
            if(!host.IsAlive() || !FileDomTarget(target)) return false;
            if(!NativeFilesAvailable()) return Publish(target, FileError("UNSUPPORTED_PLATFORM"));
            const std::string key = (chars) cacheNamespace;
            if(!FileCacheKey(key)) return Publish(target, FileError("INVALID_ARGUMENT"));
            if(resetNamespace)
            {
                const auto prefix = std::string("file/cache/") + key + "/preview/";
                for(auto i = sessions.begin(); i != sessions.end(); )
                    if(i->second->prefix == prefix) i = sessions.erase(i); else ++i;
                Invalidate();
            }
            if(sessions.size() >= 4) return Publish(target, FileError("SESSION_LIMIT"));
            try
            {
                static std::atomic<unsigned long long> serial{0};
                const auto token = std::string("file_") + std::to_string(++serial);
                const std::string prefix = "file/cache/" + key + "/";
                const String directory = Platform::File::RootForAssetsRem() + String(prefix.c_str());
                auto session = std::make_shared<FileSession>(std::string((chars) directory), prefix);
                sessions.emplace(token, std::move(session));
                return Publish(target, "{\"ok\":1,\"session\":\"" + token + "\",\"error_code\":\"\"}");
            }
            catch(const std::exception&) { return Publish(target, FileError("CACHE_UNAVAILABLE")); }
        }

        bool Close(const String& token, const String& target)
        {
            if(!host.IsAlive() || (target.Length() && !FileDomTarget(target))) return false;
            const auto found = sessions.find((chars) token);
            if(found == sessions.end()) return Publish(target, FileError("SESSION_NOT_FOUND"));
            // Engine destruction cancels and joins its scan, preview and output workers.
            sessions.erase(found);
            Invalidate();
            return Publish(target, "{\"ok\":1,\"error_code\":\"\"}");
        }
    };

    template<typename Operation>
    bool WithFileSession(const std::shared_ptr<FileServices>& files,
        const String& token, const String& target, Operation operation)
    {
        if(!files->host.IsAlive() || (target.Length() && !FileDomTarget(target))) return false;
        const auto session = files->Find(token);
        if(!session) return files->Publish(target, FileError("SESSION_NOT_FOUND"));
        try { return files->Publish(target, operation(*session)); }
        catch(const std::exception&) { return files->Publish(target, FileError("IO_ERROR")); }
    }

    template<typename Parameters>
    void ReturnFileStatus(const Parameters& params, bool status)
    {
        if(params.CanReturn()) params.Return(status ? "1" : "0");
    }
}


ZAY_DECLARE_GLUE_CLASS("file", GlueFile)

bool GlueFile::Register(ZaySon& NewZayson, const ZayGlue::Bindings& context)
{
    auto* view = context.view;
    if(!view)
    {
        BOSS_TRACE("[file] Missing required GLUE host callback/view");
        return false;
    }

    ////////////////////////////////////////////////////////////////////////////////
    // gettext
    NewZayson.AddGlue("gettext", ZAY_DECLARE_GLUE(params, view)
    {
        if(params.ParamCount() == 2)
        {
            auto Title = params.Param(0).ToText();
            auto Topic = params.Param(1).ToText();
            SolverValue OldText;
            if(params.CanReturn(&OldText))
            {
                String GetText = OldText.ToText();
                if(Platform::Popup::TextDialog(GetText, Title, Topic))
                {
                    params.Return('\'' + GetText + '\'');
                    view->invalidate();
                }
            }
        }
    }, "[Title:'타이틀명'][Topic:'토픽명']", "유저로부터 값을 하나 입력받아 지역변수로 Return처리");

    ////////////////////////////////////////////////////////////////////////////////
    // savetext
    NewZayson.AddGlue("savetext", ZAY_DECLARE_GLUE(params)
    {
        if(2 <= params.ParamCount())
        {
            auto FileName = params.Param(0).ToText();
            auto Text = params.Param(1).ToText();
            Text.Replace("\\n", "\r\n");
            auto UniqueFlag = (2 < params.ParamCount())? (params.Param(2).ToInteger() != 0) : false;
            auto ExtendPath = (3 < params.ParamCount())? params.Param(3).ToText() : String();

            if(0 < ExtendPath.Length())
            {
                String FilePath = ExtendPath + FileName;
                if(UniqueFlag)
                {
                    const sint32 DotPos = FileName.Find(0, ".");
                    if(DotPos != -1)
                    for(sint32 i = 1; i < 1000 && Platform::File::Exist(FilePath); ++i)
                        FilePath = ExtendPath + FileName.Left(DotPos) + String::Format("_%03d", i) + FileName.Offset(DotPos);
                }
                if(auto NewFile = Platform::File::OpenForWrite(FilePath, true))
                {
                    Platform::File::Write(NewFile, (bytes)(chars) Text, Text.Length());
                    Platform::File::Close(NewFile);
                    ZayWidgetDOM::SetValue("savefile.log", "'[SAVED usb] " + FilePath + '\'');
                }
                else ZayWidgetDOM::SetValue("savefile.log", "'[NOT SAVE usb] " + FilePath + '\'');
            }
            else
            {
                String AssetName = FileName;
                if(UniqueFlag)
                {
                    const sint32 DotPos = FileName.Find(0, ".");
                    if(DotPos != -1)
                    for(sint32 i = 1; i < 1000 && Asset::Exist(AssetName); ++i)
                        AssetName = FileName.Left(DotPos) + String::Format("_%03d", i) + FileName.Offset(DotPos);
                }
                Text.ToAsset(AssetName, true);
                ZayWidgetDOM::SetValue("savefile.log", "'[SAVED assets-rem] " + AssetName + '\'');
            }
        }
    }, "[FileName:'파일명'][Text:'ABC']#[UniqueFlag:1][ExtendPath:'D:/']", "Text를 파일에 저장");

    ////////////////////////////////////////////////////////////////////////////////
    // savebase64
    NewZayson.AddGlue("savebase64", ZAY_DECLARE_GLUE(params)
    {
        if(params.ParamCount() == 2)
        {
            auto FileName = params.Param(0).ToText();
            auto JsonText = params.Param(1).ToText();
            if(auto NewAsset = Asset::OpenForWrite(FileName, true))
            {
                const BOSS::Context Json(ST_Json, SO_OnlyReference, JsonText);
                String Base64 = Json("base64").GetText();
                buffer ImageData = AddOn::Ssl::FromBASE64(Base64.Replace("\\/", "/"));
                Asset::Write(NewAsset, (bytes) ImageData, Buffer::CountOf(ImageData));
                Asset::Close(NewAsset);
                Buffer::Free(ImageData);
            }
        }
    }, "[FileName:'파일명'][JsonText:'{\"base64\":\"...\"}']", "JsonText속 base64항목을 디코딩해 파일로 저장");

    ////////////////////////////////////////////////////////////////////////////////
    // loadcsv
    NewZayson.AddGlue("loadcsv", ZAY_DECLARE_GLUE(params)
    {
        if(2 <= params.ParamCount())
        {
            auto FileName = params.Param(0).ToText();
            auto DomNameD = params.Param(1).ToText();
            auto ExtendPath = (2 < params.ParamCount())? params.Param(2).ToText() : String();
            if(DomNameD.Left(2) == "d.")
            {
                String Csv;
                if(0 < ExtendPath.Length())
                {
                    String FilePath = ExtendPath + FileName;
                    Csv = String::FromFile(FilePath);
                }
                else
                {
                    const String AssetName = FileName;
                    Csv = String::FromAsset(AssetName);
                }

                Csv.Replace("\r\n", "\n");
                const Strings CsvLines = String::Split(Csv, '\n');
                for(sint32 i = 0, iend = CsvLines.Count(); i < iend; ++i)
                {
                    const Strings CsvCells = String::Split(CsvLines[i], ',');
                    const String DomHead = DomNameD.Offset(2) + String::Format(".%d", i);
                    for(sint32 j = 0, jend = CsvCells.Count(); j < jend; ++j)
                    {
                        const String CurCell = CsvCells[j].Trim();
                        sint32 OffsetCheck = 0;
                        Parser::GetFloat<double>(CurCell, CurCell.Length(), &OffsetCheck);
					    if(OffsetCheck == CurCell.Length() && 0 < OffsetCheck) // 실수 또는 정수
					        ZayWidgetDOM::SetValue(DomHead + String::Format(".%d", j), CurCell);
                        else ZayWidgetDOM::SetValue(DomHead + String::Format(".%d", j), '\'' + CurCell + '\'');
                    }
                    ZayWidgetDOM::SetValue(DomHead + ".count", String::FromInteger(CsvCells.Count()));
                }
                ZayWidgetDOM::SetValue(DomNameD.Offset(2) + ".count", String::FromInteger(CsvLines.Count()));
            }
        }
    }, "[FileName:'파일명'][DomNameD:'d.value']#[ExtendPath:'D:/']", "파일에서 Csv를 로딩");

    ////////////////////////////////////////////////////////////////////////////////
    // savejson: 지정한 DOM의 현재 값을 assets-rem의 JSON 파일에 저장합니다.
    NewZayson.AddGlue("savejson", ZAY_DECLARE_GLUE(params)
    {
        if(params.ParamCount() == 2)
        {
            const String AssetName = params.Param(0).ToText();
            const String DomNameD = params.Param(1).ToText();
            if(DomNameD.Left(2) == "d." && 2 < DomNameD.Length())
            {
                BOSS::Context Json;
                ZayWidgetDOM::GetJson(Json, DomNameD.Offset(2) + '.');
                Json.SaveJson().ToAsset(AssetName, true);
            }
        }
    }, "[AssetName:'main/widget/main_settings.json'][DomNameD:'d.settings']", "DOM을 JSON 에셋으로 저장");

    ////////////////////////////////////////////////////////////////////////////////
    // loadjson
    NewZayson.AddGlue("loadjson", ZAY_DECLARE_GLUE(params)
    {
        if(params.ParamCount() == 2)
        {
            auto JsonText = params.Param(0).ToText();
            auto DomNameD = params.Param(1).ToText();
            if(DomNameD.Left(2) == "d.")
            {
                String DomHeader = DomNameD.Offset(2) + '.';
                ZayWidgetDOM::RemoveVariables(DomHeader);
                const BOSS::Context Json(ST_Json, SO_OnlyReference, JsonText);
                ZayWidgetDOM::SetJson(Json, DomHeader);
            }
        }
    }, "[JsonText:'{a:b}'][DomNameD:'d.value']", "Json을 Dom으로 전개");
    // Generic filesystem API v2. Each widget owns its own handle table.
    const auto files = std::make_shared<FileServices>(context);
    ZayWidgetDOM::SetValue("program.file_api_version", "2");

    NewZayson.AddGlue("file_capabilities", ZAY_DECLARE_GLUE(p, files)
    {
        if(p.ParamCount() == 1)
            ReturnFileStatus(p, files->Publish(p.Param(0).ToText(), BOSS::FileSystem::Engine::Capabilities(), true));
    }, "[ResultDom:'d.files.native']", "Filesystem support and API version");

    NewZayson.AddGlue("file_session_open", ZAY_DECLARE_GLUE(p, files)
    {
        if(p.ParamCount() >= 2 && p.ParamCount() <= 3)
            ReturnFileStatus(p, files->Open(p.Param(0).ToText(), p.Param(1).ToText(),
                p.ParamCount() == 3 && p.Param(2).ToInteger() != 0));
    }, "[CacheNamespace:'myapp'][ResultDom:'d.files.session']#[ResetNamespace:0]", "Create an independent filesystem session");

    NewZayson.AddGlue("file_session_close", ZAY_DECLARE_GLUE(p, files)
    {
        if(p.ParamCount() >= 1 && p.ParamCount() <= 2)
            ReturnFileStatus(p, files->Close(p.Param(0).ToText(), p.ParamCount() == 2 ? p.Param(1).ToText() : String()));
    }, "[Session]#[ResultDom]", "Cancel work and release a filesystem session");

    NewZayson.AddGlue("file_drives_list", ZAY_DECLARE_GLUE(p, files)
    {
        if(p.ParamCount() == 1)
            ReturnFileStatus(p, files->PublishDrives(p.Param(0).ToText()));
    }, "[ResultDom:'d.files.drives']", "Enumerate local volumes");

    NewZayson.AddGlue("file_folder_pick", ZAY_DECLARE_GLUE(p, files)
    {
        if(p.ParamCount() != 1 || !files->host.IsAlive() || !FileDomTarget(p.Param(0).ToText())) return;
        if(!NativeFilesAvailable("folder_picker")) { ReturnFileStatus(p, files->Publish(p.Param(0).ToText(), FileError("UNSUPPORTED_PLATFORM"))); return; }
        const auto path = BOSS::FileSystem::Engine::PickDirectory();
        Context result;
        result.At("ok").Set("1");
        result.At("path").Set(path.c_str());
        result.At("cancelled").Set(path.empty() ? "1" : "0");
        ReturnFileStatus(p, files->Publish(p.Param(0).ToText(), (chars) result.SaveJson()));
    }, "[ResultDom:'d.files.picked']", "Choose a folder without starting a scan");

    NewZayson.AddGlue("file_scan_start", ZAY_DECLARE_GLUE(p, files)
    {
        if(p.ParamCount() == 3)
            ReturnFileStatus(p, WithFileSession(files, p.Param(0).ToText(), p.Param(2).ToText(), [&](FileSession& s) {
                const auto result = s.engine.Start((chars) p.Param(1).ToText());
                s.ClearImage(); return result;
            }));
    }, "[Session][RootPath][ResultDom]", "Start a read-only background scan");

    NewZayson.AddGlue("file_scan_control", ZAY_DECLARE_GLUE(p, files)
    {
        if(p.ParamCount() >= 2 && p.ParamCount() <= 3)
            ReturnFileStatus(p, WithFileSession(files, p.Param(0).ToText(), p.ParamCount() == 3 ? p.Param(2).ToText() : String(),
                [&](FileSession& s) { return s.engine.Control((chars) p.Param(1).ToText()); }));
    }, "[Session][Action:'pause|resume|cancel']#[ResultDom]", "Pause, resume or cancel a scan");

    NewZayson.AddGlue("file_scan_poll", ZAY_DECLARE_GLUE(p, files)
    {
        if(p.ParamCount() == 2)
            ReturnFileStatus(p, WithFileSession(files, p.Param(0).ToText(), p.Param(1).ToText(),
                [](FileSession& s) { return s.engine.Poll(); }));
    }, "[Session][ResultDom]", "Read current scan statistics");

    NewZayson.AddGlue("file_items_query", ZAY_DECLARE_GLUE(p, files)
    {
        if(p.ParamCount() == 5)
            ReturnFileStatus(p, WithFileSession(files, p.Param(0).ToText(), p.Param(4).ToText(),
                [&](FileSession& s) { return s.engine.Items((chars) p.Param(1).ToText(), p.Param(2).ToInteger(), p.Param(3).ToInteger()); }));
    }, "[Session][Mode][Offset][Limit][ResultDom]", "Query a bounded page of scan items");

    NewZayson.AddGlue("file_item_get", ZAY_DECLARE_GLUE(p, files)
    {
        if(p.ParamCount() == 3)
            ReturnFileStatus(p, WithFileSession(files, p.Param(0).ToText(), p.Param(2).ToText(),
                [&](FileSession& s) { return s.engine.Item((chars) p.Param(1).ToText()); }));
    }, "[Session][ItemId][ResultDom]", "Read one generation-bound scan item");

    NewZayson.AddGlue("file_issues_query", ZAY_DECLARE_GLUE(p, files)
    {
        if(p.ParamCount() == 4)
            ReturnFileStatus(p, WithFileSession(files, p.Param(0).ToText(), p.Param(3).ToText(),
                [&](FileSession& s) { return s.engine.Issues(p.Param(1).ToInteger(), p.Param(2).ToInteger()); }));
    }, "[Session][Offset][Limit][ResultDom]", "Read inaccessible or excluded locations");

    NewZayson.AddGlue("file_preview_request", ZAY_DECLARE_GLUE(p, files)
    {
        if(p.ParamCount() == 3)
            ReturnFileStatus(p, WithFileSession(files, p.Param(0).ToText(), p.Param(2).ToText(), [&](FileSession& s) {
                const auto result = s.engine.Preview((chars) p.Param(1).ToText());
                s.ObservePreview(result); return result;
            }));
    }, "[Session][ItemId][ResultDom]", "Request bounded text, folder or image preview");

    NewZayson.AddGlue("file_preview_poll", ZAY_DECLARE_GLUE(p, files)
    {
        if(p.ParamCount() == 2)
            ReturnFileStatus(p, WithFileSession(files, p.Param(0).ToText(), p.Param(1).ToText(), [](FileSession& s) {
                const auto result = s.engine.PreviewPoll(); s.ObservePreview(result); return result;
            }));
    }, "[Session][ResultDom]", "Read asynchronous preview state");

    NewZayson.AddGlue("file_preview_release", ZAY_DECLARE_GLUE(p, files)
    {
        if(p.ParamCount() >= 1 && p.ParamCount() <= 2)
            ReturnFileStatus(p, WithFileSession(files, p.Param(0).ToText(), p.ParamCount() == 2 ? p.Param(1).ToText() : String(),
                [&](FileSession& s) { s.ClearImage(); const auto result = s.engine.PreviewRelease(); files->Invalidate(); return result; }));
    }, "[Session]#[ResultDom]", "Release the session preview");

    NewZayson.AddGlue("file_reveal", ZAY_DECLARE_GLUE(p, files)
    {
        if(p.ParamCount() >= 2 && p.ParamCount() <= 3)
            ReturnFileStatus(p, WithFileSession(files, p.Param(0).ToText(), p.ParamCount() == 3 ? p.Param(2).ToText() : String(),
                [&](FileSession& s) { return s.engine.Reveal((chars) p.Param(1).ToText()); }));
    }, "[Session][ItemId]#[ResultDom]", "Reveal the verified current item in the system file manager");

    NewZayson.AddGlue("file_rules_set", ZAY_DECLARE_GLUE(p, files)
    {
        if(p.ParamCount() >= 3 && p.ParamCount() <= 4)
            ReturnFileStatus(p, WithFileSession(files, p.Param(0).ToText(), p.ParamCount() == 4 ? p.Param(3).ToText() : String(),
                [&](FileSession& s) { return s.engine.SetRules(p.Param(1).ToInteger(), p.Param(2).ToFloat()); }));
    }, "[Session][UnchangedDays][MinimumGiB]#[ResultDom]", "Configure folder filtering thresholds");

    NewZayson.AddGlue("file_snapshot_save", ZAY_DECLARE_GLUE(p, files)
    {
        if(p.ParamCount() == 2)
            ReturnFileStatus(p, WithFileSession(files, p.Param(0).ToText(), p.Param(1).ToText(),
                [](FileSession& s) { return s.engine.SaveSnapshot(); }));
    }, "[Session][ResultDom]", "Save a scan snapshot in the session cache namespace");

    NewZayson.AddGlue("file_snapshots_list", ZAY_DECLARE_GLUE(p, files)
    {
        if(p.ParamCount() == 2)
            ReturnFileStatus(p, WithFileSession(files, p.Param(0).ToText(), p.Param(1).ToText(),
                [](FileSession& s) { return s.engine.Snapshots(); }));
    }, "[Session][ResultDom]", "List saved scan snapshots");

    NewZayson.AddGlue("file_snapshot_load", ZAY_DECLARE_GLUE(p, files)
    {
        if(p.ParamCount() == 3)
            ReturnFileStatus(p, WithFileSession(files, p.Param(0).ToText(), p.Param(2).ToText(), [&](FileSession& s) {
                const auto result = s.engine.LoadSnapshot((chars) p.Param(1).ToText()); s.ClearImage(); return result;
            }));
    }, "[Session][SnapshotId][ResultDom]", "Load a read-only historical scan");

    NewZayson.AddGlue("file_report_export", ZAY_DECLARE_GLUE(p, files)
    {
        if(p.ParamCount() == 2)
            ReturnFileStatus(p, WithFileSession(files, p.Param(0).ToText(), p.Param(1).ToText(),
                [](FileSession& s) { return s.engine.ExportReport(); }));
    }, "[Session][ResultDom]", "Export a CSV report in the session cache namespace");

    NewZayson.AddGlue("file_task_poll", ZAY_DECLARE_GLUE(p, files)
    {
        if(p.ParamCount() == 2)
            ReturnFileStatus(p, WithFileSession(files, p.Param(0).ToText(), p.Param(1).ToText(),
                [](FileSession& s) { return s.engine.TaskPoll(); }));
    }, "[Session][ResultDom]", "Read snapshot or export task state");

    NewZayson.AddGlue("file_task_cancel", ZAY_DECLARE_GLUE(p, files)
    {
        if(p.ParamCount() >= 1 && p.ParamCount() <= 2)
            ReturnFileStatus(p, WithFileSession(files, p.Param(0).ToText(), p.ParamCount() == 2 ? p.Param(1).ToText() : String(),
                [](FileSession& s) { return s.engine.TaskCancel(); }));
    }, "[Session]#[ResultDom]", "Cancel an uncommitted output task");

    // Native content component registered by the same module; no central renderer switch.
    // It has no named hit region and therefore does not consume background mouse input.
    NewZayson.AddComponent(ZayExtend::ComponentType::ContentWithParameter, "file_preview", ZAY_DECLARE_COMPONENT(panel, p, files)
    {
        if(p.ParamCount() == 1)
            if(const auto session = files->Find(p.Param(0).ToText())) session->Draw(panel);
        return panel._push_pass();
    });

    return true;
}
