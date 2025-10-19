// Lock-free task queue templates for coordinating KV operations
#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <immintrin.h>
#include <sched.h>

inline constexpr int CMD_GET = 1;
inline constexpr int CMD_SET = 2;
inline constexpr int CMD_POST = 3;
inline constexpr int CMD_DELETE = 4;

template <typename K, typename V>
struct Task {
	int cmd;
	K key;
	V value;
	bool has_value;
	int client_pid;
	int task_id;
};

template <typename K, typename V>
class TaskQueue {
private:
	static constexpr std::size_t QUEUE_SIZE = 1024;
	static constexpr int MAX_RETRIES = 1000;

	alignas(64) Task<K, V> tasks[QUEUE_SIZE];
	alignas(64) std::atomic<std::uint64_t> head;
	alignas(64) std::atomic<std::uint64_t> tail;
	alignas(64) std::atomic<std::uint64_t> version;

public:
	TaskQueue() : head(0), tail(0), version(0) {}

	bool try_push(const Task<K, V>& task, int max_retries = MAX_RETRIES) {
		int retries = 0;
		int backoff = 1;

		while (retries < max_retries) {
			std::uint64_t current_tail = tail.load(std::memory_order_acquire);
			std::uint64_t current_head = head.load(std::memory_order_acquire);
			std::uint64_t next_tail = (current_tail + 1) % QUEUE_SIZE;

			if (next_tail == current_head % QUEUE_SIZE) {
				return false;
			}

			std::size_t index = current_tail % QUEUE_SIZE;
			tasks[index] = task;

			if (tail.compare_exchange_weak(current_tail, next_tail,
										   std::memory_order_release,
										   std::memory_order_relaxed)) {
				version.fetch_add(1, std::memory_order_release);
				return true;
			}

			for (int i = 0; i < backoff; ++i) {
				_mm_pause();
			}
			backoff = (backoff << 1) & 0xFF;
			++retries;
		}

		return false;
	}

	bool try_pop(Task<K, V>& task, int max_retries = MAX_RETRIES) {
		int retries = 0;
		int backoff = 1;

		while (retries < max_retries) {
			std::uint64_t current_head = head.load(std::memory_order_acquire);
			std::uint64_t current_tail = tail.load(std::memory_order_acquire);

			if (current_head % QUEUE_SIZE == current_tail % QUEUE_SIZE) {
				return false;
			}

			std::size_t index = current_head % QUEUE_SIZE;
			task = tasks[index];

			std::uint64_t next_head = (current_head + 1) % QUEUE_SIZE;

			if (head.compare_exchange_weak(current_head, next_head,
										   std::memory_order_release,
										   std::memory_order_relaxed)) {
				version.fetch_add(1, std::memory_order_release);
				return true;
			}

			for (int i = 0; i < backoff; ++i) {
				_mm_pause();
			}
			backoff = (backoff << 1) & 0xFF;
			++retries;
		}

		return false;
	}

	void push(const Task<K, V>& task) {
		while (!try_push(task, MAX_RETRIES)) {
			sched_yield();
		}
	}

	bool pop(Task<K, V>& task) {
		while (!try_pop(task, MAX_RETRIES)) {
			sched_yield();
		}
		return true;
	}

	std::size_t size() const {
		std::uint64_t current_tail = tail.load(std::memory_order_relaxed);
		std::uint64_t current_head = head.load(std::memory_order_relaxed);

		if (current_tail >= current_head) {
			return (current_tail - current_head) % QUEUE_SIZE;
		}
		return QUEUE_SIZE - ((current_head - current_tail) % QUEUE_SIZE);
	}

	bool empty() const {
		return head.load(std::memory_order_acquire) % QUEUE_SIZE ==
			   tail.load(std::memory_order_acquire) % QUEUE_SIZE;
	}

	bool full() const {
		std::uint64_t current_tail = tail.load(std::memory_order_acquire);
		std::uint64_t current_head = head.load(std::memory_order_acquire);
		return ((current_tail + 1) % QUEUE_SIZE) == (current_head % QUEUE_SIZE);
	}
};
