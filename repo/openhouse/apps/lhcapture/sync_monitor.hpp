// Live LH1 sync-flash layout check for lhcapture --check / --scenario. Independent of
// the disambiguator: groups the sync-width pulses of each index event and reports where
// in the group each station flashes, its skip=0 cadence, and the OOTX data bits, so a
// firmware mode (stock A/b/c, multi N = 2..4, flash policy) can be verified before and
// while recording.
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <deque>
#include <map>
#include <mutex>
#include <string>
#include <vector>

#include "lh/types.hpp"

namespace lhcap {

class SyncMonitor {
 public:
  void OnPulse(const std::string& device, const lh::LightPulse& p) {
    std::lock_guard lock(mutex_);
    auto& d = devices_[device];
    if (p.width < 56e-6) {  // sweep hit: credited to the group it follows
      if (d.open && p.time - d.groupStart < 8.3e-3) ++d.hits;
      return;
    }
    if (p.width > 140e-6) return;  // sync widths 62.5..135.4 us
    if (d.open && p.time - d.groupStart < 1.2e-3) {
      // Several sensors see one flash: keep one per offset (10 us), widest-agreeing.
      const double off = p.time - d.groupStart;
      for (auto& f : d.current) if (std::abs(f.offset - off) < 15e-6) { f.width = std::max(f.width, p.width); return; }
      d.current.push_back({off, p.width});
      return;
    }
    if (d.open) Close(d);
    d.open = true; d.groupStart = p.time; d.current = {{0, p.width}};
  }

  // One line per device; called once a second.
  std::string Report() {
    std::lock_guard lock(mutex_);
    std::string out;
    for (auto& [name, d] : devices_) {
      while (!d.groups.empty() && d.groups.back().time - d.groups.front().time > kWindow) d.groups.pop_front();
      if (d.groups.size() < 20) { out += "  " + name + ": no sync flashes\n"; continue; }
      out += "  " + name + ": " + Describe(d) + "\n";
    }
    return out;
  }

 private:
  // Was 4.0, then 1.5. Long enough for the cadence/variance maths (>=20 groups), short
  // enough that a station being blocked shows up within a second.
  static constexpr double kWindow = 0.8;
  struct Flash { double offset, width; };
  struct Group { double time; std::vector<Flash> flashes; int hits; };
  struct Device {
    bool open = false;
    double groupStart = 0;
    std::vector<Flash> current;
    int hits = 0;
    std::deque<Group> groups;
  };
  // Width class -> (skip, data, axis); 62.5 us + 10.42 us * (skip*4 + data*2 + axis).
  static int WidthClass(double w) { return std::clamp(int(std::lround((w - 62.5e-6) / 10.4166667e-6)), 0, 7); }

  void Close(Device& d) {
    d.groups.push_back({d.groupStart, d.current, d.hits});
    d.hits = 0;
  }

  static std::string Describe(const Device& d) {
    const auto& g = d.groups;
    const double span = g.back().time - g.front().time;
    const double rate = (g.size() - 1) / std::max(span, 1e-6);
    // Cluster offsets in 125 us bins (stations are 250 or 417 us apart).
    struct Cluster { double sum = 0; int n = 0, skip0 = 0; long hits = 0; };
    std::map<int, Cluster> clusters;
    std::map<size_t, int> perGroup;
    for (const auto& gr : g) {
      ++perGroup[gr.flashes.size()];
      std::vector<int> seen;  // a reflection can put a second flash in a bin
      for (const auto& f : gr.flashes) {
        const int bin = int(std::lround(f.offset / 125e-6));
        if (std::find(seen.begin(), seen.end(), bin) != seen.end()) continue;
        seen.push_back(bin);
        auto& c = clusters[bin];
        c.sum += f.offset; ++c.n;
        const int k = WidthClass(f.width);
        if (!(k & 4)) { ++c.skip0; c.hits += gr.hits; }
      }
    }
    char buf[256];
    std::snprintf(buf, sizeof buf, "%.0f groups/s, flashes/group", rate);
    std::string s = buf;
    for (auto [n, c] : perGroup) { std::snprintf(buf, sizeof buf, " %zu:%d%%", n, int(100.0 * c / g.size())); s += buf; }
    s += " |";
    for (const auto& [bin, c] : clusters) {
      if (c.n < int(g.size()) / 20) continue;  // stray reflections
      // Share of index events after which this station sweeps: 1/2 stock, 1/N multi.
      const double share = double(c.skip0) / g.size();
      std::string cadence = "-";
      if (share > 0.02) { std::snprintf(buf, sizeof buf, "%.0f%% (1/%.1f)", 100 * share, 1 / share); cadence = buf; }
      std::snprintf(buf, sizeof buf, " @%3.0fus seen %3.0f%% sweeps after %s, %.0f hits/s", 1e6 * c.sum / c.n,
                    100.0 * c.n / g.size(), cadence.c_str(), c.hits / std::max(span, 1e-6));
      s += buf;
      s += " |";
    }
    s += " => " + Verdict(clusters, g.size());
    return s;
  }

  template <class Clusters>
  static std::string Verdict(const Clusters& clusters, size_t groups) {
    std::vector<double> offs;
    for (const auto& [bin, c] : clusters) if (c.n >= int(groups) / 20) offs.push_back(1e6 * c.sum / c.n);
    if (offs.empty()) return "?";
    if (offs.size() == 1) return "one station";
    bool stock = false, multi = false;
    for (double o : offs) {
      if (o < 30) continue;
      if (std::abs(o - 417) < 40) stock = true;
      else if (std::abs(std::fmod(o + 125, 250) - 125) < 40) multi = true;
    }
    if (stock && !multi) return "stock 2-station (417 us)";
    if (multi && !stock) {
      std::string slots;
      for (double o : offs) slots += " " + std::to_string(int(std::lround(o / 250)));
      return "multi stagger, slots relative to first:" + slots;
    }
    return "MIXED / unexpected offsets";
  }

  std::mutex mutex_;
  std::map<std::string, Device> devices_;
};

}  // namespace lhcap
