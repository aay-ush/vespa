// Copyright Verizon Media. Licensed under the terms of the Apache 2.0 license. See LICENSE in the project root.

#pragma once

#include "label.h"
#include "memory_usage_stuff.h"
#include <vespa/vespalib/util/arrayref.h>
#include <vespa/vespalib/stllike/identity.h>
#include <vespa/vespalib/stllike/hashtable.h>
#include <vespa/vespalib/util/shared_string_repo.h>
#include <vector>

namespace vespalib::eval {

/**
 * A wrapper around vespalib::hashtable, using it to map a list of
 * labels (a sparse address) to an integer value (dense subspace
 * index). Labels are represented by string enum values stored and
 * handled outside this class.
 **/
class FastAddrMap
{
public:
    // label hasing functions
    static constexpr uint32_t hash_label(label_t label) { return label; }
    static constexpr uint32_t hash_label(const label_t *label) { return *label; }
    static constexpr uint32_t combine_label_hash(uint32_t full_hash, uint32_t next_hash) {
        return ((full_hash * 31) + next_hash);
    }
    template <typename T>
    static constexpr uint32_t hash_labels(ConstArrayRef<T> addr) {
        uint32_t hash = 0;
        for (const T &label: addr) {
            hash = combine_label_hash(hash, hash_label(label));
        }
        return hash;
    }

    // typed uint32_t index used to identify sparse address/dense subspace
    struct Tag {
        uint32_t idx;
        static constexpr uint32_t npos() { return uint32_t(-1); }
        static constexpr Tag make_invalid() { return Tag{npos()}; }
        constexpr bool valid() const { return (idx != npos()); }
    };

    // sparse hash set entry
    struct Entry {
        Tag tag;
        uint32_t hash;
    };

    // alternative key(s) used for lookup in sparse hash set
    template <typename T> struct AltKey {
        ConstArrayRef<T> key;
        uint32_t hash;
    };

    // view able to convert tags into sparse addresses
    struct LabelView {
        size_t addr_size;
        const std::vector<label_t> &labels;
        LabelView(size_t num_mapped_dims, SharedStringRepo::HandleView handle_view)
            : addr_size(num_mapped_dims), labels(handle_view.handles()) {}
        ConstArrayRef<label_t> get_addr(size_t idx) const {
            return {&labels[idx * addr_size], addr_size};
        }
    };

    // hashing functor for sparse hash set
    struct Hash {
        template <typename T>
        constexpr uint32_t operator()(const AltKey<T> &key) const { return key.hash; }
        constexpr uint32_t operator()(const Entry &entry) const { return entry.hash; }
    };

    // equality functor for sparse hash set
    struct Equal {
        const LabelView &label_view;
        Equal(const LabelView &label_view_in) : label_view(label_view_in) {}
        static constexpr bool eq_labels(label_t a, label_t b) { return (a == b); }
        static constexpr bool eq_labels(label_t a, const label_t *b) { return (a == *b); }
        template <typename T>
        bool operator()(const Entry &a, const AltKey<T> &b) const {
            if ((a.hash != b.hash) || (b.key.size() != label_view.addr_size)) {
                return false;
            }
            auto a_key = label_view.get_addr(a.tag.idx);
            for (size_t i = 0; i < a_key.size(); ++i) {
                if (!eq_labels(a_key[i], b.key[i])) {
                    return false;
                }
            }
            return true;
        }
    };

    using HashType = hashtable<Entry, Entry, Hash, Equal, Identity, hashtable_base::and_modulator>;

private:
    LabelView _labels;
    HashType _map;

public:
    FastAddrMap(size_t num_mapped_dims, SharedStringRepo::HandleView handle_view, size_t expected_subspaces)
        : _labels(num_mapped_dims, handle_view),
          _map(expected_subspaces * 2, Hash(), Equal(_labels)) {}
    ~FastAddrMap();
    FastAddrMap(const FastAddrMap &) = delete;
    FastAddrMap &operator=(const FastAddrMap &) = delete;
    FastAddrMap(FastAddrMap &&) = delete;
    FastAddrMap &operator=(FastAddrMap &&) = delete;
    static constexpr size_t npos() { return -1; }
    ConstArrayRef<label_t> get_addr(size_t idx) const { return _labels.get_addr(idx); }
    size_t size() const { return _map.size(); }
    constexpr size_t addr_size() const { return _labels.addr_size; }
    template <typename T>
    size_t lookup(ConstArrayRef<T> addr, uint32_t hash) const {
        AltKey<T> key{addr, hash};
        auto pos = _map.find(key);
        return (pos == _map.end()) ? npos() : pos->tag.idx;
    }
    template <typename T>
    size_t lookup(ConstArrayRef<T> addr) const {
        return lookup(addr, hash_labels(addr));
    }
    void add_mapping(uint32_t hash) {
        uint32_t idx = _map.size();
        _map.force_insert(Entry{{idx}, hash});
    }
    template <typename F>
    void each_map_entry(F &&f) const {
        _map.for_each([&](const auto &entry)
                      {
                          f(entry.tag.idx, entry.hash);
                      });
    }
    MemoryUsage estimate_extra_memory_usage() const {
        MemoryUsage extra_usage;
        size_t map_self_size = sizeof(_map);
        size_t map_used = _map.getMemoryUsed();
        size_t map_allocated = _map.getMemoryConsumption();
        // avoid double-counting the map itself
        map_used = std::min(map_used, map_used - map_self_size);
        map_allocated = std::min(map_allocated, map_allocated - map_self_size);
        extra_usage.incUsedBytes(map_used);
        extra_usage.incAllocatedBytes(map_allocated);
        return extra_usage;
    }
};

}
