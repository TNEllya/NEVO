#!/usr/bin/env python3
"""Push the locally-built nevo-server image from the Debian server to Docker Hub."""
import os
import sys
import paramiko

# 目标主机与账号一律从环境变量注入（禁止硬编码；缺失时脚本拒绝执行）
HOST = os.environ.get("NEVO_SSH_HOST", "")
USER = os.environ.get("NEVO_SSH_USER", "")
if not HOST or not USER:
    sys.stderr.write("缺少环境变量 NEVO_SSH_HOST / NEVO_SSH_USER（目标主机地址与用户名）\n")
    sys.exit(1)
# 凭据一律从环境变量注入（禁止硬编码；缺失时脚本拒绝执行）
PASSWORD = os.environ.get("NEVO_SSH_PASSWORD", "")
if not PASSWORD:
    sys.stderr.write("缺少环境变量 NEVO_SSH_PASSWORD（目标主机 SSH 密码）\n")
    sys.exit(1)

DOCKER_USER = os.environ.get("DOCKER_USER", "")
DOCKER_TOKEN = os.environ.get("DOCKER_TOKEN", "")
if not DOCKER_USER or not DOCKER_TOKEN:
    sys.stderr.write("缺少环境变量 DOCKER_USER / DOCKER_TOKEN（Docker Hub 用户名与 Personal Access Token）\n")
    sys.exit(1)
IMAGE_LOCAL = "nevo-server:latest"
IMAGE_REMOTE = f"{DOCKER_USER}/nevo-server:latest"


def connect():
    ssh = paramiko.SSHClient()
    ssh.set_missing_host_key_policy(paramiko.AutoAddPolicy())
    ssh.connect(HOST, username=USER, password=PASSWORD, timeout=30)
    return ssh


def run(ssh, cmd, label, timeout=300, stdin_data=None):
    print(f"\n=== {label} ===", flush=True)
    print(f">>> {cmd}", flush=True)
    stdin, stdout, stderr = ssh.exec_command(cmd, timeout=timeout, get_pty=True)
    if stdin_data:
        stdin.write(stdin_data)
        stdin.channel.shutdown_write()

    # Stream output in real time so we can see push progress
    out_lines = []
    err_lines = []
    while not stdout.channel.exit_status_ready() or stdout.channel.recv_ready() or stderr.channel.recv_stderr_ready():
        if stdout.channel.recv_ready():
            data = stdout.channel.recv(4096).decode("utf-8", errors="replace")
            if data:
                print(data, end="", flush=True)
                out_lines.append(data)
        if stderr.channel.recv_stderr_ready():
            data = stderr.channel.recv_stderr(4096).decode("utf-8", errors="replace")
            if data:
                print(data, end="", file=sys.stderr, flush=True)
                err_lines.append(data)
    code = stdout.channel.recv_exit_status()
    print(f"\n<<< exit code: {code}", flush=True)
    return code, "".join(out_lines), "".join(err_lines)


def main():
    ssh = connect()
    print(f"Connected to {HOST} as {USER}.\n", flush=True)

    run(ssh, "sudo docker --version", "Check Docker")
    run(
        ssh,
        f"sudo docker login -u {DOCKER_USER} --password-stdin",
        "Login to Docker Hub",
        timeout=60,
        stdin_data=DOCKER_TOKEN + "\n",
    )
    run(ssh, f"sudo docker tag {IMAGE_LOCAL} {IMAGE_REMOTE}", "Tag image")
    run(ssh, f"sudo docker push {IMAGE_REMOTE}", "Push image", timeout=600)
    run(ssh, "sudo docker logout", "Logout")

    ssh.close()
    print("\nPush complete.", flush=True)
    print(f"Pull command: docker pull {IMAGE_REMOTE}", flush=True)


if __name__ == "__main__":
    main()
