# 临时测试脚本：验证 updater.py 修复
import sys
import os

# 将 gui_python 目录加入路径
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

from updater import VersionInfo


def test_version_parse():
    cases = [
        ("0.1.0", (0, 1, 0)),
        ("v0.1.0", (0, 1, 0)),
        ("V0.1.0", (0, 1, 0)),
        ("B_V0.01", (0, 1, 0)),
        ("1.2.3-beta", (1, 2, 3)),
        ("v2.0", (2, 0, 0)),
        ("release-3.4.5", (3, 4, 5)),
        ("invalid", (0, 0, 0)),
    ]
    for s, expected in cases:
        result = VersionInfo.parse(s)
        assert result == expected, f"parse({s!r}) = {result}, expected {expected}"
    print("[OK] VersionInfo.parse 全部通过")


def test_is_newer_than():
    info = VersionInfo(version="B_V0.02")
    assert info.is_newer_than("0.1.0") is True
    assert info.is_newer_than("0.2.0") is False
    print("[OK] is_newer_than 逻辑正确")


def test_fetch_asset_selection():
    """验证 _fetch_latest_release 能正确选择 Windows 客户端 asset。"""
    from unittest.mock import patch, MagicMock
    from updater import Updater

    mock_response = {
        "tag_name": "B_V0.02",
        "body": "Test changelog",
        "published_at": "2026-05-27T06:03:37Z",
        "name": "BETA Version 0.02",
        "assets": [
            {
                "name": "NEVO.exe",
                "browser_download_url": "https://example.com/NEVO.exe",
                "size": 128076946,
                "digest": "sha256:abc123",
            },
            {
                "name": "NEVO.Server.Manager.zip",
                "browser_download_url": "https://example.com/server.zip",
                "size": 93619980,
                "digest": "sha256:def456",
            },
        ],
    }

    with patch("updater.requests.get") as mock_get:
        mock_resp = MagicMock()
        mock_resp.status_code = 200
        mock_resp.json.return_value = mock_response
        mock_get.return_value = mock_resp

        updater = Updater()
        info = updater._fetch_latest_release()

        assert info is not None
        assert info.version == "B_V0.02"
        assert info.download_url == "https://example.com/NEVO.exe"
        assert info.sha256 == "abc123"
        print("[OK] Windows 客户端 asset 选择正确")

def test_rate_limit_error():
    """验证 403 rate limit 返回友好的中文提示。"""
    from unittest.mock import patch, MagicMock
    from updater import Updater, CheckError, _ReleaseCache

    _ReleaseCache.clear()

    with patch("updater.requests.get") as mock_get:
        mock_resp = MagicMock()
        mock_resp.status_code = 403
        mock_resp.json.return_value = {"message": "API rate limit exceeded"}
        mock_get.return_value = mock_resp

        updater = Updater()
        try:
            updater._fetch_latest_release()
            assert False, "Expected CheckError"
        except CheckError as e:
            msg = str(e)
            assert "rate limit" in msg.lower() or "速率限制" in msg, f"Unexpected message: {msg}"
            print("[OK] 速率限制错误提示正确")


def test_github_token_headers():
    """验证 set_github_token 后请求头包含 Authorization。"""
    from updater import _get_github_headers
    headers = _get_github_headers("ghp_test_token")
    assert headers.get("Authorization") == "Bearer ghp_test_token"
    assert headers.get("User-Agent") == "NEVO-Client/Updater"
    print("[OK] GitHub token 请求头正确")


if __name__ == "__main__":
    test_version_parse()
    test_is_newer_than()
    test_fetch_asset_selection()
    test_rate_limit_error()
    test_github_token_headers()
    print("所有测试通过")