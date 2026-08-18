# Linux GTK3 chat shell (exploration)

Isolated native host view. No ChatComponent, Æther, networking, persistence, or threads.

## GTK widgets used

- `GtkWindow` — application window titled `App Traverse Chat`
- `GtkBox` — vertical layout plus local-UID, remote-UID, and message rows
- `GtkLabel` — Local UID / Remote UID captions
- `GtkEntry` — read-only local UID, remote UID, message input
- `GtkButton` — Add and Send
- `GtkScrolledWindow` — transcript viewport
- `GtkTextView` / `GtkTextBuffer` — read-only transcript

## Lines / files required

- `CMakeLists.txt` — 11 lines, standalone `pkg-config` GTK3 build
- `main.cpp` — 135 lines, window construction and fake Add/Send handlers
- `README.md` — this file

Three files. Configures independently of the repository root.

## Event-loop ownership

`gtk_init` then `gtk_main` in `main`. The host process owns the GTK3 main loop. Add and Send run on GTK signal callbacks on that same thread. No extra loop, idle pump, or worker thread.

## Host-facing operations a later backend needs

1. **current snapshot** — transcript and peer list to paint the native view
2. **local UID** — value for the read-only local UID field
3. **add peer** — remote UID from the Add row
4. **submit text** — message from Send / Enter
5. **changed callback** — native view refresh when snapshot content changes
