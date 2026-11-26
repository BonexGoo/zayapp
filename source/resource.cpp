#include <boss.hpp>
#include "resource.hpp"

#include <format/boss_bmp.hpp>
#include <format/boss_png.hpp>
#include <platform/boss_platform.hpp>
#include <service/boss_boxr.hpp>

namespace BOSS
{
    class BoolClass
    {
    public:
        BoolClass() {mFlag = false;}
        ~BoolClass() {}
    public:
        bool mFlag;
    };
    class AtlasSet
    {
    public:
        AtlasSet& operator=(const AtlasSet& rhs)
        {
            KeyVerCode = rhs.KeyVerCode;
            KeyFileName = rhs.KeyFileName;
            AtlasFileName = rhs.AtlasFileName;
            AtlasFolderName = rhs.AtlasFolderName;
            FileSize = rhs.FileSize;
            ModifyTime = rhs.ModifyTime;
            CreateTime = rhs.CreateTime;
            Updated = rhs.Updated;
            return *this;
        }
    public:
        sint32 KeyVerCode {0}; // 1 또는 2
        String KeyFileName;
        String AtlasFileName;
        String AtlasFolderName;
        uint64 FileSize {0};
        uint64 ModifyTime {0};
        uint64 CreateTime {0};
        bool Updated {false};
    };
    static Map<Image> gImageMap;
    static Map<BoolClass> gExistMap;
    static id_assetpath gAssetPath = nullptr;
    static String gAtlasDir; // 개별 앱의 이미지 디렉터리
    static const String gMainAtlasDir("main/image/");
    static Array<AtlasSet> gAtlasSets;

    R::R(chars name, bool main_only)
    {
        // main_only pins shell controls to main/image. Site icons intentionally use the app image directory.
        const String CacheName = main_only? String("@main/") + name : String(name);
        mImage = gImageMap.Access(CacheName);
        bool& Exist = gExistMap(CacheName).mFlag;
        if(!mImage)
        {
            mImage = &gImageMap(CacheName);
            // 파일형 이미지 로딩시도
            String Path = (main_only? gMainAtlasDir : gAtlasDir) + name;
            Exist = mImage->SetName(Path.Replace('.', '/')).Load(gAssetPath);
            if(!Exist && !main_only)
            {
                Path = gMainAtlasDir + name;
                Exist = mImage->SetName(Path.Replace('.', '/')).Load(gAssetPath);
            }
            // 아틀라스형 이미지들 로딩시도
            for(sint32 i = gAtlasSets.Count() - 1; 0 <= i && !Exist; --i)
            {
                if(main_only && gAtlasSets[i].AtlasFileName.Left(11) != "main/image/") continue;
                Path = gAtlasSets[i].AtlasFolderName + '/' + name;
                Exist = mImage->SetName(Path).Load(gAssetPath);
            }
            // 로딩실패시
            if(!Exist)
            {
                if(id_asset_read PngAsset = Asset::OpenForRead(gMainAtlasDir + "noimg.png"))
                {
                    const sint32 PngSize = Asset::Size(PngAsset);
                    buffer PngBuffer = Buffer::Alloc(BOSS_DBG PngSize);
                    Asset::Read(PngAsset, (uint08*) PngBuffer, PngSize);
                    Asset::Close(PngAsset);
                    mImage->LoadBitmap(Png().ToBmp((bytes) PngBuffer, true));
                    Buffer::Free(PngBuffer);
                }
                else
                {
                    id_bitmap NewBitmap = Bmp::Create(4, 10, 10);
                    Bmp::FillColor(NewBitmap, Color(255, 0, 0).argb);
                    id_bitmap OldBitmap = mImage->ChangeBitmap(NewBitmap);
                    Bmp::Remove(OldBitmap);
                }
            }
        }
        mExist = Exist;
    }

    R::~R()
    {
    }

    void R::SetAssetPath(id_assetpath assetpath)
    {
        AssetPath::Release(gAssetPath);
        gAssetPath = assetpath;
    }

    void R::SetAtlasDir(chars dirname)
    {
        if(*dirname == '\0') gAtlasDir = "";
        else gAtlasDir = String::Format("%s/", dirname);
    }

    void R::ClearAll(bool withcache, bool keep_main)
    {
        gImageMap.Reset();
        gExistMap.Reset();
        if(keep_main)
        {
            for(sint32 i = gAtlasSets.Count() - 1; 0 <= i; --i)
                if(gAtlasSets[i].AtlasFileName.Left(11) != "main/image/")
                    gAtlasSets.SubtractionSection(i);
        }
        else gAtlasSets.Clear();
        if(withcache)
        {
            // 기존 앱 캐시와 분리된 메인 캐시를 함께 정리
            const String AtlasDirs[] = {gAtlasDir, gMainAtlasDir};
            for(const auto& AtlasDir : AtlasDirs)
            {
                if(keep_main && AtlasDir == gMainAtlasDir) continue;
                const String AtlasPath = (Platform::File::RootForAssetsRem() + AtlasDir).SubTail(1);
                Platform::File::Search(AtlasPath,
                    [](chars itemname, payload data)->void
                    {
                        if(Platform::File::Exist(itemname))
                            Platform::File::Remove(WString::FromChars(itemname), false);
                    }, nullptr, true);
                Platform::File::RemoveDir(WString::FromChars(AtlasPath), true);
            }
        }
    }

    void R::AddAtlas(chars key_filename, chars map_filename, const Context& ctx, sint32 keyver, bool main_atlas)
    {
        // 전체 에셋 경로를 캐시 키로 사용하여 메인/앱 아틀라스를 분리
        const String AtlasPath = (main_atlas)? gMainAtlasDir + map_filename : gAtlasDir + map_filename;
        // 기존 Image를 제거
        gImageMap.Reset();
        gExistMap.Reset();

        // 동일한 AtlasSet를 제거
        for(sint32 i = 0, iend = gAtlasSets.Count(); i < iend; ++i)
            if(!gAtlasSets[i].AtlasFileName.CompareNoCase(AtlasPath))
            {
                gAtlasSets.SubtractionSection(i);
                break;
            }

        auto& NewAtlasSet = gAtlasSets.AtAdding();
        NewAtlasSet.KeyVerCode = keyver;
        NewAtlasSet.KeyFileName = gMainAtlasDir + key_filename;
        NewAtlasSet.AtlasFileName = AtlasPath;
        NewAtlasSet.AtlasFolderName = (main_atlas? String() : String(Asset::GetWebCachePrefix())) + String(AtlasPath).Replace('.', '_');
        // Use the derived folder as cache identity; remote sites may share identical atlas filenames.
        NewAtlasSet.FileSize = ctx(NewAtlasSet.AtlasFolderName)("filesize").GetInt(0);
        NewAtlasSet.ModifyTime = ctx(NewAtlasSet.AtlasFolderName)("modifytime").GetInt(0);
        NewAtlasSet.CreateTime = ctx(NewAtlasSet.AtlasFolderName)("createtime").GetInt(0);
    }

    void R::SaveAtlas(Context& ctx)
    {
        for(sint32 i = 0, iend = gAtlasSets.Count(); i < iend; ++i)
        {
            ctx.At(gAtlasSets[i].AtlasFolderName).At("filesize").Set(String::FromInteger(gAtlasSets[i].FileSize));
            ctx.At(gAtlasSets[i].AtlasFolderName).At("modifytime").Set(String::FromInteger(gAtlasSets[i].ModifyTime));
            ctx.At(gAtlasSets[i].AtlasFolderName).At("createtime").Set(String::FromInteger(gAtlasSets[i].CreateTime));
        }
    }

    bool R::IsAtlasUpdated()
    {
        bool AnyUpdated = false;
        for(sint32 i = 0, iend = gAtlasSets.Count(); i < iend; ++i)
        {
            auto& CurAtlasSet = gAtlasSets.At(i);
            const String& FullPath = CurAtlasSet.AtlasFileName;
            uint64 FileSize = 0, ModifyTime = 0, CreateTime = 0;
            if(Asset::Exist(FullPath, nullptr, &FileSize, &CreateTime, nullptr, &ModifyTime))
            if(CurAtlasSet.FileSize != FileSize || CurAtlasSet.ModifyTime != ModifyTime || CurAtlasSet.CreateTime != CreateTime)
            {
                CurAtlasSet.FileSize = FileSize;
                CurAtlasSet.ModifyTime = ModifyTime;
                CurAtlasSet.CreateTime = CreateTime;
                CurAtlasSet.Updated = true;
                AnyUpdated = true;
            }
        }

        if(AnyUpdated)
        {
            Platform::BroadcastNotify("AtlasUpdated", nullptr, NT_ZayAtlas, nullptr, true);
            return true;
        }
        return false;
    }

    String R::PrintUpdatedAtlas(bool everything)
    {
        Context UpdatedAtlas;
        for(sint32 i = 0, iend = gAtlasSets.Count(); i < iend; ++i)
        {
            if(gAtlasSets[i].Updated || everything)
            {
                UpdatedAtlas.At(i).At("folder").Set(Platform::File::RootForAssetsRem() + gAtlasSets[i].AtlasFolderName);
                UpdatedAtlas.At(i).At("filesize").Set(String::FromInteger(gAtlasSets[i].FileSize));
                UpdatedAtlas.At(i).At("modifytime").Set(String::FromInteger(gAtlasSets[i].ModifyTime));
            }
        }
        return UpdatedAtlas.SaveJson();
    }

    void R::RebuildAll()
    {
        gImageMap.Reset();
        gExistMap.Reset();
        for(sint32 i = 0, iend = gAtlasSets.Count(); i < iend; ++i)
        {
            BoxrBuilder Builder;
            if(gAtlasSets[i].Updated)
            {
                gAtlasSets.At(i).Updated = false;
                Builder.LoadAtlas(gAtlasSets[i].KeyFileName,
                    gAtlasSets[i].AtlasFileName, 0 < i, gAtlasSets[i].KeyVerCode);
            }
            Builder.SaveSubImages(gAtlasSets[i].AtlasFolderName);
        }
    }

    void R::MakeGrapImage(chars name, sint32 x, sint32 y, sint32 w, sint32 h, float blur, bool use_repaint)
    {
        auto& CurImage = gImageMap(name);
        const rect128 GrabRect = {x, y, x + w, y + h};
        auto GrabImage = Platform::Utility::GetWindowImage(GrabRect, blur, use_repaint);
        if(auto NewBitmap = Platform::Utility::ImageToBitmap(GrabImage))
        {
            id_bitmap OldBitmap = CurImage.ChangeBitmap(NewBitmap);
            Bmp::Remove(OldBitmap);
            gExistMap(name).mFlag = true;
        }
    }

    void R::ClearImages(Strings names)
    {
        for(sint32 i = 0, iend = names.Count(); i < iend; ++i)
        {
            gImageMap.Remove(names[i]);
            gExistMap.Remove(names[i]);
        }
    }
}
