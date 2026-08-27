# Threading and Export Specification
- Audio thread: ring-buffer write only.
- Message thread: UI interaction and lightweight state changes.
- Worker pool: phrase detection, zero-cross search, fades, normalization, waveform thumbnails, WAV writing.
- Drag export: create a stable temporary WAV before beginning the OS drag operation.
- Cleanup: delete abandoned temporary files on startup and graceful shutdown.
- Session restore: store metadata, not raw audio, unless the user explicitly saves a capture.
