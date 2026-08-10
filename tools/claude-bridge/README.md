# Claude questions on CrossPoint

This is a four-button X4 adaptation of
[claudeq](https://github.com/Positronico/claudeq)'s primary interaction:
Claude Code's `AskUserQuestion` options appear on the reader and the selected
answers are returned to Claude.

The host side is one short-lived hook process per question. There is no bridge
daemon. If the X4 is unavailable, busy, outside the Claude Activity, or receives
an unsupported multi-select prompt, the hook produces no decision and Claude's
normal terminal picker appears.

## Configure the device

Build with `claude_bridge` enabled. The feature uses the existing background
web server, so set **Settings → Connect → Background server** to **On charge**
(while USB is connected) or **Always**. Then open **Claude** from Home.

Generate a token:

```bash
python3 -c 'import secrets; print(secrets.token_urlsafe(32))'
```

Put it on the SD card as `/claude_bridge.json`:

```json
{"token":"replace-with-the-generated-token"}
```

The firmware consumes that file on boot and stores it under `/.crosspoint/`.

## Configure Claude Code

Store the same token on the computer in
`~/.config/crosspoint/claudeq.json`:

```json
{"token":"replace-with-the-generated-token"}
```

The hook uses the firmware's existing UDP discovery protocol. A fixed URL is
optional:

```json
{"token":"replace-with-the-generated-token","url":"http://crosspoint-my-x4.local"}
```

Add this to Claude Code settings, using the absolute path to the hook:

```json
{
  "hooks": {
    "PreToolUse": [
      {
        "matcher": "AskUserQuestion",
        "hooks": [
          {
            "type": "command",
            "command": "python3 /absolute/path/to/tools/claude-bridge/askquestion_hook.py",
            "timeout": 300
          }
        ]
      }
    ]
  }
}
```

Open **Claude** from the X4 home screen before Claude asks a question. The
buttons are Back (fall through to the terminal), Select, Up, and Down.

Set `CLAUDEQ_TITLE` in the hook environment if you want a fixed session title;
otherwise the X4 shows the current working directory's basename.
