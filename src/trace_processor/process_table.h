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

#ifndef SRC_TRACE_PROCESSOR_PROCESS_TABLE_H_
#define SRC_TRACE_PROCESSOR_PROCESS_TABLE_H_

#include <sqlite3.h>
#include <limits>
#include <memory>

#include "src/trace_processor/table.h"
#include "src/trace_processor/trace_storage.h"

namespace perfetto {
namespace trace_processor {

// The implementation of the SQLite table containing each unique process with
// their details (only name at the moment).
class ProcessTable : public Table {
 public:
  enum Column { kUpid = 0, kName = 1 };

  ProcessTable(const TraceStorage*);
  static sqlite3_module CreateModule();

  // Implementation of Table.
  int BestIndex(sqlite3_index_info*) override;
  int Open(sqlite3_vtab_cursor**) override;

 private:
  using Constraint = sqlite3_index_info::sqlite3_index_constraint;
  using OrderBy = sqlite3_index_info::sqlite3_index_orderby;

  class Cursor : public Table::Cursor {
   public:
    Cursor(const TraceStorage*);

    // Implementation of Table::Cursor.
    int Filter(int idxNum,
               const char* idxStr,
               int argc,
               sqlite3_value** argv) override;
    int Next() override;
    int Eof() override;
    int Column(sqlite3_context* context, int N) override;

   private:
    struct UpidFilter {
      TraceStorage::UniquePid min;
      TraceStorage::UniquePid max;
      TraceStorage::UniquePid current;
      bool desc;
    };

    const TraceStorage* const storage_;
    UpidFilter upid_filter_;
  };

  const TraceStorage* const storage_;
};
}  // namespace trace_processor
}  // namespace perfetto

#endif  // SRC_TRACE_PROCESSOR_PROCESS_TABLE_H_
