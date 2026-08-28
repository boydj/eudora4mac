// In-memory Transport for protocol tests: scripted request/response pairs
// with exact wire-byte assertions.

#pragma once

#include <cstdio>
#include <string>
#include <vector>

#include "net/transport.hpp"

namespace eutest {

class ScriptedTransport : public eudora::Transport {
public:
    struct Exchange {
        std::string expect_send; // exact bytes the client must send ("" for
                                 // server-speaks-first steps)
        std::string reply;       // bytes the server then delivers
    };

    explicit ScriptedTransport(std::vector<Exchange> script)
        : script_(std::move(script)) {}

    bool all_consumed() const {
        return step_ >= script_.size() && inbox_pos_ >= inbox_.size();
    }
    const std::vector<std::string> &failures() const { return failures_; }

    eudora::NetError connect(const std::string &, std::uint16_t, long) override {
        connected_ = true;
        advance_server_first();
        return eudora::NetError::None;
    }

    eudora::NetError send(std::string_view data) override {
        pending_send_ += data;
        // Match as many script steps as the sent bytes cover.
        while (step_ < script_.size() && !script_[step_].expect_send.empty() &&
               pending_send_.size() >= script_[step_].expect_send.size()) {
            const std::string &want = script_[step_].expect_send;
            if (pending_send_.compare(0, want.size(), want) != 0) {
                failures_.push_back("send mismatch at step " + std::to_string(step_) +
                                    ": got [" + pending_send_ + "] want [" + want + "]");
                pending_send_.clear();
                return eudora::NetError::IoError;
            }
            pending_send_.erase(0, want.size());
            inbox_ += script_[step_].reply;
            ++step_;
            advance_server_first();
        }
        return eudora::NetError::None;
    }

    long recv(char *buffer, long max) override {
        if (inbox_pos_ >= inbox_.size()) {
            last_ = eudora::NetError::Closed;
            return 0;
        }
        const long n = static_cast<long>(
            std::min<std::size_t>(static_cast<std::size_t>(max),
                                  inbox_.size() - inbox_pos_));
        std::copy_n(inbox_.data() + inbox_pos_, n, buffer);
        inbox_pos_ += static_cast<std::size_t>(n);
        return n;
    }

    eudora::NetError disconnect() override {
        connected_ = false;
        return eudora::NetError::None;
    }
    eudora::NetError last_error() const override { return last_; }
    std::string local_host_name() override { return "client.example.com"; }
    void flush_input(long) override { inbox_pos_ = inbox_.size(); }

private:
    void advance_server_first() {
        while (step_ < script_.size() && script_[step_].expect_send.empty()) {
            inbox_ += script_[step_].reply;
            ++step_;
        }
    }

    std::vector<Exchange> script_;
    std::size_t step_ = 0;
    std::string pending_send_;
    std::string inbox_;
    std::size_t inbox_pos_ = 0;
    std::vector<std::string> failures_;
    bool connected_ = false;
    eudora::NetError last_ = eudora::NetError::None;
};

} // namespace eutest
