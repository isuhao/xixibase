/*
   Copyright [2011] [Yao Yuan(yeaya@163.com)]

   Licensed under the Apache License, Version 2.0 (the "License");
   you may not use this file except in compliance with the License.
   You may obtain a copy of the License at

       http://www.apache.org/licenses/LICENSE-2.0

   Unless required by applicable law or agreed to in writing, software
   distributed under the License is distributed on an "AS IS" BASIS,
   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
   See the License for the specific language governing permissions and
   limitations under the License.
*/

#include <new>
#include <assert.h>
#include "cache.h"
#include "currtime.h"
#include "stats.h"
#include "util.h"
#include "log.h"
#include "peer_cache_pdu.h"
#include "settings.h"

Cache_Mgr cache_mgr_;

#define CALC_ITEM_SIZE(k, d, e) (sizeof(Cache_Item) + k + d + e)
#define CHUNK_ALIGN_BYTES 8

Cache_Watch::Cache_Watch(uint32_t watch_id, uint32_t expire_time) {
	watch_id_ = watch_id;
	expire_time_ = expire_time;
	sequence_ = 0;
}

void Cache_Watch::check_and_set_callback(boost::shared_ptr<Cache_Watch_Sink>& sp, uint32_t ack_sequence, uint32_t expire_time,
										 uint32_t& sequence, std::vector<uint64_t>& updated_list, std::vector<watch_notify_type>&/*out*/ updated_type_list) {
	expire_time_ = expire_time;
	if (!wait_updated_list_.empty()) {
		if (ack_sequence == sequence_) {
			wait_updated_list_.clear();
			wait_updated_type_list_.clear();
		} else {
			for (size_t i = 0; i < wait_updated_list_.size(); i++) {
				updated_list.push_back(wait_updated_list_[i]);
				updated_type_list.push_back(wait_updated_type_list_[i]);
			}
//			std::vector<uint64_t>::iterator it = wait_updated_list_.begin();
//			while (it != wait_updated_list_.end()) {
//				updated_list.push_back(*it);
//				++it;
//			}
			sequence = sequence_;
			return;
		}
	}
	if (updated_list_.size() > 0) {
		for (size_t i = 0; i < updated_list_.size(); i++) {
			updated_list.push_back(updated_list_[i]);
				updated_type_list.push_back(updated_type_list_[i]);
		}
//		std::vector<uint64_t>::iterator it = updated_list_.begin();
//		while (it != updated_list_.end()) {
//			updated_list.push_back(*it);
//			++it;
//		}
		updated_list_.swap(wait_updated_list_);
		updated_type_list_.swap(wait_updated_type_list_);
		sequence = next_sequence();
		return;
	} else {
		boost::shared_ptr<Cache_Watch_Sink> p = wp_.lock();
		if (p != NULL) {
			p->on_cache_watch_notify(watch_id_);
		}
		wp_ = sp;
		sequence = 0;
		return;
	}
}

void Cache_Watch::check_and_clear_callback(boost::shared_ptr<Cache_Watch_Sink>& sp, uint32_t& sequence,
										   std::vector<uint64_t>& updated_list, std::vector<watch_notify_type>&/*out*/ updated_type_list) {
	boost::shared_ptr<Cache_Watch_Sink> p = wp_.lock();
	if (sp == p) {
		if (updated_list_.size() > 0 && wait_updated_list_.empty()) {
			for (size_t i = 0; i < updated_list_.size(); i++) {
				updated_list.push_back(updated_list_[i]);
				updated_type_list.push_back(updated_type_list_[i]);
			}
//			std::vector<uint64_t>::iterator it = updated_list_.begin();
//			while (it != updated_list_.end()) {
//				updated_list.push_back(*it);
//				++it;
//			}
			updated_list_.swap(wait_updated_list_);
			updated_type_list_.swap(wait_updated_type_list_);
			sequence = next_sequence();
		}
		wp_.reset();
	}
}

void Cache_Watch::notify_watch(uint64_t cache_id, watch_notify_type type) {
	updated_list_.push_back(cache_id);
	updated_type_list_.push_back(type);
	boost::shared_ptr<Cache_Watch_Sink> p = wp_.lock();
	if (p != NULL) {
		p->on_cache_watch_notify(watch_id_);
		wp_.reset();
	}
}

Cache_Mgr::Cache_Mgr() {
	last_cache_id_ = 0;
	class_id_max_ = 0;
	mem_limit_ = 0;
	mem_used_ = 0;
	memset(max_size_, 0, sizeof(max_size_));
#ifdef USING_BOOST_POOL
	memset(&pools_, 0, sizeof(pools_));
#endif
	last_class_id_ = CLASSID_MIN;

	last_print_stats_time_ = 0;

	last_check_expired_time_ = 0;
	last_watch_id_ = 0;

	memset(expire_check_time_, 0, sizeof(expire_check_time_));
	for (int i = 0; i < 32; i++) {
		expiration_time_[i] = 1 << i;
	}
	expiration_time_[32] = UINT32_C(0xFFFFFFFF);
	//  for (int i = 0; i < 33; i++) {
	//    LOG_INFO("expiration_time_" << i << " =" << expiration_time_[i]);
	//  }
	memset(free_cache_max_count, 0, sizeof(free_cache_max_count));
}

Cache_Mgr::~Cache_Mgr() {
}

void Cache_Mgr::init(uint64_t limit, uint32_t item_size_max, uint32_t item_size_min, double factor) {
	LOG_INFO("Cache_Mgr::init, limit=" << limit << " item_size_max=" << item_size_max << " item_size_min=" << item_size_min
		<< " factor=" << factor << " sizeof_item=" << sizeof(Cache_Item));

	uint32_t size = sizeof(Cache_Item) + item_size_min;

	mem_limit_ = limit;

	class_id_max_ = CLASSID_MIN;
	for (; class_id_max_ < CLASSID_MAX && size <= (sizeof(Cache_Item) + item_size_max) / factor; ++class_id_max_) {

		if (size % CHUNK_ALIGN_BYTES) {
			size += CHUNK_ALIGN_BYTES - (size % CHUNK_ALIGN_BYTES);
		}

		max_size_[class_id_max_] = size;
		free_cache_max_count[class_id_max_] = 1024 * 1024 / size;
		if (free_cache_max_count[class_id_max_] > 1000) {
			free_cache_max_count[class_id_max_] = 1000;
		}
#ifdef USING_BOOST_POOL
		pools_[class_id_max_] = new boost::pool<>(size);
#endif
		size = (uint32_t)(size * factor);
	}
	max_size_[class_id_max_] = sizeof(Cache_Item) + item_size_max;
	stats_.set_max_class_id(class_id_max_);
	LOG_INFO("Cache_Mgr::init, class_id_max=" << class_id_max_ << " max_size=" << max_size_[class_id_max_]);
}

uint32_t Cache_Mgr::get_class_id(uint32_t size) {
	uint32_t class_id = last_class_id_;
	if (size < max_size_[class_id]) {
		if (size > max_size_[class_id - 1]) {
			return class_id;
		} else {
			for (--class_id; class_id >= CLASSID_MIN; --class_id) {
				if (size > max_size_[class_id - 1]) {
					break;
				}
			}
			last_class_id_ = class_id;
			return class_id;
		}
	} else {
		for (; class_id <= class_id_max_; ++class_id) {
			if (size <= max_size_[class_id]) {
				last_class_id_ = class_id;
				return class_id;
			}
		}
	}
	return 0;
}

uint64_t Cache_Mgr::get_cache_id() {
	if (++last_cache_id_ == 0) {
		last_cache_id_ = 1;
	}
	return last_cache_id_;
}

void Cache_Mgr::check_expired() {
	uint32_t curr_time = curr_time_.get_current_time();
	if (curr_time != last_check_expired_time_) {
		last_check_expired_time_ = curr_time;
		cache_lock_.lock();

		expire_watchs(curr_time);
		expire_items(curr_time);
		free_flushed_items();

		cache_lock_.unlock();
	}
}

void Cache_Mgr::stats(const XIXI_Stats_Req_Pdu* pdu, std::string& result) {
	cache_lock_.lock();
	switch (pdu->sub_op()) {
	case XIXI_STATS_SUB_OP_ADD_GROUP:
		if (stats_.add_group(pdu->group_id)) {
			result = "success";
		} else {
			result = "fail";
		}
		break;
	case XIXI_STATS_SUB_OP_REMOVE_GROUP:
		if (stats_.remove_group(pdu->group_id)) {
			result = "success";
		} else {
			result = "fail";
		}
		break;
	case XIXI_STATS_SUB_OP_GET_STATS_GROUP_ONLY:
		stats_.get_stats(pdu->group_id, pdu->class_id, result);
		break;
//	case XIXI_STATS_SUB_OP_GET_AND_CLEAR_STATS_GROUP_ONLY:
//		stats_.get_and_clear_stats(pdu->group_id, pdu->class_id, result);
//		break;
	case XIXI_STATS_SUB_OP_GET_STATS_SUM_ONLY:
		stats_.get_stats(pdu->class_id, result);
		break;
//	case XIXI_STATS_SUB_OP_GET_AND_CLEAR_STATS_SUM_ONLY:
//		stats_.get_and_clear_stats(pdu->class_id, result);
//		break;
	default:
		result = "unknown sub command";
		break;
	}
	cache_lock_.unlock();
}

void Cache_Mgr::print_stats() {
	uint32_t curr_time = curr_time_.get_current_time();
	if (curr_time >= last_print_stats_time_ + 30) {
		last_print_stats_time_ = curr_time;
		cache_lock_.lock();

		stats_.print();

		cache_lock_.unlock();
	}
}

void Cache_Mgr::expire_items(uint32_t curr_time) {
	for (int i = 0; i < 33; i++) {
		if (curr_time >= expire_check_time_[i]) {
			expire_check_time_[i] = curr_time_.realtime(curr_time, expiration_time_[i]);

			Cache_Item* it = expire_list_[i].front();
			while (it != NULL) {
				if (it->expire_time <= curr_time) {
					Cache_Item* next = it->next();
					do_unlink(it, WATCH_NOTIFY_TYPE_EXPIRED);
					it = next;
				} else {
					Cache_Item* next = it->next();
					uint32_t expiration = it->expire_time - curr_time;
					for (int j = i; j >= 0; j--) {
						if (expiration >= expiration_time_[j]) {
							expire_list_[i].remove(it);
							it->expiration_id = j;
							expire_list_[j].push_front(it);
							break;
						}
					}
					it = next;
				}
			}
		}
	}
}

Cache_Item* Cache_Mgr::do_alloc(uint32_t group_id, uint32_t key_length, uint32_t flags, 
								uint32_t expire_time, uint32_t data_size, uint32_t ext_size) {
	uint32_t item_size = CALC_ITEM_SIZE(key_length, data_size, ext_size);

	uint32_t id = get_class_id(item_size);
	if (id == 0) {
		return NULL;
	}
	item_size = max_size_[id];

	Cache_Item* it = free_cache_list_[id].pop_front();
	if (it == NULL) {
		if (mem_used_ + item_size <= mem_limit_) {
#ifdef USING_BOOST_POOL
			void* buf = pools_[id]->malloc();
#else
			void* buf = malloc(item_size);
#endif
			if (buf != NULL) {
				it = new (buf) Cache_Item;
				mem_used_ += item_size;
			} else {
				return NULL;
			}
		} else {
			return NULL;
		}
	}

	it->class_id = (uint8_t)id;
	it->expiration_id = (uint8_t)get_expiration_id(curr_time_.get_current_time(), expire_time);
	it->ref_count = 1;
	it->group_id = group_id;
	it->key_length = key_length;
	it->data_size = data_size;
	it->expire_time = expire_time;
	it->flags = flags;
	it->ext_size = ext_size;

	return it;
}

void Cache_Mgr::free_item(Cache_Item* it) {
	assert(!expire_list_[it->expiration_id].is_linked(it));
	assert(it->ref_count == 0);

	uint32_t id = it->class_id;
	it->reset();
	if (free_cache_list_[id].size() < free_cache_max_count[id]) {
		free_cache_list_[id].push_front(it);
	} else {
		uint32_t item_size = max_size_[id];
#ifdef USING_BOOST_POOL
		pools_[id]->free(it);
#else
		::free(it);
#endif
		mem_used_ -= item_size;
	}
}

bool Cache_Mgr::item_size_ok(uint32_t key_length, uint32_t data_size, uint32_t ext_size) {
	return get_class_id(CALC_ITEM_SIZE(key_length, data_size, ext_size)) != 0;
}

uint32_t Cache_Mgr::get_expiration_id(uint32_t curr_time, uint32_t expire_time) {
	if (expire_time == 0) {
		return 33;
	} else if (expire_time > curr_time) {
		uint32_t expiration = expire_time - curr_time;
		uint32_t i = 1;
		for (; i < 33; i++) {
			if (expiration < expiration_time_[i]) {
				break;
			}
		}
		return i - 1;
	} else {
		return 0;
	}
}

void Cache_Mgr::do_link(Cache_Item* it) {
	cache_hash_map_.insert(it, it->hash_value_);

	stats_.item_link(it->group_id, it->class_id, it->total_size());

	it->cache_id = get_cache_id();
	it->last_update_time = curr_time_.get_current_time();

	it->ref_count++;
	expire_list_[it->expiration_id].push_back(it);
}

void Cache_Mgr::do_unlink(Cache_Item* it, watch_notify_type type) {
	assert(expire_list_[it->expiration_id].is_linked(it));

	stats_.item_unlink(it->group_id, it->class_id, it->total_size());

	Cache_Key ck(it->group_id, it->get_key(), it->key_length);
	cache_hash_map_.remove(&ck, it->hash_value_);

	expire_list_[it->expiration_id].remove(it);

	if (it->watch_item != NULL) {
		notify_watch(it, type);
		delete it->watch_item;
		it->watch_item = NULL;
	}
	do_release_reference(it);
}

void Cache_Mgr::do_unlink_flush(Cache_Item* it) {
	assert(expire_list_[it->expiration_id].is_linked(it));

	stats_.item_unlink(it->group_id, it->class_id, it->total_size());
	Cache_Key ck(it->group_id, it->get_key(), it->key_length);
	cache_hash_map_.remove(&ck, it->hash_value_);

	expire_list_[it->expiration_id].remove(it);
	if (it->watch_item != NULL) {
		notify_watch(it, WATCH_NOTIFY_TYPE_FLUSHED);
		delete it->watch_item;
		it->watch_item = NULL;
	}
	flush_cache_list_.push_back(it);
}

void Cache_Mgr::do_release_reference(Cache_Item* it) {
	assert(it->ref_count > 0);
	it->ref_count--;
	if (it->ref_count == 0) {
		free_item(it);
	}
}

void Cache_Mgr::do_replace(Cache_Item* it, Cache_Item* new_it) {
	do_unlink(it, WATCH_NOTIFY_TYPE_DATA_UPDATED);
	do_link(new_it);
}

Cache_Item* Cache_Mgr::do_get(uint32_t group_id, const uint8_t* key, uint32_t key_length, uint32_t hash_value) {
	Cache_Key ck(group_id, key, key_length);
	Cache_Item* it = cache_hash_map_.find(&ck, hash_value);

	if (it != NULL) {
		LOG_TRACE("Cache_Mgr.do_get, found, key " << string((char*)key, key_length));
		it->ref_count++;
	} else {
		LOG_TRACE("Cache_Mgr.do_get, not found, key " << string((char*)key, key_length));
	}

	return it;
}

Cache_Item* Cache_Mgr::do_get(uint32_t group_id, const uint8_t* key, uint32_t key_length,
							  uint32_t hash_value, uint32_t&/*out*/ expiration) {
	Cache_Key ck(group_id, key, key_length);
	Cache_Item* it = cache_hash_map_.find(&ck, hash_value);

	if (it != NULL) {
		LOG_TRACE("Cache_Mgr.do_get, found, key " << string((char*)key, key_length));

		if (it->expire_time == 0) {
			it->ref_count++;
			expiration = 0;
		} else {
			uint32_t currtime = curr_time_.get_current_time();
			if (it->expire_time > currtime) {
				it->ref_count++;
				expiration = it->expire_time - currtime;
			} else {
				do_unlink(it, WATCH_NOTIFY_TYPE_EXPIRED);
				it = NULL;
			}
		}
	} else {
		LOG_TRACE("Cache_Mgr.do_get, not found, key " << string((char*)key, key_length));
	}

	return it;
}

Cache_Item* Cache_Mgr::do_get_touch(uint32_t group_id, const uint8_t* key, uint32_t key_length,
		uint32_t hash_value, uint32_t expiration) {
	Cache_Key ck(group_id, key, key_length);
	Cache_Item* it = cache_hash_map_.find(&ck, hash_value);

	if (it != NULL) {
		LOG_TRACE("Cache_Mgr.do_get_touch, found, key " << string((char*)key, key_length));

		it->ref_count++;
		it->expire_time = curr_time_.realtime(expiration);
	} else {
		LOG_TRACE("Cache_Mgr.do_get_touch, not found, key " << string((char*)key, key_length));
	}

	return it;
}

Cache_Item* Cache_Mgr::alloc_item(uint32_t group_id, uint32_t key_length, uint32_t flags,
								  uint32_t expiration, uint32_t data_size, uint32_t ext_size) {
	Cache_Item* it;
	cache_lock_.lock();
	it = do_alloc(group_id, key_length, flags, curr_time_.realtime(expiration), data_size, ext_size);
	cache_lock_.unlock();
	return it;
}

Cache_Item*  Cache_Mgr::get(uint32_t group_id, const uint8_t* key, uint32_t key_length, uint32_t watch_id,
							bool is_base, uint32_t&/*out*/ expiration, xixi_reason&/*out*/ reason) {
	Cache_Item* item;
	uint32_t hash_value = hash32(key, key_length, group_id);
	reason = XIXI_REASON_SUCCESS;
	cache_lock_.lock();

	item = do_get(group_id, key, key_length, hash_value, expiration);
	if (item != NULL) {
		if (watch_id != 0) {
			if (is_valid_watch_id(watch_id)) {
				item->add_watch(watch_id);
				stats_.get_hit_watch(group_id, item->class_id, item->total_size());
			} else {
				reason = XIXI_REASON_WATCH_NOT_FOUND;
				stats_.get_hit_watch_miss(group_id, item->class_id);
				do_release_reference(item);
				item = NULL;
			}
		} else {
			if (is_base) {
				stats_.get_base_hit(item->group_id, item->class_id);
			} else {
				stats_.get_hit_no_watch(group_id, item->class_id, item->total_size());
			}
		}
	} else {
		reason = XIXI_REASON_NOT_FOUND;
		if (is_base) {
			stats_.get_base_miss(group_id);
		} else {
			stats_.get_miss(group_id);
		}
	}
	cache_lock_.unlock();
	return item;
}

Cache_Item* Cache_Mgr::get_touch(uint32_t group_id, const uint8_t* key, uint32_t key_length, uint32_t watch_id,
								uint32_t expiration, xixi_reason&/*out*/ reason) {
	Cache_Item* item;
	uint32_t hash_value = hash32(key, key_length, group_id);
	reason = XIXI_REASON_SUCCESS;
	cache_lock_.lock();

	item = do_get_touch(group_id, key, key_length, hash_value, expiration);
	if (item != NULL) {
	  if (watch_id != 0) {
		  if (is_valid_watch_id(watch_id)) {
			  item->add_watch(watch_id);
			  stats_.get_touch_hit_watch(group_id, item->class_id, item->total_size());
		  } else {
			  reason = XIXI_REASON_WATCH_NOT_FOUND;
			  stats_.get_touch_hit_watch_miss(group_id, item->class_id);
			  do_release_reference(item);
			  item = NULL;
		  }
	  } else {
		  stats_.get_touch_hit_no_watch(group_id, item->class_id, item->total_size());
	  }
	} else {
		reason = XIXI_REASON_NOT_FOUND;
		stats_.get_touch_miss(group_id);
	}
	cache_lock_.unlock();
	return item;
}
/*
bool Cache_Mgr::get_base(uint32_t group_id, const uint8_t* key, uint32_t key_length, uint64_t&/*out * / cache_id,
						 uint32_t&/*out* / flags, uint32_t&/*out * / expiration, char* /*out* / ext, uint32_t&/*in out* / ext_size) {
	Cache_Item* it;
	bool ret;
	uint32_t hash_value = hash32(key, key_length, group_id);
	cache_lock_.lock();
	it = do_get(group_id, key, key_length, hash_value, expiration);
	if (it != NULL) {
		cache_id = it->cache_id;
		flags = it->flags;
		if (it->ext_size > 0 && ext_size > 0) {
			if (ext_size > it->ext_size) {
				ext_size = it->ext_size;
			}
			memcpy(ext, it->get_ext(), ext_size);
		} else {
			ext_size = 0;
		}
		stats_.get_base_hit(it->group_id, it->class_id);
		do_release_reference(it);
		ret = true;
	} else {
		stats_.get_base_miss(group_id);
		ret = false;
	}
	cache_lock_.unlock();
	return ret;
}
*/
bool Cache_Mgr::update_flags(uint32_t group_id, const uint8_t* key, uint32_t key_length, const XIXI_Update_Flags_Req_Pdu* pdu, uint64_t&/*out*/ cache_id) {
	Cache_Item* it;
	bool ret = true;
	cache_id = 0;
	uint32_t hash_value = hash32(key, key_length, group_id);
	cache_lock_.lock();
	it = do_get(group_id, key, key_length, hash_value);
	if (it != NULL) {
		if (pdu->cache_id == 0 || pdu->cache_id == it->cache_id) {
			it->flags = pdu->flags;
			if (it->watch_item != NULL) {
				notify_watch(it, WATCH_NOTIFY_TYPE_BASE_INFO_UPDATED);
			}
			it->cache_id = get_cache_id();
			it->last_update_time = curr_time_.get_current_time();

			cache_id = it->cache_id;
			stats_.update_flags_success(it->group_id, it->class_id);
			do_release_reference(it);
		} else {
			cache_id = it->cache_id;
			stats_.update_flags_mismatch(it->group_id, it->class_id);
			ret = false;
		}
	} else {
		stats_.update_flags_miss(group_id);
		ret = false;
	}
	cache_lock_.unlock();
	return ret;
}

bool Cache_Mgr::update_expiration(uint32_t group_id, const uint8_t* key, uint32_t key_length, const XIXI_Update_Expiration_Req_Pdu* pdu, uint64_t&/*out*/ cache_id) {
	Cache_Item* it;
	bool ret = true;
	cache_id = 0;
	uint32_t hash_value = hash32(key, key_length, group_id);
	cache_lock_.lock();
	it = do_get(group_id, key, key_length, hash_value);
	if (it != NULL) {
		if (pdu->cache_id == 0 || pdu->cache_id == it->cache_id) {
			uint32_t expire_time = curr_time_.realtime(pdu->expiration);
			uint32_t expiration_id = get_expiration_id(curr_time_.get_current_time(), expire_time);
			if (expiration_id != it->expiration_id) {
				expire_list_[it->expiration_id].remove(it);
				it->expiration_id = expiration_id;
				expire_list_[expiration_id].push_front(it);
			}
			it->expire_time = expire_time;

			cache_id = it->cache_id;
			stats_.update_expiration_success(it->group_id, it->class_id);
			do_release_reference(it);
		} else {
			cache_id = -1;
			stats_.update_expiration_mismatch(it->group_id, it->class_id);
			ret = false;
		}
	} else {
		stats_.update_expiration_miss(group_id);
		ret = false;
	}
	cache_lock_.unlock();
	return ret;
}

void Cache_Mgr::release_reference(Cache_Item* item) {
	cache_lock_.lock();
	do_release_reference(item);
	cache_lock_.unlock();
}

#include <boost/filesystem.hpp>
Cache_Item* Cache_Mgr::load_from_file(uint32_t group_id, const uint8_t* key, uint32_t key_length, uint32_t watch_id, uint32_t expiration, xixi_reason&/*out*/ reason) {
	string filename = settings_.home_dir + "webapps" + (char*)key;
	LOG_DEBUG("load_from_file " << filename);

	FILE* file = fopen(filename.c_str(), "rb");
	if (file == NULL) {
		reason = XIXI_REASON_NOT_FOUND;
		return NULL;
	}
	fseek(file, 0, SEEK_END);
	size_t file_size = ftell(file);
	fseek(file, 0, SEEK_SET);
 	LOG_DEBUG("load_from_file " << filename << " size=" << file_size);
	uint32_t suffix_size;
	const char* suffix = get_suffix((const char*)key, key_length, suffix_size);

	uint32_t mime_type_length = 0;
	const uint8_t* mime_type = settings_.get_mime_type((const uint8_t*)suffix, suffix_size, mime_type_length);

	Cache_Item* item = alloc_item(group_id, key_length, 0,
		expiration, (uint32_t)file_size, mime_type_length);

	if (item == NULL) {
		fclose(file);
		file = NULL;
		if (item_size_ok(key_length, (uint32_t)file_size, mime_type_length)) {
			reason = XIXI_REASON_OUT_OF_MEMORY;
		} else {
			reason = XIXI_REASON_TOO_LARGE;
		}
		return NULL;
	}

	memcpy(item->get_key(), key, key_length);
	size_t count = fread(item->get_data(), (uint32_t)file_size, 1, file);
	fclose(file);
	file = NULL;

	if (mime_type != NULL) {
		item->set_ext(mime_type);
	}

	item->calc_hash_value();

	reason = XIXI_REASON_SUCCESS;

	cache_lock_.lock();

	Cache_Item* old_it = do_get(group_id, key, key_length, item->hash_value_);
	if (old_it == NULL) {
		if (watch_id != 0) {
			if (is_valid_watch_id(watch_id)) {
				item->add_watch(watch_id);
				stats_.add_success_watch(item->group_id, item->class_id, item->total_size());
			} else {
				stats_.add_watch_miss(item->group_id, item->class_id);
				reason = XIXI_REASON_WATCH_NOT_FOUND;
			}
		} else {
			stats_.add_success(item->group_id, item->class_id, item->total_size());
		}

		if (reason == XIXI_REASON_SUCCESS) {
			do_link(item);
		} else {
			do_release_reference(item);
			item = NULL;
		}
	} else {
		do_release_reference(item);
		item = old_it;
	}

	cache_lock_.unlock();

	return item;
}

xixi_reason Cache_Mgr::add(Cache_Item* item, uint32_t watch_id, uint64_t&/*out*/ cache_id) {
	xixi_reason reason = XIXI_REASON_SUCCESS;

	cache_lock_.lock();

	Cache_Item* old_it = do_get(item->group_id, item->get_key(), item->key_length, item->hash_value_);
	if (old_it == NULL) {
		if (watch_id != 0) {
			if (is_valid_watch_id(watch_id)) {
				item->add_watch(watch_id);
				stats_.add_success_watch(item->group_id, item->class_id, item->total_size());
			} else {
				stats_.add_watch_miss(item->group_id, item->class_id);
				reason = XIXI_REASON_WATCH_NOT_FOUND;
			}
		} else {
			stats_.add_success(item->group_id, item->class_id, item->total_size());
		}

		if (reason == XIXI_REASON_SUCCESS) {
			do_link(item);
			cache_id = item->cache_id;
		}
	} else {
		do_release_reference(old_it);
		stats_.add_fail(item->group_id, item->class_id);
		reason = XIXI_REASON_EXISTS;
	}

	cache_lock_.unlock();
	return reason;
}

xixi_reason Cache_Mgr::set(Cache_Item* item, uint32_t watch_id, uint64_t&/*out*/ cache_id) {
	xixi_reason reason = XIXI_REASON_SUCCESS;

	cache_lock_.lock();

	Cache_Item* old_it = do_get(item->group_id, item->get_key(), item->key_length, item->hash_value_);
	if (old_it != NULL) {
		//    LOG_ERROR("Cache_Mgr set, key " << string((char*)item->get_key(), item->key_length) << " hash_value=" << it->hash_value);
		if (item->cache_id == 0 || item->cache_id == old_it->cache_id) {
			if (watch_id != 0) {
				if (is_valid_watch_id(watch_id)) {
					stats_.set_success_watch(item->group_id, item->class_id, item->total_size());
				} else {
					stats_.set_watch_miss(item->group_id, item->class_id);
					reason = XIXI_REASON_WATCH_NOT_FOUND;
				}
			} else {
				stats_.set_success(item->group_id, item->class_id, item->total_size());
			}
			if (reason == XIXI_REASON_SUCCESS) {
				do_replace(old_it, item);
				// after notify last watch, then add new watch
				if (watch_id != 0) {
					item->add_watch(watch_id);
				}
				cache_id = item->cache_id;
			}
		} else {
			stats_.set_mismatch(item->group_id, item->class_id);
			reason = XIXI_REASON_MISMATCH;
		}
		do_release_reference(old_it);
	} else {
		if (watch_id != 0) {
			if (is_valid_watch_id(watch_id)) {
				item->add_watch(watch_id);
				stats_.set_success_watch(item->group_id, item->class_id, item->total_size());
			} else {
				stats_.set_watch_miss(item->group_id, item->class_id);
				reason = XIXI_REASON_WATCH_NOT_FOUND;
			}
		} else {
			stats_.set_success(item->group_id, item->class_id, item->total_size());
		}
		if (reason == XIXI_REASON_SUCCESS) {
			do_link(item);
			cache_id = item->cache_id;
		}
	}
	cache_lock_.unlock();
	return reason;
}

xixi_reason Cache_Mgr::replace(Cache_Item* it, uint32_t watch_id, uint64_t&/*out*/ cache_id) {
	xixi_reason reason = XIXI_REASON_SUCCESS;

	cache_lock_.lock();

	Cache_Item* old_it = do_get(it->group_id, it->get_key(), it->key_length, it->hash_value_);

	if (old_it == NULL) {
		reason = XIXI_REASON_NOT_FOUND;
		stats_.replace_miss(it->group_id);
	} else if (it->cache_id == 0 || it->cache_id == old_it->cache_id) {
		if (watch_id != 0) {
			if (is_valid_watch_id(watch_id)) {
				stats_.replace_success_watch(it->group_id, it->class_id, it->total_size());
			} else {
				stats_.replace_watch_miss(it->group_id, it->class_id);
				reason = XIXI_REASON_WATCH_NOT_FOUND;
			}
		} else {
			stats_.replace_success(old_it->group_id, old_it->class_id, it->total_size());
		}
		if (reason == XIXI_REASON_SUCCESS) {
			do_replace(old_it, it);
			// after notify last watch, then add new watch
			if (watch_id != 0) {
				it->add_watch(watch_id);
			}
			cache_id = it->cache_id;
		}
		do_release_reference(old_it);
	} else {
		reason = XIXI_REASON_MISMATCH;
		stats_.replace_mismatch(it->group_id, it->class_id);
		do_release_reference(old_it);
	}
	cache_lock_.unlock();
	return reason;
}

xixi_reason Cache_Mgr::append(Cache_Item* it, uint32_t watch_id, uint64_t&/*out*/ cache_id) {
	xixi_reason reason = XIXI_REASON_SUCCESS;

	cache_lock_.lock();

	Cache_Item* old_it = do_get(it->group_id, it->get_key(), it->key_length, it->hash_value_);
	if (old_it != NULL) {
		if (it->cache_id != 0 && it->cache_id != old_it->cache_id) {
			stats_.append_mismatch(it->group_id, it->class_id);
			reason = XIXI_REASON_MISMATCH;
		} else {
			Cache_Item* new_it = do_alloc(it->group_id, it->key_length, old_it->flags, old_it->expire_time, it->data_size + old_it->data_size, old_it->ext_size);
			if (new_it != NULL) {
				new_it->set_key_with_hash(it->get_key(), it->hash_value_);
				memcpy(new_it->get_data(), old_it->get_data(), old_it->data_size);
				memcpy(new_it->get_data() + old_it->data_size, it->get_data(), it->data_size);
				new_it->set_ext(old_it->get_ext());
				if (watch_id != 0) {
					if (is_valid_watch_id(watch_id)) {
						stats_.append_success_watch(it->group_id, it->class_id, it->total_size());
					} else {
						stats_.append_watch_miss(it->group_id, it->class_id);
						reason = XIXI_REASON_WATCH_NOT_FOUND;
					}
				} else {
					stats_.append_success(old_it->group_id, old_it->class_id, it->total_size());
				}
				if (reason == XIXI_REASON_SUCCESS) {
					do_replace(old_it, new_it);
					// after notify last watch, then add new watch
					if (watch_id != 0) {
						it->add_watch(watch_id);
					}
					cache_id = new_it->cache_id;
				}
				do_release_reference(new_it);
			} else {
				stats_.append_out_of_memory(it->group_id, it->class_id);
				reason = XIXI_REASON_OUT_OF_MEMORY;
			}
			do_release_reference(old_it);
		}
	} else {
		stats_.append_miss(it->group_id);
		reason = XIXI_REASON_NOT_FOUND;
	}

	cache_lock_.unlock();
	return reason;
}

xixi_reason Cache_Mgr::prepend(Cache_Item* it, uint32_t watch_id, uint64_t&/*out*/ cache_id) {
	xixi_reason reason = XIXI_REASON_SUCCESS;

	cache_lock_.lock();

	Cache_Item* old_it = do_get(it->group_id, it->get_key(), it->key_length, it->hash_value_);
	if (old_it != NULL) {
		if (it->cache_id != 0 && it->cache_id != old_it->cache_id) {
			stats_.prepend_mismatch(it->group_id, it->class_id);
			reason = XIXI_REASON_MISMATCH;
		} else {
			Cache_Item* new_it = do_alloc(it->group_id, it->key_length, old_it->flags, old_it->expire_time, it->data_size + old_it->data_size, old_it->ext_size);
			if (new_it != NULL) {
				new_it->set_key_with_hash(it->get_key(), it->hash_value_);
				memcpy(new_it->get_data(), it->get_data(), it->data_size);
				memcpy(new_it->get_data() + it->data_size, old_it->get_data(), old_it->data_size);
				new_it->set_ext(old_it->get_ext());
				if (watch_id != 0) {
					if (is_valid_watch_id(watch_id)) {
						stats_.prepend_success_watch(it->group_id, it->class_id, it->total_size());
					} else {
						stats_.prepend_watch_miss(it->group_id, it->class_id);
						reason = XIXI_REASON_WATCH_NOT_FOUND;
					}
				} else {
					stats_.prepend_success(old_it->group_id, old_it->class_id, it->total_size());
				}
				if (reason == XIXI_REASON_SUCCESS) {
					// after notify last watch, then add new watch
					if (watch_id != 0) {
						it->add_watch(watch_id);
					}
					do_replace(old_it, new_it);
					cache_id = new_it->cache_id;
				}
				do_release_reference(new_it);
			} else {
				stats_.prepend_out_of_memory(it->group_id, it->class_id);
				reason = XIXI_REASON_OUT_OF_MEMORY;
			}
			do_release_reference(old_it);
		}
	} else {
		stats_.prepend_miss(it->group_id);
		reason = XIXI_REASON_NOT_FOUND;
	}

	cache_lock_.unlock();
	return reason;
}

xixi_reason Cache_Mgr::remove(uint32_t group_id, const uint8_t* key, uint32_t key_length, uint64_t cache_id) {
	xixi_reason reason;
	uint32_t hash_value = hash32(key, key_length, group_id);

	cache_lock_.lock();

	Cache_Item* it = do_get(group_id, key, key_length, hash_value);
	if (it != NULL) {
		if (cache_id == 0 || cache_id == it->cache_id) {
			stats_.delete_success(group_id, it->class_id);
			do_unlink(it, WATCH_NOTIFY_TYPE_DELETED);
			reason = XIXI_REASON_SUCCESS;
		} else {
			stats_.delete_mismatch(group_id, it->class_id);
			reason = XIXI_REASON_MISMATCH;
		}
		do_release_reference(it);
	} else {
		stats_.delete_miss(group_id);
		reason = XIXI_REASON_NOT_FOUND;
	}

	cache_lock_.unlock();
	return reason;
}

#define INT64_MAX_STORAGE_LEN 25
xixi_reason Cache_Mgr::delta(uint32_t group_id, const uint8_t* key, uint32_t key_length, bool incr, int64_t delta, uint64_t&/*in and out*/ cache_id, int64_t&/*out*/ value) {
	xixi_reason reason;
	uint32_t hash_value = hash32(key, key_length, group_id);
	cache_lock_.lock();

	Cache_Item* it = do_get(group_id, key, key_length, hash_value);
	if (it == NULL) {
		cache_id = 0;
		value = 0;
		if (incr) {
			stats_.incr_miss(group_id);
		} else {
			stats_.decr_miss(group_id);
		}
		reason = XIXI_REASON_NOT_FOUND;
	} else if (cache_id == 0 || cache_id == it->cache_id) {
		value = 0;
		safe_toi64((char*)it->get_data(), it->data_size, value);
		if (incr) {
			value += delta;
		} else {
			value -= delta;
		}

		char buf[INT64_MAX_STORAGE_LEN];
		uint32_t data_size = _snprintf(buf, INT64_MAX_STORAGE_LEN, "%"PRId64, value);
		if (data_size != it->data_size) {
			Cache_Item* new_it = do_alloc(it->group_id, it->key_length, it->flags, it->expire_time, data_size, it->ext_size);
			if (new_it == NULL) {
				reason = XIXI_REASON_OUT_OF_MEMORY;
			} else {
				new_it->set_key_with_hash(it->get_key(), it->hash_value_);
				memcpy(new_it->get_data(), buf, data_size);
				new_it->set_ext(it->get_ext());
				do_replace(it, new_it);
				cache_id = new_it->cache_id;
				do_release_reference(new_it);
				if (incr) {
					stats_.incr_success(group_id);
				} else {
					stats_.decr_success(group_id);
				}
				reason = XIXI_REASON_SUCCESS;
			}
		} else {
			it->cache_id = get_cache_id();
			it->last_update_time = curr_time_.get_current_time();

			memcpy(it->get_data(), buf, data_size);
			cache_id = it->cache_id;
			if (incr) {
				stats_.incr_success(group_id);
			} else {
				stats_.decr_success(group_id);
			}
			reason = XIXI_REASON_SUCCESS;
		}
		do_release_reference(it);
	} else {
		cache_id = 0;
		value = 0;
		if (incr) {
			stats_.incr_mismatch(group_id);
		} else {
			stats_.decr_mismatch(group_id);
		}
		reason = XIXI_REASON_MISMATCH;
		do_release_reference(it);
	}

	cache_lock_.unlock();
	return reason;
}

void Cache_Mgr::flush(uint32_t group_id, uint32_t&/*out*/ flush_count, uint64_t&/*out*/ flush_size) {
	flush_count = 0;
	flush_size = 0;
	cache_lock_.lock();
	for (int i = 0; i < 34; i++) {
		Cache_Item* it = expire_list_[i].front();
		while (it != NULL) {
			Cache_Item* next = it->next();
			if (it->group_id == group_id) {
				flush_count++;
				flush_size += it->total_size();
				//  do_unlink_flush(it);
				do_unlink(it, WATCH_NOTIFY_TYPE_FLUSHED);
			}
			it = next;
		}
	}
	stats_.flush(group_id);
	cache_lock_.unlock();
}

uint32_t Cache_Mgr::get_watch_id() {
	for (int i = 0; i < 100; i++) {
		if (++last_watch_id_ == 0) {
			last_watch_id_ = 1;
		}
		if (watch_map_.find(last_watch_id_) == watch_map_.end()) {
			return last_watch_id_;
		}
	}
	return 0;
}

bool Cache_Mgr::is_valid_watch_id(uint32_t watch_id) {
	return watch_map_.find(watch_id) != watch_map_.end();
}

uint32_t Cache_Mgr::create_watch(uint32_t group_id, uint32_t max_next_check_interval) {
	cache_lock_.lock();
	uint32_t watch_id = get_watch_id();
	if (watch_id != 0) {
		boost::shared_ptr<Cache_Watch> sp(new Cache_Watch(watch_id, curr_time_.realtime(max_next_check_interval)));
		watch_map_[watch_id] = sp;
		stats_.create_watch(group_id);
	}
	cache_lock_.unlock();
	return watch_id;
}

bool Cache_Mgr::check_watch_and_set_callback(boost::shared_ptr<Cache_Watch_Sink>& sp, uint32_t group_id, uint32_t watch_id,
											 uint32_t ack_sequence, uint32_t max_next_check_interval,
											 uint32_t& sequence, std::vector<uint64_t>& updated_list, std::vector<watch_notify_type>&/*out*/ updated_type_list) {
	 bool ret = true;
	 cache_lock_.lock();
	 std::map<uint32_t, boost::shared_ptr<Cache_Watch> >::iterator it = watch_map_.find(watch_id);
	 if (it != watch_map_.end()) {
		 it->second->check_and_set_callback(sp, ack_sequence, curr_time_.realtime(max_next_check_interval), sequence, updated_list, updated_type_list);
		 stats_.check_watch(group_id);
	 } else {
		 ret = false;
		 stats_.check_watch_miss(group_id);
	 }
	 cache_lock_.unlock();
	 return ret;
}

bool Cache_Mgr::check_watch_and_clear_callback(boost::shared_ptr<Cache_Watch_Sink>& sp, uint32_t watch_id,
											   uint32_t& sequence, std::vector<uint64_t>& updated_list, std::vector<watch_notify_type>&/*out*/ updated_type_list) {
	bool ret = true;
	cache_lock_.lock();
	std::map<uint32_t, boost::shared_ptr<Cache_Watch> >::iterator it = watch_map_.find(watch_id);
	if (it != watch_map_.end()) {
		it->second->check_and_clear_callback(sp, sequence, updated_list, updated_type_list);
	} else {
		ret = false;
	}
	cache_lock_.unlock();
	return ret;
}

void Cache_Mgr::notify_watch(Cache_Item* item, watch_notify_type type) {
	std::set<uint32_t>::iterator it = item->watch_item->watch_map.begin();
	while (it != item->watch_item->watch_map.end()) {
		uint32_t watch_id = *it;
		std::map<uint32_t, boost::shared_ptr<Cache_Watch> >::iterator it2 = watch_map_.find(watch_id);
		if (it2 != watch_map_.end()) {
			it2->second->notify_watch(item->cache_id, type);
			++it;
		} else {
			item->watch_item->watch_map.erase(it++);
		}
	}
}

void Cache_Mgr::expire_watchs(uint32_t curr_time) {
	//  LOG_INFO("Cache_Mgr::expire_watchs curr_time=" << curr_time);
	std::map<uint32_t, boost::shared_ptr<Cache_Watch> >::iterator it = watch_map_.begin();
	while (it != watch_map_.end()) {
		if (it->second->is_expired(curr_time)) {
			watch_map_.erase(it++);
		} else {
			++it;
		}
	}
}

void Cache_Mgr::free_flushed_items() {
	Cache_Item* item = flush_cache_list_.front();
	while (item != NULL) {
		Cache_Item* next = item->next();
		flush_cache_list_.remove(item);
		do_release_reference(item);
		item = next;
	}
}

