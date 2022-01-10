// Copyright (c) 2011 The LevelDB Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file. See the AUTHORS file for names of contributors.
//
// Thread-safe (provides internal synchronization)

#ifndef STORAGE_LEVELDB_DB_TABLE_CACHE_H_
#define STORAGE_LEVELDB_DB_TABLE_CACHE_H_

#include <string>
#include <stdint.h>

#include "dbformat.h"
#include "lsbm/cache.h"
#include "lsbm/table.h"
#include "port/port.h"

namespace leveldb {

class Env;

class TableCache {
 public:
  TableCache(const std::string& dbname, const Options* options, int entries);
  ~TableCache();

  // Return an iterator for the specified file number (the corresponding
  // file length must be exactly "file_size" bytes).  If "tableptr" is
  // non-NULL, also sets "*tableptr" to point to the Table object
  // underlying the returned iterator, or NULL if no Table object underlies
  // the returned iterator.  The returned "*tableptr" object is owned by
  // the cache and should not be deleted, and is valid for as long as the
  // returned iterator is live.
  Iterator* NewIterator(const ReadOptions& options,
                        uint64_t file_number,
                        uint64_t file_size,
                        Table** tableptr = NULL);

  // If a seek to internal key "k" in specified file finds an entry,
  // call (*handle_result)(arg, found_key, found_value).
  Status Get(const ReadOptions& options,
             uint64_t file_number,
             uint64_t file_size,
             const Slice& k,
             void* arg,
             void (*handle_result)(void*, const Slice&, const Slice&));
  int GetRange(const ReadOptions& options,
		  const Comparator* ucmp,
          uint64_t file_number,
          uint64_t file_size,
          const Slice& start,
          const Slice& end);
  Status GetKeyRangeCached(uint64_t file_number, uint64_t file_size,std::vector<Slice *> *result);
  Status LoadTable(uint64_t file_number, uint64_t file_size);
  bool isTableHot(uint64_t file_number, uint64_t file_size);
  int getCacheNum(uint64_t file_number, uint64_t file_size);

  bool ContainKey(const ReadOptions& options,
		  	uint64_t file_number,
	  	  	uint64_t file_size,
	  	  	const Slice& k);

  Status SkipFilterGet(const ReadOptions& options,
		  	  	uint64_t file_number,
		  	  	uint64_t file_size,
		  	  	const Slice& k,
				void* arg,
				void (*saver)(void*, const Slice&, const Slice&));

  // Evict any entry for the specified file number
  void Evict(uint64_t file_number, uint64_t file_size, bool evict_block_cache);
  Status FindTable(uint64_t file_number, uint64_t file_size, Cache::Handle**);


  Cache *GetCache(){
	  return cache_;
  }
  double Percent(){
	  return cache_->Percent();
  }
  uint64_t Usage(){
	  return cache_->Used();
  }
 private:
  Env* const env_;
  const std::string dbname_;
  const Options* options_;
  Cache* cache_;

};

}  // namespace leveldb

#endif  // STORAGE_LEVELDB_DB_TABLE_CACHE_H_
