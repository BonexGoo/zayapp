#pragma once
#include <boss.hpp>

class InnerTable
{
public:
    // [column] NoSelect, Default, Lv1
    // [row] Version, RF level Min, Level, PulseNum
    const String* Cell(chars column, chars row) const;
    const String* Cell(sint32 x, sint32 y) const;
    const String ColumnName(sint32 x) const;
    const String RowName(sint32 y) const;

public:
    sint32 TableIndex() const {return mInnerIndex;}
    sint32 ColumnCount() const {return mColumns.Count();}
    sint32 RowCount() const {return mRows.Count();}

private:
    friend class DependencyTable;
    Map<sint32> mColumns;
    Map<sint32> mRows;
    Array<Strings> mCells;
    sint32 mInnerIndex {-1};
};
typedef Map<InnerTable> InnerTables;

class DependencyTable
{
public:
    // [group] WRNK, ACSC, INNER
    // [table] Lesion, Face, Level
    // [dependency] O25, Z25, Z49G, EDGE, BI100, MONO100
    void Parse(chars group, chars csvtext);
    const InnerTable* Table(chars group, chars table, chars dependency = "");

private:
    InnerTables mTables; // WRNK.Lesion, WRNK.Face.O25, INNER.Level.EDGE
};
