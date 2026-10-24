#ifndef __DYNARRAY_H
#define __DYNARRAY_H
#include <limits>
#include <stdexcept>
#include <vector>

// Indexed game storage retains its original block-growth policy. Typed
// ownership handles copying, exceptions and nontrivial element lifetimes.
// The old memcpy hint is redundant with the standard container.
template<class T> class CDynArray {
public:
    explicit CDynArray(int = 0, long grow_by = 128) { SetGrowBy(grow_by); }
    T& operator[](int index) {
        if (index < 0) throw std::out_of_range("negative game array index");
        const auto required = static_cast<std::size_t>(index)+1;
        if (required > items_.size()) {
            const auto extent = ((required+grow_by_-1)/grow_by_)*grow_by_;
            if (extent > static_cast<std::size_t>(std::numeric_limits<int>::max()) || extent > items_.max_size())
                throw std::length_error("game array exceeds index range");
            items_.resize(extent);
        }
        return items_[static_cast<std::size_t>(index)];
    }
    void Flush() { std::vector<T>().swap(items_); }
    long ArraySize() const { return static_cast<long>(items_.size()); }
    void SetMemCopyable(int) {}
    void SetGrowBy(long value) {
        if (value <= 0 || value > std::numeric_limits<int>::max()) throw std::invalid_argument("invalid game array growth");
        grow_by_ = static_cast<std::size_t>(value);
    }
private:
    std::vector<T> items_;
    std::size_t grow_by_;
};
#endif
