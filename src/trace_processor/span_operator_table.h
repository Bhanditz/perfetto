/*
 * Copyright (C) 2018 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef SRC_TRACE_PROCESSOR_SPAN_OPERATOR_TABLE_H_
#define SRC_TRACE_PROCESSOR_SPAN_OPERATOR_TABLE_H_

#include <sqlite3.h>
#include <array>
#include <deque>
#include <memory>

#include "src/trace_processor/scoped_db.h"
#include "src/trace_processor/table.h"

namespace perfetto {
namespace trace_processor {

//
class SpanOperatorTable : public Table {
 public:
  enum Column {
    kTimestamp = 0,
    kDuration = 1,
    kJoinValue = 2,
    // All other columns are dynamic depending on the joined tables.
  };
  struct Value {
    enum Type {
      kText = 0,
      kULong = 1,
      kUInt = 2,
    };

    Type type;
    std::string text_value;
    uint64_t ulong_value;
    uint32_t uint_value;
  };
  struct TableColumn {
    std::string name;
    std::string type_name;
    Value::Type type = Value::Type::kText;
  };

  SpanOperatorTable(sqlite3*, const TraceStorage*);

  static void RegisterTable(sqlite3* db, const TraceStorage* storage);

  // Table implementation.
  std::string CreateTableStmt(int argc, const char* const* argv) override;
  std::unique_ptr<Table::Cursor> CreateCursor() override;
  int BestIndex(const QueryConstraints& qc, BestIndexInfo* info) override;

 private:
  static constexpr uint8_t kReservedColumns = Column::kJoinValue + 1;

  class FilterState {
   public:
    FilterState(SpanOperatorTable*, ScopedStmt t1_stmt, ScopedStmt t2_stmt);

    int Initialize();
    int Next();
    int Eof();
    int Column(sqlite3_context* context, int N);

   private:
    struct TableRow {
      uint64_t ts = 0;
      uint64_t dur = 0;
      std::vector<Value> values;  // One for each column.
    };
    struct TableState {
      uint64_t latest_ts = std::numeric_limits<uint64_t>::max();
      size_t col_count = 0;
      ScopedStmt stmt;

      // TODO(lalitm): change this from being an arrray to a map.
      std::array<TableRow, base::kMaxCpus> rows;
    };

    int ExtractNext(bool pull_t1);
    int SetupReturnForJoinValue(uint32_t join_value,
                                const TableRow& t1_row,
                                const TableRow& t2_row);
    void ReportSqliteResult(sqlite3_context* context,
                            SpanOperatorTable::Value value);

    uint64_t ts_ = 0;
    uint64_t dur_ = 0;
    uint32_t join_val_ = 0;
    TableRow t1_to_ret_;
    TableRow t2_to_ret_;

    TableState t1_;
    TableState t2_;

    // TODO(lalitm): change this to be a iterator into t1's rows.
    uint32_t cleanup_join_val_;
    bool is_eof_ = true;

    SpanOperatorTable* const table_;
  };

  class Cursor : public Table::Cursor {
   public:
    Cursor(SpanOperatorTable*, sqlite3* db);
    ~Cursor() override;

    // Methods to be implemented by derived table classes.
    int Filter(const QueryConstraints& qc, sqlite3_value** argv) override;
    int Next() override;
    int Eof() override;
    int Column(sqlite3_context* context, int N) override;

   private:
    sqlite3* const db_;
    SpanOperatorTable* const table_;
    std::unique_ptr<FilterState> filter_state_;
  };

  struct TableDefinition {
    std::string name;
    std::vector<TableColumn> cols;
    std::string join_col_name;
  };

  TableDefinition t1_;
  TableDefinition t2_;
  std::string join_col_;

  sqlite3* const db_;
};

}  // namespace trace_processor
}  // namespace perfetto

#endif  // SRC_TRACE_PROCESSOR_SPAN_OPERATOR_TABLE_H_
