#include <boss.hpp>
#include "dependencytable.hpp"

#include <string>
#include <vector>
#include <map>
#include <set>
#include <stdexcept>

const String* InnerTable::Cell(chars column, chars row) const
{
    if(!column || !row) return nullptr;
    const sint32* X = mColumns.Access(column);
    const sint32* Y = mRows.Access(row);
    return (X && Y)? Cell(*X, *Y) : nullptr;
}

const String* InnerTable::Cell(sint32 x, sint32 y) const
{
    if(x < 0 || ColumnCount() <= x || y < 0 || RowCount() <= y)
        return nullptr;
    return &mCells[y][x];
}

const String InnerTable::ColumnName(sint32 x) const
{
    if(x < 0 || ColumnCount() <= x) return String();
    for(sint32 i = 0, iend = mColumns.Count(); i < iend; ++i)
    {
        chararray Key;
        if(const sint32* Index = mColumns.AccessByOrder(i, &Key))
        if(*Index == x) return String(&Key[0]);
    }
    return String();
}

const String InnerTable::RowName(sint32 y) const
{
    if(y < 0 || RowCount() <= y) return String();
    for(sint32 i = 0, iend = mRows.Count(); i < iend; ++i)
    {
        chararray Key;
        if(const sint32* Index = mRows.AccessByOrder(i, &Key))
        if(*Index == y) return String(&Key[0]);
    }
    return String();
}

void DependencyTable::Parse(chars group, chars csvtext)
{
    if(!group || !*group || !csvtext)
        throw std::invalid_argument("DependencyTable::Parse: invalid argument");

    std::string Text(csvtext);
    if(Text.compare(0, 3, "\xEF\xBB\xBF") == 0) Text.erase(0, 3);
    std::map<std::string, InnerTable> Parsed;
    std::vector<std::string> Dependencies, Columns, ActiveKeys;
    std::set<std::string> SeenRows;
    std::vector<std::string> Cells;
    std::string CellText;
    size_t Line = 1, RecordLine = 1;
    sint32 TableIndex = 0;
    enum { Start, Bare, Quoted, Closed } State = Start;
    const auto Trim = [](const std::string& s) -> std::string
    {
        const size_t First = s.find_first_not_of(" \t\r\n");
        if(First == std::string::npos) return "";
        return s.substr(First, s.find_last_not_of(" \t\r\n") - First + 1);
    };
    const auto Fail = [&](const std::string& reason)
    {
        throw std::runtime_error("CSV line " + std::to_string(RecordLine) + ": " + reason);
    };
    const auto FinishCell = [&]()
    {
        Cells.push_back(Trim(CellText));
        CellText.clear(); State = Start;
    };
    const auto FinishRecord = [&]()
    {
        FinishCell();
        bool Blank = true;
        for(const auto& Item : Cells) if(!Item.empty()) Blank = false;
        if(!Blank)
        {
            const std::string& First = Cells[0];
            if(First == "Dependencies")
            {
                Dependencies.clear(); ActiveKeys.clear(); Columns.clear(); SeenRows.clear();
                std::set<std::string> Unique;
                for(size_t i = 1; i < Cells.size(); ++i)
                {
                    if(Cells[i].empty() || !Unique.insert(Cells[i]).second)
                        Fail("empty or duplicate dependency");
                    Dependencies.push_back(Cells[i]);
                }
            }
            else if(First.size() >= 2 && First.front() == '[' && First.back() == ']')
            {
                const std::string Name = First.substr(1, First.size() - 2);
                if(Name.empty()) Fail("empty table name");
                ActiveKeys.clear(); Columns.clear(); SeenRows.clear();
                const std::string Base = std::string(group) + "." + Name;
                if(Dependencies.empty()) ActiveKeys.push_back(Base);
                else for(const auto& Dep : Dependencies) ActiveKeys.push_back(Base + "." + Dep);
                for(const auto& Key : ActiveKeys)
                {
                    if(Parsed.count(Key)) Fail("duplicate table: " + Key);
                    Parsed.emplace(Key, InnerTable());
                    Parsed.at(Key).mInnerIndex = TableIndex;
                }
                TableIndex++;
                if(Cells.size() < 2) Fail("missing columns");
                std::set<std::string> Unique;
                for(size_t i = 1; i < Cells.size(); ++i)
                {
                    if(Cells[i].empty() || !Unique.insert(Cells[i]).second)
                        Fail("empty or duplicate column");
                    Columns.push_back(Cells[i]);
                    for(const auto& Key : ActiveKeys)
                        Parsed.at(Key).mColumns(Cells[i].c_str()) = sint32(i - 1);
                }
            }
            else
            {
                if(ActiveKeys.empty()) Fail("row before table header");
                if(First.empty() || Cells.size() != Columns.size() + 1)
                    Fail("row column count mismatch: " + First);
                if(!SeenRows.insert(First).second) Fail("duplicate row: " + First);
                for(const auto& Key : ActiveKeys)
                {
                    auto& Table = Parsed.at(Key);
                    Table.mRows(First.c_str()) = Table.mCells.Count();
                    auto& Row = Table.mCells.AtAdding();
                    for(size_t i = 0; i < Columns.size(); ++i)
                        Row.AtAdding() = String(Cells[i + 1].c_str());
                }
            }
        }
        Cells.clear();
    };
    for(size_t i = 0; i < Text.size(); ++i)
    {
        const char c = Text[i];
        if(State == Quoted)
        {
            if(c == '"')
            {
                if(i + 1 < Text.size() && Text[i + 1] == '"') { CellText += '"'; ++i; }
                else State = Closed;
            }
            else { CellText += c; if(c == '\n') ++Line; }
        }
        else if(c == ',') FinishCell();
        else if(c == '\r' || c == '\n')
        {
            if(c == '\r' && i + 1 < Text.size() && Text[i + 1] == '\n') ++i;
            FinishRecord(); RecordLine = ++Line;
        }
        else if(State == Closed)
        {
            if(c != ' ' && c != '\t') Fail("characters after closing quote");
        }
        else if(c == '"')
        {
            if(State != Start || !Trim(CellText).empty()) Fail("unexpected quote");
            CellText.clear(); State = Quoted;
        }
        else { CellText += c; if(c != ' ' && c != '\t') State = Bare; }
    }
    if(State == Quoted) Fail("unclosed quote");
    if(!Cells.empty() || !CellText.empty() || State == Closed) FinishRecord();
    if(Parsed.empty()) Fail("no tables");
    for(const auto& Item : Parsed)
        mTables(Item.first.c_str()) = Item.second;
}

const InnerTable* DependencyTable::Table(chars group, chars table, chars dependency)
{
    if(!group || !table) return nullptr;
    std::string Key = std::string(group) + "." + table;
    if(dependency && *dependency)
        Key += std::string(".") + dependency;
    return mTables.Access(Key.c_str());
}
