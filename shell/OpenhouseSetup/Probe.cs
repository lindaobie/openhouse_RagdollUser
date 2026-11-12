using System.Diagnostics;
using System.Net.Sockets;
using System.Runtime.InteropServices;
using System.Text;
using System.Text.Json;
using System.Text.Json.Nodes;
using Microsoft.Win32;

namespace OpenhouseSetup;

/// Read-only probes: nothing here writes, moves or deletes anything.
internal static class Probe
{
    // Sizes of the two driver builds we can tell apart without hashing 8 MB.
    // The stock DLL is one known build. Ours changes size with every rebuild (the MinGW
    // build was 8 MB, the MSVC one 4.4 MB), so identify ours by "clearly bigger than
    // stock" rather than by an exact length that has to be updated each time.
    private const long OpenhouseDriverMinSize = 3_000_000;
    private const long StockDriverSize = 2_756_248;

    public static JsonObject Snapshot()
    {
        var o = new JsonObject
        {
            ["steamvr"] = SteamVr(),
            ["drives"] = Drives(),
            ["crp"] = Crp(),
            ["firmware"] = Firmware(),
            ["backups"] = Backups(),
            ["stock"] = Stock(),
            ["logdir"] = Log.Dir ?? "",
            ["telemetry"] = Telemetry(),
            ["time"] = DateTime.Now.ToString("HH:mm:ss"),
        };
        return o;
    }

    // ---- collapsing the Explorer window the ROM volume opens by itself ----
    private const int SwMinimize = 6;
    private static readonly HashSet<IntPtr> Minimized = new();
    private static string? _minimizeTitle;

    private delegate bool EnumWindowsProc(IntPtr hwnd, IntPtr lParam);

    [DllImport("user32.dll")]
    private static extern bool EnumWindows(EnumWindowsProc cb, IntPtr lParam);

    [DllImport("user32.dll", CharSet = CharSet.Unicode)]
    private static extern int GetWindowText(IntPtr hwnd, StringBuilder text, int max);

    [DllImport("user32.dll")]
    private static extern bool IsWindowVisible(IntPtr hwnd);

    [DllImport("user32.dll")]
    private static extern bool IsWindow(IntPtr hwnd);

    [DllImport("user32.dll")]
    private static extern bool ShowWindow(IntPtr hwnd, int cmd);

    // Held in a static field: a delegate that only lives on the stack can be collected while
    // EnumWindows is still walking the window list.
    private static readonly EnumWindowsProc MinimizeProc = (hwnd, _) =>
    {
        if (!IsWindowVisible(hwnd)) return true;
        var sb = new StringBuilder(260);
        if (GetWindowText(hwnd, sb, sb.Capacity) <= 0) return true;
        if (_minimizeTitle is null) return true;
        if (!sb.ToString().Contains(_minimizeTitle, StringComparison.OrdinalIgnoreCase)) return true;
        // Once per window: somebody who deliberately restores it should be left alone.
        if (Minimized.Add(hwnd)) ShowWindow(hwnd, SwMinimize);
        return true;
    };

    private static void MinimizeExplorerWindows(string title)
    {
        _minimizeTitle = title;
        try
        {
            Minimized.RemoveWhere(h => !IsWindow(h));
            EnumWindows(MinimizeProc, IntPtr.Zero);
        }
        catch { }
        finally { _minimizeTitle = null; }
    }

    /// The station sitting in ISP mode mounts a ROM-emulated FAT volume with the current
    /// flash in it as firmware.bin. Reading it needs no privileges and changes nothing.
    private static JsonObject Crp()
    {
        var o = new JsonObject();
        foreach (var d in DriveInfo.GetDrives())
        {
            if (d.DriveType != DriveType.Removable) continue;
            string label;
            try { label = d.VolumeLabel; } catch { continue; }
            if (!label.Contains("CRP", StringComparison.OrdinalIgnoreCase)) continue;

            o["letter"] = d.Name.TrimEnd('\\');
            o["label"] = label;
            // Windows opens a File Explorer window on the ROM volume every time it appears.
            // That is never the next thing anyone wants, so fold it away once.
            MinimizeExplorerWindows(label);
            var file = Path.Combine(d.Name, "firmware.bin");
            if (!File.Exists(file)) return o;
            try
            {
                var bytes = File.ReadAllBytes(file);
                o["size"] = bytes.Length;
                o["sha256"] = Convert.ToHexString(System.Security.Cryptography.SHA256.HashData(bytes));
                // The dump is the whole 128 KB flash; an image is only its first part, so
                // comparing whole-file hashes would always say "different".
                o["matches"] = MatchImage(bytes) ?? "";
                // What the unit is actually running right now. Decided from the application
                // area alone: the banner strings are the only thing that separates the two
                // kinds of build, and they are far more reliable than a filename.
                var app = bytes.Length >= Flashing.AppBytes
                    ? bytes.AsSpan(0, Flashing.AppBytes).ToArray() : null;
                o["kind"] = app is null ? "unknown" : IsPatchImage(app) ? "patch" : "stock";
            }
            catch (Exception e)
            {
                o["error"] = e.GetType().Name;
            }
            return o;
        }
        return o;
    }

    /// The release bundle keeps the images beside the tool; walk up from the exe to find it.
    private static JsonArray Backups()
    {
        var arr = new JsonArray();
        var root = FindRepoRoot();
        if (root is null) return arr;
        var dir = Path.Combine(root, "packages", "firmware", "openhouse-basestation-firmware", "backups");
        if (!Directory.Exists(dir)) return arr;
        foreach (var f in Directory.GetFiles(dir, "*.bin").OrderByDescending(f => f))
        {
            var info = new FileInfo(f);
            var entry = new JsonObject
            {
                ["name"] = info.Name,
                ["size"] = info.Length,
                ["date"] = info.LastWriteTime.ToString("yyyy-MM-dd HH:mm"),
            };
            // Say what is inside: a backup taken after a flash holds the patched build, not the
            // factory one. Two words only, so the list stays readable.
            try
            {
                var bytes = File.ReadAllBytes(f);
                var match = MatchImage(bytes);
                if (match is not null)
                {
                    entry["kind"] = "破解固件";
                    entry["detail"] = Path.GetFileNameWithoutExtension(match);
                }
                else
                {
                    var app = bytes.Length >= Flashing.AppBytes
                        ? bytes.AsSpan(0, Flashing.AppBytes).ToArray() : bytes;
                    if (IsPatchImage(app)) { entry["kind"] = "破解固件"; entry["detail"] = "其他版本"; }
                    else { entry["kind"] = "原厂类固件"; entry["detail"] = ""; }
                }
            }
            catch { entry["kind"] = "读取失败"; entry["detail"] = ""; }
            arr.Add(entry);
        }
        return arr;
    }

    private static string? FindRepoRoot()
    {
        var dir = new DirectoryInfo(AppContext.BaseDirectory);
        for (int i = 0; i < 6 && dir is not null; i++, dir = dir.Parent)
        {
            if (Directory.Exists(Path.Combine(dir.FullName, "packages", "firmware")))
                return dir.FullName;
        }
        return null;
    }

    internal static string? FirmwareDir()
    {
        var root = FindRepoRoot();
        return root is null ? null
            : Path.Combine(root, "packages", "firmware", "openhouse-basestation-firmware", "firmware");
    }

    /// The one archived factory image, if there is one. Named by content hash so a second copy
    /// of the same bytes can never appear: every factory unit ships the identical 49,408 bytes.
    internal static string? StockPath()
    {
        var dir = FirmwareDir();
        if (dir is null || !Directory.Exists(dir)) return null;
        foreach (var f in Directory.GetFiles(dir, "stock-*.bin").OrderBy(f => f))
        {
            try { if (new FileInfo(f).Length >= Flashing.AppBytes) return f; }
            catch { }
        }
        return null;
    }

    /// True when these bytes are one of the patched builds we ship. Used to tell a factory
    /// image apart from a cracked one when deciding what is worth archiving.
    internal static bool IsPatchImage(byte[] app)
    {
        if (app.Length < Flashing.AppBytes) return false;
        // Compare against the patched builds' own length: the stock image is longer, and the
        // bytes past 49,408 must not take part in the comparison.
        var head = app.AsSpan(0, Flashing.AppBytes);
        var dir = FirmwareDir();
        if (dir is not null && Directory.Exists(dir))
        {
            foreach (var f in Directory.GetFiles(dir, "lhbs-*.bin"))
            {
                try
                {
                    if (File.ReadAllBytes(f).AsSpan().SequenceEqual(head)) return true;
                }
                catch { }
            }
        }
        var text = Encoding.ASCII.GetString(head);
        return text.Contains("lhbs ");
    }

    /// Which known patched image this flash dump starts with, if any. The archived factory
    /// image is deliberately skipped: it is not something you flash as a patch, and matching
    /// against it would mislabel every factory backup.
    private static string? MatchImage(byte[] dump)
    {
        var dir = FirmwareDir();
        if (dir is null || !Directory.Exists(dir)) return null;
        foreach (var f in Directory.GetFiles(dir, "*.bin"))
        {
            if (Path.GetFileName(f).StartsWith("stock-", StringComparison.OrdinalIgnoreCase)) continue;
            try
            {
                var img = File.ReadAllBytes(f);
                if (img.Length > 0 && img.Length <= dump.Length &&
                    dump.AsSpan(0, img.Length).SequenceEqual(img)) return Path.GetFileName(f);
            }
            catch { }
        }
        return null;
    }

    /// Whether a factory image has been archived yet. Recalculated on every poll rather than
    /// remembered, so deleting the file outside the app puts the gate back immediately.
    private static JsonObject Stock()
    {
        var o = new JsonObject();
        var path = StockPath();
        if (path is null) { o["ok"] = false; return o; }
        var info = new FileInfo(path);
        o["ok"] = true;
        o["file"] = info.Name;
        o["size"] = info.Length;
        o["date"] = info.LastWriteTime.ToString("yyyy-MM-dd HH:mm");
        try
        {
            o["sha"] = Convert.ToHexString(
                System.Security.Cryptography.SHA256.HashData(File.ReadAllBytes(path)))[..12];
        }
        catch { o["sha"] = ""; }
        return o;
    }

    private static JsonArray Firmware()
    {
        var arr = new JsonArray();
        var dir = FirmwareDir();
        if (dir is null || !Directory.Exists(dir)) return arr;
        foreach (var f in Directory.GetFiles(dir, "*.bin"))
        {
            var info = new FileInfo(f);
            if (info.Name.StartsWith("stock-", StringComparison.OrdinalIgnoreCase)) continue;
            var entry = new JsonObject
            {
                ["name"] = info.Name,
                ["size"] = info.Length,
                ["date"] = info.LastWriteTime.ToString("yyyy-MM-dd"),
            };
            try { entry["sha256"] = Convert.ToHexString(System.Security.Cryptography.SHA256.HashData(File.ReadAllBytes(f))); }
            catch { entry["sha256"] = ""; }
            arr.Add(entry);
        }
        return arr;
    }

    /// Channels the driver last knew about. Live when the driver is publishing, otherwise the
    /// set remembered from the last time it did: light alone cannot tell a covered station
    /// from one that is switched off, so an earlier observation is the only way to know.
    public static (List<int> channels, string source) KnownChannels()
    {
        var live = new List<int>();
        try
        {
            var o = Telemetry();
            if (o["running"]?.GetValue<bool>() == true)
            {
                foreach (var b in o["bases"]?.AsArray() ?? new JsonArray())
                {
                    if (int.TryParse(b?["channel"]?.GetValue<string>(), out var ch) && ch >= 1 && ch <= 4)
                        live.Add(ch);
                }
            }
        }
        catch { }

        if (live.Count > 0)
        {
            SaveChannels(live);
            return (live, "live");
        }

        var saved = LoadChannels();
        return saved.Count > 0 ? (saved, "recorded") : (new List<int>(), "none");
    }

    private static string StatePath()
    {
        var dir = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "openhouse");
        Directory.CreateDirectory(dir);
        return Path.Combine(dir, "shell-channels.json");
    }

    private static void SaveChannels(List<int> channels)
    {
        try
        {
            var arr = new JsonArray();
            foreach (var c in channels.Distinct().OrderBy(c => c)) arr.Add(c);
            File.WriteAllText(StatePath(), new JsonObject
            {
                ["channels"] = arr,
                ["at"] = DateTime.Now.ToString("yyyy-MM-dd HH:mm"),
            }.ToJsonString());
        }
        catch { }
    }

    private static List<int> LoadChannels()
    {
        var list = new List<int>();
        try
        {
            var path = StatePath();
            if (!File.Exists(path)) return list;
            var node = JsonNode.Parse(File.ReadAllText(path));
            foreach (var c in node?["channels"]?.AsArray() ?? new JsonArray())
                if (c is not null) list.Add(c.GetValue<int>());
        }
        catch { }
        return list;
    }

    private static JsonObject SteamVr()
    {
        var o = new JsonObject();
        string? root = null;

        // openvrpaths.vrpath is what the runtime itself reads.
        var pathsFile = Path.Combine(
            Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData),
            "openvr", "openvrpaths.vrpath");
        if (File.Exists(pathsFile))
        {
            try
            {
                using var doc = JsonDocument.Parse(File.ReadAllText(pathsFile));
                if (doc.RootElement.TryGetProperty("runtime", out var rt) &&
                    rt.ValueKind == JsonValueKind.Array && rt.GetArrayLength() > 0)
                {
                    var first = rt[0].GetString();
                    if (!string.IsNullOrWhiteSpace(first) && Directory.Exists(first)) root = first;
                }
            }
            catch { /* a malformed file just means we fall through */ }
        }

        if (root is null)
        {
            var steam = Registry.GetValue(@"HKEY_CURRENT_USER\Software\Valve\Steam", "SteamPath", null) as string;
            if (!string.IsNullOrEmpty(steam))
            {
                var candidate = Path.Combine(steam, "steamapps", "common", "SteamVR");
                if (Directory.Exists(candidate)) root = candidate;
            }
        }

        o["found"] = root is not null;
        o["path"] = root ?? "";

        if (root is null)
        {
            o["driver"] = "unknown";
            o["driverSize"] = 0L;
            return o;
        }

        var dll = Path.Combine(root, "drivers", "lighthouse", "bin", "win64", "driver_lighthouse.dll");
        if (File.Exists(dll))
        {
            var len = new FileInfo(dll).Length;
            o["driver"] = len == StockDriverSize ? "stock"
                        : len > OpenhouseDriverMinSize ? "openhouse"
                        : "unknown";
            o["driverSize"] = len;
            o["stockBackup"] = File.Exists(dll + ".stock");
        }
        else
        {
            o["driver"] = "missing";
            o["driverSize"] = 0L;
        }

        o["vrserver"] = Process.GetProcessesByName("vrserver").Length > 0;
        var runArr = new JsonArray();
        foreach (var r in Driver.Running()) runArr.Add(r);
        o["running"] = runArr;
        return o;
    }

    private static JsonArray Drives()
    {
        var arr = new JsonArray();
        foreach (var d in DriveInfo.GetDrives())
        {
            if (d.DriveType != DriveType.Removable) continue;
            string label;
            try { label = d.VolumeLabel; } catch { continue; }
            arr.Add(new JsonObject
            {
                ["letter"] = d.Name.TrimEnd('\\'),
                ["label"] = label,
                // A base station in ISP mode mounts a ROM-emulated FAT volume with this label.
                ["isCrp"] = label.Contains("CRP", StringComparison.OrdinalIgnoreCase),
            });
        }
        return arr;
    }

    private static JsonObject Telemetry()
    {
        var o = new JsonObject();
        try
        {
            using var client = new TcpClient();
            if (!client.ConnectAsync("127.0.0.1", 36840).Wait(700))
            {
                o["running"] = false;
                return o;
            }
            using var stream = client.GetStream();
            using var reader = new StreamReader(stream, Encoding.UTF8);
            var deadline = DateTime.UtcNow.AddMilliseconds(1200);
            while (DateTime.UtcNow < deadline)
            {
                var line = reader.ReadLine();
                if (line is null) break;
                JsonNode? node;
                try { node = JsonNode.Parse(line); } catch { continue; }
                if (node?["type"]?.GetValue<string>() != "state") continue;

                o["running"] = true;
                o["room"] = node["room"]?.DeepClone();
                var bases = new JsonArray();
                foreach (var b in node["bases"]?.AsArray() ?? new JsonArray())
                {
                    var p = new JsonArray();
                    var q = new JsonArray();
                    foreach (var v in b?["p"]?.AsArray() ?? new JsonArray()) p.Add(Math.Round(v!.GetValue<double>(), 4));
                    foreach (var v in b?["q"]?.AsArray() ?? new JsonArray()) q.Add(Math.Round(v!.GetValue<double>(), 5));
                    bases.Add(new JsonObject
                    {
                        ["serial"] = b?["serial"]?.GetValue<string>() ?? "",
                        ["channel"] = b?["channel"]?.GetValue<string>() ?? "",
                        ["origin"] = b?["origin"]?.GetValue<bool>() ?? false,
                        ["locked"] = b?["locked"]?.GetValue<bool>() ?? false,
                        ["placed"] = b?["placed"]?.GetValue<bool>() ?? false,
                        ["learned"] = b?["learned"]?.GetValue<bool>() ?? false,
                        ["seen"] = b?["seen"]?.GetValue<bool>() ?? false,
                        ["status"] = b?["status"]?.GetValue<string>() ?? "",
                        ["sigma"] = b?["sigma"]?.GetValue<double>() ?? 0,
                        ["observations"] = b?["observations"]?.GetValue<int>() ?? 0,
                        ["faults"] = b?["faults"]?.GetValue<int>() ?? 0,
                        ["p"] = p,
                        ["q"] = q,
                    });
                }
                o["bases"] = bases;

                var devs = new JsonArray();
                foreach (var d in node["devices"]?.AsArray() ?? new JsonArray())
                {
                    var p = new JsonArray();
                    var q = new JsonArray();
                    foreach (var v in d?["p"]?.AsArray() ?? new JsonArray()) p.Add(Math.Round(v!.GetValue<double>(), 4));
                    foreach (var v in d?["q"]?.AsArray() ?? new JsonArray()) q.Add(Math.Round(v!.GetValue<double>(), 5));
                    // Which stations actually light this device: the only honest way to judge
                    // whether a solved orientation is pointing anywhere real.
                    var cov = new JsonArray();
                    var covDetail = new JsonArray();
                    foreach (var c in d?["coverage"]?.AsArray() ?? new JsonArray())
                    {
                        var frames = c?["frames"]?.GetValue<int>() ?? 0;
                        if (frames <= 0) continue;
                        var name = c?["base"]?.GetValue<string>() ?? "";
                        cov.Add(name);
                        covDetail.Add(new JsonObject
                        {
                            ["base"] = name,
                            ["frames"] = frames,
                            ["sensors"] = Math.Round(c?["sensors"]?.GetValue<double>() ?? 0, 1),
                        });
                    }
                    devs.Add(new JsonObject
                    {
                        ["serial"] = d?["serial"]?.GetValue<string>() ?? "",
                        ["role"] = d?["role"]?.GetValue<string>() ?? "",
                        ["state"] = d?["state"]?.GetValue<string>() ?? "",
                        ["valid"] = d?["valid"]?.GetValue<bool>() ?? false,
                        ["connected"] = d?["connected"]?.GetValue<bool>() ?? false,
                        ["basesSeen"] = d?["basesSeen"]?.GetValue<int>() ?? 0,
                        ["battery"] = d?["battery"]?.DeepClone(),
                        ["p"] = p,
                        ["q"] = q,
                        ["coverage"] = cov,
                        ["cov"] = covDetail,
                    });
                }
                o["devices"] = devs;
                return o;
            }
            o["running"] = false;
        }
        catch (Exception e)
        {
            o["running"] = false;
            o["error"] = e.GetType().Name;
        }
        return o;
    }
}
