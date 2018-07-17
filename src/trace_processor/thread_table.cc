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

#include "src/trace_processor/thread_table.h"

#include "perfetto/base/logging.h"
#include "src/trace_processor/query_constraints.h"
#include "src/trace_processor/sqlite_utils.h"

namespace perfetto {
namespace trace_processor {

namespace {

using namespace sqlite_utils;

}  // namespace

ThreadTable::ThreadTable(const TraceStorage* storage) : storage_(storage) {
}

sqlite3_module ThreadTable::CreateModule() {
  sqlite3_module module = Table::CreateModule();
  module.xConnect = [](sqlite3* db, void* raw_args, int, const char* const*,
                       sqlite3_vtab** tab, char**) {
    int res = sqlite3_declare_vtab(db,
                                   "CREATE TABLE threads("
                                   "utid UNSIGNED INT, "
                                   "upid UNSIGNED INT, "
                                   "name TEXT, "
                                   "PRIMARY KEY(utid)"
                                   ") WITHOUT ROWID;");
    if (res != SQLITE_OK)
      return res;
    TraceStorage* storage = static_cast<TraceStorage*>(raw_args);
    *tab = static_cast<sqlite3_vtab*>(new ThreadTable(storage));
    return SQLITE_OK;
  };
  return module;
}

int ThreadTable::Open(sqlite3_vtab_cursor** ppCursor) {
  *ppCursor = static_cast<sqlite3_vtab_cursor*>(new Cursor(storage_));
  return SQLITE_OK;
}

// Called at least once but possibly many times before filtering things and is
// the best time to keep track of constriants.
int ThreadTable::BestIndex(sqlite3_index_info* idx) {
  QueryConstraints qc;

  for (int i = 0; i < idx->nOrderBy; i++) {
    Column column = static_cast<Column>(idx->aOrderBy[i].iColumn);
    unsigned char desc = idx->aOrderBy[i].desc;
    qc.AddOrderBy(column, desc);
  }
  idx->orderByConsumed = true;

  for (int i = 0; i < idx->nConstraint; i++) {
    const auto& cs = idx->aConstraint[i];
    if (!cs.usable)
      continue;
    qc.AddConstraint(cs.iColumn, cs.op);

    idx->estimatedCost = cs.iColumn == Column::kUtid ? 10 : 100;

    // argvIndex is 1-based so use the current size of the vector.
    int argv_index = static_cast<int>(qc.constraints().size());
    idx->aConstraintUsage[i].argvIndex = argv_index;
  }

  idx->idxStr = qc.ToNewSqlite3String().release();
  idx->needToFreeIdxStr = true;

  return SQLITE_OK;
}

ThreadTable::Cursor::Cursor(const TraceStorage* storage) : storage_(storage) {
}

int ThreadTable::Cursor::Column(sqlite3_context* context, int N) {
  auto thread = storage_->GetThread(utid_filter_.current);
  switch (N) {
    case Column::kUtid: {
      sqlite3_result_int64(context, utid_filter_.current);
      break;
    }
    case Column::kUpid: {
      sqlite3_result_int64(context, thread.upid);
      break;
    }
    case Column::kName: {
      const auto& name = storage_->GetString(thread.name_id);
      sqlite3_result_text(context, name.c_str(),
                          static_cast<int>(name.length()), nullptr);
      break;
    }
    default: {
      PERFETTO_FATAL("Unknown column %d", N);
      break;
    }
  }
  return SQLITE_OK;
}

int ThreadTable::Cursor::Filter(int /*idxNum*/,
                                const char* idxStr,
                                int argc,
                                sqlite3_value** argv) {
  QueryConstraints qc = QueryConstraints::FromString(idxStr);

  PERFETTO_DCHECK(qc.constraints().size() == static_cast<size_t>(argc));

  utid_filter_.min = 1;
  utid_filter_.max = static_cast<uint32_t>(storage_->thread_count());
  utid_filter_.desc = false;
  utid_filter_.current = utid_filter_.min;

  for (size_t j = 0; j < qc.constraints().size(); j++) {
    const auto& cs = qc.constraints()[j];
    if (cs.iColumn == Column::kUtid) {
      TraceStorage::UniqueTid constraint_utid =
          static_cast<TraceStorage::UniqueTid>(sqlite3_value_int(argv[j]));
      // Filter the range of utids that we are interested in, based on the
      // constraints in the query. Everything between min and max (inclusive)
      // will be returned.
      if (IsOpGe(cs.op) || IsOpGt(cs.op)) {
        utid_filter_.min =
            IsOpGt(cs.op) ? constraint_utid + 1 : constraint_utid;
      } else if (IsOpLe(cs.op) || IsOpLt(cs.op)) {
        utid_filter_.max =
            IsOpLt(cs.op) ? constraint_utid - 1 : constraint_utid;
      } else if (IsOpEq(cs.op)) {
        utid_filter_.min = constraint_utid;
        utid_filter_.max = constraint_utid;
      }
    }
  }
  for (const auto& ob : qc.order_by()) {
    if (ob.iColumn == Column::kUtid) {
      utid_filter_.desc = ob.desc;
      utid_filter_.current =
          utid_filter_.desc ? utid_filter_.max : utid_filter_.min;
    }
  }

  return SQLITE_OK;
}

int ThreadTable::Cursor::Next() {
  utid_filter_.desc ? --utid_filter_.current : ++utid_filter_.current;

  return SQLITE_OK;
}

int ThreadTable::Cursor::Eof() {
  return utid_filter_.desc ? utid_filter_.current < utid_filter_.min
                           : utid_filter_.current > utid_filter_.max;
}
}  // namespace trace_processor
}  // namespace perfetto
