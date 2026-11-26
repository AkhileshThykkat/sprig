#!/usr/bin/env node
// Runs the prebuilt native sprig for this platform, shipped in npm/bin/.
import { spawnSync } from "node:child_process";
import { existsSync } from "node:fs";
import { fileURLToPath } from "node:url";

const target = process.platform === "darwin" ? "darwin-universal" : `${process.platform}-${process.arch}`;
const bin = fileURLToPath(new URL(`./bin/sprig-${target}`, import.meta.url));
const args = process.argv.slice(2);

// `sprig setup`: connect Claude Code, Codex and pi, using this package's files.
if (args[0] === "setup") {
  const script = fileURLToPath(new URL("../install.sh", import.meta.url));
  const pi = fileURLToPath(new URL("../integrations/pi/sprig.ts", import.meta.url));
  const r = spawnSync("sh", [script, "--agents-only", "--copy", ...args.slice(1)], {
    stdio: "inherit",
    env: { ...process.env, SPRIG_PI_FILE: pi },
  });
  process.exit(r.status ?? 1);
}

if (!existsSync(bin)) {
  if (args[0] === "hook" || args[0] === "emit") process.exit(0); // never break an agent
  console.error(`sprig: no prebuilt binary for ${target}; build it from https://github.com/AkhileshThykkat/sprig`);
  process.exit(1);
}

// The native viewer handles Ctrl-C itself (and restores the terminal);
// this wrapper only has to outlive it and pass its exit status on.
for (const signal of ["SIGINT", "SIGTERM", "SIGHUP"]) process.on(signal, () => {});
const result = spawnSync(bin, args, { stdio: "inherit" });
process.exit(result.status ?? 1);
