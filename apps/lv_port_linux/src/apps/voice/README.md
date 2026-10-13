# Optional voice navigation

Build with MIMI_ENABLE_VOICE_CONTROL=ON, or select voice_control in the developer
build profile. OFF excludes the app from the launcher. No hardware interface
or private platform credential is changed by this optional application.

Each page session starts with voice disabled. The user explicitly enables it,
then starts a bounded 30-second recognition session. The existing Tuya provider
owns audio capture and cloud speech; this app never opens an audio device.
A pre-existing free-chat session is not taken over. Leaving, canceling, timeout
or receiving the first user transcript exits only the session this app started.

The current Tuya adapter emits genuine ASR with the prefix `You: ` and assistant
replies with `Assistant: `. This is an adapter convention, not an authenticated
role field. Voice proposals therefore require both an observed listening state
and an explicit local confirmation. Unknown/unprefixed text and assistant
replies are ignored. Confirmation expires after 30 seconds, is single-use,
and editing the text invalidates it. Transcript text is not persisted or logged.

The deterministic vocabulary supports opening Settings, Wi-Fi, Bluetooth,
Video, Pairing, Smart Home, the app drawer, and returning Home (Chinese/English).
It does not execute shell commands, arbitrary screen coordinates, updates,
account actions or IoT controls. Text input uses the same preview/confirmation
path. Only the explicit whitelist can execute; prose containing a command,
multi-command text and instructions to ignore rules are rejected.

Host fixtures cover disabled input, assistant rejection, preview before action,
single-use confirmation and expiry. Actual microphone-to-cloud-to-navigation
acceptance still needs to run on the target; injected IPC text is not that test.
A future provider with authenticated transcript roles/session IDs should use a
separate versioned capability instead of changing the frozen IPC contract.

## Legacy transcript handling

The app remembers the most recent valid user-prefixed text for the current page,
including cached GET_STATUS replies seen before recognition. Exact repeats are
ignored with an explanation. The legacy topic cannot prove utterance freshness:
a deliberately repeated phrase may also be ignored; type it or rephrase it.
This filter does not replace authenticated utterance/session identifiers, and
explicit local confirmation remains mandatory. No transcript is persisted.

Receipt checks the 30-second deadline directly, even if the UI timer has not
run. Ended provider sessions, unavailable providers and transcripts longer than
127 UTF-8 bytes cannot create proposals. Long text is rejected before the
textarea can truncate it into a valid command. Manual edits stop recognition so
a subsequent speech response cannot replace typed text. Host tests cover these
boundaries; actual spoken recognition still requires target-board acceptance.
