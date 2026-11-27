#include <boss.hpp>
#include "glue_table.hpp"
#include <service/boss_zaywidget.hpp>
#include <string>
#include <exception>

ZAY_DECLARE_GLUE_CLASS("table", GlueTable)

bool GlueTable::Register(ZaySon& NewZayson, const ZayGlue::Bindings& bindings)
{
    const auto LoadedFiles = bindings.host.Find<Map<bool>&()>("table.loaded_files");
    const auto Parse = bindings.host.Find<void(chars, const String&)>("table.parse");
    const auto ClearFocus = bindings.host.Find<void()>("table.clear_focus");
    const auto Focus = bindings.host.Find<bool(chars, chars, chars)>("table.focus");
    const auto HasFocus = bindings.host.Find<bool()>("table.has_focus");
    const auto TableIndex = bindings.host.Find<sint32()>("table.index");
    const auto NamedCell = bindings.host.Find<bool(chars, chars, String&)>("table.cell.named");
    const auto IndexedCell = bindings.host.Find<bool(sint32, sint32, String&)>("table.cell.indexed");
    const auto ColumnCount = bindings.host.Find<sint32()>("table.columns");
    const auto RowCount = bindings.host.Find<sint32()>("table.rows");
    const auto ColumnName = bindings.host.Find<String(sint32)>("table.column_name");
    const auto RowName = bindings.host.Find<String(sint32)>("table.row_name");
    if(!LoadedFiles || !Parse || !ClearFocus || !Focus ||
        !HasFocus || !TableIndex || !NamedCell || !IndexedCell ||
        !ColumnCount || !RowCount || !ColumnName || !RowName)
    {
        BOSS_TRACE("[table] Missing required typed host operations");
        return false;
    }

    // focustable
    NewZayson.AddGlue("focustable", ZAY_DECLARE_GLUE(params, LoadedFiles, Parse, ClearFocus, Focus, HasFocus, TableIndex)
    {
        // loaded_file = focustable("table_new/Target_", "INNER", "Level", "EDGE")
        // loaded_file = focustable("table_new/CoreX_", "PORE", "Face", "Z25")
        // loaded_file = focustable("table_new/CoreX_", "ATAC", "Level", "Z25", "BI200")

        (*ClearFocus)();
        String LoadedFile;
        if(3 <= params.ParamCount())
        {
            const String FileTitle = params.Param(0).ToText();
            const String Group = params.Param(1).ToText();
            const String TableName = params.Param(2).ToText();
            const String RequestedFile = FileTitle + Group + ".csv";
            const String DefaultFile = FileTitle + "default.csv";

            // Only default groups use the filename prefix to avoid collisions.
            const std::string Prefix((chars) FileTitle);
            const size_t Slash = Prefix.find_last_of("/\\");
            const String DefaultGroup((Prefix.substr(
                (Slash == std::string::npos)? 0 : Slash + 1) + "default").c_str());
            const String RequestedGroup = (Group == "default")? DefaultGroup : Group;

            const auto LoadTableFile = [&](const String& AssetName, const String& ParseGroup) -> bool
            {
                if((*LoadedFiles)().Access(AssetName))
                    return true;
                if(auto CurAsset = Asset::OpenForRead(AssetName))
                {
                    Asset::Close(CurAsset);
                    try
                    {
                        const String CsvText = String::FromAsset(AssetName);
                        (*Parse)(ParseGroup, CsvText);
                        (*LoadedFiles)()(AssetName) = true;
                        return true;
                    }
                    catch(const std::exception& Error)
                    {
                        BOSS_TRACE("[focustable] %s: %s", (chars) AssetName, Error.what());
                    }
                }
                return false;
            };
            const auto FindDependentTable = [&](const String& SearchGroup) -> bool
            {
                if(params.ParamCount() == 3)
                    return (*Focus)(SearchGroup, TableName, nullptr);
                for(sint32 i = 3; i < params.ParamCount(); ++i)
                {
                    const String Dependency = params.Param(i).ToText();
                    if((*Focus)(SearchGroup, TableName, Dependency))
                        return true;
                }
                return false;
            };

            if(LoadTableFile(RequestedFile, RequestedGroup))
                FindDependentTable(RequestedGroup);
            if((*HasFocus)())
                LoadedFile = RequestedFile;
            else if(LoadTableFile(DefaultFile, DefaultGroup))
            {
                FindDependentTable(DefaultGroup);
                if(!(*HasFocus)())
                    (*Focus)(DefaultGroup, TableName, nullptr);
                if((*HasFocus)())
                    LoadedFile = DefaultFile;
            }
        }
        if((*HasFocus)())
            LoadedFile += String::Format("<%d>", (*TableIndex)());
        if(params.CanReturn())
            params.Return('\'' + LoadedFile + '\'');
    }, "[FileTitle:'table_new/Target_'][Group:'INNER'][Table:'Level']#[Dependency1:'Z25'][Dependency2:'BI200']", "테이블 포커싱");

    ////////////////////////////////////////////////////////////////////////////////
    // gettablecell
    NewZayson.AddGlue("gettablecell", ZAY_DECLARE_GLUE(params, HasFocus, NamedCell)
    {
        // cell0 = gettablecell("Forehead", "Level")
        // cell1 = gettablecell("Forehead", "Interval")
        // cell2 = gettablecell("Forehead", "MotorSpeed")

        if(params.CanReturn())
        {
            String Result = "''";
            if(params.ParamCount() == 2 && (*HasFocus)())
            {
                const String Column = params.Param(0).ToText();
                const String Row = params.Param(1).ToText();
                String Cell;
                if((*NamedCell)(Column, Row, Cell))
                {
                    sint32 OffsetCheck = 0;
                    Parser::GetFloat<double>(Cell, Cell.Length(), &OffsetCheck);
                    if(0 < OffsetCheck && OffsetCheck == Cell.Length())
                        Result = Cell;
                    else
                    {
                        String Text = Cell;
                        Text.Replace("\\", "\\\\");
                        Text.Replace("'", "\\'");
                        Text.Replace("\r", "\\r");
                        Text.Replace("\n", "\\n");
                        Result = '\'' + Text + '\'';
                    }
                }
            }
            params.Return(Result);
        }
    }, "[Column:'NoSelect'][Row:'Level']", "테이블셀 반환");

    ////////////////////////////////////////////////////////////////////////////////
    // loadtable
    NewZayson.AddGlue("loadtable", ZAY_DECLARE_GLUE(params, HasFocus, IndexedCell, ColumnCount, RowCount, ColumnName, RowName)
    {
        if(params.ParamCount() == 1 && (*HasFocus)())
        {
            const String DomNameD = params.Param(0).ToText();
            if(DomNameD.Left(2) == "d.")
            {
                const String DomRoot = DomNameD.Offset(2);
                for(sint32 y = 0, yend = 1 + (*RowCount)(); y < yend; ++y)
                {
                    const String DomHead = DomRoot + String::Format(".%d", y);
                    ZayWidgetDOM::RemoveVariables(DomHead + ".");
                    for(sint32 x = 0, xend = 1 + (*ColumnCount)(); x < xend; ++x)
                    {
                        String Cell;
                        bool HasCell = true;
                        if(y == 0)
                        {
                            if(x != 0) Cell = (*ColumnName)(x - 1);
                        }
                        else if(x == 0)
                            Cell = (*RowName)(y - 1);
                        else HasCell = (*IndexedCell)(x - 1, y - 1, Cell);
                        if(HasCell)
                        {
                            sint32 OffsetCheck = 0;
                            Parser::GetFloat<double>(Cell, Cell.Length(), &OffsetCheck);
                            String Value = Cell;
                            if(OffsetCheck <= 0 || OffsetCheck != Cell.Length())
                            {
                                Value.Replace("\\", "\\\\");
                                Value.Replace("'", "\\'");
                                Value.Replace("\r", "\\r");
                                Value.Replace("\n", "\\n");
                                Value = '\'' + Value + '\'';
                            }
                            ZayWidgetDOM::SetValue(DomHead + String::Format(".%d", x), Value);
                        }
                    }
                    ZayWidgetDOM::SetValue(DomHead + ".count", String::FromInteger(1 + (*ColumnCount)()));
                }
                ZayWidgetDOM::SetValue(DomRoot + ".count", String::FromInteger(1 + (*RowCount)()));
            }
        }
    }, "[DomNameD:'d.value']", "포커싱된 테이블을 Dom으로 전개");
    return true;
}
