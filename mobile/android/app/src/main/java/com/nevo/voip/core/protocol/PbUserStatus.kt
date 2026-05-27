package com.nevo.voip.core.protocol

enum class PbUserStatus(val status: Int) {
    OFFLINE(0),
    ONLINE(1),
    AWAY(2),
    MUTED(3),
    DEAFENED(4);

    companion object {
        fun fromStatus(status: Int): PbUserStatus? = entries.find { it.status == status }
    }
}