#!/usr/bin/env python3
"""Upload NEVO to the Debian server, build the Docker image in the background,
and start the service once the build finishes.

The SSH server drops idle long-running sessions, so the actual docker compose
build is run detached on the server while we poll for completion with short
commands.
"""
import io
import os
import sys
import tarfile
import time

import paramiko

HOST = "192.168.31.39"
USER = "llya"
# 凭据一律从环境变量注入（禁止硬编码；缺失时脚本拒绝执行）
PASSWORD = os.environ.get("NEVO_SSH_PASSWORD", "")
if not PASSWORD:
    sys.stderr.write("缺少环境变量 NEVO_SSH_PASSWORD（目标主机 SSH 密码）\n")
    sys.exit(1)
REMOTE_DIR = "/home/llya/nevo"
ARCHIVE = "/home/llya/nevo_deploy.tar.gz"
LOCAL_PROJECT = r"C:\Users\yzd20\Desktop\Project\NEVO"
BUILD_LOG = "/tmp/nevo_build.log"
BUILD_PID = "/tmp/nevo_build.pid"

INCLUDE = [
    "CMakeLists.txt",
    "server_config.example.json",
    "server_config.json",
    ".env.example",
    ".dockerignore",
    "Dockerfile",
    "docker-compose.yml",
    "docker-compose.dev.yml",
    "docker-compose.pull.yml",
    "docker-entrypoint.sh",
    "3rdparty",
    "cmake",
    "proto",
    "src",
    "web",
]

EXCLUDE_DIRS = {
    ".git", ".venv", "build", "out", "dist", "__pycache__", "node_modules",
    ".vs", ".idea", "*.egg-info", ".pytest_cache", "htmlcov"
}


def connect():
    ssh = paramiko.SSHClient()
    ssh.set_missing_host_key_policy(paramiko.AutoAddPolicy())
    ssh.connect(HOST, username=USER, password=PASSWORD, timeout=30)
    return ssh


def run(ssh, cmd, timeout=30):
    print(f"\n>>> {cmd}")
    stdin, stdout, stderr = ssh.exec_command(cmd, timeout=timeout)
    code = stdout.channel.recv_exit_status()
    out = stdout.read().decode("utf-8", errors="replace")
    err = stderr.read().decode("utf-8", errors="replace")
    if out:
        print(out.rstrip())
    if err:
        print(err.rstrip(), file=sys.stderr)
    print(f"<<< exit code: {code}")
    return code, out, err


def make_archive():
    print("Packing project (this may take a minute)...")
    buf = io.BytesIO()
    with tarfile.open(fileobj=buf, mode="w:gz") as tar:
        for item in INCLUDE:
            local_path = os.path.join(LOCAL_PROJECT, item)
            if not os.path.exists(local_path):
                print(f"  skip missing: {item}")
                continue
            arcname = item.replace("\\", "/")
            if os.path.isfile(local_path):
                tar.add(local_path, arcname=arcname)
            else:
                for root, dirs, files in os.walk(local_path):
                    dirs[:] = [d for d in dirs if d not in EXCLUDE_DIRS]
                    for f in files:
                        fp = os.path.join(root, f)
                        rel = os.path.relpath(fp, LOCAL_PROJECT).replace("\\", "/")
                        tar.add(fp, arcname=rel)
    buf.seek(0)
    size = len(buf.getvalue())
    print(f"Archive size: {size / 1024 / 1024:.2f} MB")
    return buf


def upload_and_extract(ssh):
    archive = make_archive()
    print(f"Uploading to {ARCHIVE}...")
    sftp = ssh.open_sftp()
    sftp.putfo(archive, ARCHIVE)
    sftp.close()
    print("Upload complete.")

    run(ssh, f"sudo rm -rf {REMOTE_DIR} && sudo mkdir -p {REMOTE_DIR}")
    run(ssh, f"sudo tar -xzf {ARCHIVE} -C {REMOTE_DIR}")
    run(ssh, f"sudo rm -f {ARCHIVE}")
    run(ssh, f"sudo chown -R {USER}:{USER} {REMOTE_DIR}")
    run(ssh, f"cd {REMOTE_DIR} && cp -n server_config.example.json server_config.json || true")
    run(ssh, f"cd {REMOTE_DIR} && cp -n .env.example .env || true")


def start_build(ssh):
    print("\nStopping any previous build and starting detached docker compose build...")
    cmd = (
        f"if [ -f {BUILD_PID} ]; then sudo kill $(cat {BUILD_PID}) 2>/dev/null || true; fi; "
        f"rm -f {BUILD_LOG} {BUILD_PID}; "
        f"cd {REMOTE_DIR} && sudo docker compose down --remove-orphans >/dev/null 2>&1 || true; "
        f"sudo sh -c 'cd {REMOTE_DIR} && nohup docker compose build --no-cache > {BUILD_LOG} 2>&1 & echo $!' > {BUILD_PID}"
    )
    code, out, err = run(ssh, cmd, timeout=60)
    if code != 0:
        print("Failed to start detached build.", file=sys.stderr)
        sys.exit(1)
    print("Detached build started.")


def poll_build():
    print("\nPolling remote build progress...")
    while True:
        # Heartbeat so the local process is not killed for inactivity.
        for _ in range(20):
            time.sleep(1)
            print(".", end="", flush=True)
        print()
        ssh = connect()
        code, out, err = run(
            ssh,
            f"if [ -f {BUILD_PID} ] && ps -p $(cat {BUILD_PID}) -o pid= >/dev/null 2>&1; then echo RUNNING; tail -n 20 {BUILD_LOG} 2>/dev/null; else echo DONE; tail -n 100 {BUILD_LOG} 2>/dev/null; fi",
            timeout=15,
        )
        ssh.close()
        lines = out.strip().splitlines()
        status = lines[0].strip() if lines else "UNKNOWN"
        tail = "\n".join(lines[1:])
        print(f"[{time.strftime('%H:%M:%S')}] status={status}")
        if tail.strip():
            print(tail)
        if status == "DONE":
            return


def start_service():
    ssh = connect()
    code, out, err = run(ssh, f"cd {REMOTE_DIR} && sudo docker compose up -d", timeout=120)
    if code != 0:
        ssh.close()
        print("Service failed to start.", file=sys.stderr)
        sys.exit(1)
    time.sleep(10)
    run(ssh, f"cd {REMOTE_DIR} && sudo docker compose ps -a")
    run(ssh, "sudo docker exec nevo-server nc -z -w3 127.0.0.1 24430", timeout=15)
    run(ssh, "sudo docker exec nevo-server nc -z -w3 127.0.0.1 24433", timeout=15)
    run(ssh, "sudo docker inspect --format='Status={{.State.Status}} Health={{.State.Health.Status}}' nevo-server", timeout=15)
    run(ssh, f"cd {REMOTE_DIR} && sudo docker compose logs --tail 30", timeout=30)
    ssh.close()


def main():
    ssh = connect()
    print(f"Connected to {HOST} as {USER}.\n")
    upload_and_extract(ssh)
    start_build(ssh)
    ssh.close()
    poll_build()
    start_service()
    print("\nDeployment complete.")


if __name__ == "__main__":
    main()
