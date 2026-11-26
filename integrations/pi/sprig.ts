// sprig for pi: sends the agent's state to a running `sprig` viewer.
// Install: cp integrations/pi/sprig.ts ~/.pi/agent/extensions/
// @ts-nocheck
import { closeSync, constants, fstatSync, openSync, writeSync } from "node:fs";

// Same lookup as the sprig binary.
function pipePath(): string {
  if (process.env.SPRIG_PIPE) return process.env.SPRIG_PIPE;
  if (process.env.XDG_RUNTIME_DIR) return `${process.env.XDG_RUNTIME_DIR}/sprig.pipe`;
  return `/tmp/sprig-${process.getuid?.() ?? 0}/sprig.pipe`;
}

// Never blocks, never throws: no viewer running is normal.
function send(mood: string): void {
  let fd: number | undefined;
  try {
    fd = openSync(pipePath(), constants.O_WRONLY | constants.O_NONBLOCK | constants.O_NOFOLLOW);
    if (fstatSync(fd).isFIFO()) writeSync(fd, `${mood}\n`);
  } catch {
    // ENOENT / ENXIO: nobody is watching
  } finally {
    if (fd !== undefined) closeSync(fd);
  }
}

export default function (pi) {
  pi.on("session_start", () => send("idle"));
  pi.on("agent_start", () => send("thinking"));
  pi.on("tool_execution_start", () => send("busy"));
  pi.on("tool_execution_end", (event) => send(event?.isError ? "error" : "thinking"));
  pi.on("ui_prompt_start", () => send("waiting"));
  pi.on("agent_end", () => send("happy"));
  pi.on("session_shutdown", () => send("sleep"));
}
