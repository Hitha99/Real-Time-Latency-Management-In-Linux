// input_latency.cpp
// Measure latency between evdev event timestamp and user-space receipt time.
//
// Build:   g++ -O2 -std=c++17 -Wall -Wextra -o input_latency input_latency.cpp
// Usage:   sudo ./input_latency /dev/input/eventX [--limit N] [--quiet]
// Example: sudo ./input_latency /dev/input/event3 --limit 500
//
// Notes:
// - By default tries to switch event timestamps to CLOCK_MONOTONIC via EVIOCSCLOCKID.
// - Prints rolling stats; Ctrl-C prints a final summary.
// - Works for EV_KEY (keyboard/mouse buttons), EV_ABS (touch), EV_REL, etc.

#include <algorithm>
#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdint>
#include <cstring>
#include <fcntl.h>
#include <iomanip>
#include <iostream>
#include <linux/input.h>
#include <poll.h>
#include <string>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>
#include <vector>

static std::atomic<bool> g_stop(false);

static void on_sigint(int) { g_stop = true; }

static inline uint64_t ts_to_ns(const timespec& ts) {
    return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}

static inline uint64_t tv_to_ns(const timeval& tv) {
    return (uint64_t)tv.tv_sec * 1000000000ull + (uint64_t)tv.tv_usec * 1000ull;
}

static double percentile(std::vector<double>& v, double p) {
    if (v.empty()) return 0.0;
    if (p <= 0.0) return v.front();
    if (p >= 100.0) return v.back();
    double idx = (p / 100.0) * (v.size() - 1);
    size_t i = (size_t)idx;
    double frac = idx - i;
    if (i + 1 < v.size()) return v[i] * (1.0 - frac) + v[i + 1] * frac;
    return v[i];
}

static std::string ev_type_to_str(uint16_t type) {
    switch (type) {
        case EV_SYN: return "SYN";
        case EV_KEY: return "KEY";
        case EV_REL: return "REL";
        case EV_ABS: return "ABS";
        case EV_MSC: return "MSC";
        case EV_SW:  return "SW";
        case EV_LED: return "LED";
        case EV_SND: return "SND";
        case EV_REP: return "REP";
        case EV_FF:  return "FF";
        case EV_PWR: return "PWR";
        case EV_FF_STATUS: return "FF_STATUS";
        default: return "UNK";
    }
}

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "Usage: sudo " << argv[0] << " /dev/input/eventX [--limit N] [--quiet]\n";
        return 1;
    }

    std::string dev = argv[1];
    int limit = 0;           // 0 = unlimited
    bool quiet = false;

    for (int i = 2; i < argc; ++i) {
        if (std::string(argv[i]) == "--limit" && i + 1 < argc) {
            limit = std::stoi(argv[++i]);
        } else if (std::string(argv[i]) == "--quiet") {
            quiet = true;
        } else {
            std::cerr << "Unknown arg: " << argv[i] << "\n";
            return 1;
        }
    }

    // Open device
    int fd = open(dev.c_str(), O_RDONLY | O_NONBLOCK);
    if (fd < 0) {
        std::perror("open");
        return 1;
    }

    // Try to set event timestamp clock to CLOCK_MONOTONIC for consistent deltas.
    // (Some devices default to REALTIME; this keeps now() comparable to event time.)
    int clkid = CLOCK_MONOTONIC;
#ifdef EVIOCSCLOCKID
    if (ioctl(fd, EVIOCSCLOCKID, &clkid) < 0) {
        if (!quiet) std::cerr << "[warn] EVIOCSCLOCKID failed; continuing with device default clock.\n";
    }
#else
    if (!quiet) std::cerr << "[info] EVIOCSCLOCKID not available on this system.\n";
#endif

    // Get device name
    char name[256] = {0};
    if (ioctl(fd, EVIOCGNAME(sizeof(name)), name) < 0) {
        std::strncpy(name, "unknown", sizeof(name)-1);
    }

    if (!quiet) {
        std::cout << "Device: " << dev << "  (\"" << name << "\")\n";
        std::cout << "Collecting input events… Press Ctrl-C to stop.\n";
    }

    // Install Ctrl-C handler
    std::signal(SIGINT, on_sigint);

    std::vector<double> latencies_us;
    latencies_us.reserve(limit > 0 ? limit : 4096);

    // Poll loop
    pollfd pfd{};
    pfd.fd = fd;
    pfd.events = POLLIN;

    // Read buffer (multiple events may be read at once)
    constexpr size_t BUF_EV = 64;
    input_event evbuf[BUF_EV];

    size_t total_events = 0;
    size_t measured_events = 0;

    // For rolling stats
    auto print_stats = [&](bool final_summary) {
        if (latencies_us.empty()) return;
        std::vector<double> v = latencies_us;
        std::sort(v.begin(), v.end());
        double avg = 0.0;
        for (double x : v) avg += x;
        avg /= v.size();
        double p50 = percentile(v, 50.0);
        double p95 = percentile(v, 95.0);
        double p99 = percentile(v, 99.0);
        if (final_summary) std::cout << "\n";
        std::cout << (final_summary ? "=== Final " : "=== Rolling ")
                  << "Latency Stats (usec) over " << v.size() << " events ===\n"
                  << "avg: " << std::fixed << std::setprecision(2) << avg
                  << "   p50: " << p50
                  << "   p95: " << p95
                  << "   p99: " << p99 << "\n";
    };

    while (!g_stop && (limit == 0 || (int)latencies_us.size() < limit)) {
        int rv = poll(&pfd, 1, 5000); // 5s timeout
        if (rv < 0) {
            if (errno == EINTR) continue;
            std::perror("poll");
            break;
        } else if (rv == 0) {
            if (!quiet) std::cout << "(idle…)\n";
            continue;
        }

        if (pfd.revents & POLLIN) {
            ssize_t n = read(fd, evbuf, sizeof(evbuf));
            if (n < 0) {
                if (errno == EAGAIN || errno == EINTR) continue;
                std::perror("read");
                break;
            }
            size_t cnt = (size_t)n / sizeof(input_event);
            for (size_t i = 0; i < cnt; ++i) {
                const input_event& ev = evbuf[i];
                ++total_events;

                // We only compute latency for meaningful events (skip SYN_REPORT unless desired)
                bool measure = (ev.type == EV_KEY || ev.type == EV_ABS || ev.type == EV_REL || ev.type == EV_MSC);
                if (!measure) continue;

                // 'ev.time' is when the kernel timestamped the event (device->kernel boundary).
                // 'now' is when user-space received it (kernel->user boundary).
                timespec now_ts{};
                clock_gettime(CLOCK_MONOTONIC, &now_ts);
                uint64_t now_ns = ts_to_ns(now_ts);
                uint64_t evt_ns = tv_to_ns(ev.time);

                // Guard against clock mismatch; skip negative deltas
                if (now_ns >= evt_ns) {
                    double delta_us = (now_ns - evt_ns) / 1000.0;
                    latencies_us.push_back(delta_us);
                    ++measured_events;

                    if (!quiet) {
                        std::cout << "[" << ev_type_to_str(ev.type) << "] code=" << ev.code
                                  << " val=" << ev.value
                                  << "  latency=" << std::fixed << std::setprecision(2)
                                  << delta_us << " us\n";
                    }

                    // Print rolling summary every 50 events
                    if (!quiet && (measured_events % 50 == 0)) {
                        print_stats(false);
                    }
                }
            }
        }
    }

    close(fd);

    if (!latencies_us.empty()) {
        print_stats(true);
    } else {
        std::cout << "No measurable input events captured. Try another device (e.g., a keyboard/touchscreen) or remove --quiet.\n";
    }

    std::cout << "Total events seen: " << total_events
              << " | Latencies measured: " << measured_events << "\n";
    return 0;
}
