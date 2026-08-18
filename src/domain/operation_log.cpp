#include "domain/operation_log.h"

#include <utility>

namespace gitman {
    operation_log_buffer::operation_log_buffer(const std::size_t capacity) noexcept
        : capacity_ { capacity > 0 ? capacity : 1 }
    {}

    void operation_log_buffer::append(operation_log_entry entry)
    {
        operation_log_record record {};
        record.sequence = ++total_appended_;
        record.entry = std::move(entry);
        records_.push_back(std::move(record));

        while (records_.size() > capacity_)
        {
            records_.pop_front();
            ++dropped_;
        }
    }

    void operation_log_buffer::clear() noexcept
    {
        records_.clear();
    }

    const std::deque<operation_log_record>& operation_log_buffer::records() const noexcept
    {
        return records_;
    }

    std::uint64_t operation_log_buffer::total_appended() const noexcept
    {
        return total_appended_;
    }

    std::uint64_t operation_log_buffer::dropped_count() const noexcept
    {
        return dropped_;
    }

    std::u8string_view log_entry_kind_name(const log_entry_kind kind) noexcept
    {
        switch (kind)
        {
        case log_entry_kind::lifecycle:
            return u8"lifecycle";
        case log_entry_kind::standard_output:
            return u8"stdout";
        case log_entry_kind::standard_error:
            return u8"stderr";
        }
        return u8"unknown";
    }
} // namespace gitman
