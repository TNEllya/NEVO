# -*- coding: utf-8 -*-
"""
文件传输数据通道最小单元测试（纯逻辑，无 Qt/网络依赖）

覆盖：
  - 聊天中继分片标记编解码（含 chunk0 文件名、unicode、截断容忍）
  - iter_file_chunks 切分
  - FileChunkAssembler 乱序重组 / 上限保护
  - 新增 wire 消息（44/45/46/47）编解码注册与往返

运行：在 gui_python 目录下
  python -m unittest tests.test_file_transfer -v
"""
import os
import sys
import unittest

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

import nevo_wire as w  # noqa: E402


class ChunkMarkerTest(unittest.TestCase):

    def test_chunk_marker_roundtrip_without_filename(self):
        payload = bytes(range(256)) * 8  # 2048 bytes
        text = w.encode_file_chunk_marker(123, 4, 1, payload)
        parsed = w.decode_file_chunk_marker(text)
        self.assertIsNotNone(parsed)
        self.assertEqual(parsed["file_id"], 123)
        self.assertEqual(parsed["chunk_count"], 4)
        self.assertEqual(parsed["chunk_index"], 1)
        self.assertEqual(parsed["data"], payload)
        self.assertEqual(parsed["filename"], "")

    def test_chunk_marker_roundtrip_with_filename(self):
        payload = b"hello chunk 0"
        text = w.encode_file_chunk_marker(9, 2, 0, payload, filename="照片 测试.png")
        parsed = w.decode_file_chunk_marker(text)
        self.assertIsNotNone(parsed)
        self.assertEqual(parsed["chunk_index"], 0)
        self.assertEqual(parsed["filename"], "照片 测试.png")
        self.assertEqual(parsed["data"], payload)

    def test_fetch_marker_roundtrip(self):
        self.assertEqual(w.decode_file_fetch_marker("[NEVOFGET|1|42]"), 42)
        self.assertIsNone(w.decode_file_fetch_marker("[NEVOFD|1|42|1|0|aGk=]"))
        self.assertIsNone(w.decode_file_fetch_marker("hello world"))

    def test_marker_detection(self):
        self.assertTrue(w.is_file_transfer_marker("[NEVOFD|1|1|1|0|aGk=]"))
        self.assertTrue(w.is_file_transfer_marker("[NEVOFGET|1|1]"))
        self.assertFalse(w.is_file_transfer_marker("normal chat"))
        self.assertFalse(w.is_file_transfer_marker(""))

    def test_chunk_size_respects_chat_limit(self):
        size = w.pick_chat_chunk_size("a_very_long_" + "x" * 200 + ".png")
        worst = w.encode_file_chunk_marker(999999999999, 10 ** 6, 0, b"x" * size, "x" * 200 + ".png")
        self.assertLessEqual(len(worst), w.FILE_CHAT_MARKER_MAX_TEXT)

    def test_truncated_marker_returns_none(self):
        text = "[NEVOFD|1|42|4|1|aGk="
        self.assertIsNone(w.decode_file_chunk_marker(text))


class ChunkingTest(unittest.TestCase):

    def test_iter_file_chunks(self):
        data = bytes(range(100))
        chunks = list(w.iter_file_chunks(data, 30))
        self.assertEqual(len(chunks), 4)
        self.assertEqual(b"".join(c for _, c in chunks), data)
        self.assertEqual([i for i, _ in chunks], [0, 1, 2, 3])

    def test_iter_file_chunks_empty(self):
        self.assertEqual(list(w.iter_file_chunks(b"", 30)), [])


class AssemblerTest(unittest.TestCase):

    def _feed(self, assembler, chunks):
        for idx, piece in chunks:
            assembler.add_chunk(idx, piece)

    def test_out_of_order_reassembly(self):
        asm = w.FileChunkAssembler(7, 3)
        self.assertFalse(asm.complete)
        self.assertFalse(asm.add_chunk(2, b"cc"))
        self.assertFalse(asm.add_chunk(0, b"aa", filename="f.bin"))
        self.assertTrue(asm.add_chunk(1, b"bb"))
        self.assertTrue(asm.complete)
        self.assertEqual(asm.data, b"aabbcc")
        self.assertEqual(asm.filename, "f.bin")

    def test_duplicate_and_invalid_index(self):
        asm = w.FileChunkAssembler(7, 2)
        self.assertFalse(asm.add_chunk(5, b"xx"))  # 越界分片被拒绝
        asm.add_chunk(0, b"aa")
        asm.add_chunk(1, b"bb")
        self.assertTrue(asm.add_chunk(0, b"aa"))  # 重复分片：完成状态保持
        self.assertEqual(asm.data, b"aabb")

    def test_max_bytes_guard(self):
        asm = w.FileChunkAssembler(7, 2, max_bytes=4)
        self.assertFalse(asm.add_chunk(0, b"abcd"))
        self.assertFalse(asm.add_chunk(1, b"x"))  # 超出上限被拒绝
        self.assertFalse(asm.complete)


class FileWireCodecTest(unittest.TestCase):
    """新增 wire 消息（44/45/46/47）必须注册并可往返。"""

    def test_upload_chunk_request_roundtrip(self):
        msg = w.FileUploadChunkRequest(file_id=5, chunk_index=2, chunk_count=10,
                                       data=bytes(range(64)))
        payload = w.serialize_control_message(44, msg)
        msg_type, decoded = w.deserialize_control_message(payload)
        self.assertEqual(msg_type, 44)
        self.assertIsInstance(decoded, w.FileUploadChunkRequest)
        self.assertEqual(decoded.file_id, 5)
        self.assertEqual(decoded.chunk_index, 2)
        self.assertEqual(decoded.chunk_count, 10)
        self.assertEqual(decoded.data, bytes(range(64)))

    def test_upload_chunk_ack_roundtrip(self):
        msg = w.FileUploadChunkAck(file_id=5, chunk_index=2, result=0)
        payload = w.serialize_control_message(45, msg)
        msg_type, decoded = w.deserialize_control_message(payload)
        self.assertEqual(msg_type, 45)
        self.assertEqual(decoded.chunk_index, 2)

    def test_download_request_roundtrip(self):
        msg = w.FileDownloadRequest(file_id=99)
        payload = w.serialize_control_message(46, msg)
        msg_type, decoded = w.deserialize_control_message(payload)
        self.assertEqual(msg_type, 46)
        self.assertEqual(decoded.file_id, 99)

    def test_download_response_roundtrip(self):
        msg = w.FileDownloadResponse(result=0, message="ok", file_id=99,
                                     filename="a.bin", file_size=300,
                                     chunk_index=1, chunk_count=2, data=b"xyz")
        payload = w.serialize_control_message(47, msg)
        msg_type, decoded = w.deserialize_control_message(payload)
        self.assertEqual(msg_type, 47)
        self.assertIsInstance(decoded, w.FileDownloadResponse)
        self.assertEqual(decoded.filename, "a.bin")
        self.assertEqual(decoded.chunk_count, 2)
        self.assertEqual(decoded.data, b"xyz")


if __name__ == "__main__":
    unittest.main()
