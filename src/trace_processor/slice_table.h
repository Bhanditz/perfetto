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

#ifndef SRC_TRACE_PROCESSOR_SLICE_TABLE_H_
#define SRC_TRACE_PROCESSOR_SLICE_TABLE_H_

#include <limits>
#include <memory>

#include "src/trace_processor/storage_cursor.h"
#include "src/trace_processor/table.h"

namespace perfetto {
namespace trace_processor {

class QueryConstraints;
class TraceStorage;

// A virtual table that allows to query slices coming from userspace events
// such as chromium TRACE_EVENT macros. Conversely to "shced" slices, these
// slices can be nested and form stacks.
// The current implementation of this table is extremely simple and not
// particularly efficient, as it delegates all the sorting and filtering to
// the SQLite query engine.
class SliceTable : public Table {
 public:
  enum Column {
    kTimestamp = 0,
    kDuration = 1,
    kUtid = 2,
    kCategory = 3,
    kName = 4,
    kDepth = 5,
    kStackId = 6,
    kParentStackId = 7,
  };

  SliceTable(sqlite3*, const TraceStorage* storage);

  static void RegisterTable(sqlite3* db, const TraceStorage* storage);

  // Table implementation.
  Table::Schema CreateSchema(int argc, const char* const* argv) override;
  std::unique_ptr<Table::Cursor> CreateCursor(const QueryConstraints&,
                                              sqlite3_value**) override;
  int BestIndex(const QueryConstraints&, BestIndexInfo*) override;

 private:
  class ValueRetriever : public StorageCursor::ValueRetriever {
   public:
    ValueRetriever(const TraceStorage* storage);

    uint32_t GetUint(size_t, uint32_t) const override;
    uint64_t GetUlong(size_t, uint32_t) const override;
    StringAndDestructor GetString(size_t, uint32_t) const override;

   private:
    const TraceStorage* storage_;
  };

  const TraceStorage* const storage_;
};

}  // namespace trace_processor
}  // namespace perfetto

#endif  // SRC_TRACE_PROCESSOR_SLICE_TABLE_H_
