# Vexara Integrated Terminal — Architecture

## Layers

| Layer | Classes | Responsibility |
|-------|---------|----------------|
| Transport | `WinConPty`, `PtyReaderThread` | ConPTY lifecycle, pipe I/O, process exit |
| Session | `TerminalSession` | Connect transport, parser, and UI signals on the main thread |
| Emulator | `VtParser`, `TerminalScreen` | UTF-8/VT byte stream to screen buffer |
| View | `PlainTextTerminalRenderer` | `TerminalScreen` to `QPlainTextEdit` |
| Input | `TerminalInputEncoder` | `QKeyEvent` to PTY bytes |
| Shell UI | `TerminalDock`, `TerminalPanel` | Profiles toolbar and widget hosting |

## Threading

- `PtyReaderThread` blocks on `ReadFile` for the ConPTY output pipe.
- Chunks are delivered to the UI thread via `bytesReceived(QByteArray)`.
- Parsing and rendering run only on the UI thread.

## Line-bridge fallback

When ConPTY is unavailable, `TerminalPanel` uses `QProcess` line mode (legacy). The VT emulator is not used on that path.
