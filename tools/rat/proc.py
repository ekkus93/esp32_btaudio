"""Subprocess helpers: plain runs, pty-attached runs, esptool pinning."""
from __future__ import annotations

import os
import pty
import shlex
import subprocess
import sys


def run_cmd(cmd, cwd=None, env=None, shell=False, timeout=None):
    print(f"RUN: {' '.join(cmd) if isinstance(cmd, (list,tuple)) else cmd}")
    try:
        if shell:
            res = subprocess.run(cmd, cwd=cwd, env=env, shell=True, check=False, timeout=timeout, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
        else:
            res = subprocess.run(cmd, cwd=cwd, env=env, check=False, timeout=timeout, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
        return res.returncode, res.stdout
    except subprocess.TimeoutExpired as e:
        return 124, getattr(e, 'output', '') or f"TIMEOUT after {timeout}s"


def run_with_pty(cmd, shell=False, timeout=None, capture_path: str | None = None):
    """Run a command attached to a pseudo-tty, streaming output to this process' stdout.

    If capture_path is provided, bytes emitted by the child are appended to that
    file (binary mode) while also being streamed to this process' stdout.

    Returns (rc, output) where output is an empty string (we stream to stdout
    and persist to capture_path instead of returning a possibly-large buffer).
    """
    if shell:
        args = ["bash", "-lc", cmd]
    else:
        args = cmd

    master_fd, slave_fd = pty.openpty()
    capture_fh = None
    try:
        if capture_path:
            # open in append-binary mode so multiple invocations don't truncate unexpectedly
            capture_fh = open(capture_path, "ab")
        proc = subprocess.Popen(args, stdin=slave_fd, stdout=slave_fd, stderr=slave_fd, close_fds=True)
        os.close(slave_fd)
        # read and stream until EOF
        try:
            while True:
                try:
                    data = os.read(master_fd, 1024)
                except OSError:
                    break
                if not data:
                    break
                # write raw bytes to stdout
                try:
                    sys.stdout.buffer.write(data)
                    sys.stdout.buffer.flush()
                except Exception:
                    # best-effort: ignore streaming errors
                    pass
                if capture_fh:
                    try:
                        capture_fh.write(data)
                        capture_fh.flush()
                    except Exception:
                        pass
        except OSError:
            pass
        try:
            proc.wait(timeout=timeout)
            rc = proc.returncode
        except subprocess.TimeoutExpired:
            proc.kill()
            rc = 124
    finally:
        try:
            os.close(master_fd)
        except Exception:
            pass
        if capture_fh:
            try:
                capture_fh.close()
            except Exception:
                pass
    return rc, ''


def ensure_esptool(version_spec: str, source_idf: str | None) -> tuple[int, str]:
    """Ensure esptool matches the required version before device runs."""
    if source_idf:
        shell_cmd = f". {shlex.quote(source_idf)} >/dev/null 2>&1 && python -m pip install {shlex.quote(version_spec)}"
        return run_cmd(shell_cmd, shell=True)
    return run_cmd([sys.executable, "-m", "pip", "install", version_spec])
