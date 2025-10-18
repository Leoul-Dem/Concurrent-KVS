#include <vector>
#include <list>
#include <mutex>
#include <thread>
#include <functional> // For std::hash

template<typename K, typename V>
class ConcurrentHashMap {
private:
    // A bucket is a linked list of key-value pairs.
    using Bucket = std::list<std::pair<const K, V>>;

    // The main table is a vector of buckets.
    std::vector<Bucket> table;

    // A vector of mutexes for lock striping.
    // Each mutex protects a "stripe" or region of the table.
    // 'mutable' allows us to lock the mutex in const methods like find().
    mutable std::vector<std::mutex> locks;

    // The hash function for keys.
    std::hash<K> hasher;

    // Helper function to get the stripe index for a given key.
    size_t get_stripe_index(const K& key) const {
        // Use the key's hash to determine which lock to use.
        return hasher(key) % locks.size();
    }

public:
    // Constructor
    explicit ConcurrentHashMap(size_t stripe_count = std::thread::hardware_concurrency())
        : table(stripe_count * 10), // Example: 10 buckets per stripe
          locks(stripe_count) {}

    // --- Public Methods ---

    /**
     * @brief Inserts a key-value pair if the key does not already exist.
     * @return True if the insertion was successful, false if the key already exists.
     */
    bool insert(const K& key, const V& value) {
        size_t stripe_index = get_stripe_index(key);
        std::lock_guard<std::mutex> guard(locks[stripe_index]); // Lock the stripe

        // Get the specific bucket within the locked stripe
        size_t bucket_index = hasher(key) % table.size();
        auto& bucket = table[bucket_index];

        // Check if key already exists in the bucket's list
        for (const auto& pair : bucket) {
            if (pair.first == key) {
                return false; // Key already exists, do not insert
            }
        }

        // Key not found, insert the new pair
        bucket.emplace_back(key, value);
        return true;
    }

    /**
     * @brief Inserts a key-value pair or updates the value if the key already exists.
     */
    void insert_or_assign(const K& key, const V& value) {
        size_t stripe_index = get_stripe_index(key);
        std::lock_guard<std::mutex> guard(locks[stripe_index]); // Lock the stripe

        size_t bucket_index = hasher(key) % table.size();
        auto& bucket = table[bucket_index];

        // Find the key in the list
        for (auto& pair : bucket) {
            if (pair.first == key) {
                pair.second = value; // Key found, update value
                return;
            }
        }

        // Key not found, insert new pair
        bucket.emplace_back(key, value);
    }

    /**
     * @brief Finds a value by its key.
     * @param key The key to search for.
     * @param value A reference to store the found value.
     * @return True if the key was found, false otherwise.
     */
    bool find(const K& key, V& value) const {
        size_t stripe_index = get_stripe_index(key);
        std::lock_guard<std::mutex> guard(locks[stripe_index]); // Lock the stripe

        size_t bucket_index = hasher(key) % table.size();
        const auto& bucket = table[bucket_index];

        for (const auto& pair : bucket) {
            if (pair.first == key) {
                value = pair.second; // Key found
                return true;
            }
        }
        return false; // Key not found
    }

    /**
     * @brief Removes a key-value pair from the map.
     * @return True if a pair was removed, false otherwise.
     */
    bool erase(const K& key) {
        size_t stripe_index = get_stripe_index(key);
        std::lock_guard<std::mutex> guard(locks[stripe_index]); // Lock the stripe

        size_t bucket_index = hasher(key) % table.size();
        auto& bucket = table[bucket_index];

        // Find and remove the element with the matching key
        for (auto it = bucket.begin(); it != bucket.end(); ++it) {
            if (it->first == key) {
                bucket.erase(it);
                return true;
            }
        }
        return false;
    }

    /**
     * @brief Returns the total number of elements in the map.
     * @warning This is a heavyweight operation that locks the entire map.
     * Avoid calling it in performance-critical code.
     */
    size_t size() const {
        // Lock all mutexes to prevent concurrent modifications
        std::vector<std::unique_lock<std::mutex>> all_locks;
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
