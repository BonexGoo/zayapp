#include <boss.hpp>
#include "glue_file.hpp"

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
    return true;
}
