# wsplugin — GStreamer WebSocket Source & Sink

A GStreamer plugin that lets you publish and receive binary media frames (JPEG, H.264, raw video, anything) over WebSocket connections. Drop `wssrc` or `wssink` anywhere in a pipeline and stream to or from any WebSocket endpoint.

---

## Overview

| Element | Base class | Role |
|---------|-----------|------|
| `wssrc` | `GstPushSrc` | Receives WebSocket binary messages and pushes them as GStreamer buffers |
| `wssink` | `GstBaseSink` | Takes GStreamer buffers and sends each one as a binary WebSocket message |
| `wsmultifilesink` | `GstBin` | Wraps `multifilesink` and adds timestamp-aware file naming templates |

Both elements support two connection modes:

- **client** — connects outbound to a `ws://` or `wss://` URI
- **server** — listens on a TCP port for incoming connections

> **Note**
> `wssrc` server mode accepts exactly **one** sender client. Additional connections are refused.
> `wssink` server mode accepts **multiple** viewer clients simultaneously and broadcasts each frame to all of them. Frames are dropped (not queued) for clients that fall behind, so the pipeline is never stalled by a slow viewer.

---

## Requirements

| Dependency | Version | Install |
|-----------|---------|---------|
| GStreamer | ≥ 1.20 | `brew install gstreamer` |
| libwebsockets | ≥ 4.x | `brew install libwebsockets` |
| OpenSSL | ≥ 3.x | `brew install openssl@3` |
| CMake | ≥ 3.16 | `brew install cmake` |

---

## Build

```bash
cmake -S . -B build
cmake --build build
```

The plugin is built as `build/gstwsplugin.so`.

---

## Test

Configure and run the native test suite:

```bash
cmake -S . -B build -DBUILD_TESTING=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

Generate the coverage report with the same build directory:

```bash
CC=gcc cmake -S . -B build -DBUILD_TESTING=ON -DENABLE_COVERAGE=ON
cmake --build build
cmake --build build --target coverage
```

The coverage target writes `build/coverage.txt` and `build/coverage.xml` and fails if the production code in `src/` drops below 100% line coverage.

---

## Install

### Option A — session only (no copy needed)

```bash
export GST_PLUGIN_PATH=$PWD/build
```

### Option B — permanent user install

```bash
cmake --install build
# installs to ~/.local/share/gstreamer-1.0/plugins/
```

### Option C — system-wide

```bash
sudo cmake --install build --prefix /usr/local
```

Verify the plugin loaded:

```bash
gst-inspect-1.0 wssrc
gst-inspect-1.0 wssink
gst-inspect-1.0 wsmultifilesink
```

---

## Element Reference

### Common properties

Both `wssrc` and `wssink` share the same connection properties:

| Property | Type | Default | Description |
|----------|------|---------|-------------|
| `mode` | enum | `client` | `client` — connect outbound; `server` — listen for connections |
| `uri` | string | `ws://localhost:8765` | WebSocket URI to connect to *(client mode)* |
| `port` | int | `8765` | TCP port to listen on *(server mode)* |
| `connect-timeout` | int | `10` | Seconds to wait for a connection before failing *(client mode)* |

### `wssrc` — WebSocket Source

Receives binary WebSocket messages and delivers each complete message as a single `GstBuffer`. Large messages fragmented by the WebSocket layer are transparently reassembled before handing off to GStreamer.

The caps advertised on the src pad are negotiated with the downstream element — if the next element is `jpegdec` the pad will carry `image/jpeg`; if it is `fakesink` or a generic element the pad carries no specific caps. You can also force caps explicitly with a capsfilter.

### `wssink` — WebSocket Sink

Receives a `GstBuffer` from upstream and sends its entire contents as a single binary WebSocket message. In server mode it maintains a per-client send queue (depth 16) and drops frames for any client that falls more than 16 frames behind.

### `wsmultifilesink` — Enhanced Multi-file Sink

`wsmultifilesink` preserves the stock `multifilesink` rolling-file behavior and adds timestamp-aware output naming through a declarative template.

Core rolling properties are mirrored directly:

| Property | Type | Default | Description |
|----------|------|---------|-------------|
| `location` | string | `%05d` | Standard `multifilesink` location pattern used when `location-template` is not set |
| `index` | int | `0` | Starting file index |
| `next-file` | enum | `buffer` | File rollover strategy |
| `max-files` | uint | `0` | Maximum number of files to retain |
| `max-file-size` | uint64 | `2147483648` | Maximum file size before rollover |
| `max-file-duration` | uint64 | `GST_CLOCK_TIME_NONE` | Maximum file duration before rollover |
| `aggregate-gops` | bool | `false` | Keep GOPs together for key-frame-based splitting |
| `post-messages` | bool | `false` | Re-post `GstMultiFileSink` file-written messages from `wsmultifilesink` |

Additional naming properties:

| Property | Type | Default | Description |
|----------|------|---------|-------------|
| `location-template` | string | unset | Template for generated output paths |
| `timestamp-utc` | bool | `false` | Use UTC instead of local time for timestamp expansion |

Supported `location-template` tokens:

- `{timestamp}` → current file creation time formatted as `%Y%m%dT%H%M%S`
- `{timestamp:%Y-%m-%d_%H-%M-%S}` → custom `strftime`-style timestamp format
- `{index}` → current file index
- `{index:05}` → zero-padded file index width
- `{{` and `}}` → literal braces

Example:

```bash
gst-launch-1.0 \
  wssrc mode=client uri=ws://127.0.0.1:8765 ! \
  wsmultifilesink next-file=max-duration max-file-duration=5000000000 \
    location-template="capture-{timestamp:%Y%m%d-%H%M%S}-{index:03}.mjpeg"
```

`wsmultifilesink` follows the same container limitations as `multifilesink`: it is suitable for independently decodable buffers or streamable container formats. For independently playable MP4 segment files, prefer `splitmuxsink`.

---

## Pipeline Cookbook

### 1. Sender (server) → Receiver (client)

The most common pattern: one process streams, another consumes.

**Sender** — runs a WebSocket server, streams a test pattern encoded as JPEG:

```bash
gst-launch-1.0 \
  videotestsrc is-live=true ! videoconvert ! jpegenc ! \
  wssink mode=server port=8765
```

**Receiver** — connects as a client, decodes and displays:

```bash
gst-launch-1.0 \
  wssrc mode=client uri=ws://127.0.0.1:8765 ! \
  jpegdec ! videoconvert ! autovideosink
```

---

### 2. Receiver (server) → Sender (client)

Flip the roles: the receiver hosts the server and the sender dials in. Useful when the sender is behind a NAT.

**Receiver** — listens for one sender:

```bash
gst-launch-1.0 \
  wssrc mode=server port=8765 ! \
  jpegdec ! videoconvert ! autovideosink
```

**Sender** — connects to the receiver and pushes frames:

```bash
gst-launch-1.0 \
  videotestsrc is-live=true ! videoconvert ! jpegenc ! \
  wssink mode=client uri=ws://192.168.1.10:8765
```

---

### 3. Broadcaster (server) → Multiple viewers (clients)

One `wssink` server — unlimited clients. Each viewer gets best-effort delivery; slow viewers drop frames independently without affecting others.

**Broadcaster:**

```bash
gst-launch-1.0 \
  videotestsrc is-live=true ! videoconvert ! jpegenc ! \
  wssink mode=server port=8765
```

**Viewer 1, Viewer 2, … (run as many as you want):**

```bash
gst-launch-1.0 \
  wssrc mode=client uri=ws://192.168.1.10:8765 ! \
  jpegdec ! videoconvert ! autovideosink
```

---

### 4. Webcam → JPEG over WebSocket → Decode → Display

Stream a real camera across a network.

**Camera host:**

```bash
gst-launch-1.0 \
  v4l2src ! videoconvert ! jpegenc quality=80 ! \
  wssink mode=server port=8765
```

**Viewer:**

```bash
gst-launch-1.0 \
  wssrc mode=client uri=ws://192.168.1.10:8765 ! \
  jpegdec ! videoconvert ! autovideosink
```

> **Tip — macOS camera**
> Replace `v4l2src` with `avfvideosrc` on macOS:
> ```bash
> avfvideosrc ! videoconvert ! jpegenc quality=80 ! wssink mode=server port=8765
> ```

---

### 5. Re-stream: WebSocket → GStreamer → WebSocket

Chain two pipelines together: receive a stream on one WebSocket and forward it to another, applying any GStreamer transform in between.

**Re-broadcaster** — receives JPEG from an upstream source, transcodes to lower quality, re-broadcasts:

```bash
gst-launch-1.0 \
  wssrc mode=client uri=ws://upstream-host:8765 ! \
  jpegdec ! videoconvert ! jpegenc quality=40 ! \
  wssink mode=server port=9000
```

---

### 6. Record a WebSocket stream to disk

```bash
gst-launch-1.0 \
  wssrc mode=client uri=ws://192.168.1.10:8765 ! \
  filesink location=capture.mjpeg
```

Play it back later:

```bash
gst-launch-1.0 \
  filesrc location=capture.mjpeg ! \
  jpegparse ! jpegdec ! videoconvert ! autovideosink
```

Split the stream into timestamped files instead:

```bash
gst-launch-1.0 \
  wssrc mode=client uri=ws://192.168.1.10:8765 ! \
  wsmultifilesink next-file=max-duration max-file-duration=5000000000 \
    location-template="capture-{timestamp:%Y%m%d-%H%M%S}-{index:03}.mjpeg"
```

---

### 7. Measure raw throughput (no decode)

Use `fakesink` to receive buffers without doing any video work — useful for benchmarking the WebSocket transport layer:

```bash
# Sender
gst-launch-1.0 \
  videotestsrc is-live=true ! videoconvert ! jpegenc ! \
  wssink mode=server port=8765

# Receiver — just count buffers
gst-launch-1.0 \
  wssrc mode=client uri=ws://127.0.0.1:8765 ! \
  fakesink sync=false
```

---

### 8. Stream to a browser

A browser WebSocket client is included at `utils/wsclient.html`. It connects to any `wssink` server broadcasting JPEG frames and renders them as a live video stream — no plugins, no dependencies.

**GStreamer sender:**

```bash
gst-launch-1.0 \
  videotestsrc is-live=true ! videoconvert ! jpegenc quality=85 ! \
  wssink mode=server port=8765
```

**Open in browser:**

```
utils/wsclient.html?host=127.0.0.1&port=8765
```

Or with a remote host:

```
utils/wsclient.html?host=192.168.1.10&port=8765
```

The page connects automatically on load and renders each incoming binary WebSocket message as a JPEG image.

> **Note** — open the file directly with `file://` or serve it from any static HTTP server (e.g. `python3 -m http.server`). The browser must be able to reach the WebSocket host.

---

### 9. H.264 stream over WebSocket

Not limited to JPEG — any encoded format works.

**Sender:**

```bash
gst-launch-1.0 \
  videotestsrc is-live=true ! videoconvert ! \
  x264enc tune=zerolatency ! \
  wssink mode=server port=8765
```

**Receiver:**

```bash
gst-launch-1.0 \
  wssrc mode=client uri=ws://127.0.0.1:8765 ! \
  h264parse ! avdec_h264 ! videoconvert ! autovideosink
```

---

## Debugging

Enable per-element debug output using `GST_DEBUG`:

```bash
# Info level — connection events
GST_DEBUG=wssrc:4,wssink:4 gst-launch-1.0 ...

# Full trace — includes every buffer and receive event  
GST_DEBUG=wssrc:7,wssink:7 gst-launch-1.0 ...
```

GStreamer debug levels: `1`=ERROR `2`=WARN `3`=FIXME `4`=INFO `5`=DEBUG `6`=LOG `7`=TRACE

---

## Continuous Integration

GitHub Actions runs the build, test suite, and coverage gate on every push and pull request. The workflow uses the same CMake targets documented above, so local verification and CI stay aligned.

---

## Architecture Notes

### Threading model

Each element runs one background thread dedicated to the libwebsockets service loop (`lws_service`). This thread owns all LWS state.

- **wssrc**: LWS thread → `GAsyncQueue` → GStreamer streaming thread (`create()`)
- **wssink**: GStreamer streaming thread (`render()`) → `GAsyncQueue` → LWS thread (writable callback)

Cross-thread wakeup uses `lws_cancel_service()`, which is safe to call from any thread.

### Message framing

Each GStreamer buffer maps 1:1 to one WebSocket binary message. The WebSocket layer is transparent — `wssrc` reassembles any messages that libwebsockets delivers in multiple chunks before handing the complete buffer to GStreamer.

### Slow client handling

`wssink` server mode maintains a send queue of up to **16 frames** per connected client. If a client's queue is full when a new frame arrives, that frame is silently dropped for that client. Other clients and the upstream pipeline are not affected.

---

## Project Structure

```
wsplugin/
├── CMakeLists.txt          # Build definition
└── src/
    ├── gstwsplugin.h       # Shared types: GstWsMode enum, defaults
    ├── gstwsplugin.c       # Plugin entry point, GstWsMode GType
    ├── gstwssrc.h          # wssrc class declaration
    ├── gstwssrc.c          # wssrc implementation
    ├── gstwssink.h         # wssink class declaration
    └── gstwssink.c         # wssink implementation
```

---

## Known Issues

### Browser `wsclient.html` requires explicit subprotocol

`new WebSocket(url)` with no second argument sends **no** `Sec-WebSocket-Protocol` header. libwebsockets 4.x rejects this connection silently — `LWS_CALLBACK_ESTABLISHED` is never called and the browser shows an immediate connection error.

**Workaround:** pass `"wsplugin"` as the subprotocol in the `WebSocket` constructor. `wsclient.html` already does this:

```js
const socket = new WebSocket(wsUrl, ["wsplugin"]);
```

If you are writing your own browser client, you must include the same second argument. Any standard WebSocket client library (Node.js `ws`, Python `websockets`, etc.) that lets you specify the subprotocol will also work by passing `"wsplugin"`.

**Root cause:** libwebsockets routes incoming WebSocket upgrades by matching the `Sec-WebSocket-Protocol` request header against the registered protocol names. When the browser sends no header, LWS finds no match and drops the connection. The LWS 4.x API does not expose a clean way to register a true catch-all entry; the `"default"` and `"http"` fallback names that the documentation hints at do not reliably intercept no-subprotocol WebSocket upgrades in practice.

---

## License

LGPL-2.1 — same as the GStreamer framework itself.
