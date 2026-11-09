using System.Drawing.Drawing2D;
using System.Runtime.InteropServices;
using Microsoft.Web.WebView2.Core;
using Microsoft.Web.WebView2.WinForms;

namespace OpenhouseSetup;

public class MainForm : Form
{
    private readonly WebView2 _web = new();

    public MainForm()
    {
        FormBorderStyle = FormBorderStyle.None;
        StartPosition = FormStartPosition.CenterScreen;
        ClientSize = new Size(1000, 680);
        MinimumSize = new Size(880, 600);
        BackColor = Color.FromArgb(0x10, 0x11, 0x14);
        Text = "openhouse setup";
        DoubleBuffered = true;

        _web.Dock = DockStyle.Fill;
        _web.DefaultBackgroundColor = Color.FromArgb(0x10, 0x11, 0x14);
        Controls.Add(_web);
        Load += async (_, _) => await Boot();
    }

    // A frameless window gets its corners rounded by DWM itself, which is the only way
    // to clip the web content: a CSS border-radius only rounds what the page draws, and
    // the OS window stays square underneath it.
    protected override void OnHandleCreated(EventArgs e)
    {
        base.OnHandleCreated(e);

        // ClientSize was written in logical pixels; the window lives in physical ones, so
        // at 150% scaling it came out a third smaller than intended and everything looked
        // oversized.
        var scale = DeviceDpi / 96f;
        if (scale > 1.01f)
            ClientSize = new Size((int)(1000 * scale), (int)(680 * scale));

        const int DWMWA_WINDOW_CORNER_PREFERENCE = 33;
        const int DWMWCP_ROUND = 2;
        var preference = DWMWCP_ROUND;
        try { DwmSetWindowAttribute(Handle, DWMWA_WINDOW_CORNER_PREFERENCE, ref preference, sizeof(int)); }
        catch { /* older Windows: square corners, nothing breaks */ }
    }

    [DllImport("dwmapi.dll")]
    private static extern int DwmSetWindowAttribute(IntPtr hwnd, int attribute, ref int value, int size);

    protected override void OnResize(EventArgs e)
    {
        base.OnResize(e);
        Invalidate();
    }

    private async Task Boot()
    {
        var dataDir = Path.Combine(
            Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData),
            "openhouse", "webview");
        Directory.CreateDirectory(dataDir);

        var env = await CoreWebView2Environment.CreateAsync(null, dataDir);
        await _web.EnsureCoreWebView2Async(env);

        var c = _web.CoreWebView2;
        c.Settings.AreDevToolsEnabled = false;
        c.Settings.AreDefaultContextMenusEnabled = false;
        c.Settings.IsStatusBarEnabled = false;
        c.Settings.IsZoomControlEnabled = false;

        // The page is served from a folder mapping, which carries no cache headers, so
        // Chromium heuristically caches it and keeps serving the previous build after an
        // update. Drop the cache once per launch.
        try { await c.Profile.ClearBrowsingDataAsync(CoreWebView2BrowsingDataKinds.DiskCache); }
        catch { /* older runtime: stale assets until the user data folder is cleared */ }

        // The page is served from a virtual host rather than file://, so it behaves
        // like an app origin without opening any port.
        c.SetVirtualHostNameToFolderMapping(
            "app.openhouse", Path.Combine(AppContext.BaseDirectory, "web"),
            CoreWebView2HostResourceAccessKind.Allow);

        c.AddHostObjectToScript("host", new HostBridge(this));
        c.Navigate("https://app.openhouse/index.html");
    }

    public void BeginDrag()
    {
        Native.ReleaseCapture();
        Native.SendMessage(Handle, Native.WM_NCLBUTTONDOWN, Native.HTCAPTION, IntPtr.Zero);
    }

    /// Progress lines from a background write operation, delivered to the page.
    public void PostMessage(string text, string level)
    {
        if (IsDisposed || !IsHandleCreated) return;
        try
        {
            BeginInvoke(() =>
            {
                var json = System.Text.Json.JsonSerializer.Serialize(new { text, level });
                _web.CoreWebView2?.PostWebMessageAsJson(json);
            });
        }
        catch { /* window closing */ }
    }

    /// Raw optical capture result, straight to the page.
    public void PostOptical(string json)
    {
        if (IsDisposed || !IsHandleCreated) return;
        try { BeginInvoke(() => _web.CoreWebView2?.PostWebMessageAsJson($"{{\"optical\":{json}}}")); }
        catch { }
    }

    /// Tells the page whether a background write is in flight, and what it is.
    public void PostBusy(bool busy, string label)
    {
        if (IsDisposed || !IsHandleCreated) return;
        try
        {
            BeginInvoke(() =>
            {
                var json = System.Text.Json.JsonSerializer.Serialize(new { busy, label });
                _web.CoreWebView2?.PostWebMessageAsJson(json);
            });
        }
        catch { }
    }

    /// The label prompt after a flash: the timestamp has to end up on the unit itself.
    public void PostModal(string stamp)
    {
        if (IsDisposed || !IsHandleCreated) return;
        try
        {
            BeginInvoke(() =>
            {
                var json = System.Text.Json.JsonSerializer.Serialize(new { modal = new { stamp } });
                _web.CoreWebView2?.PostWebMessageAsJson(json);
            });
        }
        catch { }
    }

    // Frameless windows have no system grip; the page draws its own but the resize
    // edges still come from WM_NCHITTEST.
    protected override void WndProc(ref Message m)
    {
        const int WM_NCHITTEST = 0x84;
        if (m.Msg == WM_NCHITTEST && WindowState == FormWindowState.Normal)
        {
            base.WndProc(ref m);
            if ((int)m.Result == 1) // HTCLIENT
            {
                var p = PointToClient(Cursor.Position);
                const int grip = 6;
                bool left = p.X <= grip, right = p.X >= ClientSize.Width - grip;
                bool top = p.Y <= grip, bottom = p.Y >= ClientSize.Height - grip;
                int hit = left && top ? 13 : right && top ? 14 : left && bottom ? 16 : right && bottom ? 17
                        : left ? 10 : right ? 11 : top ? 12 : bottom ? 15 : 0;
                if (hit != 0) m.Result = hit;
            }
            return;
        }
        base.WndProc(ref m);
    }
}
