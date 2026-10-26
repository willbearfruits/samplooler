// The daemon connection: a reader thread on the unix socket, JSON lines both ways (rig/PROTOCOL.md section 2).
// The UI thread never waits on it: it queues lines with send() and takes what arrived with drain().
#pragma once
#include <nlohmann/json.hpp>

#include <atomic>
#include <chrono>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace slrig {

class NetClient {
public:
    struct Msg {
        nlohmann::json j;
        double rx = 0;          // seconds on the steady clock (see now())
    };

    explicit NetClient(std::string socketPath);
    ~NetClient();
    NetClient(const NetClient&) = delete;
    NetClient& operator=(const NetClient&) = delete;

    void start();
    void stop();
    void send(const nlohmann::json& j);            // dropped while not connected
    void drain(std::vector<Msg>& out);             // appends everything that arrived
    bool connected() const { return conn_.load(); }
    const std::string& path() const { return path_; }

    static double now();                           // steady clock, seconds
    static std::string defaultPath();

private:
    void run();
    void wake();
    void post(nlohmann::json j, double rx);

    std::string path_;
    std::thread th_;
    std::atomic<bool> quit_{false}, conn_{false};
    std::mutex mu_;
    std::vector<Msg> inbox_;
    std::string outbox_;
    int wake_[2] = {-1, -1};
};

} // namespace slrig
