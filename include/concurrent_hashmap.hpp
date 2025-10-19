// ConcurrentHashMap template providing striped locking for thread-safe access
#pragma once

#include <functional>
#include <list>
#include <mutex>
#include <thread>
#include <utility>
#include <vector>

template <typename K, typename V>
class ConcurrentHashMap {
private:
	using Bucket = std::list<std::pair<const K, V>>;

	std::vector<Bucket> table;
	mutable std::vector<std::mutex> locks;
	std::hash<K> hasher;

	size_t get_stripe_index(const K& key) const {
		return hasher(key) % locks.size();
	}

public:
	explicit ConcurrentHashMap(size_t stripe_count = std::thread::hardware_concurrency())
		: table(stripe_count * 10), locks(stripe_count) {}

	bool insert(const K& key, const V& value) {
		size_t stripe_index = get_stripe_index(key);
		std::lock_guard<std::mutex> guard(locks[stripe_index]);

		size_t bucket_index = hasher(key) % table.size();
		auto& bucket = table[bucket_index];

		for (const auto& pair : bucket) {
			if (pair.first == key) {
				return false;
			}
		}

		bucket.emplace_back(key, value);
		return true;
	}

	void insert_or_assign(const K& key, const V& value) {
		size_t stripe_index = get_stripe_index(key);
		std::lock_guard<std::mutex> guard(locks[stripe_index]);

		size_t bucket_index = hasher(key) % table.size();
		auto& bucket = table[bucket_index];

		for (auto& pair : bucket) {
			if (pair.first == key) {
				pair.second = value;
				return;
			}
		}

		bucket.emplace_back(key, value);
	}

	bool find(const K& key, V& value) const {
		size_t stripe_index = get_stripe_index(key);
		std::lock_guard<std::mutex> guard(locks[stripe_index]);

		size_t bucket_index = hasher(key) % table.size();
		const auto& bucket = table[bucket_index];

		for (const auto& pair : bucket) {
			if (pair.first == key) {
				value = pair.second;
				return true;
			}
		}
		return false;
	}

	bool erase(const K& key) {
		size_t stripe_index = get_stripe_index(key);
		std::lock_guard<std::mutex> guard(locks[stripe_index]);

		size_t bucket_index = hasher(key) % table.size();
		auto& bucket = table[bucket_index];

		for (auto it = bucket.begin(); it != bucket.end(); ++it) {
			if (it->first == key) {
				bucket.erase(it);
				return true;
			}
		}
		return false;
	}

	size_t size() const {
		std::vector<std::unique_lock<std::mutex>> all_locks;
		all_locks.reserve(locks.size());
		for (auto& lock : locks) {
			all_locks.emplace_back(lock);
		}

		size_t total_size = 0;
		for (const auto& bucket : table) {
			total_size += bucket.size();
		}
		return total_size;
	}
};
