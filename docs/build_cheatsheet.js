const fs = require("fs");
const { Document, Packer, Paragraph, TextRun, Table, TableRow, TableCell, WidthType, ShadingType, BorderStyle,
  AlignmentType, LevelFormat, HeadingLevel } = require("docx");

const NAVY = "0F1B2D", ORANGE = "F5A623", TEAL = "1F9E89", GREY = "5B6B80", LIGHT = "EEF1F5";
const FONT = "Calibri";
const W = 9360; // content width in DXA (US Letter, 1" margins)

const run = (text, o = {}) => new TextRun({ text, font: o.font || FONT, size: (o.size || 12) * 2, bold: o.bold, color: o.color || NAVY, italics: o.italic });
const para = (children, o = {}) => new Paragraph({ children: Array.isArray(children) ? children : [run(children, o)], spacing: { after: o.after ?? 120 }, alignment: o.align });
const heading = (t) => new Paragraph({ heading: HeadingLevel.HEADING_1, spacing: { before: 280, after: 140 },
  children: [new TextRun({ text: t, font: "Cambria", size: 32, bold: true, color: NAVY })] });
const border = { style: BorderStyle.SINGLE, size: 4, color: "D5DCE5" };
const borders = { top: border, bottom: border, left: border, right: border };

function cell(text, width, o = {}) {
  return new TableCell({
    width: { size: width, type: WidthType.DXA }, borders,
    shading: o.fill ? { type: ShadingType.CLEAR, color: "auto", fill: o.fill } : undefined,
    margins: { top: 90, bottom: 90, left: 120, right: 120 },
    children: [new Paragraph({ children: Array.isArray(text) ? text : [run(text, { size: o.size || 12, bold: o.bold, color: o.color, font: o.font })] })],
  });
}

function table(widths, rows) {
  return new Table({
    width: { size: W, type: WidthType.DXA }, columnWidths: widths,
    rows: rows.map((r, i) => new TableRow({
      tableHeader: i === 0,
      children: r.map((t, c) => cell(t, widths[c], i === 0 ? { fill: NAVY, color: "FFFFFF", bold: true } :
        { fill: i % 2 ? "FFFFFF" : "F6F8FB", bold: c === 0 })),
    })),
  });
}

// How to run: Command (monospace) | What it does
const runCmds = [
  ["cd \"C:\\Users\\imved\\OneDrive\\Desktop\\OS_CP\"", "Go to the project folder (do this first)."],
  ["powershell -ExecutionPolicy Bypass -File build.ps1", "Build the app (only needed after code changes)."],
  [".\\build\\SpaceRescue.exe", "Open the app and scan C:."],
  [".\\build\\SpaceRescue.exe \"D:\\\"", "Scan another drive or folder."],
  [".\\build\\SpaceRescue.exe /bench C: | more", "Speed test. Run in an Admin PowerShell to use the fast MFT reader."],
  [".\\build\\SpaceRescue.exe /tray", "Start the low-space alert in the system tray."],
  [".\\build\\SpaceRescue.exe /classic", "Open the older interface."],
  ["python docs\\build_presentation.py", "Rebuild the slide deck."],
];

function runTable() {
  const widths = [5000, W - 5000];
  return new Table({
    width: { size: W, type: WidthType.DXA }, columnWidths: widths,
    rows: [["Command", "What it does"], ...runCmds].map((r, i) => new TableRow({
      tableHeader: i === 0, cantSplit: true,
      children: r.map((t, c) => cell(t, widths[c], i === 0 ? { fill: NAVY, color: "FFFFFF", bold: true } :
        { fill: i % 2 ? "FFFFFF" : "F6F8FB", size: c === 0 ? 10.5 : 12, font: c === 0 ? "Consolas" : FONT, color: c === 0 ? TEAL : NAVY, bold: c === 0 })),
    })),
  });
}

const slides = [
  ["Slide", "Say this"],
  ["Title", "“A disk manager that still works when the disk is full.”"],
  ["Introduction", "“The OS knows where every byte is; the user just sees ‘low disk space’. We connect the two.”"],
  ["Problem", "“At 512 MB free, updates fail and apps crash. Tools are slow and don’t say what’s safe.”"],
  ["Windows tools table", "“No built-in tool shows where space went, offers undo, AND warns early.”"],
  ["Use cases", "“Student, developer, gamer, normal user: each gets a specific answer.”"],
  ["User’s side", "“Notice → understand → act safely → stay safe.”"],
  ["Our solution", "“Each problem has a fix: Rescue Mode, fast scan, safety badges, locks, alerts.”"],
  ["Objectives", "“Six goals: rescue, fast, explain, clean safely, warn early, teach internals.”"],
  ["Related work", "“WinDirStat, WizTree and others show sizes, but none warn early or handle a full disk.”"],
  ["OS concepts (file systems)", "“We use the MFT, extents, clusters and slack directly. Each card shows the file in our code.”"],
  ["Concurrency", "“8 threads share a folder queue protected by a lock; no lock is held during disk reads.”"],
  ["Memory and protection", "“Full disk = less RAM (pagefile can’t grow), so we cap memory and lower our priority.”"],
  ["Built for 0 bytes free", "“Scan lives only in RAM: 255 MB peak, tray uses 1.6 MB, 19 bytes written.”"],
  ["Algorithms", "“Parallel tree walk, MFT parsing, treemap, duplicate funnel, alert hysteresis.”"],
  ["Deep dive", "“1 thread 25 s → 8 threads 8.2 s. 12 threads got slower: that’s Amdahl’s law.”"],
  ["Methodology", "“Scan → model → analyse → decide → act with undo. Tested with benchmarks and fsutil.”"],
  ["Architecture", "“User → UI → engine → Windows APIs. Nothing is written to the full drive.”"],
  ["Product", "“Overview, Clean up, System, Disk internals, Rescue Mode: real screenshots.”"],
  ["Results", "“58 s → 8.2 s, answer in 3 s, 255 MB RAM, tray 1.6 MB.”"],
  ["Conclusion", "“Done: fast, safe, zero-write. Pending: 0-byte disk test, admin MFT benchmark.”"],
];

const qa = [
  ["What is the MFT?", "NTFS’s table with one record per file, like Unix inodes. We read it directly when run as admin, which is faster."],
  ["What is slack space?", "Files get whole 4 KB clusters; the unused tail is wasted. That’s internal fragmentation (about 4 GB on our C:)."],
  ["What is fragmentation?", "A file stored in many separate pieces (extents) instead of one. We draw it: one 8 GB file was in 58 pieces."],
  ["How is it multithreaded?", "8 worker threads take folders from a shared queue (producer–consumer). A mutex (Windows SRW lock) guards the queue, and a condition variable tells workers when the work is done."],
  ["How do you avoid deadlock?", "We never hold two locks at once, and never hold a lock while reading the disk."],
  ["Why not use 16 threads?", "We measured it: 12 threads were slower than 8. The disk and the lock become the bottleneck (Amdahl’s law)."],
  ["Why does a full disk reduce RAM?", "The pagefile is on the same disk and can’t grow, so we cap our memory and fold small files together."],
  ["Why does the Recycle Bin free nothing?", "It’s just a rename on the same drive. Space comes back only when the bin is emptied, which is why we offer quarantine to another drive."],
];

const C = require("./cheatsheet_concepts.js");

// Table: Concept | Simple (like a kid) | Technical | Where in our code. Smaller font so 4 columns fit.
function conceptSection(title, rows) {
  const widths = [1900, 2400, 3660, 1400];
  const head = ["Concept", "Simple", "Technical", "In our code"];
  const mk = (t, c, i) => cell(t, widths[c], i === 0 ? { fill: NAVY, color: "FFFFFF", bold: true, size: 10.5 } :
    { fill: i % 2 ? "FFFFFF" : "F6F8FB", bold: c === 0, size: 10.5, color: c === 3 ? TEAL : (c === 1 ? GREY : NAVY) });
  return [
    heading(title),
    new Table({
      width: { size: W, type: WidthType.DXA }, columnWidths: widths,
      rows: [head, ...rows].map((r, i) => new TableRow({ tableHeader: i === 0, cantSplit: true, children: r.map((t, c) => mk(t, c, i)) })),
    }),
  ];
}

const numbering = { config: [{ reference: "nums", levels: [{ level: 0, format: LevelFormat.DECIMAL, text: "%1.", alignment: AlignmentType.LEFT,
  style: { paragraph: { indent: { left: 400, hanging: 300 } } } }] }] };

const children = [
  new Paragraph({ children: [new TextRun({ text: "SpaceRescue – what to say", font: "Cambria", size: 44, bold: true, color: NAVY })], spacing: { after: 60 } }),
  para([run("Read this only. Short lines you can say out loud.", { color: GREY, italic: true })], { after: 200 }),

  heading("The whole project in 3 lines"),
  ...[
    "When C: is full, Windows breaks, and disk tools break too because they also need space and RAM.",
    "SpaceRescue scans fast (8 s for 1.7M files), tells you what is SAFE to delete, and writes nothing to the full drive.",
    "It also shows OS internals (clusters, slack, fragments, MFT) on your real disk.",
  ].map(t => new Paragraph({ numbering: { reference: "nums", level: 0 }, spacing: { after: 100 }, children: [run(t, { size: 13 })] })),

  heading("How to run it (for the demo)"),
  para([run("Open PowerShell and type these. For the demo you only need the first and third lines.", { color: GREY, italic: true })]),
  runTable(),

  heading("One line per slide"),
  table([2600, W - 2600], slides),

  heading("8 questions they will ask"),
  table([3000, W - 3000], [["Question", "Short answer"], ...qa]),

  ...conceptSection("OS concepts: file systems and storage", C.storage),
  ...conceptSection("OS concepts: concurrency and synchronization", C.concurrency),
  ...conceptSection("OS concepts: memory, scheduling and protection", C.memory),
  ...conceptSection("Algorithms", C.algorithms),

  heading("If you forget everything, say this"),
  new Table({
    width: { size: W, type: WidthType.DXA }, columnWidths: [W],
    rows: [new TableRow({ children: [new TableCell({
      width: { size: W, type: WidthType.DXA }, borders: { top: { style: BorderStyle.NONE }, bottom: { style: BorderStyle.NONE }, left: { style: BorderStyle.NONE }, right: { style: BorderStyle.NONE } },
      shading: { type: ShadingType.CLEAR, color: "auto", fill: NAVY }, margins: { top: 200, bottom: 200, left: 240, right: 240 },
      children: [new Paragraph({ children: [
        new TextRun({ text: "“SpaceRescue reads the file system the way the OS does: the MFT, clusters and extents. It uses 8 threads to scan 1.7 million files in 8 seconds, tells you what’s safe to delete with undo, and never writes to the full disk.”",
          font: FONT, size: 26, bold: true, color: "FFFFFF" })] })],
    })] })],
  }),
];

const doc = new Document({
  numbering,
  styles: { default: { document: { run: { font: FONT, size: 24 } } } },
  sections: [{ properties: { page: { size: { width: 12240, height: 15840 }, margin: { top: 1080, bottom: 1080, left: 1440, right: 1440 } } }, children }],
});
Packer.toBuffer(doc).then(b => { fs.writeFileSync(process.argv[2], b); console.log("saved", process.argv[2]); });
