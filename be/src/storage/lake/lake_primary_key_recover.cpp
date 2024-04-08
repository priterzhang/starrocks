// Copyright 2021-present StarRocks, Inc. All rights reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     https://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "storage/lake/lake_primary_key_recover.h"

#include "column/column.h"
#include "common/constexpr.h"
#include "fs/fs_util.h"
#include "storage/chunk_helper.h"
#include "storage/lake/meta_file.h"
#include "storage/lake/tablet.h"
#include "storage/lake/update_manager.h"
#include "storage/primary_key_encoder.h"
#include "storage/storage_engine.h"
#include "storage/tablet_meta_manager.h"

namespace starrocks::lake {

Status LakePrimaryKeyRecover::pre_cleanup() {
    // 1. reset delvec in metadata and clean delvec in builder
    // TODO reclaim delvec files
    _metadata->clear_delvec_meta();
    // 2. reset primary index
    _tablet->update_mgr()->try_remove_primary_index_cache(_tablet->id());
    DataDir* data_dir = StorageEngine::instance()->get_persistent_index_store(_tablet->id());
    if (data_dir != nullptr) {
        // clear local persistent index's index meta from rocksdb, and index files.
        RETURN_IF_ERROR(TabletMetaManager::remove_tablet_persistent_index_meta(data_dir, _tablet->id()));
        std::string tablet_pk_path =
                strings::Substitute("$0/$1/", data_dir->get_persistent_index_path(), _tablet->id());
        RETURN_IF_ERROR(fs::remove_all(tablet_pk_path));
    }
    return Status::OK();
}

// Primary key schema
starrocks::Schema LakePrimaryKeyRecover::generate_pkey_schema() {
    std::shared_ptr<TabletSchema> tablet_schema = std::make_shared<TabletSchema>(_metadata->schema());
    std::vector<ColumnId> pk_columns(tablet_schema->num_key_columns());
    for (auto i = 0; i < tablet_schema->num_key_columns(); i++) {
        pk_columns[i] = (ColumnId)i;
    }
    return ChunkHelper::convert_schema(tablet_schema, pk_columns);
}

Status LakePrimaryKeyRecover::sort_rowsets(std::vector<RowsetPtr>* rowsets) {
    std::sort(rowsets->begin(), rowsets->end(), [](const RowsetPtr& a, const RowsetPtr& b) {
        const RowsetMetadataPB& rowset_meta_a = a->metadata();
        const RowsetMetadataPB& rowset_meta_b = b->metadata();
        // if rowset was generated by compaction, use max compact input rowset id as its compare id
        uint32_t rowset_a_comp_id = rowset_meta_a.has_max_compact_input_rowset_id()
                                            ? rowset_meta_a.max_compact_input_rowset_id()
                                            : rowset_meta_a.id();
        uint32_t rowset_b_comp_id = rowset_meta_b.has_max_compact_input_rowset_id()
                                            ? rowset_meta_b.max_compact_input_rowset_id()
                                            : rowset_meta_b.id();
        return rowset_a_comp_id < rowset_b_comp_id;
    });
    return Status::OK();
}

Status LakePrimaryKeyRecover::rowset_iterator(
        const starrocks::Schema& pkey_schema, OlapReaderStatistics& stats,
        const std::function<Status(const std::vector<ChunkIteratorPtr>&,
                                   const std::vector<std::unique_ptr<RandomAccessFile>>&, const std::vector<uint32_t>&,
                                   uint32_t)>& handler) {
    auto rowsets = _tablet->get_rowsets(_metadata);
    // Sort the rowsets in order of primary key occurrence,
    // so we can generate correct delvecs
    RETURN_IF_ERROR(sort_rowsets(&rowsets));
    for (auto& rowset : rowsets) {
        auto res = rowset->get_each_segment_iterator(pkey_schema, &stats);
        if (!res.ok()) {
            return res.status();
        }
        auto& itrs = res.value();
        RETURN_IF_ERROR(handler(itrs, {}, {}, rowset->id()));
    }
    return Status::OK();
}

// generate delvec and save
Status LakePrimaryKeyRecover::finalize_delvec(const PrimaryIndex::DeletesMap& new_deletes) {
    for (auto& new_delete : new_deletes) {
        auto delvec_ptr = std::make_shared<DelVector>();
        auto& del_ids = new_delete.second;
        delvec_ptr->init(_metadata->version(), del_ids.data(), del_ids.size());
        _builder->append_delvec(delvec_ptr, new_delete.first);
    }
    return Status::OK();
}

int64_t LakePrimaryKeyRecover::tablet_id() {
    return _tablet->id();
}

} // namespace starrocks::lake