package com.nevo.voip.core.protocol

enum class ResultCode(val code: Int) {
    OK(0),
    ERROR_UNKNOWN(1),
    ERROR_AUTH_FAILED(2),
    ERROR_PERMISSION_DENIED(3),
    ERROR_CHANNEL_NOT_FOUND(4),
    ERROR_ALREADY_IN_CHANNEL(5),
    ERROR_INVALID_REQUEST(6),
    ERROR_CONNECTION_FAILED(7),
    ERROR_TIMEOUT(8);

    companion object {
        fun fromCode(code: Int): ResultCode? = entries.find { it.code == code }
    }
}