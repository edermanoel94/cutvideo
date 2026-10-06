# cutvideo

A C program that extracts clips from longer video files without re-encoding.
It reads a JSON configuration file describing which clips to extract (with start and end timestamps) and writes one `.mp4` file per clip.

## How it works

`cutvideo` uses FFmpeg's libav libraries to remux packets directly, so no decoding or re-encoding takes place.
This makes extraction fast and lossless.

For each clip it:

1. Seeks to the nearest keyframe at or before the requested start time.
2. Waits for the first video keyframe and uses it as the real start of the clip.
3. Copies video, audio and subtitle packets (other stream types are skipped) until the end time is reached.
4. Shifts timestamps so the clip starts at t=0 and writes a valid MP4 container.

> Because cuts happen on keyframes, a clip may begin slightly before the requested `startTime`.

For an in-depth walkthrough of the libav concepts used here (in Portuguese), see the [AVCODEC tutorial](tutorial%20de%20AVCODEC.md).

## Dependencies

| Library | Purpose |
|---|---|
| `libavformat` | Container demuxing and muxing (FFmpeg) |
| `libavcodec` | Codec parameter handling (FFmpeg) |
| `libavutil` | Timestamp math, memory helpers (FFmpeg) |
| `json-c` | JSON parsing |

The build also needs `gcc`, `make` and `pkg-config`.

### Installing dependencies

**Arch Linux**

```sh
sudo pacman -S base-devel pkgconf ffmpeg json-c
```

**Ubuntu / Debian**

```sh
sudo apt install build-essential pkg-config libavformat-dev libavcodec-dev libavutil-dev libjson-c-dev
```

**macOS (Homebrew)**

```sh
brew install pkg-config ffmpeg json-c
```

## Build

```sh
make release   # optimised build (-O2)
make debug     # debug build (-g -O0), also the default target of `make`
make clean     # remove the binary
```

> `compile_flags.txt` exists only for clangd LSP support.
> It is not used for building.

## Usage

```sh
./cutvideo <input.json>
```

Output files are written as `<clip.name>.mp4` in the current working directory.
If a clip fails to be extracted, the remaining clips of the same video are skipped.

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

The top-level array supports multiple video entries.
Each entry requires:

| Field | Description |
|---|---|
| `title` | Human-readable label (not used at runtime) |
| `inputVideoPath` | Path to the source video file (relative paths are resolved from the current working directory) |
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
      { "name": "demo",    "startTime": "5:30", "endTime": "12:00" }
    ]
  }
]
```

Run:

```sh
./cutvideo clips.json
```

This produces `opening.mp4` and `demo.mp4` in the current directory.

## TODO

- [ ] Replace whitespace with underscores in clip names.
- [ ] Add an output directory option.
