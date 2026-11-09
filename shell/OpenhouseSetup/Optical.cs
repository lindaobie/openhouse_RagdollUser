using System.Collections.Concurrent;
using System.Diagnostics;
using System.Text;
using System.Text.Json.Nodes;
using System.Text.RegularExpressions;

namespace OpenhouseSetup;

/// Raw optical capture: what the stations actually flash, read straight off the receiver.
/// It needs SteamVR out of the way (the receiver is exclusive) and does not use the driver
/// at all, so it still works when the room will not converge.
internal static class Optical
{
    private static readonly Regex Line = new(
        @"^(?<dev>LHR-[0-9A-F]+):\s*(?<groups>\d+)\s*groups/s,\s*flashes/group\s*(?<flashes>[^|]+)\|(?<slots>.*)$",
        RegexOptions.Compiled);

    private static readonly Regex Slot = new(
        @"@\s*(?<us>\d+)us\s*seen\s*(?<seen>\d+)%\s*sweeps after\s*(?<sweeps>\d+)%\s*\((?<ratio>[^)]*)\),\s*(?<hits>\d+)\s*hits/s",
        RegexOptions.Compiled);

    private static volatile bool _stop;
    public static bool Running { get; private set; }

    public static void Stop() => _stop = true;

    public static string? ExePath()
    {
        var dir = new DirectoryInfo(AppContext.BaseDirectory);
        for (int i = 0; i < 6 && dir is not null; i++, dir = dir.Parent)
        {
            var p = Path.Combine(dir.FullName, "packages", "driver", "openhouse-windows-x64", "apps", "lhcapture.exe");
            if (File.Exists(p)) return p;
        }
        return null;
    }

    /// Keeps capturing until stopped or maxSeconds elapse, posting a fresh picture as it goes.
    public static void Run(int maxSeconds, Action<string, string> emit, Action<string> post)
    {
        var exe = ExePath();
        if (exe is null) { emit("找不到 lhcapture.exe", "warn"); return; }

        var running = Driver.Running();
        if (running.Count > 0)
        {
            emit($"接收器同一时间只能被一个程序占用，所以先把 SteamVR 退掉：{string.Join("、", running)}", "warn");
            Driver.StopAll(emit);
            emit("SteamVR 已退出。检查完你可以重新打开它。", "ok");
        }

        var state = new ConcurrentDictionary<string, JsonObject>();
        var gate = new object();
        var lastPost = DateTime.MinValue;

        var info = new ProcessStartInfo(exe, "--check")
        {
            RedirectStandardOutput = true,
            RedirectStandardError = true,
            UseShellExecute = false,
            CreateNoWindow = true,
            WorkingDirectory = Path.GetDirectoryName(exe)!,
            StandardOutputEncoding = Encoding.UTF8,
        };

        _stop = false;
        Running = true;
        emit("开始采集。数据每 5 秒刷新一次，直到你点停止或者满 10 分钟。", "ok");

        using var proc = new Process { StartInfo = info };
        var noise = 0;
        proc.OutputDataReceived += (_, e) =>
        {
            if (e.Data is null) return;
            var line = e.Data.Trim();
            var m = Line.Match(line);
            if (!m.Success)
            {
                // Anything else lhcapture says is still worth seeing: it is where "no receiver"
                // and similar complaints show up.
                if (line.Length > 0 && line != "---" && noise < 12)
                {
                    noise++;
                    emit("lhcapture: " + line, "");
                }
                return;
            }

            state[m.Groups["dev"].Value] = Entry(m, line);
            lock (gate)
            {
                // lhcapture prints a report every five seconds; posting on every line would
                // push dozens of identical frames a second at the page.
                if ((DateTime.UtcNow - lastPost).TotalMilliseconds < 900) return;
                lastPost = DateTime.UtcNow;
                post(Payload(state));
            }
        };
        proc.ErrorDataReceived += (_, e) =>
        {
            // Redirected but unread stderr both hides the reason a capture failed and can
            // block the child process once the pipe fills up.
            if (string.IsNullOrWhiteSpace(e.Data)) return;
            emit("lhcapture 错误：" + e.Data.Trim(), "warn");
        };
        proc.Start();
        proc.BeginOutputReadLine();
        proc.BeginErrorReadLine();

        var deadline = DateTime.UtcNow.AddSeconds(maxSeconds);
        while (!proc.HasExited && !_stop && DateTime.UtcNow < deadline) Thread.Sleep(200);

        try { if (!proc.HasExited) proc.Kill(entireProcessTree: true); } catch { }
        Thread.Sleep(300);
        Running = false;

        var count = state.Count;
        if (count == 0)
            emit("没有解析到任何设备。常见原因：基站没有照射到 tracker、tracker 没开、接收器没插。", "warn");
        else
            emit(_stop ? "已停止。" : "时间到，自动停止。", "ok");

        post(Payload(state));
    }

    private static JsonObject Entry(Match m, string line)
    {
        var slots = new JsonArray();
        foreach (Match s in Slot.Matches(m.Groups["slots"].Value))
        {
            slots.Add(new JsonObject
            {
                ["us"] = int.Parse(s.Groups["us"].Value),
                ["seen"] = int.Parse(s.Groups["seen"].Value),
                ["sweeps"] = int.Parse(s.Groups["sweeps"].Value),
                ["ratio"] = s.Groups["ratio"].Value.Trim(),
                ["hits"] = int.Parse(s.Groups["hits"].Value),
            });
        }

        var verdict = "";
        var arrow = line.IndexOf("=>", StringComparison.Ordinal);
        if (arrow >= 0) verdict = line[(arrow + 2)..].Trim();

        return new JsonObject
        {
            ["serial"] = m.Groups["dev"].Value,
            ["groups"] = int.Parse(m.Groups["groups"].Value),
            ["flashes"] = m.Groups["flashes"].Value.Trim(),
            ["slots"] = slots,
            ["verdict"] = verdict,
        };
    }

    private static string Payload(ConcurrentDictionary<string, JsonObject> state)
    {
        var devices = new JsonArray();
        foreach (var e in state.Values) devices.Add(e.DeepClone());
        return new JsonObject { ["devices"] = devices, ["running"] = Running }.ToJsonString();
    }
}
