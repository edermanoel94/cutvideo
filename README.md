# cutvideo

A C program that extracts video clips from longer video files without re-encoding. It reads a JSON configuration file describing which clips to extract (with start/end timestamps) and produces individual `.mp4` files per clip.

## How it works

`cutvideo` uses FFmpeg's libav libraries to remux packets directly — no decoding or re-encoding occurs. This makes extraction fast and lossless. It seeks to the nearest keyframe before the requested start time, filters packets by timestamp, normalises timestamps so each clip starts at t=0, and writes a valid MP4 container.

## Dependencies

| Library | Purpose |
|---|---|
| `libavformat` | Container demuxing and muxing (FFmpeg) |
| `libavcodec` | Codec parameter handling (FFmpeg) |
| `libavutil` | Timestamp math, memory helpers (FFmpeg) |
| `json-c` | JSON parsing |

### Installing dependencies

**Arch Linux**
```sh
sudo pacman -S ffmpeg json-c
```

**Ubuntu / Debian**
```sh
sudo apt install libavformat-dev libavcodec-dev libavutil-dev libjson-c-dev
```

**macOS (Homebrew)**
```sh
brew install ffmpeg json-c
```

## Build

There is no Makefile. Compile with gcc:

```sh
gcc main.c -o cutvideo -lavformat -lavcodec -lavutil -ljson-c
```

> `compile_flags.txt` exists only for clangd LSP support — it is not used for building.

## Usage

```sh
./cutvideo <input.json>
```

Output files are written as `<clip.name>.mp4` in the current working directory.

## JSON configuration

```json
[
  {
    "title": "My Video",
    "inputVideoPath": "/path/to/video.mp4",
    "clips": [
      { "name": "intro",     "startTime": "10.5",    "endTime": "30.0"    },
      { "name": "highlight", "startTime": "1:05",    "endTime": "1:45"    },
      { "name": "ending",    "startTime": "1:10:30", "endTime": "1:15:00" }
    ]
  }
]
```

The top-level array supports multiple video entries. Each entry requires:

| Field | Description |
|---|---|
| `title` | Human-readable label (not used at runtime) |
| `inputVideoPath` | Absolute path to the source video file |
| `clips` | Array of clip definitions |

Each clip requires:

| Field | Description |
|---|---|
| `name` | Output filename (without `.mp4`) |
| `startTime` | Start timestamp (see formats below) |
| `endTime` | End timestamp (see formats below) |

### Accepted time formats

| Format | Example | Interpreted as |
|---|---|---|
| Decimal seconds | `"90.5"` | 90.5 s |
| MM:SS | `"1:30"` | 90.0 s |
| MM:SS.ms | `"1:30.5"` | 90.5 s |
| HH:MM:SS | `"1:00:30"` | 3630.0 s |
| HH:MM:SS.ms | `"1:00:30.5"` | 3630.5 s |

## Example

Given `clips.json`:

```json
[
  {
    "title": "Conference Talk",
    "inputVideoPath": "/home/user/Videos/talk.mp4",
    "clips": [
      { "name": "opening", "startTime": "0.0",  "endTime": "120.0" },
      { "name": "demo",    "startTime": "5:30",  "endTime": "12:00" }
    ]
  }
]
```

Run:

```sh
./cutvideo clips.json
```

This produces `opening.mp4` and `demo.mp4` in the current directory.
