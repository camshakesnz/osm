# Chirp measurement

Measure with a set of 1–6 log-sweep chirps instead of continuous noise:
fire a set, look at the result, change the DSP, fire again.

## Use

1. **Generator** → type **Chirp**. Set the sweep length (default 1 s), start/end
   frequency (default 20 Hz – 20 kHz) and the number of chirps (1–6).
   Use the generator gain as usual.
2. **Measurement** → mode **Chirp** or **Chirp LTW** (the mode list that also holds 10…16 / LTW).
   Reference and measurement channels work as normal, including the generator loop.
3. Switch the generator **on**. It plays the set and **switches itself off** when it's done.
   The measurement panel shows the progress (*waiting… / capturing 2/3 / done 3/3*), and
   the trace updates after every chirp.
4. With **auto store** ticked (the default, on the measurement panel in chirp modes), the
   result is stored as a trace when the set finishes, named with the time
   (e.g. *Measurement @ 18:05*). Untick it to store by hand. Change the DSP and fire again
   to verify.

Resetting the average (or clicking the status text) gets ready for a new set without
replaying the generator, for example when the chirps come from somewhere else.

From the iPad remote: turning the generator on fires the set and Store stores it,
the same as on the laptop.

## Modes

| Mode | Gate | Use |
|---|---|---|
| **Chirp** | one long gate (the 1.5 s tail) | highest resolution; shows room modes and reflections |
| **Chirp LTW** | frequency dependent (~28 cycles at 20 Hz to ~88 at 20 kHz, the same as OSM's LTW mode) | rejects late reflections and noise at mid/high frequencies; closest to Live-Capture's Multi-Win |

Both gates **open at the first arrival**, not the biggest peak: the earliest point within
100 ms before the peak that comes within 15 dB of it. So a strong reflection or room mode
can't pull the gate or the delay late. The estimated delay (and the *apply delay* button)
uses the same first arrival.

## What you get

- Magnitude, phase, group delay (from phase), impulse response, and coherence.
- Coherence is the agreement *between chirps*, so it needs **2 or more** chirps
  (one chirp shows 1.0 everywhere).
- With **3 or more** chirps, a chirp that disagrees strongly with the others in a band
  (a bang, a door, someone talking) is left out of that band.
- Harmonic distortion products are gated out of the response.
- Mic calibration files apply as usual.

## How it works

Each chirp is detected on the reference channel and captured with 0.1 s before it
and the sweep plus 1.5 s after it. It is deconvolved against its own reference,
h = IFFT(Y/X), with a minimum-phase band limit to the swept range. The impulse
responses are gated around the first arrival. This removes the region before the
arrival, where the sweep's harmonic products land, and most of the deconvolved noise.
The gated responses are then averaged as complex numbers across chirps. The result
is shown at raw FFT resolution at low frequencies and in 1/96-octave bands above that.

Timing: one chirp period is sweep + 1.5 s tail + 0.35 s guard. A 1 s, 3-chirp set
takes about 9 s.

Code: `src/math/chirpsignal.h` (shared stimulus definition), `src/math/chirpanalyzer.*`
(capture and analysis), `src/generator/chirp.*` (generator), and the chirp sections of
`src/source/measurement.cpp`. Simulation tests are in `tests/chirp/chirp_test.cpp`:

    g++ -O2 -std=c++17 -Isrc tests/chirp/chirp_test.cpp src/math/chirpanalyzer.cpp -o chirp_test && ./chirp_test

# Pop-out windows

Any chart can be opened in its own window, for example the **numeric meters on a
second display** while the main window keeps the graphs.

- Click **↗** in the bottom left corner of a chart (next to its ⚙) to open that chart in a new window, with the
  same settings (for Numeric: the same meter layout and sources).
  Or **View → New window (meters)** (Ctrl+Shift+M) opens a fresh Numeric window.
- A new window opens on another display if there is one.
- The **⤢** button in the bottom right corner (or F11) toggles full screen; Esc leaves full screen.
- The chart type and its settings can be changed inside the window as normal; clicking
  a meter opens its settings, and a chart's ⚙ opens its settings, in the main window.
- Open windows, with their size, position, full screen state and settings, come back on
  the next start. Closing a window forgets it.

# Loudness

Chart type **Loudness**: stream/programme loudness to EBU R 128 (ITU-R BS.1770-4),
like a simple loudness meter plug-in, inside OSM.

- **Settings:** click the chart; its settings appear in the bottom bar: **measurement**,
  **channels** (Stereo/Mono), **target** and **reset**. The chart itself shows the
  measurement's name, the channel mode and the target as a heading.
- **Source:** the measurement's **measurement channel is Left** and its **reference channel
  is Right**, so set those to the stream's L and R inputs. **Mono** uses the measurement
  channel only. The measurement must be switched on.
- **Bars:** sample peak per channel in dBFS (-60 to 0) with a 2 s peak hold. Green below
  -18, amber to -6, red above. Under each bar: the maximum **true peak** (dBTP) since reset,
  red above -1 dBTP.
- **Numbers:** **Short-term** (3 s) and **Integrated** (gated programme loudness) in LUFS.
  Underneath: **Momentary** (400 ms), loudness **Range** (LRA, LU), **True peak max**, and
  measuring **time**.
- **Target:** -14, -16, -18, -23 or -24 LUFS. Integrated turns green within 1 LU of the
  target and amber within 3 LU.
- **Reset** starts a new programme: it clears integrated, range, true peak and time.
  Switching Stereo/Mono also starts a new one.
- Loudness is measured only while a Loudness chart is showing that measurement (including
  in a pop-out window). After it has been hidden for more than 5 s, it starts fresh next time.
- The chart (source and target) can be popped out like any other chart.

Verified against the EBU Tech 3341 (loudness, gating) and Tech 3342 (range) test cases at
44.1, 48 and 96 kHz: `tests/loudness/loudness_test.cpp`.

# Layout

The window is laid out in the style of Smaart:

- **Left: data bar.** Captured (stored) traces, folders, averages/math, filters and EQ, with
  buttons for *Folder*, *Import*, *Open* and *Save* along the bottom and *Capture* (store every
  active measurement, Ctrl+X) at the top. Double-click a folder to open it; while a folder is
  open, new captures (including chirp auto-store) go into it. *‹ Back* returns to the top level.
  Hide or show the bar with *Data* (top right), Ctrl+B or View → Data bar. It hides itself when
  the window is narrower than about 980 px.
- **Right: live measurements and the generator.** Each measurement block shows M (measurement)
  and R (reference) input meters, and quick controls:
  - **delay** in ms: click to type a value, or scroll (Shift scrolls in 0.01 ms steps);
  - **Find**: set the delay to the current estimate (the tooltip shows it);
  - **Track**: follow the delay automatically. It only moves the delay when the estimate stands
    well clear of the noise and the last two estimates agree (within 0.25 ms); the button is amber
    while it waits for that and green when locked. In chirp modes it follows each completed set.
  - **gain** − / + (1 dB, Shift 0.1 dB), or type/scroll the value.
  The signal generator block sits at the bottom: type, On/Off (Ctrl+G) and level − / +
  (Shift: 6 dB steps).
- **Settings** open in a floating window from the **⚙** buttons (each measurement, the generator,
  each chart's bottom-left corner, folders). Drag it by its title bar; Esc or ✕ closes it. While
  it is open, clicking another chart or measurement switches it to that item. *Dock* puts the
  settings back along the bottom of the window permanently (*Float* undoes it).

## Views

The buttons at the top right are views. Each view keeps its own charts: how many, their types and
all their settings. Changes are saved as you make them, so switching to another view and back (or
restarting) brings back the view exactly as you left it. Two views are made for you:

- **Live**: three SPL meters along the top, spectrum (RTA, filled) in the middle, spectrograph at the bottom;
- **TF**: impulse at the top, magnitude in the middle, phase at the bottom.

Right-click *Live* or *TF* and choose **Reset to default** to put it back to that layout. If you
had charts set up before views existed, they are kept as a third view, *My layout*. *+* adds a view
(a copy of the current one); right-click a view to rename, duplicate or delete it. Space flips
between the last two views and Alt+1…6 picks a view. Ctrl+1/2/3 set the number of charts in
the current view. When the window grows, the view's biggest chart takes the extra height.

## Spectrograph colours

The spectrograph now uses heat colours by default, in deep Smaart-like shades: nothing (black)
below the *min* level, then navy and blue, green at *mid*, yellow, red, and white at *max*. Set min/mid/max in the chart's settings to
suit the level of what you are measuring. *colours: classic* brings back the original look.

## Filled spectrum

In the spectrum (RTA) chart's settings, **fill** (line mode) fills the area under each trace
in a slightly darker shade of its colour, with the line on top, like Smaart. Each trace is drawn
in list order, so a later trace covers an earlier one where it is lower.

## Dark mode

Dark mode (View → Dark Mode, Ctrl+D) uses near-black panels and black SPL meters, closer to
Smaart's dark look.

## Smoothing per chart

Every frequency chart (spectrum, magnitude, phase, coherence, group/phase delay, spectrograph,
crest factor) has its own smoothing, shown in its bottom-right corner, e.g. **1/12 oct ▾**. Click it
to choose 1/1 … 1/48 octave (or *raw* for the spectrum). It only changes that chart, and is saved
with the view, so the magnitude can be 1/6 oct while the phase stays at 1/24, and the Live and TF
views keep their own. (It is the same setting as *ppo*, points per octave, in the chart's settings.)
The measurement's own settings (average type, LPF/FIFO, FFT size, window) are time averaging and
resolution of the analysis, shared by every chart showing that measurement.
