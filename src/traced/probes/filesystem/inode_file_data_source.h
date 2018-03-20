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

#ifndef SRC_TRACED_PROBES_FILESYSTEM_INODE_FILE_DATA_SOURCE_H_
#define SRC_TRACED_PROBES_FILESYSTEM_INODE_FILE_DATA_SOURCE_H_

#include <sys/stat.h>
#include <sys/types.h>
#include <map>
#include <set>
#include <string>

#include "perfetto/ftrace_reader/ftrace_controller.h"
#include "perfetto/tracing/core/basic_types.h"
#include "perfetto/tracing/core/trace_writer.h"
#include "src/traced/probes/filesystem/fs_mount.h"

#include "perfetto/trace/filesystem/inode_file_map.pbzero.h"

namespace perfetto {

using Inode = uint64_t;
using InodeFileMap = protos::pbzero::InodeFileMap;

class InodeMapValue {
 public:
  protos::pbzero::InodeFileMap_Entry_Type type() const { return entry_type_; }
  std::set<std::string> paths() const { return paths_; }
  void SetType(protos::pbzero::InodeFileMap_Entry_Type entry_type) {
    entry_type_ = entry_type;
  }
  void SetPaths(std::set<std::string> paths) { paths_ = paths; }
  void AddPath(std::string path) { paths_.emplace(path); }

 private:
  protos::pbzero::InodeFileMap_Entry_Type entry_type_;
  std::set<std::string> paths_;
};

void ScanFilesDFS(
    const std::string& root_directory,
    const std::function<void(BlockDeviceID,
                             Inode,
                             std::string,
                             protos::pbzero::InodeFileMap_Entry_Type)>& fn);

void CreateDeviceToInodeMap(
    const std::string& root_directory,
    std::map<BlockDeviceID, std::map<Inode, InodeMapValue>>* block_device_map);

class InodeFileDataSource {
 public:
  explicit InodeFileDataSource(
      std::map<BlockDeviceID, std::map<Inode, InodeMapValue>>*
          file_system_inodes,
      std::unique_ptr<TraceWriter> writer);

  void WriteInodes(const FtraceMetadata& metadata);

 private:
  std::map<BlockDeviceID, std::map<Inode, InodeMapValue>>* file_system_inodes_;
  std::multimap<BlockDeviceID, std::string> mount_points_;
  std::unique_ptr<TraceWriter> writer_;
};

}  // namespace perfetto

#endif  // SRC_TRACED_PROBES_FILESYSTEM_INODE_FILE_DATA_SOURCE_H_
