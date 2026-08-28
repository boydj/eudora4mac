#include "mailstore/summary.hpp"

namespace eudora {

void TableOfContents::append(MessageSummary sum) {
    sum.serial_num = next_serial_num++;
    sums.push_back(std::move(sum));
}

bool TableOfContents::remove(int index) {
    if (index < 0 || index >= count())
        return false;
    if (sums[static_cast<std::size_t>(index)].state == MessageState::BusySending)
        return false; // DeleteSum refuses while a send is in flight
    sums.erase(sums.begin() + index);
    return true;
}

int TableOfContents::find_by_hash(std::uint32_t uid_hash) const {
    if (!valid_hash(uid_hash))
        return -1;
    for (int i = 0; i < count(); ++i)
        if (sums[static_cast<std::size_t>(i)].uid_hash == uid_hash)
            return i;
    return -1;
}

int TableOfContents::find_by_serial(std::int32_t serial_num) const {
    for (int i = 0; i < count(); ++i)
        if (sums[static_cast<std::size_t>(i)].serial_num == serial_num)
            return i;
    return -1;
}

void TableOfContents::recalc_kbytes(std::int64_t mailbox_size) {
    std::int64_t used = 0;
    for (const auto &s : sums)
        if (s.offset >= 0) // IMAP placeholders have offset < 0
            used += s.length;
    used_k = static_cast<std::uint32_t>((used + 1023) / 1024);
    total_k = static_cast<std::uint32_t>((mailbox_size + 1023) / 1024);
}

std::int32_t TableOfContents::append_spot() const {
    std::int32_t end = 0;
    for (const auto &s : sums)
        if (s.offset > -1 && end < s.offset + s.length)
            end = s.offset + s.length;
    return end;
}

} // namespace eudora
