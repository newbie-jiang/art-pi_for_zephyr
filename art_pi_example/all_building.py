
#!/usr/bin/env python3
# -*- coding: utf-8 -*-

import os
import re
import sys
import json
import time
import shlex
import subprocess
from datetime import datetime
from pathlib import Path

# 并行编译加速（可改固定值）
os.environ.setdefault("CMAKE_BUILD_PARALLEL_LEVEL", str(os.cpu_count() or 8))

# ==== 基本路径：脚本放在 art_pi_example 目录下运行 ====
WORKDIR = Path(__file__).resolve().parent

SUMMARY_MD = WORKDIR / "summary_build.md"      # Markdown 汇总
STATE_FILE = WORKDIR / ".build_state.json"     # 增量编译状态
CMDS_FILE  = WORKDIR / "commands_build.txt"    # 仅列“特殊目标”的命令
LOG_DIR = WORKDIR / "logs"
LOG_DIR.mkdir(exist_ok=True)

# mcuboot 私钥路径
KEY_PATH = Path.home() / "zephyrproject" / "bootloader" / "mcuboot" / "root-rsa-2048.pem"

# 标准默认命令（未在 build_commands.txt 出现的目标都走它）
STD_CMD = 'west build -p always -b art_pi -d $BUILDDIR $APP -- -DCONFIG_BOOTLOADER_MCUBOOT=y -DCONFIG_MCUBOOT_SIGNATURE_KEY_FILE="\\\"$KEY\\\""'


# 识别需要编译的目录：以“数字.”开头
TARGET_DIR_RE = re.compile(r"^(\d+)\..+$")

# ----------------- 工具函数 -----------------

def load_state():
    if STATE_FILE.exists():
        try:
            return json.loads(STATE_FILE.read_text(encoding="utf-8"))
        except Exception:
            pass
    return {"targets": {}}

def save_state(state: dict):
    STATE_FILE.write_text(json.dumps(state, ensure_ascii=False, indent=2), encoding="utf-8")

def find_targets(base: Path):
    """自动发现所有以 '数字.' 开头的一级子目录，按数字排序。"""
    found = []
    for name in os.listdir(base):
        p = base / name
        if not p.is_dir():
            continue
        m = TARGET_DIR_RE.match(name)
        if m:
            idx = int(m.group(1))
            found.append((idx, name))
    found.sort(key=lambda x: x[0])
    return [name for _, name in found]

def dir_latest_mtime(path: Path) -> float:
    """递归取目录下所有文件的最新修改时间（秒）。空目录返回 0。"""
    latest = 0.0
    if not path.exists():
        return latest
    for root, _, files in os.walk(path):
        for fn in files:
            fp = Path(root) / fn
            try:
                mt = fp.stat().st_mtime
                if mt > latest:
                    latest = mt
            except FileNotFoundError:
                pass
    return latest

def ts_to_str(ts: float) -> str:
    if ts <= 0:
        return "-"
    return datetime.fromtimestamp(ts).strftime("%Y-%m-%d %H:%M:%S")

def extract_link_line(output: str):
    m = re.search(r"\[\d+/\d+\]\s+Linking C executable .*zephyr\.elf", output)
    return m.group(0) if m else ""

def extract_memory_block(output: str):
    last_idx = output.rfind("Memory region")
    if last_idx == -1:
        return ""
    tail = output[last_idx:]
    m = re.search(r"(Memory region[^\n]*\n(?:.+\n){1,120}?)(?:\n\S|\Z)", tail)
    return m.group(1).strip() if m else ""

def parse_row_used_and_pct(line: str):
    toks = line.strip().split()
    if len(toks) >= 3 and toks[0].endswith(":"):
        used = toks[1]
        if len(toks) >= 3 and not toks[2].endswith("%"):
            used += " " + toks[2]
        pct = toks[-1] if toks[-1].endswith("%") else None
        return used, pct
    return None, None

def extract_flash_ram_summary(mem_block: str):
    flash_used = flash_pct = ram_used = ram_pct = None
    if not mem_block:
        return flash_used, flash_pct, ram_used, ram_pct
    for line in mem_block.splitlines():
        s = line.strip()
        if s.startswith("FLASH:"):
            flash_used, flash_pct = parse_row_used_and_pct(s)
        elif s.startswith("RAM:"):
            ram_used, ram_pct = parse_row_used_and_pct(s)
    return flash_used, flash_pct, ram_used, ram_pct

def extract_first_error(output: str):
    pats = [
        r"(?m)^CMake Error[^\n]*\n(?:.+\n){0,10}",
        r"(?m)error: .+",
        r"(?m)FAILED: .+",
        r"(?m)FATAL: .+",
    ]
    for pat in pats:
        m = re.search(pat, output)
        if m:
            txt = m.group(0).strip()
            if len(txt) > 600:
                txt = txt[:600] + " ..."
            return txt
    return ""

def load_special_commands():
    """读取 build_commands.txt，仅解析 '<target>: <cmd>'；# 注释/空行忽略。"""
    overrides = {}
    if not CMDS_FILE.exists():
        return overrides
    for raw in CMDS_FILE.read_text(encoding="utf-8").splitlines():
        line = raw.strip()
        if not line or line.startswith("#"):
            continue
        if ":" not in line:
            continue
        target, cmd = line.split(":", 1)
        target = target.strip()
        cmd = cmd.strip()
        if target and cmd:
            overrides[target] = cmd
    return overrides

def expand_vars(cmd_template: str, target: str, build_dir: Path) -> str:
    repl = {
        "$WORKDIR": str(WORKDIR),
        "$APP": str(WORKDIR / target),
        "$BUILDDIR": str(build_dir),
        "$KEY": str(KEY_PATH),
    }
    s = cmd_template
    for k, v in repl.items():
        s = s.replace(k, v)
    return s

def parse_build_dir_from_args(argv: list, default_dir: Path) -> Path:
    """从 argv 解析 -d <dir>，若无则返回 default_dir。"""
    for i, tok in enumerate(argv):
        if tok == "-d" and i + 1 < len(argv):
            return Path(argv[i + 1])
    return default_dir

def run_build(command_template: str, target_dir: str):
    # 默认给每个目标一个独立 build 目录
    default_build_dir = WORKDIR / f"build-{target_dir}"

    # 变量替换 + 解析
    cmd_str = expand_vars(command_template, target_dir, default_build_dir)
    argv = shlex.split(cmd_str)

    # 解析实际 build 目录（若命令未显式 -d，则用默认）
    actual_build_dir = parse_build_dir_from_args(argv, default_build_dir)

    start = time.perf_counter()
    try:
        proc = subprocess.run(
            argv, cwd=WORKDIR, text=True,
            stdout=subprocess.PIPE, stderr=subprocess.STDOUT
        )
        out = proc.stdout
        code = proc.returncode
    except FileNotFoundError as e:
        out = f"[ERROR] Failed to invoke: {argv[0]} ({e})"
        code = 127
    elapsed = time.perf_counter() - start

    # 保存完整日志
    log_file = (LOG_DIR / f"{target_dir}.log")
    log_file.write_text(out, encoding="utf-8", errors="ignore")

    # 解析摘要
    link_line = extract_link_line(out)
    mem_block = extract_memory_block(out)
    flash_used, flash_pct, ram_used, ram_pct = extract_flash_ram_summary(mem_block)
    first_err = extract_first_error(out)
    ok = (code == 0)

    return {
        "target": target_dir,
        "ok": ok,
        "exit_code": code,
        "link_line": link_line,
        "mem_block": mem_block,
        "flash_used": flash_used,
        "flash_pct": flash_pct,
        "ram_used": ram_used,
        "ram_pct": ram_pct,
        "first_err": first_err,
        "build_dir": str(actual_build_dir),
        "log_file": str(log_file),
        "elapsed_sec": elapsed,
        "last_build_ts": datetime.now().strftime("%Y-%m-%d %H:%M:%S"),
        "status_text": "OK" if ok else f"FAIL({code})",
        "cmd_shown": " ".join(argv),
    }

# ----------------- 主流程 -----------------

def main():
    if not WORKDIR.exists():
        print(f"工作目录不存在: {WORKDIR}")
        sys.exit(1)

    targets = find_targets(WORKDIR)  # 自动发现所有 N.xxx 目录
    if not targets:
        print("未找到任何以“数字.”开头的示例目录")
        sys.exit(2)

    state = load_state()
    overrides = load_special_commands()
    cmds_mtime = int(CMDS_FILE.stat().st_mtime) if CMDS_FILE.exists() else 0

    print(f"将处理以下目标：{', '.join(targets)}\n")

    results = []
    start_ts = datetime.now().strftime("%Y-%m-%d %H:%M:%S")

    for t in targets:
        content_mtime = int(dir_latest_mtime(WORKDIR / t))
        prev = state.get("targets", {}).get(t)

        # 选择命令模板：优先特殊命令，否则标准命令
        used_template = "override" if t in overrides else "default"
        cmd_tpl = overrides.get(t, STD_CMD)

        # 仅当“上次构建成功 + 源码无变更 + commands 文件也无变更”时，才 SKIP
        can_skip = (
            prev is not None and
            prev.get("ok", False) and
            int(prev.get("last_content_mtime", -1)) == content_mtime and
            int(prev.get("cmds_mtime", -1)) == cmds_mtime
        )

        if can_skip:
            res = {
                "target": t,
                "ok": prev.get("ok", False),
                "exit_code": prev.get("exit_code", 0),
                "link_line": prev.get("link_line", ""),
                "mem_block": prev.get("mem_block", ""),
                "flash_used": prev.get("flash_used"),
                "flash_pct": prev.get("flash_pct"),
                "ram_used": prev.get("ram_used"),
                "ram_pct": prev.get("ram_pct"),
                "first_err": prev.get("first_err", ""),
                "build_dir": prev.get("build_dir", str(WORKDIR / f"build-{t}")),
                "log_file": prev.get("log_file", str(LOG_DIR / f"{t}.log")),
                "elapsed_sec": prev.get("elapsed_sec", 0.0),
                "last_build_ts": prev.get("last_build_ts", "-"),
                "last_content_mtime": content_mtime,
                "status_text": "SKIP(no changes)",
                "cmd_shown": prev.get("cmd_shown", ""),
                "used_template": used_template,
            }
            print(f"==> {t}: SKIP（未变更且上次成功） [{used_template}]")
            results.append(res)
            continue

        print(f"==> Building {t} ... [{used_template}]")
        build_res = run_build(cmd_tpl, t)
        build_res["last_content_mtime"] = content_mtime
        build_res["used_template"] = used_template
        build_res["cmds_mtime"] = cmds_mtime
        print(f"    {t}: {build_res['status_text']}")
        results.append(build_res)

        # 更新状态（注意：失败也会记录；下次在“未变更”的情况下不会跳过）
        state.setdefault("targets", {})[t] = {
            "ok": build_res["ok"],
            "exit_code": build_res["exit_code"],
            "link_line": build_res["link_line"],
            "mem_block": build_res["mem_block"],
            "flash_used": build_res["flash_used"],
            "flash_pct": build_res["flash_pct"],
            "ram_used": build_res["ram_used"],
            "ram_pct": build_res["ram_pct"],
            "first_err": build_res["first_err"],
            "build_dir": build_res["build_dir"],
            "log_file": build_res["log_file"],
            "elapsed_sec": build_res["elapsed_sec"],
            "last_build_ts": build_res["last_build_ts"],
            "last_content_mtime": build_res["last_content_mtime"],
            "cmd_shown": build_res["cmd_shown"],
            "used_template": build_res["used_template"],
            "cmds_mtime": cmds_mtime,
        }
        save_state(state)

    # === 输出 Markdown 汇总 ===
    ok_cnt = sum(1 for r in results if r["status_text"].startswith("OK"))
    fail_cnt = sum(1 for r in results if r["status_text"].startswith("FAIL"))
    skip_cnt = sum(1 for r in results if r["status_text"].startswith("SKIP"))

    lines = []
    lines.append("# Batch Build Summary")
    lines.append("")
    lines.append(f"- **Time**: {start_ts}")
    lines.append(f"- **Working dir**: `{WORKDIR}`")
    lines.append(f"- **Key**: `{KEY_PATH}` {'✅ exists' if KEY_PATH.exists() else '❌ MISSING!'}")
    lines.append(f"- **Parallel**: `CMAKE_BUILD_PARALLEL_LEVEL={os.environ.get('CMAKE_BUILD_PARALLEL_LEVEL')}`")
    lines.append(f"- **Commands file**: `{CMDS_FILE}` {'(found)' if CMDS_FILE.exists() else '(not found — only default used)'}")
    lines.append("")
    lines.append(f"**Totals**: {len(results)}  •  ✅ OK: {ok_cnt}  •  ❌ FAIL: {fail_cnt}  •  ⏭️ SKIP: {skip_cnt}")
    lines.append("")
    lines.append("| Target | Status | Template | Last Change | Last Build | FLASH | RAM | Elapsed | Build dir | Log |")
    lines.append("|---|---|---|---:|---:|---:|---:|---:|---|---|")

    for r in results:
        change_ts = ts_to_str(r.get("last_content_mtime", 0))
        last_build = r.get("last_build_ts", "-")
        fu = r.get("flash_used") or "?"
        fp = r.get("flash_pct") or "?"
        ru = r.get("ram_used") or "?"
        rp = r.get("ram_pct") or "?"
        elapsed = r.get("elapsed_sec", 0.0)
        lines.append(
            f"| `{r['target']}` | {r['status_text']} | {r.get('used_template','-')} | {change_ts} | {last_build} | "
            f"{fu} ({fp}) | {ru} ({rp}) | {elapsed:.1f}s | `{r['build_dir']}` | `{r['log_file']}` |"
        )

    for r in results:
        lines.append("")
        lines.append(f"## {r['target']} — {r['status_text']}")
        lines.append("")
        if r.get("cmd_shown"):
            lines.append(f"- **Command**: `{r['cmd_shown']}`")
        if r.get("link_line"):
            lines.append(f"- `{r['link_line']}`")
        lines.append(f"- **Build dir**: `{r['build_dir']}`")
        lines.append(f"- **Log**: `{r['log_file']}`")
        lines.append(f"- **Last change**: {ts_to_str(r.get('last_content_mtime', 0))}")
        lines.append(f"- **Last build**: {r.get('last_build_ts', '-')}")
        lines.append(f"- **Elapsed**: {r.get('elapsed_sec', 0.0):.1f}s")
        if r.get("mem_block"):
            lines.append("")
            lines.append("<details><summary>Memory usage table</summary>")
            lines.append("")
            lines.append("```text")
            lines.append(r["mem_block"])
            lines.append("```")
            lines.append("</details>")
        if r["status_text"].startswith("FAIL") and r.get("first_err"):
            lines.append("")
            lines.append("<details><summary>First error</summary>")
            lines.append("")
            lines.append("```text")
            lines.append(r["first_err"])
            lines.append("```")
            lines.append("</details>")

    SUMMARY_MD.write_text("\n".join(lines), encoding="utf-8")
    print(f"\n已写出汇总: {SUMMARY_MD}")

if __name__ == "__main__":
    main()
