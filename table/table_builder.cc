// Copyright (c) 2011 The LevelDB Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file. See the AUTHORS file for names of contributors.

#include "leveldb/table_builder.h"

#include <assert.h>
#include "leveldb/comparator.h"
#include "leveldb/env.h"
#include "leveldb/filter_policy.h"
#include "leveldb/options.h"
#include "leveldb/table.h"
#include "table/block_builder.h"
#include "table/filter_block.h"
#include "table/format.h"
#include "util/coding.h"
#include "util/crc32c.h"
#include "table/block.h"
#include "leveldb/cache.h"

#include <stdio.h>
#include <vector>
#include "leveldb/params.h"



namespace leveldb {


struct TableBuilder::Rep {
  Options options;
  Options index_block_options;
  WritableFile* file;
  uint64_t offset;
  Status status;
  BlockBuilder data_block;
  BlockBuilder index_block;
  std::string first_key;
  std::string last_key;
  int64_t num_entries;
  bool closed;          // Either Finish() or Abandon() has been called.
  FilterBlockBuilder* filter_block;

  // We do not emit the index entry for a block until we have seen the
  // first key for the next data block.  This allows us to use shorter
  // keys in the index block.  For example, consider a block boundary
  // between the keys "the quick brown fox" and "the who".  We can use
  // "the r" as the key for the index block entry since it is >= all
  // entries in the first block and < all entries in subsequent
  // blocks.
  //
  // Invariant: r->pending_index_entry is true only if data_block is empty.
  bool pending_index_entry;
  BlockHandle pending_handle;  // Handle to add to index block

  std::string compressed_output;

  Rep(const Options& opt, WritableFile* f)
      : options(opt),
        index_block_options(opt),
        file(f),
        offset(0),
        data_block(&options),
        index_block(&index_block_options),
        num_entries(0),
        closed(false),
        filter_block(opt.filter_policy == NULL ? NULL
                     : new FilterBlockBuilder(opt.filter_policy)),
        pending_index_entry(false) {
    index_block_options.block_restart_interval = 1;
  }
};

TableBuilder::TableBuilder(const Options& options, WritableFile* file, bool pre_caching)
    : rep_(new Rep(options, file)),
	  pre_caching(pre_caching),
	  cached_block_number(0),
	  cachedRanges(NULL){
  if (rep_->filter_block != NULL) {
    rep_->filter_block->StartBlock(0);
  }
  rangeCursor[0] = 0;
  rangeCursor[1] = 0;
}

TableBuilder::~TableBuilder() {
  assert(rep_->closed);  // Catch errors where caller forgot to call Finish()
  delete rep_->filter_block;
  delete rep_;
}

Status TableBuilder::ChangeOptions(const Options& options) {
  // Note: if more fields are added to Options, update
  // this function to catch changes that should not be allowed to
  // change in the middle of building a Table.
  if (options.comparator != rep_->options.comparator) {
    return Status::InvalidArgument("changing comparator while building table");
  }

  // Note that any live BlockBuilders point to rep_->options and therefore
  // will automatically pick up the updated options.
  rep_->options = options;
  rep_->index_block_options = options;
  rep_->index_block_options.block_restart_interval = 1;
  return Status::OK();
}

void TableBuilder::Add(const Slice& key, const Slice& value) {
  Rep* r = rep_;
  assert(!r->closed);
  if (!ok()) return;
  if (r->num_entries > 0) {
    //assert(r->options.comparator->Compare(key, Slice(r->last_key)) > 0);
  }else{
	//fist key of the file is also the first key of the block
    // 设置 first key
	  r->first_key.assign(key.data(),key.size());
  }

  //also indicate this key is the first key of this block
  if (r->pending_index_entry) {
    assert(r->data_block.empty());
    r->options.comparator->FindShortestSeparator(&r->last_key, key);
    std::string handle_encoding;
    r->pending_handle.EncodeTo(&handle_encoding);
    r->index_block.Add(r->last_key, Slice(handle_encoding));
    r->pending_index_entry = false;
  }

  if (r->filter_block != NULL) {
    r->filter_block->AddKey(key);
  }

  r->last_key.assign(key.data(), key.size());
  r->num_entries++;
  r->data_block.Add(key, value);

  const size_t estimated_block_size = r->data_block.CurrentSizeEstimate();
  if (estimated_block_size >= r->options.block_size) {
    // flush
    Flush(pre_caching);
  }
}

void TableBuilder::Flush(bool cache) {
  Rep* r = rep_;
  assert(!r->closed);
  if (!ok()) return;
  if (r->data_block.empty()) return;
  assert(!r->pending_index_entry);
  // 写入对应的数据块
  WriteBlock(&r->data_block, &r->pending_handle,cache);
  if (ok()) {
    r->pending_index_entry = true;
    r->status = r->file->Flush();
  }
  if (r->filter_block != NULL) {
    r->filter_block->StartBlock(r->offset);
  }
}

static void DeleteCachedBlock_builder(const Slice& key, void* value) {
  Block* block = reinterpret_cast<Block*>(value);
  delete block;
}

void TableBuilder::WriteBlock(BlockBuilder* block, BlockHandle* handle, bool cache) {
  // File format contains a sequence of blocks where each block has:
  //    block_data: uint8[n]
  //    type: uint8
  //    crc: uint32
  assert(ok());
  Rep* r = rep_;
  Slice raw = block->Finish();

  Slice block_contents;
  CompressionType type = r->options.compression;
  // TODO(postrelease): Support more compression options: zlib?
  switch (type) {
    case kNoCompression:
      block_contents = raw;
      break;

    case kSnappyCompression: {
      std::string* compressed = &r->compressed_output;
      if (port::Snappy_Compress(raw.data(), raw.size(), compressed) &&
          compressed->size() < raw.size() - (raw.size() / 8u)) {
        block_contents = *compressed;
      } else {
        // Snappy not supported, or compressed less than 12.5%, so just
        // store uncompressed form
        block_contents = raw;
        type = kNoCompression;
      }
      break;
    }
  }

  // 如果开启了缓存替换
  //for pre-fetching
  if(cache&&r->options.block_cache&&cachedRanges){

      bool shouldCache = false;
      for(int w = 0; w<2; w++){

          // 遍历对应的需要执行替换的缓存的范围
      	  for(int i = rangeCursor[w]; i < (*cachedRanges)[w].size(); i++){
      		  Slice *s = (*cachedRanges)[w][i];

            // 如果当前块的第一个 Key 大于对应的缓存块的上界，即超出范围
      		  //current writing block has passed this range, should never overlap with any coming blocks
      		  if(r->options.comparator->Compare(r->first_key,s[1])>0){
      			  rangeCursor[w]++;
      		  }else if(r->options.comparator->Compare(r->last_key,s[0])>=0){
              // 如果当前块的最后一个 Key 大于缓存块的下界，那么需要缓存该数据块，因为有重叠
          		  //current block overlap with this range
      			  //printf("keys: %s %s %s %s\n",r->first_key.c_str(),s[0].ToString().c_str(),r->last_key.c_str(),s[1].ToString().c_str());
      			  shouldCache = true;
      			  cached_block_number++;
      			  break;
      		  }
      	  }
      	  if(shouldCache){
      		  break;
      	  }
      }

      // 把新的构建的块插入到对应的缓存中
      //insert this block into cache since it will be visited in the future with a high potential
      if(shouldCache){
      		char cache_key_buffer[16];
      		//EncodeFixed64(cache_key_buffer, table->rep_->cache_id);
      		EncodeFixed64(cache_key_buffer, r->file->getFilenumber());
      		EncodeFixed64(cache_key_buffer+8, handle->offset());
      		Slice key(cache_key_buffer, sizeof(cache_key_buffer));
      		char *chs = new char[block_contents.size()];
      		memcpy(chs,block_contents.data(),block_contents.size());
      		Block *blockobj = new Block(chs,block_contents.size(),true);
      		leveldb::Cache::Handle *handle = r->options.block_cache->Insert(key,(void*)blockobj,blockobj->size(),&DeleteCachedBlock_builder);
      		r->options.block_cache->Release(handle);
      		//delete blockobj;
      }
  }

  // 写入块编码后的数据到文件
  WriteRawBlock(block_contents, type, handle);

  r->compressed_output.clear();
  block->Reset();
}

void TableBuilder::WriteRawBlock(const Slice& block_contents,
                                 CompressionType type,
                                 BlockHandle* handle) {
  Rep* r = rep_;
  handle->set_offset(r->offset);
  handle->set_size(block_contents.size());
  r->status = r->file->Append(block_contents);
  if (r->status.ok()) {
    char trailer[kBlockTrailerSize];
    trailer[0] = type;
    uint32_t crc = crc32c::Value(block_contents.data(), block_contents.size());
    crc = crc32c::Extend(crc, trailer, 1);  // Extend crc to cover block type
    EncodeFixed32(trailer+1, crc32c::Mask(crc));
    r->status = r->file->Append(Slice(trailer, kBlockTrailerSize));
    if (r->status.ok()) {
      r->offset += block_contents.size() + kBlockTrailerSize;
    }
  }
}

Status TableBuilder::status() const {
  return rep_->status;
}

Status TableBuilder::Finish() {
  Rep* r = rep_;
  Flush(false);
  assert(!r->closed);
  r->closed = true;

  BlockHandle filter_block_handle, metaindex_block_handle, index_block_handle;

  // Write filter block
  if (ok() && r->filter_block != NULL) {
    WriteRawBlock(r->filter_block->Finish(), kNoCompression,
                  &filter_block_handle);
  }

  // Write metaindex block
  if (ok()) {
    BlockBuilder meta_index_block(&r->options);
    if (r->filter_block != NULL) {
      // Add mapping from "filter.Name" to location of filter data
      std::string key = "filter.";
      key.append(r->options.filter_policy->Name());
      std::string handle_encoding;
      filter_block_handle.EncodeTo(&handle_encoding);
      meta_index_block.Add(key, handle_encoding);
    }

    // TODO(postrelease): Add stats and other meta blocks
    WriteBlock(&meta_index_block, &metaindex_block_handle,false);
  }

  // Write index block
  if (ok()) {
    if (r->pending_index_entry) {
      r->options.comparator->FindShortSuccessor(&r->last_key);
      std::string handle_encoding;
      r->pending_handle.EncodeTo(&handle_encoding);
      r->index_block.Add(r->last_key, Slice(handle_encoding));
      r->pending_index_entry = false;
    }
    WriteBlock(&r->index_block, &index_block_handle,false);
  }

  // Write footer
  if (ok()) {
    Footer footer;
    footer.set_metaindex_handle(metaindex_block_handle);
    footer.set_index_handle(index_block_handle);
    std::string footer_encoding;
    footer.EncodeTo(&footer_encoding);
    r->status = r->file->Append(footer_encoding);
    if (r->status.ok()) {
      r->offset += footer_encoding.size();
    }
  }
  return r->status;
}

void TableBuilder::Abandon() {
  Rep* r = rep_;
  assert(!r->closed);
  r->closed = true;
}

uint64_t TableBuilder::NumEntries() const {
  return rep_->num_entries;
}

uint64_t TableBuilder::FileSize() const {
  return rep_->offset;
}

}  // namespace leveldb
