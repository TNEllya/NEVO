#!/usr/bin/env python3
"""Deploy NEVO server to the remote Debian host via SSH (host from NEVO_SSH_HOST)."""
import io
import os
import sys
import tarfile
import time

import paramiko

# 目标主机与账号一律从环境变量注入（禁止硬编码；缺失时脚本拒绝执行）
HOST = os.environ.get("NEVO_SSH_HOST", "")
USER = os.environ.get("NEVO_SSH_USER", "")
if not HOST or not USER:
    sys.stderr.write("缺少环境变量 NEVO_SSH_HOST / NEVO_SSH_USER（目标主机地址与用户名）\n")
    sys.exit(1)
# 凭据一律从环境变量注入（禁止硬编码；缺失时脚本拒绝执行）
ROOT_PASSWORD = os.environ.get("NEVO_SSH_ROOT_PASSWORD", "")
USER_PASSWORD = os.environ.get("NEVO_SSH_USER_PASSWORD", "") or ROOT_PASSWORD
if not ROOT_PASSWORD:
    sys.stderr.write("缺少环境变量 NEVO_SSH_ROOT_PASSWORD（目标主机 root 密码）\n")
    sys.exit(1)
REMOTE_DIR = f"/home/{USER}/nevo"
ARCHIVE = f"/home/{USER}/nevo_deploy.tar.gz"
LOCAL_PROJECT = r"C:\Users\yzd20\Desktop\Project\NEVO"

# Files/directories required to build and run the Docker image.
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


def connect_ssh(username, password):
    print(f"Connecting to {HOST} as {username}...")
    ssh = paramiko.SSHClient()
    ssh.set_missing_host_key_policy(paramiko.AutoAddPolicy())
    ssh.connect(HOST, username=username, password=password, timeout=30)
    print("Connected.")
    return ssh


def run_cmd(ssh, cmd, timeout=60):
    print(f"\n>>> {cmd}")
    stdin, stdout, stderr = ssh.exec_command(cmd, timeout=timeout)
    exit_code = stdout.channel.recv_exit_status()
    out = stdout.read().decode("utf-8", errors="replace")
    err = stderr.read().decode("utf-8", errors="replace")
    if out:
        print(out)
    if err:
        print(err, file=sys.stderr)
    print(f"<<< exit code: {exit_code}")
    return exit_code, out, err


def run_cmd_pty(ssh, cmd, password=ROOT_PASSWORD, timeout=60):
    """Run a command as root via su inside a PTY, feeding the root password."""
    escaped = cmd.replace("'", "'\\''")
    full_cmd = f"su - root -c '{escaped}'"
    print(f"\n>>> [pty] {full_cmd}")
    transport = ssh.get_transport()
    channel = transport.open_session()
    channel.settimeout(timeout)
    channel.get_pty()
    channel.exec_command(full_cmd)

    # Wait for password prompt.
    output = b""
    deadline = time.time() + 5
    while time.time() < deadline:
        if channel.recv_ready():
            chunk = channel.recv(4096)
            output += chunk
            if b"Password" in output or b"password" in output:
                break
        time.sleep(0.1)

    channel.send(password + "\n")

    while not channel.exit_status_ready():
        time.sleep(0.2)

    exit_code = channel.recv_exit_status()
    while channel.recv_ready():
        output += channel.recv(4096)
    while channel.stderr_ready():
        output += channel.recv_stderr(4096)
    channel.close()

    text = output.decode("utf-8", errors="replace")
    if text:
        print(text)
    print(f"<<< exit code: {exit_code}")
    return exit_code, text, ""


def ensure_sudoers(ssh):
    """Ensure USER can run passwordless sudo."""
    print(f"\nChecking sudo access for {USER}...")
    code, out, err = run_cmd(ssh, "sudo -n whoami", timeout=10)
    if code == 0 and "root" in out:
        print(f"{USER} already has passwordless sudo.")
        return True

    print(f"Configuring passwordless sudo for {USER} via root...")
    script = (
        "mkdir -p /etc/sudoers.d && "
        f"printf '%s\\n' '{USER} ALL=(ALL) NOPASSWD:ALL' > /etc/sudoers.d/{USER}-nevo && "
        f"chmod 440 /etc/sudoers.d/{USER}-nevo && "
        f"visudo -c -f /etc/sudoers.d/{USER}-nevo"
    )
    code, _, err = run_cmd_pty(ssh, script, timeout=30)
    if code != 0:
        print("Failed to configure sudoers.", file=sys.stderr)
        return False

    code, out, _ = run_cmd(ssh, "sudo -n whoami", timeout=10)
    if code == 0 and "root" in out:
        print("Sudo configured successfully.")
        return True
    print("Sudo verification failed.", file=sys.stderr)
    return False


def main():
    ssh = connect_ssh(USER, USER_PASSWORD)

    if not ensure_sudoers(ssh):
        ssh.close()
        sys.exit(1)

    # Upload archive
    archive = make_archive()
    print(f"Uploading to {ARCHIVE}...")
    sftp = ssh.open_sftp()
    sftp.putfo(archive, ARCHIVE)
    sftp.close()
    print("Upload complete.")

    # Prepare remote directory and extract
    run_cmd(ssh, f"sudo rm -rf {REMOTE_DIR} && sudo mkdir -p {REMOTE_DIR}")
    run_cmd(ssh, f"sudo tar -xzf {ARCHIVE} -C {REMOTE_DIR}")
    run_cmd(ssh, f"sudo rm -f {ARCHIVE}")
    run_cmd(ssh, f"sudo chown -R {USER}:{USER} {REMOTE_DIR}")

    # Install Docker if missing
    print("\nChecking Docker installation...")
    code, _, _ = run_cmd(ssh, "docker --version && docker compose version", timeout=30)
    if code != 0:
        print("Installing Docker...")
        run_cmd(ssh, "sudo apt-get update && sudo apt-get install -y ca-certificates curl gnupg lsb-release", timeout=300)
        run_cmd(ssh, "sudo install -m 0755 -d /etc/apt/keyrings")
        run_cmd(ssh, "curl -fsSL https://download.docker.com/linux/debian/gpg | sudo gpg --dearmor -o /etc/apt/keyrings/docker.gpg", timeout=60)
        run_cmd(ssh, 'echo "deb [arch=$(dpkg --print-architecture) signed-by=/etc/apt/keyrings/docker.gpg] https://download.docker.com/linux/debian $(lsb_release -cs) stable" | sudo tee /etc/apt/sources.list.d/docker.list > /dev/null')
        run_cmd(ssh, "sudo apt-get update && sudo apt-get install -y docker-ce docker-ce-cli containerd.io docker-buildx-plugin docker-compose-plugin", timeout=300)
        run_cmd(ssh, f"sudo usermod -aG docker {USER}")
        run_cmd(ssh, "docker --version && docker compose version", timeout=30)

    # Prepare config and env
    run_cmd(ssh, f"cd {REMOTE_DIR} && cp -n server_config.example.json server_config.json || true")
    run_cmd(ssh, f"cd {REMOTE_DIR} && cp -n .env.example .env || true")

    # Build and run
    print("\nBuilding and starting NEVO server...")
    run_cmd(ssh, f"cd {REMOTE_DIR} && sudo docker compose down --remove-orphans || true", timeout=60)
    code, _, err = run_cmd(ssh, f"cd {REMOTE_DIR} && sudo docker compose build --no-cache", timeout=1800)
    if code != 0:
        print("Build failed. Fetching logs...", file=sys.stderr)
        run_cmd(ssh, f"cd {REMOTE_DIR} && sudo docker compose logs --tail 100", timeout=60)
        ssh.close()
        sys.exit(1)

    code, _, err = run_cmd(ssh, f"cd {REMOTE_DIR} && sudo docker compose up -d", timeout=120)
    if code != 0:
        print("Start failed. Fetching logs...", file=sys.stderr)
        run_cmd(ssh, f"cd {REMOTE_DIR} && sudo docker compose logs --tail 100", timeout=60)
        ssh.close()
        sys.exit(1)

    # Wait and verify
    print("\nWaiting for services to start...")
    time.sleep(15)
    run_cmd(ssh, f"cd {REMOTE_DIR} && sudo docker compose ps", timeout=30)
    run_cmd(ssh, "sudo docker exec nevo-server nc -z -w3 127.0.0.1 24430", timeout=30)
    run_cmd(ssh, "sudo docker exec nevo-server nc -z -w3 127.0.0.1 24433", timeout=30)
    run_cmd(ssh, "sudo docker inspect --format='{{.State.Health.Status}}' nevo-server", timeout=30)
    run_cmd(ssh, f"cd {REMOTE_DIR} && sudo docker compose logs --tail 50", timeout=30)

    ssh.close()
    print("\nDeployment complete.")


if __name__ == "__main__":
    main()
