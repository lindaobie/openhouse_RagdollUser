using System.Runtime.InteropServices;
using System.Text.Json.Nodes;

namespace OpenhouseSetup;

static class Program
{
    [STAThread]
    static void Main()
    {
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

    public string Version() => "1.0.0-demo";

    // Lets a launcher (or a test) open on a given page: OPENHOUSE_PAGE=base.flash
    public string StartPage() => Environment.GetEnvironmentVariable("OPENHOUSE_PAGE") ?? "";

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

    /// Starts or stops a raw optical capture. Returns an error string, or "".
    public string StartOptical(int seconds)
    {
        if (Optical.Running) { Optical.Stop(); return ""; }
        if (_busy) return Busy("等它跑完");
        if (Optical.ExePath() is null) return "找不到 lhcapture.exe";
        Begin("检查基站摆位");
        _ = Task.Run(() =>
        {
            try { Optical.Run(seconds, Emit, _form.PostOptical); }
            catch (Exception e) { Emit("失败：" + e.Message, "warn"); }
            finally { End(); }
        });
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

    private void Emit(string text, string level = "") => _form.PostMessage(text, level);

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

        var path = FirmwarePath(name) ?? (Path.IsPathRooted(name) && File.Exists(name) ? name : null);
        if (kind == "restore")
        {
            var dir0 = BackupDir();
            path = dir0.Length == 0 ? null : Path.Combine(dir0, name);
            if (path is null || !File.Exists(path)) { Emit("找不到备份 " + name, "warn"); return; }
        }
        if (path is null) { Emit("找不到镜像 " + name, "warn"); return; }

        var image = File.ReadAllBytes(path);
        if (kind == "restore")
        {
            // A backup is a whole flash dump, so it has to be exactly that size.
            if (image.Length != Flashing.FlashBytes)
            {
                Emit($"备份大小 {image.Length} 字节，不是完整的 128 KB，拒绝写入", "warn");
                return;
            }
            if (Flashing.ReadProtected(image))
            {
                Emit("这份备份带读保护字（0x2FC），写回去会把 USB 引导锁死，拒绝", "warn");
                return;
            }
        }
        else
        {
            if (!Flashing.VectorsOk(image)) { Emit("镜像向量校验不通过，拒绝写入", "warn"); return; }
            if (Flashing.ReadProtected(image)) { Emit("镜像带读保护字（0x2FC），写入会把 USB 引导永久锁死，拒绝", "warn"); return; }
        }

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

        // Back up before anything is written: this is the only copy of what is in there now.
        var dir = BackupDir();
        if (dir.Length == 0) { Emit("找不到备份目录，拒绝写入", "warn"); return; }
        Directory.CreateDirectory(dir);
        var stamp = DateTime.Now.ToString("yyyyMMdd-HHmmss");
        var backup = Path.Combine(dir, $"firmware-{stamp}.bin");
        File.WriteAllBytes(backup, current);
        Emit($"已备份到 backups\\{Path.GetFileName(backup)}", "ok");
        if (Flashing.SamePrefix(image, current, image.Length))
        {
            Emit(kind == "restore" ? "盘内已经就是这份备份的内容，不需要写" : "盘内已经是这个固件，不需要写", "ok");
            Flashing.Eject(drive);
            return;
        }
        Emit("写入中…");
        Flashing.WriteImage(drive, image);
        Emit("写入完成，弹出设备", "ok");
        // Only a flash needs the label: a restore puts back what the unit already was.
        if (kind == "flash") _form.PostModal(stamp);
        Flashing.Eject(drive);

        Emit("固件刷写成功，准备验证。请拔掉 USB 和电源，按原步骤重新插一次。", "warn");

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
                Emit("已检测到拔出。现在按四步重新插上：按住背面按钮 → 插 USB → 插电源 → 松手", "ok");
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
                    ? $"固件已写入，等待重新插上以校验（剩余 {stamp2}）"
                    : $"固件刷写成功，请拔掉 USB 和电源后按原步骤重新插一次（剩余 {stamp2}）", "warn");
            }
            Thread.Sleep(1000);
        }

        if (again is null)
        {
            Emit("超时：10 分钟内没有完成重新插拔，这次校验没有做。下次刷写时会重新校验。", "warn");
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
            Emit("校验通过：flash 里就是刚写进去的镜像，刷写完成。", "ok");
            Emit("拔下基站，正常接电源（不要按背面按钮），它就会用新固件启动");
        }
        else if (Flashing.SamePrefix(current, check, Math.Min(current.Length, check.Length)))
        {
            Emit("写入没有落到 flash：读回的仍然是刷写前的内容。请重新刷一次。", "warn");
        }
        else
        {
            Emit("校验失败：读回的内容和镜像不一致", "warn");
        }
        Flashing.Eject(again);
    }
}
