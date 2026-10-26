# SpaceRescue Design System

**One moment, one instrument.** The user's disk is full and they are stressed. Every screen answers three
questions in this order: *how bad is it?* → *where did the space go?* → *what can I safely free right now?*
The look is a calm, precise instrument, closer to a well-made audio meter than a web dashboard.

## 1. Principles
1. **One hero per screen.** One large visual (sunburst, treemap, meter) and one primary action. Everything else is secondary.
2. **Numbers carry context.** Never a bare "12.4 GB". Always "12.4 GB · 18% of C:" or "frees 12.4 GB".
3. **Safety is visible.** Every cleanup item shows a safety badge: Safe, Review, Caution or Protected. Destructive paths take three steps: overview → review list → confirm, followed by an undo toast.
4. **Calm urgency.** Rescue Mode changes tone (warm accent, the reserve action first) but never flashes or shouts.
5. **Native first.** Windows 11 conventions: Segoe UI Variable, rounded corners, Mica-like base, system light/dark, keyboard everywhere.

## 2. Color tokens
One deep neutral family, one signature accent, four semantic colors, and one ordered file-type palette.

| Token | Dark | Light | Use |
|---|---|---|---|
| `base` | `#0E1116` | `#F4F6F8` | window background (Mica tint) |
| `surface1` | `#151A21` | `#FFFFFF` | cards |
| `surface2` | `#1B212A` | `#F0F2F5` | raised rows, inputs, nav selection |
| `surface3` | `#232A35` | `#E6E9EE` | hover, pressed |
| `stroke` | `#FFFFFF14` | `#0000001A` | 1 px card borders, dividers |
| `text` | `#E8ECF1` | `#11151B` | primary text |
| `textDim` | `#9AA4B2` | `#556070` | secondary text |
| `textFaint` | `#6B7584` | `#8A94A3` | captions, axis labels |
| **`accent`** | **`#35D6C0`** | **`#0B8F80`** | signature "rescue teal": primary buttons, focus, selection |
| `accentInk` | `#04201C` | `#FFFFFF` | text on accent |
| `safe` | `#4CC38A` | `#1E8A57` | Safe to remove |
| `review` | `#F2B84B` | `#A86A00` | Review first |
| `caution` | `#FF6F61` | `#C8372D` | Caution / Rescue Mode / destructive |
| `protected` | `#A391FF` | `#6B54E0` | Windows-managed, never delete by hand |
| `free` | `#FFFFFF1F` | `#0000001F` | free space in rings and bars (neutral, never colored) |

**File-type palette.** Perceptually ordered, with similar lightness and chroma (OKLCH L≈0.74 / C≈0.13 dark, L≈0.58 light) and hue steps of ~33°. Each category always gets the same color:

| # | Category | Dark | Light |
|---|---|---|---|
| 1 | Video | `#F27A8C` | `#C2405A` |
| 2 | Images | `#F59E6C` | `#C0612A` |
| 3 | Audio | `#E8C25A` | `#9C7A0A` |
| 4 | Documents | `#9BD46A` | `#4E8A1F` |
| 5 | Archives & installers | `#4FD3A0` | `#14895F` |
| 6 | Code & dev caches | `#3FCFD9` | `#0F8590` |
| 7 | Programs & libraries | `#5AA9F5` | `#2167B8` |
| 8 | Disk images & VMs | `#8C8CF7` | `#4B4BC4` |
| 9 | System & Windows | `#B784F0` | `#7A43B8` |
| 10 | Other / no extension | `#8E99A8` | `#5E6878` |

## 3. Typography
Segoe UI Variable (falls back to Segoe UI). **Tabular figures** (`tnum`) for every number that updates or aligns.

| Role | Font | Size / line | Weight |
|---|---|---|---|
| Hero number | Variable Display | 44 / 52 | Semibold |
| Title | Variable Display | 28 / 36 | Semibold |
| Section | Variable Display | 20 / 28 | Semibold |
| Card number | Variable Display | 22 / 28 | Semibold, tnum |
| Body | Variable Text | 14 / 20 | Regular |
| Body strong | Variable Text | 14 / 20 | Semibold |
| Caption | Variable Text | 12 / 16 | Regular |
| Overline | Variable Small | 11 / 16 | Semibold, +6% tracking, uppercase |

## 4. Space, shape, elevation
- **Spacing grid:** 4 px. Steps 4, 8, 12, 16, 24, 32, 48. Card padding 20, page margins 32, nav rail 76 px.
- **Radius:** 6 for chips and inputs, 8 for buttons, 12 for cards, 16 for hero panels. Rings and bars have rounded caps.
- **Elevation:**
  - E0 is flat on `base`.
  - E1 is a card: `surface1` + 1 px `stroke`.
  - E2 is hover/raised: `surface2` + a soft shadow, 0 4 16 at 24% black (dark) or 8% (light).
  - E3 is the toast or command palette: `surface2` + shadow 0 12 32 at 40% / 16%.

## 5. Motion
| Name | Duration | Curve | Used for |
|---|---|---|---|
| quick | 120 ms | cubic-bezier(0.2, 0, 0, 1) | hover, press |
| standard | 200 ms | cubic-bezier(0.2, 0, 0, 1) | tab change (fade + 8 px slide), card lift |
| emphasized | 320 ms | cubic-bezier(0.05, 0.7, 0.1, 1) | sunburst unfold, drill-in |
| ticker | 600 ms | ease-out | number count-up |

Motion is disabled when Windows "Animation effects" is off (`SPI_GETCLIENTAREAANIMATION`).

## 6. Icons
Segoe Fluent Icons (falls back to Segoe MDL2 Assets), 16 px in lists and 20 px in the nav rail. A single line weight; no emoji.
Nav: Overview `E80F` (home), Explore `E8B7` (folder), Map `E9D9` (chart), Clean up `E74D` (delete), System `E770` (system),
Disk internals `EDA2` (hard drive), History `E81C` (history), Settings `E713`. Lock badge `E72E`.

## 7. Components
- **Nav rail:** icon above label, accent pill on the selected item, keyboard Ctrl+1…8.
- **Hero meter:** sunburst with free space as a neutral ring segment. The centre shows "C: · 512 MB free" and its state.
- **Reclaim meter:** a stacked horizontal bar by safety level (Safe / Review / Caution) with the total as the hero number.
- **Insight card:** icon, title, one-line explanation, size + share, and a safety badge. The whole card is clickable (opens the review list).
- **Review list:** checkbox rows, safety badge, running "Selected: 3.4 GB" bar with Recycle / Quarantine / Compress.
- **Toast:** bottom centre, E3, with an action button (Undo). Auto-hides after 10 s and pauses on hover.
- **States:** every panel has empty, loading (skeleton shimmer) and error (caution icon + plain sentence + retry) states.

## 8. Rescue Mode
The base stays the same. The hero ring's used part switches to `caution`, and a slim `caution` banner appears under the title bar.
The primary action becomes "Release reserve (+512 MB)", then "Free space safely". Copy uses short sentences and no exclamation marks.
