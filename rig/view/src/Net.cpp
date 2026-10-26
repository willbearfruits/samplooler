#include "Net.h"

#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

namespace slrig {

NetClient::NetClient(std::string socketPath) : path_(std::move(socketPath)) {}

NetClient::~NetClient() { stop(); }

double NetClient::now()
{
    using namespace std::chrono;
    return duration<double>(steady_clock::now().time_since_epoch()).count();
}

std::string NetClient::defaultPath()
{
    const char* run = std::getenv("XDG_RUNTIME_DIR");
    std::string dir = run && *run ? run : "/tmp";
    return dir + "/slrig/daemon.sock";
}

void NetClient::start()
{
    if (th_.joinable()) return;
    if (pipe2(wake_, O_NONBLOCK | O_CLOEXEC) != 0) wake_[0] = wake_[1] = -1;
    quit_ = false;
    th_ = std::thread([this] { run(); });
}

void NetClient::stop()
{
    if (!th_.joinable()) return;
    quit_ = true;
    wake();
    th_.join();
    for (int& fd : wake_)
        if (fd >= 0) { close(fd); fd = -1; }
}

void NetClient::wake()
{
    if (wake_[1] >= 0) {
        char c = 1;
        [[maybe_unused]] ssize_t n = write(wake_[1], &c, 1);
    }
}

void NetClient::send(const nlohmann::json& j)
{
    if (!conn_) return;
    std::string line = j.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace) + "\n";
    {
        std::lock_guard<std::mutex> lk(mu_);
        outbox_ += line;
    }
    wake();
}

void NetClient::drain(std::vector<Msg>& out)
{
    std::vector<Msg> got;
    {
        std::lock_guard<std::mutex> lk(mu_);
        got.swap(inbox_);
    }
    if (out.empty()) out.swap(got);
    else for (auto& m : got) out.push_back(std::move(m));
}

void NetClient::post(nlohmann::json j, double rx)
{
    std::lock_guard<std::mutex> lk(mu_);
    if (inbox_.size() > 50000) inbox_.erase(inbox_.begin(), inbox_.begin() + 10000);   // nobody drains: keep the newest
    inbox_.push_back({std::move(j), rx});
}

void NetClient::run()
{
    while (!quit_) {
        int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
        bool ok = false;
        if (fd >= 0 && path_.size() < sizeof(sockaddr_un::sun_path)) {
            sockaddr_un a{};
            a.sun_family = AF_UNIX;
            std::memcpy(a.sun_path, path_.c_str(), path_.size() + 1);
            ok = connect(fd, reinterpret_cast<sockaddr*>(&a), sizeof a) == 0;
        }
        if (!ok) {
            if (fd >= 0) close(fd);
            // try again in a second (a stop() wakes us at once)
            pollfd w{wake_[0], POLLIN, 0};
            poll(&w, wake_[0] >= 0 ? 1 : 0, 1000);
            char buf[64];
            while (wake_[0] >= 0 && read(wake_[0], buf, sizeof buf) > 0) {}
            continue;
        }
        fcntl(fd, F_SETFL, fcntl(fd, F_GETFL) | O_NONBLOCK);
        {
            std::lock_guard<std::mutex> lk(mu_);
            outbox_.clear();
        }
        conn_ = true;
        post(nlohmann::json{{"ev", "_connected"}}, now());

        std::string rx, tx;
        std::vector<nlohmann::json> lines;
        char buf[65536];
        bool alive = true;
        while (alive && !quit_) {
            {
                std::lock_guard<std::mutex> lk(mu_);
                if (!outbox_.empty()) { tx += outbox_; outbox_.clear(); }
            }
            pollfd p[2] = {{fd, static_cast<short>(POLLIN | (tx.empty() ? 0 : POLLOUT)), 0}, {wake_[0], POLLIN, 0}};
            int r = poll(p, wake_[0] >= 0 ? 2 : 1, 200);
            if (r < 0 && errno != EINTR) break;
            if (wake_[0] >= 0 && (p[1].revents & POLLIN))
                while (read(wake_[0], buf, sizeof buf) > 0) {}
            if (p[0].revents & POLLIN) {
                for (;;) {
                    ssize_t n = recv(fd, buf, sizeof buf, 0);
                    if (n > 0) { rx.append(buf, static_cast<size_t>(n)); continue; }
                    if (n == 0) alive = false;
                    else if (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR) alive = false;
                    break;
                }
                double t = now();
                size_t start = 0;
                for (size_t nl; (nl = rx.find('\n', start)) != std::string::npos; start = nl + 1) {
                    if (nl == start) continue;
                    auto j = nlohmann::json::parse(rx.begin() + static_cast<long>(start), rx.begin() + static_cast<long>(nl),
                                                   nullptr, false);
                    if (!j.is_discarded()) lines.push_back(std::move(j));
                }
                rx.erase(0, start);
                if (rx.size() > (16u << 20)) rx.clear();          // a line that never ends
                if (!lines.empty()) {
                    std::lock_guard<std::mutex> lk(mu_);
                    for (auto& j : lines) inbox_.push_back({std::move(j), t});
                    lines.clear();
                }
            } else if (p[0].revents & (POLLHUP | POLLERR | POLLNVAL)) {
                alive = false;
            }
            if (!tx.empty() && alive) {
                ssize_t n = ::send(fd, tx.data(), tx.size(), MSG_NOSIGNAL);
                if (n > 0) tx.erase(0, static_cast<size_t>(n));
                else if (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR) alive = false;
            }
        }
        close(fd);
        conn_ = false;
        post(nlohmann::json{{"ev", "_disconnected"}}, now());
        if (!quit_) {
            pollfd w{wake_[0], POLLIN, 0};
            poll(&w, wake_[0] >= 0 ? 1 : 0, 1000);
        }
    }
}

} // namespace slrig
