# A test pattern that moves 60 times a second at the top of the screen (like a
# video behind the island), for tests\benchmark.ps1. Always on top, never
# activated, clicks pass through; it closes itself after -Seconds.
#
#   pwsh -File tests\moving_backdrop.ps1 [-Seconds 30]

param([int]$Seconds = 30)
Add-Type -AssemblyName System.Windows.Forms, System.Drawing
# PowerShell 7 keeps the drawing classes in several assemblies: reference all that are loaded.
$refs = [AppDomain]::CurrentDomain.GetAssemblies() | Where-Object { $_.Location -and $_.GetName().Name -match '^System\.(Windows\.Forms|Drawing|ComponentModel|Private\.Windows|Runtime$|Collections|Primitives)' } | ForEach-Object Location
Add-Type -ReferencedAssemblies $refs -TypeDefinition @'
using System; using System.Drawing; using System.Windows.Forms;
public class MovingBackdrop : Form {
    int shift = 0; Timer timer = new Timer();
    public MovingBackdrop(int width, int height) {
        FormBorderStyle = FormBorderStyle.None; ShowInTaskbar = false; TopMost = true; DoubleBuffered = true;
        StartPosition = FormStartPosition.Manual; Location = new Point(0, 0); Size = new Size(width, height);
        timer.Interval = 16; timer.Tick += (s, e) => { shift = (shift + 6) % 80; Invalidate(); }; timer.Start();
    }
    protected override bool ShowWithoutActivation { get { return true; } }
    protected override CreateParams CreateParams { get {
        var p = base.CreateParams; p.ExStyle |= 0x08000000 | 0x80 | 0x80000 | 0x20 | 0x8; return p; } }  // no-activate, tool window, layered, transparent, topmost
    protected override void OnHandleCreated(EventArgs e) { base.OnHandleCreated(e); SetLayered(Handle); }
    [System.Runtime.InteropServices.DllImport("user32")] static extern bool SetLayeredWindowAttributes(IntPtr h, uint k, byte a, uint f);
    static void SetLayered(IntPtr h) { SetLayeredWindowAttributes(h, 0, 255, 2); }
    protected override void OnPaint(PaintEventArgs e) {
        Color[] c = { Color.Red, Color.Orange, Color.Gold, Color.LimeGreen, Color.DeepSkyBlue, Color.RoyalBlue, Color.MediumOrchid, Color.White };
        for (int x = -80, i = 0; x < Width; x += 40, i++) using (var b = new SolidBrush(c[i % c.Length])) e.Graphics.FillRectangle(b, x + shift, 0, 40, Height);
    }
}
'@
$form = New-Object MovingBackdrop ([System.Windows.Forms.Screen]::PrimaryScreen.Bounds.Width), 620
$close = New-Object System.Windows.Forms.Timer
$close.Interval = $Seconds * 1000
$close.Add_Tick({ $form.Close() })
$close.Start()
[System.Windows.Forms.Application]::Run($form)
