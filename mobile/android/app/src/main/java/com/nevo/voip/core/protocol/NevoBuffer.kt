package com.nevo.voip.core.protocol

import java.nio.ByteBuffer
import java.nio.ByteOrder

class NevoBuffer {
    private companion object {
        private const val INITIAL_CAPACITY = 1024
    }

    private var writeBuf: ByteBuffer? = null
    private var readBuf: ByteBuffer? = null

    constructor() {
        writeBuf = ByteBuffer.allocate(INITIAL_CAPACITY).order(ByteOrder.LITTLE_ENDIAN)
    }

    constructor(data: ByteArray) {
        readBuf = ByteBuffer.wrap(data).order(ByteOrder.LITTLE_ENDIAN)
    }

    private fun ensureWriteCapacity(additional: Int) {
        val buf = writeBuf ?: return
        if (buf.remaining() < additional) {
            val newCapacity = maxOf(buf.capacity() * 2, buf.position() + additional)
            val newBuf = ByteBuffer.allocate(newCapacity).order(ByteOrder.LITTLE_ENDIAN)
            buf.flip()
            newBuf.put(buf)
            writeBuf = newBuf
        }
    }

    fun writeU16(value: Int) {
        ensureWriteCapacity(2)
        writeBuf!!.putShort(value.toShort())
    }

    fun writeU32(value: Int) {
        ensureWriteCapacity(4)
        writeBuf!!.putInt(value)
    }

    fun writeU64(value: Long) {
        ensureWriteCapacity(8)
        writeBuf!!.putLong(value)
    }

    fun writeBool(value: Boolean) {
        ensureWriteCapacity(1)
        writeBuf!!.put(if (value) 1.toByte() else 0.toByte())
    }

    fun writeString(value: String) {
        val encoded = value.toByteArray(Charsets.UTF_8)
        writeU32(encoded.size)
        ensureWriteCapacity(encoded.size)
        writeBuf!!.put(encoded)
    }

    fun writeBytes(value: ByteArray) {
        writeU32(value.size)
        ensureWriteCapacity(value.size)
        writeBuf!!.put(value)
    }

    fun writeBytesRaw(value: ByteArray) {
        ensureWriteCapacity(value.size)
        writeBuf!!.put(value)
    }

    fun readU16(): Int {
        return (readBuf!!.short.toInt() and 0xFFFF)
    }

    fun readU32(): Int {
        return readBuf!!.int
    }

    fun readU64(): Long {
        return readBuf!!.long
    }

    fun readBool(): Boolean {
        return readBuf!!.get().toInt() != 0
    }

    fun readString(): String {
        val length = readU32()
        val bytes = ByteArray(length)
        readBuf!!.get(bytes)
        return String(bytes, Charsets.UTF_8)
    }

    fun readBytes(): ByteArray {
        val length = readU32()
        val bytes = ByteArray(length)
        readBuf!!.get(bytes)
        return bytes
    }

    fun readRaw(n: Int): ByteArray {
        if (readBuf!!.remaining() < n) {
            throw IllegalArgumentException(
                "readRaw: requested $n bytes but only ${readBuf!!.remaining()} bytes remaining"
            )
        }
        val bytes = ByteArray(n)
        readBuf!!.get(bytes)
        return bytes
    }

    fun getBytes(): ByteArray {
        val buf = writeBuf ?: return ByteArray(0)
        buf.flip()
        val result = ByteArray(buf.remaining())
        buf.get(result)
        return result
    }

    fun position(): Int {
        return writeBuf?.position() ?: readBuf?.position() ?: 0
    }

    fun remaining(): Int {
        return readBuf?.remaining() ?: 0
    }
}