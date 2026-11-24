#!/usr/bin/env python3
"""Pack-backed parking check. Run on an isolated GPU host with --pack PATH --exe build/maya-ds4.

Exercises the real GEN/DONE and slot writer in serial and four-lane mode. Slots are written to a
temporary folder. The checkpoints, generated-token oracle and final on-disk primaries are checked.
"""
import argparse
from pathlib import Path
import tempfile
import time

from slot_keep_test import Engine, config_env, engine_env, meta_tokens, text_tokens


def run(exe, pack, settings, lanes, prompts, folder):
    await_line = 600
    a, b = "a" * 64, "b" * 64
    env = engine_env(settings, True, str(folder), 16)
    env.update(MAYA_DS4_SLOT_MIN="1", MAYA_DS4_SLOT_CAP="4", MAYA_DS4_RESTORE_POINTS="4")
    args = ["--pack", pack, "--dspark", "none", "--expert-profile", "none", "--vision", "off",
            "--parallel", str(lanes), "--serve", "--max-context", "1024"]
    eng = Engine(exe, args, env)

    def send(rid, prompt, key="", order=0, count=1, cold=False):
        fields = [f"GEN {count}", "spec=0", "temperature=0", "marks=64"]
        if cold:
            fields.append("cold=1")
        if key:
            fields.append(f"thread={key}:{order}")
        if lanes > 1:
            fields.append(f"rid={rid}")
        fields.append(",".join(map(str, prompt)))
        eng.proc.stdin.write((" ".join(fields) + "\n").encode())
        eng.proc.stdin.flush()

    def collect(*requested):
        deadline = time.monotonic() + await_line
        results = {}
        tokens = {rid: [] for rid in requested}
        while len(results) < len(requested):
            line = eng._line(deadline)
            f = line.split()
            if not f or f[0] not in ("ERR", "T", "DONE"):
                continue
            if f[0] == "ERR":
                raise RuntimeError(line)
            rid = int(f.pop(1)) if lanes > 1 else requested[0]
            if f[0] == "T":
                tokens[rid].append(int(f[1]))
            else:
                results[rid] = (int(f[8]), tokens[rid])
        return results

    def turn(rid, prompt, *args, **kw):
        send(rid, prompt, *args, **kw)
        return collect(rid)[rid]

    a1, b1, a2, b2, old, anonymous = prompts
    try:
        eng.start(1800)
        # Cold references are anonymous and taken before the references could have any shared state.
        oracle_a = turn(1, a2, cold=True)[1]
        oracle_b = turn(2, b2, cold=True)[1]
    finally:
        if eng.proc is not None:
            eng.quit()
        eng.stderr_lines()

    with tempfile.TemporaryDirectory(dir=folder) as clean:
        measured_env = {**env, "MAYA_DS4_SLOT_DIR": clean}
        eng = Engine(exe, args, measured_env)
        try:
            eng.start(1800)
            turn(1, a1, a, 1)
            turn(2, b1, b, 2)
            assert turn(3, a2, a, 5) == (64, oracle_a)
            assert turn(4, b2, b, 6) == (64, oracle_b)
            # A delayed older request and a newer request carry divergent tails. In four-lane mode
            # both are in flight together. The older one has enough work to complete last.
            if lanes > 1:
                send(5, old, a, 3, count=8)
                send(6, a2, a, 7)
                collect(5, 6)
            else:
                turn(5, a2, a, 7)
                turn(6, old, a, 3)
            turn(7, anonymous)
        finally:
            if eng.proc is not None:
                eng.quit()
            eng.stderr_lines()
        primaries = {}
        unkeyed = []
        for meta in Path(clean).glob("slot-*.meta"):
            data = meta.read_bytes()
            key = next((k for k in (a, b) if data.endswith(k.encode())), None)
            tokens = meta_tokens(str(meta))
            if key:
                assert key not in primaries, f"duplicate primary in {meta.name}"
                primaries[key] = tokens
            else:
                unkeyed.append(tokens)
        assert primaries == {a: a2, b: b2}, "a late turn replaced a newer snapshot"
        assert anonymous in unkeyed, "an anonymous prompt lost its content-based slot"


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--pack", required=True)
    ap.add_argument("--exe", default="build/maya-ds4")
    ap.add_argument("--config", help="optional local DS4 environment settings")
    args = ap.parse_args()
    from strata_tokenizer import Tokenizer
    tok = Tokenizer.from_dir(str(Path(args.pack) / "tokenizer"))
    a = text_tokens(tok, "Atlas", 64)
    b = text_tokens(tok, "Birch", 64)
    tails = [text_tokens(tok, name, 16) for name in ("Prague", "London", "Boston", "Tokyo", "York")]
    prompts = [a + tails[0], b + tails[1], a + tails[2], b + tails[3], a + tails[4], tails[0]]
    assert len({t[0] for t in tails}) == 5 and a[0] != b[0]
    for lanes in (1, 4):
        with tempfile.TemporaryDirectory() as folder:
            run(args.exe, args.pack, config_env(args.config) if args.config else {}, lanes, prompts, Path(folder))
        print(f"passed {lanes}-lane changed-tail, anonymous and overlap checks")


if __name__ == "__main__":
    main()
