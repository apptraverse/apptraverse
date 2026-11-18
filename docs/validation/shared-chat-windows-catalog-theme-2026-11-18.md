# Shared Chat Windows theme and transport — 2026-11-18

The Windows chat uses the common WindowCatalog Win32 presentation path: native
controls, the `WinUiEnvironment` system baseline, `UiDeviceState` LOCAL
appearance, and model Events for the native Theme menu. The previously accepted
English-only chat UI keeps its localization selector absent. The chat link
remains the direct SDK `P2pStream` over TCP, with SharedSync ACK/retry.

Executed on Windows with Visual Studio 2022 v143:

- Debug `shared_node_chat` and `apptraverse_shared_chat_ui_model_test` built.
- The model test loaded the newly generated embedded UI supply with the chat
  Windows presenter registered and passed.
- Chat UI model and RAM tests passed (2/2 CTest).
- A live Debug window exposed all four native Theme choices through UI
  Automation. Selecting Light changed the visible native palette.
- Release `shared_node_chat` built with the embedded supply. A live Release
  client reached Online, exposed the four Theme choices, applied Soft dark,
  and restored that LOCAL choice after process restart.
- The Win32 process test passed with three real Æther chat processes: join,
  Unicode delivery, offline participant, restart/rejoin, missed delivery,
  native layout, LOCAL frame restore, and clean shutdown.

The first manual window used a launcher working directory where the default
relative profile could not be created. The process test used explicit profile
paths and established working chat sessions. Actual Windows 7 and Narrator
speech were not executed.
