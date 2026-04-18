#pragma once

#include <map>
#include <unordered_map>
#include <utility>

namespace hgps {

template <template <class...> class TMap, class TRow, class TCol, class TCell>
class Map2d {
  public:
    using TableType = TMap<TRow, TMap<TCol, TCell>>;
    using RowType = TMap<TCol, TCell>;
    using IteratorType = typename TableType::iterator;
    using ConstIteratorType = typename TableType::const_iterator;

    Map2d() = default;

    explicit Map2d(TableType &&data) noexcept : table_{std::move(data)} {}

    IteratorType begin() noexcept { return table_.begin(); }
    IteratorType end() noexcept { return table_.end(); }

    ConstIteratorType begin() const noexcept { return table_.cbegin(); }
    ConstIteratorType end() const noexcept { return table_.cend(); }
    ConstIteratorType cbegin() const noexcept { return table_.cbegin(); }
    ConstIteratorType cend() const noexcept { return table_.cend(); }

    bool empty() const noexcept { return table_.empty(); }

    bool empty(const TRow &row_key) const { return table_.at(row_key).empty(); }

    std::size_t rows() const noexcept { return table_.size(); }

    std::size_t columns(const TRow &row_key) const { return table_.at(row_key).size(); }

    bool contains(const TRow &row_key) const noexcept { return table_.contains(row_key); }

    bool contains(const TRow &row_key, const TCol &col_key) const {
        if (!table_.contains(row_key)) {
            return false;
        }
        return table_.at(row_key).contains(col_key);
    }

    RowType &at(const TRow &row_key) { return table_.at(row_key); }

    const RowType &at(const TRow &row_key) const { return table_.at(row_key); }

    TCell &at(const TRow &row_key, const TCol &col_key) { return table_.at(row_key).at(col_key); }

    const TCell &at(const TRow &row_key, const TCol &col_key) const {
        return table_.at(row_key).at(col_key);
    }

    template <class... FArgs>
    auto insert_row(FArgs &&...args) noexcept {
        return table_.insert(std::forward<FArgs>(args)...);
    }

    template <class FRow, class... FArgs>
    auto insert(FRow &&row_key, FArgs &&...args) noexcept {
        auto [it, inserted] = table_.try_emplace(std::forward<FRow>(row_key), RowType{});
        return it->second.insert(std::forward<FArgs>(args)...);
    }

    template <class... FArgs>
    auto emplace_row(FArgs &&...args) noexcept {
        return table_.emplace(std::forward<FArgs>(args)...);
    }

    template <class FRow, class... FArgs>
    auto emplace(FRow &&row_key, FArgs &&...args) noexcept {
        auto [it, inserted] = table_.try_emplace(std::forward<FRow>(row_key), RowType{});
        return it->second.emplace(std::forward<FArgs>(args)...);
    }

  private:
    TableType table_;
};

template <class TRow, class TCol, class TCell>
using UnorderedMap2d = Map2d<std::unordered_map, TRow, TCol, TCell>;

template <class TRow, class TCol, class TCell>
using OrderedMap2d = Map2d<std::map, TRow, TCol, TCell>;

} // namespace hgps