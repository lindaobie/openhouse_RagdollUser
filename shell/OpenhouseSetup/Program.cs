using System.Runtime.InteropServices;
using System.Text.Json.Nodes;

namespace OpenhouseSetup;

static class Program
{
    [STAThread]
    static void Main()
    {
        Log.Open();
        ApplicationConfiguration.Initialize();
        Application.Run(new MainForm());
    }
}

internal static class Native
{
    [DllImport("user32.dll")]
    internal static extern bool ReleaseCapture();

    [DllImport("user32.dll", CharSet = CharSet.Auto)]
    internal static extern IntPtr SendMessage(IntPtr hWnd, int msg, IntPtr wParam, IntPtr lParam);

    internal const int WM_NCLBUTTONDOWN = 0xA1;
    internal const int HTCAPTION = 0x2;
}

[ComVisible(true)]
[ClassInterface(ClassInterfaceType.AutoDual)]
public class HostBridge
{
    private readonly MainForm _form;
    private bool _busy;
    private string _op = "";

    private void Begin(string label) { _busy = true; _op = label; _form.PostBusy(true, label); }
    private void End() { _busy = false; _op = ""; _form.PostBusy(false, ""); }

    private string Busy(string howToEnd) =>
        string.IsNullOrEmpty(_op) ? "上一个操作还没有结束" : $"正在{_op}，{howToEnd}";

    public HostBridge(MainForm form) => _form = form;

    public string GetStatus() => System.Text.Json.JsonSerializer.Serialize(Probe.Snapshot());

    public void DragWindow() => _form.BeginDrag();

    public void Minimize() => _form.WindowState = FormWindowState.Minimized;

    public void Close() => _form.Close();

    public string Version() => "1.2.2";

    // Lets a launcher (or a test) open on a given page: OPENHOUSE_PAGE=base.flash
    public string StartPage() => Environment.GetEnvironmentVariable("OPENHOUSE_PAGE") ?? "";

    /// Opens a link in the user's browser. The page runs inside WebView2, where a plain
    /// anchor would navigate the app itself away from its own UI.
    public void OpenUrl(string url)
    {
        if (string.IsNullOrWhiteSpace(url)) return;
        if (!url.StartsWith("https://", StringComparison.OrdinalIgnoreCase)) return;
        try { System.Diagnostics.Process.Start(new System.Diagnostics.ProcessStartInfo(url) { UseShellExecute = true }); }
        catch { }
    }

    /// Opens the streaming subscription the map page uses; harmless if already open.
    public void StartLive() => Probe.StartLive();

    /// Drops it again when the map page is left. The driver only broadcasts to connected
    /// clients, so nothing is wasted when nobody is watching.
    public void StopLive() => Probe.StopLive();

    /// Starts a write operation in the background. Returns an error string, or "".
    public string StartFlash(string kind, string firmwareName)
    {
        if (_busy) return Busy("等它跑完，或者把设备拔掉");
        if (Flashing.FindCrp(out _) is null) return "没有检测到 CRP 设备";
        Begin(kind == "restore" ? "还原固件" : "刷写固件");
        _ = Task.Run(() =>
        {
            try { RunFlash(kind, firmwareName); }
            catch (Exception e) { Emit("失败：" + e.Message, "warn"); }
            finally { End(); }
        });
        return "";
    }

    /// Starts a driver install or restore in the background. Returns an error string, or "".
    public string StartDriver(string action)
    {
        if (_busy) return Busy("等它跑完");
        Begin(action == "uninstall" ? "还原原厂驱动" : "安装驱动");
        _ = Task.Run(() =>
        {
            try
            {
                if (action == "install") Driver.Install(Emit);
                else if (action == "uninstall") Driver.Uninstall(Emit);
            }
            catch (Exception e) { Emit("失败：" + e.Message, "warn"); }
            finally { End(); }
        });
        return "";
    }

    /// Starts a raw optical capture. Returns an error string, or "".
    /// Stopping goes through StopOptical: deciding by "is it running" meant a click that
    /// landed a few milliseconds late restarted the capture instead of ending it.
    public string StartOptical(int seconds)
    {
        if (Optical.Running) return "";
        if (_busy) return Busy("等它跑完");
        if (Optical.ExePath() is null) return "找不到 lhcapture.exe";
        Begin("检查追踪质量");
        _ = Task.Run(() =>
        {
            try { Optical.Run(seconds, Emit, _form.PostOptical); }
            catch (Exception e) { Emit("失败：" + e.Message, "warn"); }
            finally { End(); }
        });
        return "";
    }

    /// Ends the capture if one is running. Safe to call while it is still winding down.
    public string StopOptical()
    {
        Optical.Stop();
        return "";
    }

    public string Backups()    {
        var dir = BackupDir();
        var arr = new JsonArray();
        if (!Directory.Exists(dir)) return arr.ToJsonString();
        foreach (var f in Directory.GetFiles(dir, "*.bin").OrderByDescending(f => f))
            arr.Add(Path.GetFileName(f));
        return arr.ToJsonString();
    }

    /// Archives the factory application image — once per distinct version, shared by the whole
    /// fleet. Sources are tried in order: the station currently in ISP mode, then every
    /// historical dump that still holds factory bytes rather than a patched build. Returns "".
    public string ExtractStock()
    {
        if (_busy) return Busy("等它跑完");

        var fwDir = Probe.FirmwareDir();
        if (fwDir is null) return "找不到 firmware 目录";

        var found = new Dictionary<string, (byte[] app, string src)>();

        void Consider(byte[] dump, string source)
        {
            var app = FactoryPart(dump);
            if (app is null) return;
            var h = Flashing.Sha(app)[..12];
            if (!found.ContainsKey(h)) found[h] = (app, source);
        }

        var drive = Flashing.FindCrp(out _);
        if (drive is not null)
        {
            try { Consider(File.ReadAllBytes(Path.Combine(drive + "\\", "firmware.bin")), $"当前接着的基站 {drive}"); }
            catch (Exception e) { return "读不到设备里的固件：" + e.Message; }
        }

        var dir = BackupDir();
        if (dir.Length > 0 && Directory.Exists(dir))
        {
            foreach (var f in Directory.GetFiles(dir, "*.bin").OrderByDescending(f => f))
            {
                try { Consider(File.ReadAllBytes(f), "历史备份 " + Path.GetFileName(f)); }
                catch { }
            }
        }

        if (found.Count == 0)
        {
            Emit("没找到原始固件：换一台没刷过破解固件的基站接上再试，或者把之前的备份放回 backups 目录。", "warn");
            return "没有可备份的原始固件";
        }

        try
        {
            Directory.CreateDirectory(fwDir);
            foreach (var kv in found.OrderBy(k => k.Key))
            {
                var name = "stock-" + kv.Key + ".bin";
                var target = Path.Combine(fwDir, name);
                if (!File.Exists(target)) File.WriteAllBytes(target, kv.Value.app);
                Emit($"已备份原始固件 {name} · {kv.Value.app.Length} 字节 · 来自{kv.Value.src}", "ok");
            }
            return "";
        }
        catch (Exception e) { return "写入失败：" + e.Message; }
    }

    /// The factory application bytes inside a flash dump, or null when the dump already holds
    /// one of our patched builds — archiving a cracked image as "原始固件" would be worse than
    /// archiving nothing at all.
    ///
    /// The stock image is much longer than the patched one: its code runs to 0x13BF8 on the
    /// units we have, 31 KB past where lhbs ends. Keeping only the first 49,408 bytes and
    /// writing those back left the rest of the stock code as 0xFF, and a restored unit would
    /// not boot at all.
    private static byte[]? FactoryPart(byte[] dump)
    {
        if (dump.Length < Flashing.AppBytes) return null;
        if (!Flashing.VectorsOk(dump)) return null;
        if (Probe.IsPatchImage(dump)) return null;
        var end = dump.Length;
        while (end > Flashing.AppBytes && dump[end - 1] == 0xFF) end--;
        return dump.AsSpan(0, end).ToArray();
    }

    private void Emit(string text, string level = "")
    {
        var tag = string.IsNullOrEmpty(level) ? "INFO"
            : level.StartsWith("wait", StringComparison.Ordinal) || level == "countdown" ? "WAIT"
            : level.ToUpperInvariant();
        if (!string.IsNullOrEmpty(text)) Log.Write(tag, text);
        _form.PostMessage(text, level, _op);
    }

    /// Where this run's log is, shown on the log page so it can be handed to someone else.
    public string LogDir() => Log.Dir ?? "";

    public void OpenLogDir()
    {
        var dir = Log.Dir;
        if (dir is null) return;
        try
        {
            System.Diagnostics.Process.Start(new System.Diagnostics.ProcessStartInfo(dir)
            {
                UseShellExecute = true,
            });
        }
        catch { }
    }

    private static string? FindRepoRoot()
    {
        var dir = new DirectoryInfo(AppContext.BaseDirectory);
        for (int i = 0; i < 6 && dir is not null; i++, dir = dir.Parent)
            if (Directory.Exists(Path.Combine(dir.FullName, "packages", "firmware")))
                return dir.FullName;
        return null;
    }

    private static string? FirmwarePath(string name)
    {
        var root = FindRepoRoot();
        if (root is null) return null;
        var path = Path.Combine(root, "packages", "firmware", "openhouse-basestation-firmware", "firmware", name);
        return File.Exists(path) ? path : null;
    }

    private static string BackupDir()
    {
        var root = FindRepoRoot();
        return root is null ? "" : Path.Combine(root, "packages", "firmware", "openhouse-basestation-firmware", "backups");
    }

    private void RunFlash(string kind, string name)
    {
        var drive = Flashing.FindCrp(out var label);
        if (drive is null) { Emit("没有检测到 CRP 设备", "warn"); return; }

        // Nothing is written until a factory image has been archived. That image is identical on
        // every unit, so one copy covers them all — but without it there is no way back, and the
        // point of the gate is that the decision is made before any flashing, not after.
        var stock = Probe.StockPath();
        if (stock is null)
        {
            Emit("还没有备份原始固件，刷写功能锁着。先在本页点「备份原始固件」。", "warn");
            return;
        }

        // A restore no longer asks which backup: they all contain the same application bytes.
        var path = kind == "restore"
            ? stock
            : FirmwarePath(name) ?? (Path.IsPathRooted(name) && File.Exists(name) ? name : null);
        if (path is null) { Emit("找不到镜像 " + name, "warn"); return; }

        var image = File.ReadAllBytes(path);

        // Both paths write the application area and stop there. The stock image is longer
        // than the patched one (80,896 vs 49,408 on our units) and must go in whole.
        if (image.Length < Flashing.AppBytes || image.Length > Flashing.FlashBytes)
        {
            Emit($"镜像 {image.Length} 字节，不在 {Flashing.AppBytes}..{Flashing.FlashBytes} 范围内，拒绝写入", "warn");
            return;
        }
        if (!Flashing.VectorsOk(image)) { Emit("镜像向量校验不通过，拒绝写入", "warn"); return; }
        if (Flashing.ReadProtected(image)) { Emit("镜像带读保护字（0x2FC），写入会把 USB 引导永久锁死，拒绝", "warn"); return; }

        Emit($"镜像 {Path.GetFileName(path)} · {image.Length} 字节 · {Flashing.Sha(image)[..16]}");
        Emit($"设备 {drive} · {label}");

        var target = Path.Combine(drive + "\\", "firmware.bin");
        var current = File.ReadAllBytes(target);
        Emit($"盘内固件 {current.Length} 字节 · {Flashing.Sha(current)[..16]}");
        var sn = Flashing.SerialOf(current);
        if (sn is not null) Emit("这台的序列号 LHB-" + sn, "ok");

        if (kind == "verify")
        {
            var ok = Flashing.SamePrefix(image, current, image.Length);
            Emit(ok ? "校验通过：盘内已经是这个镜像" : "不一致：盘内固件不是这个镜像", ok ? "ok" : "warn");
            return;
        }

        // No dump is taken here. The factory image is archived once, and every remaining byte of
        // a 128 KB backup is either identical to a known image or sits in sectors this write
        // cannot reach, so per-write copies only ever produced duplicates.
        if (Flashing.SamePrefix(image, current, image.Length))
        {
            Emit(kind == "restore" ? "盘内已经就是这份备份的内容，不需要写" : "盘内已经是这个固件，不需要写", "ok");
            Flashing.Eject(drive);
            return;
        }
        Emit("写入中…");
        Flashing.WriteImage(drive, image);
        Emit("写入完成，弹出设备", "ok");
        Flashing.Eject(drive);

        Emit("固件已写入。请拔掉基站上的所有线（USB 和电源），再按上面 4 步重新接一次。", "wait");

        // The ROM volume remounts within a second of the eject, so waiting for "present"
        // passes instantly and silently reads a cached copy. Require it to be absent for
        // four consecutive polls (2 s) before believing it was actually unplugged.
        // Waiting for the physical unplug/replug is the step people get wrong, so keep saying
        // what to do, with the time left, until it happens or ten minutes pass.
        var deadline = DateTime.UtcNow.AddMinutes(10);
        int absent = 0, tick = 0;
        bool unplugged = false;
        string? again = null;

        while (DateTime.UtcNow < deadline)
        {
            bool present = Flashing.FindCrp(out _) is not null;
            absent = present ? 0 : absent + 1;

            // Only trust a reappearance that followed a real absence: the ROM volume remounts
            // by itself after an eject, and reading then returns a cached copy.
            if (!unplugged && absent >= 4)
            {
                unplugged = true;
                Emit("已检测到拔出。现在重新接上：按住背面按钮 → 插 USB → 插电源 → 松手", "wait-step");
            }
            else if (unplugged && present)
            {
                again = Flashing.FindCrp(out _);
                break;
            }

            tick++;
            if (tick % 4 == 0)
            {
                var left = deadline - DateTime.UtcNow;
                var stamp2 = $"{(int)left.TotalMinutes}:{left.Seconds:00}";
                Emit(unplugged
                    ? $"已经拔掉了。按上面 4 步重新接：按住背面按钮 → 插 USB → 插电源 → 松手（剩余 {stamp2}）"
                    : $"固件已写入。请拔掉基站上的所有线（USB 和电源），再按上面 4 步重新接一次。（剩余 {stamp2}）", "countdown");
            }
            Thread.Sleep(1000);
        }

        if (again is null)
        {
            Emit("超时：10 分钟内没有完成重新插拔，这次校验没有做。下次刷写时会重新校验。", "wait-timeout");
            return;
        }

        var check = File.ReadAllBytes(Path.Combine(again + "\\", "firmware.bin"));
        var matched = Flashing.SamePrefix(image, check, image.Length);
        if (matched)
        {
            // Windows can still answer from cache right after a replug, and that cached copy is
            // exactly what we just wrote. Read again a few seconds later before believing it.
            Thread.Sleep(3000);
            try { check = File.ReadAllBytes(Path.Combine(again + "\\", "firmware.bin")); }
            catch { }
            matched = Flashing.SamePrefix(image, check, image.Length);
        }

        if (matched)
        {
            Emit("校验通过：flash 里就是刚写进去的镜像，刷写完成。", "wait-ok");
            Emit("拔下基站，正常接电源（不要按背面按钮），它就会用新固件启动");
        }
        else if (Flashing.SamePrefix(current, check, Math.Min(current.Length, check.Length)))
        {
            Emit("写入没有落到 flash：读回的仍然是刷写前的内容。请重新刷一次。", "wait-fail");
        }
        else
        {
            Emit("校验失败：读回的内容和镜像不一致", "wait-fail");
        }
        Flashing.Eject(again);
    }
}
