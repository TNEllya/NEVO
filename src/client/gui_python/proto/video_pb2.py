# -*- coding: utf-8 -*-
# Generated manually for NEVO video call support. DO NOT EDIT!
# source: video.proto
"""Generated protocol buffer code."""
from google.protobuf import descriptor as _descriptor
from google.protobuf import descriptor_pool as _descriptor_pool
from google.protobuf import runtime_version as _runtime_version
from google.protobuf import symbol_database as _symbol_database
from google.protobuf.internal import builder as _builder
_runtime_version.ValidateProtobufRuntimeVersion(
    _runtime_version.Domain.PUBLIC,
    6,
    31,
    1,
    '',
    'video.proto'
)
# @@protoc_insertion_point(imports)

_sym_db = _symbol_database.Default()




DESCRIPTOR = _descriptor_pool.Default().AddSerializedFile(b'\n\x0bvideo.proto\x12\nnevo.video"\xfb\x01\n\x11VideoPacketHeader\x12\x17\n\x0fsequence_number\x18\x01 \x01(\r\x12\x11\n\tsender_id\x18\x02 \x01(\x04\x12\x12\n\nchannel_id\x18\x03 \x01(\x04\x12\x11\n\ttimestamp\x18\x04 \x01(\r\x12\x12\n\nframe_type\x18\x05 \x01(\r\x12\x16\n\x0efragment_index\x18\x06 \x01(\r\x12\x16\n\x0efragment_total\x18\x07 \x01(\r\x12\r\n\x05width\x18\x08 \x01(\r\x12\x0e\n\x06height\x18\t \x01(\r\x12\x0b\n\x03fps\x18\n \x01(\r\x12\x12\n\ntcp_tunnel\x18\x0b \x01(\x08\x12\x0f\n\x07call_id\x18\x0c \x01(\x04b\x06proto3')

_globals = globals()
_builder.BuildMessageAndEnumDescriptors(DESCRIPTOR, _globals)
_builder.BuildTopDescriptorsAndMessages(DESCRIPTOR, 'video_pb2', _globals)
if not _descriptor._USE_C_DESCRIPTORS:
  DESCRIPTOR._loaded_options = None
  _globals['_VIDEOPACKETHEADER']._serialized_start=28
  _globals['_VIDEOPACKETHEADER']._serialized_end=278
# @@protoc_insertion_point(module_scope)
