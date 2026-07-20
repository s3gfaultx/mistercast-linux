# MiSTerCast

Low-latency Wayland desktop and PipeWire audio streaming to the Groovy_MiSTer core.

The GUI supports selectable NTSC and PAL CRT modelines from 256x240p through 720x576i. The project is currently intended for private/local use because the source MiSTerCast repository does not include a redistribution license.

## Build

Arch Linux dependencies:

```sh
sudo pacman -S --needed cmake ninja qt6-base pipewire wireplumber lz4 pkgconf \
  xdg-desktop-portal xdg-desktop-portal-kde
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

Qt Multimedia is deliberately not used. Source selection goes through the XDG ScreenCast portal, after which the application consumes the selected PipeWire node directly.

For a user-local SteamOS/Arch installation:

```sh
cmake --install build --prefix "$HOME/.local"
```

Ensure `$HOME/.local/bin` is in `PATH`. The desktop entry and scalable icon are installed under `$HOME/.local/share`.

A local Arch package can also be built from the checkout:

```sh
makepkg -D packaging -si
```

## Hardware Pattern Test

Start the Groovy_MiSTer core, connect the MiSTer over Gigabit Ethernet, then run:

```sh
./build/mistercast --pattern 192.168.1.50
```

The test uses the default 320x240p60 modeline and displays color bars with a square that changes every frame. Stop it with `Ctrl+C`.

Use `./build/mistercast --tone 192.168.1.50` to include a 440 Hz test tone. Network audio must be enabled in the core OSD.

## GUI

Run `./build/mistercast`, choose **Select Desktop Source**, and approve a monitor or window in the compositor portal. Enter the MiSTer address and start streaming. With diagnostics enabled, the GUI reports the median, p95, and maximum age since PipeWire dequeue for newly captured frames, plus the number of output frames that reused an unchanged capture. Capture processing, preflight-ready wait, compression, UDP-capacity wait, and socket-submission time are also reported. Frame age and the component timings are complementary measurements and should not be added together.

Choose the CRT output mode before streaming. On first launch, the bundled presets are copied to the editable user file at `~/.config/MiSTerCast/MiSTerCast/modelines.dat`. Use **Edit File** to open it and **Reload** after making changes. Invalid lines are skipped with a message in the status log. Changing or reloading the selected mode while streaming performs a synchronous transport restart so frames from different modes cannot be mixed.

Interlaced modes alternate odd and even source lines into field-sized network payloads and synchronize the field phase with MiSTer status acknowledgements. Timing values shown in the GUI are read-only and come directly from the selected modeline.

The source is center-cropped to 4:3 by default. Alignment, offsets, and rotation can be changed without restarting capture. The streaming path retains only the newest frame; it never queues stale video to improve apparent smoothness.

Network audio must also be enabled in the Groovy_MiSTer core OSD. The GUI warns if MiSTer acknowledgements report audio disabled.

Enable **Show diagnostics** to display detailed latency, audio, transport, and FPGA metrics and print their periodic values to the terminal. The setting is disabled by default; connection messages, warnings, and errors remain visible. Audio capture starts with a 256-frame (5.33 ms) jitter reserve, lowers it after sustained full reads, and raises it immediately after an underflow, within a 128-512 frame range. The reserve remains available to absorb field-timing corrections rather than being held back from packet reads. `underflow` means the PipeWire ring did not contain enough samples and the remainder was zero-filled. `stale/overflow` reports captured samples discarded to bound latency or overwritten because the ring filled. `UDP peak` and `pacing overruns` show transport pressure. These counters are cumulative for the current stream.

The FPGA delivery line reports status samples where MiSTer's fallback framebuffer was active, its VRAM stream was unsynchronized, or its pixel queue was empty. Video transmission reuses the latest capture without waiting at each field boundary and keeps every frame in the negotiated LZ4 format. Delivery starts with a half-field reserve and slowly reduces it to no less than three-eighths of a field after sustained healthy FPGA feedback; any fallback, unsynchronized, or queue-empty report immediately restores the half-field reserve.

## Runtime Requirements

- Wayland compositor with XDG ScreenCast portal support
- PipeWire and a running session manager such as WirePlumber
- Wired Gigabit Ethernet required; both ends and every intervening switch port must negotiate 1000 Mb/s full duplex
- Groovy_MiSTer core listening on UDP port 32100

The currently active desktop compositor determines how early a rendered frame becomes available to PipeWire. Application metrics therefore measure from PipeWire delivery onward; use a high-speed camera showing both source and CRT for capture-to-photon measurements.

## Troubleshooting

- If MiSTer does not acknowledge the stream, verify its address, the Groovy_MiSTer core, UDP port 32100, firewall rules, and VLAN/routing configuration.
- If the source chooser does not open, verify that `xdg-desktop-portal` and the compositor-specific backend are running.
- If output audio is missing, check the default PipeWire sink with `wpctl status`, verify that a session manager such as WirePlumber is running, and enable network audio in the core OSD.
- If diagnostics show UDP queue growth, repeated drops, pacing overruns, fallback, unsynced, or queue-empty samples, verify both Ethernet ports with `ethtool`. A damaged cable or 100 Mb/s switch port cannot sustain high-resolution video payloads.
