#include <atomic>
#include <string.h>
#include <stdint.h>
#include <sched.h>
#include <immintrin.h> // For _mm_pause()

// Commands as integers
#define CMD_GET 1
#define CMD_SET 2
#define CMD_POST 3
#define CMD_DELETE 4

template<typename K, typename V>
struct Task {
  int cmd;
  K key;
  V value;
  bool has_value;
  int client_pid;
  int task_id;
};

// Lock-free ring buffer using compare-and-swap
template<typename K, typename V>
class TaskQueue {
private:
  static constexpr size_t QUEUE_SIZE = 1024;
  static constexpr int MAX_RETRIES = 1000;

  alignas(64) Task<K, V> tasks[QUEUE_SIZE];

  // Head and tail must be on separate cache lines to avoid false sharing
  alignas(64) std::atomic<uint64_t> head;  // Consumer index
  alignas(64) std::atomic<uint64_t> tail;  // Producer index

  // Version counter for ABA problem prevention
  alignas(64) std::atomic<uint64_t> version;

public:
  TaskQueue() : head(0), tail(0), version(0) {}

  // Non-blocking push with exponential backoff
  bool try_push(const Task<K, V>& task, int max_retries = MAX_RETRIES) {
    int retries = 0;
    int backoff = 1;

    while (retries < max_retries) {
      // Read current state (transaction begin)
      uint64_t current_tail = tail.load(std::memory_order_acquire);
      uint64_t current_head = head.load(std::memory_order_acquire);
      uint64_t next_tail = (current_tail + 1) % QUEUE_SIZE;

      // Check if queue is full
      if (next_tail == current_head % QUEUE_SIZE) {
        return false; // Queue full
      }

      // Optimistically write the task
      size_t index = current_tail % QUEUE_SIZE;
      tasks[index] = task;

      // Try to commit the transaction using CAS
      if (tail.compare_exchange_weak(current_tail, next_tail,
                                      std::memory_order_release,
                                      std::memory_order_relaxed)) {
        version.fetch_add(1, std::memory_order_release);
        return true; // Success!
      }

      // Transaction failed - exponential backoff
      for (int i = 0; i < backoff; i++) {
        _mm_pause(); // CPU hint for spin-wait loop
      }
      backoff = (backoff << 1) & 0xFF; // Cap backoff at 256
      retries++;
    }

    return false; // Failed after max retries
  }

  // Non-blocking pop with exponential backoff
  bool try_pop(Task<K, V>& task, int max_retries = MAX_RETRIES) {
    int retries = 0;
    int backoff = 1;

    while (retries < max_retries) {
      // Read current state (transaction begin)
      uint64_t current_head = head.load(std::memory_order_acquire);
      uint64_t current_tail = tail.load(std::memory_order_acquire);

      // Check if queue is empty
      if (current_head % QUEUE_SIZE == current_tail % QUEUE_SIZE) {
        return false; // Queue empty
      }

      // Optimistically read the task
      size_t index = current_head % QUEUE_SIZE;
      task = tasks[index];

      uint64_t next_head = (current_head + 1) % QUEUE_SIZE;

      // Try to commit the transaction using CAS
      if (head.compare_exchange_weak(current_head, next_head,
                                      std::memory_order_release,
                                      std::memory_order_relaxed)) {
        version.fetch_add(1, std::memory_order_release);
        return true; // Success!
      }

      // Transaction failed - exponential backoff
      for (int i = 0; i < backoff; i++) {
        _mm_pause();
      }
      backoff = (backoff << 1) & 0xFF;
      retries++;
    }

    return false; // Failed after max retries
  }

  // Blocking push with spinning
  void push(const Task<K, V>& task) {
    while (!try_push(task, 100)) {
      // Yield to other threads/processes
      sched_yield();
    }
  }

  // Blocking pop with spinning
  bool pop(Task<K, V>& task) {
    while (!try_pop(task, 100)) {
      sched_yield();
    }
    return true;
  }

  // Get approximate queue size (may be stale)
  size_t size() const {
    uint64_t current_tail = tail.load(std::memory_order_relaxed);
    uint64_t current_head = head.load(std::memory_order_relaxed);

    if (current_tail >= current_head) {
      return (current_tail - current_head) % QUEUE_SIZE;
    } else {
      return QUEUE_SIZE - ((current_head - current_tail) % QUEUE_SIZE);
    }
  }

  bool empty() const {
    return head.load(std::memory_order_acquire) % QUEUE_SIZE ==
           tail.load(std::memory_order_acquire) % QUEUE_SIZE;
  }

  bool full() const {
    uint64_t current_tail = tail.load(std::memory_order_acquire);
    uint64_t current_head = head.load(std::memory_order_acquire);
    return ((current_tail + 1) % QUEUE_SIZE) == (current_head % QUEUE_SIZE);
  }
};