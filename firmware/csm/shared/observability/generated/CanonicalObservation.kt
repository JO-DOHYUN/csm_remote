// Generated from CSM canonical observation contract. No runtime hooks or authority.
package com.hamt.vsm.observability

object CanonicalObservation {
    object Stage {
        const val BEGIN: Int = 1
        const val COMPLETE: Int = 2
        const val STAGED: Int = 3
        const val REPLACED: Int = 4
        const val TAKEN: Int = 5
        const val ACCEPTED: Int = 6
        const val REJECTED: Int = 7
        const val APPLIED: Int = 8
        const val CANCELED: Int = 9
        const val CLOSED: Int = 10
        const val LOST: Int = 11
        const val SNAPSHOT: Int = 12
    }
    object Producer {
        const val M7_FOREGROUND: Int = 1
        const val M7_REALTIME_WORKER: Int = 2
        const val M7_SOCKET_WORKER: Int = 3
        const val APP_INTENT: Int = 16
        const val APP_REALTIME_PLANE: Int = 17
        const val APP_UDP_SEND: Int = 18
        const val APP_UDP_RECEIVE: Int = 19
        const val APP_CONTROL_SEND: Int = 20
        const val APP_CONTROL_RECEIVE: Int = 21
        const val APP_OBSERVER_RECEIVE: Int = 22
        const val APP_TRUTH_WORKER: Int = 23
        const val APP_NETWORK_LIFECYCLE: Int = 24
        const val APP_CAPTURE_WORKER: Int = 25
        const val APP_CONTROL_PLANE: Int = 26
    }
    object Clock {
        const val M7_MILLIS_U32: Int = 1
        const val ANDROID_ELAPSED_REALTIME_NANOS: Int = 2
        const val ANDROID_SYSTEM_NANO_TIME: Int = 3
    }
    object AdmissionField {
        const val WIRE_SEQUENCE: Long = 1L
        const val GENERATION: Long = 2L
        const val AUTHORITY_EPOCH: Long = 4L
        const val PROOF_REFERENCE: Long = 8L
        const val RX_TOKEN: Long = 16L
        const val SOURCE_EPOCH: Long = 32L
        const val ACTIVATION_EPOCH: Long = 64L
        const val FORWARD_AGE_MS: Long = 128L
        const val PROOF_AGE_MS: Long = 256L
    }
    const val SCHEMA_SHA256: String = "b2cf35a8531d16a4fd1856101f7e02c7d110bf689bce403bd00c038ac083c34b"
    const val RECORD_TYPE: Int = 30
    const val HEADER_BYTES: Int = 72
    private val hashBytes = byteArrayOf(-78, -49, 53, -88, 83, 29, 22, -92, -3, 24, 86, 16, 31, 126, 2, -57, -47, 16, -65, 104, -101, -50, 64, 59, -48, 12, 3, -118, -64, -125, -61, 75)
    data class Meta(val producer: Int, val clock: Int, val traceSequence: Long,
                    val epoch: ULong, val bootId: ULong, val ticks: ULong)
    sealed interface Event {
        val id: Int
        fun write(out: ByteArray, base: Int)
    }
    private fun size(id: Int): Int = when(id) {
        1 -> 21
        2 -> 37
        3 -> 144
        4 -> 45
        5 -> 33
        6 -> 27
        7 -> 37
        8 -> 27
        9 -> 24
        10 -> 18
        11 -> 50
        else -> -1
    }
    private fun owns(id: Int, p: Int): Boolean = when(id) {
        1 -> p == 16
        2 -> p == 2 || p == 18 || p == 19
        3 -> p == 2 || p == 18 || p == 19
        4 -> p == 1 || p == 2 || p == 17 || p == 23
        5 -> p == 1 || p == 2 || p == 17 || p == 19
        6 -> p == 1 || p == 3 || p == 20 || p == 21 || p == 16 || p == 26
        7 -> p == 1 || p == 3 || p == 20 || p == 21 || p == 22 || p == 23 || p == 25 || p == 26
        8 -> p == 3 || p == 24
        9 -> p == 1 || p == 2 || p == 3 || p == 17 || p == 24 || p == 25
        10 -> p == 1 || p == 2 || p == 3 || p == 16 || p == 17 || p == 18 || p == 19 || p == 20 || p == 21 || p == 22 || p == 23 || p == 24 || p == 25 || p == 26
        11 -> p == 16
        else -> false
    }
    private fun clockValid(p: Int, c: Int): Boolean = when(p) {
        1 -> c == 1
        2 -> c == 1
        3 -> c == 1
        16 -> c == 3
        17 -> c == 3
        18 -> c == 2
        19 -> c == 2
        20 -> c == 2
        21 -> c == 2
        22 -> c == 2
        23 -> c == 3
        24 -> c == 2
        25 -> c == 3
        26 -> c == 3
        else -> false
    }
    private fun valid(meta: Meta, id: Int): Boolean =
        owns(id, meta.producer) && clockValid(meta.producer, meta.clock) &&
        meta.traceSequence in 0L..4294967295L &&
        (meta.clock != 1 || meta.ticks <= 4294967295uL)
    private fun put(out: ByteArray, offset: Int, value: ULong, count: Int) {
        for (i in 0 until count) out[offset+i] = (value shr (8*i)).toByte()
    }
    private fun get(bytes: ByteArray, offset: Int, count: Int): ULong {
        var value = 0uL
        for(i in 0 until count) value = value or ((bytes[offset+i].toInt() and 255).toULong() shl (8*i))
        return value
    }
    private fun crc(bytes: ByteArray, start: Int, end: Int): Int {
        var value=65535
        for(i in start until end) {
            value=value xor ((bytes[i].toInt() and 255) shl 8)
            repeat(8) { value=((value shl 1) xor (if(value and 32768 != 0) 4129 else 0)) and 65535 }
        }
        return value
    }
    data class Intent(
        val generation: Long,
        val authority_epoch: Long,
        val drive: Int,
        val steering: Int,
        val ehb: Long,
        val lane_mask: Int,
    ) : Event {
        override val id: Int get() = 1
        override fun write(out: ByteArray, base: Int) {
            require(generation in 0L..4294967295L)
            put(out, base + 0, generation.toULong(), 4)
            require(authority_epoch in 0L..4294967295L)
            put(out, base + 4, authority_epoch.toULong(), 4)
            put(out, base + 8, drive.toULong(), 4)
            put(out, base + 12, steering.toULong(), 4)
            require(ehb in 0L..4294967295L)
            put(out, base + 16, ehb.toULong(), 4)
            require(lane_mask in 0..255)
            put(out, base + 20, lane_mask.toULong(), 1)
        }
    }

    data class DatagramIo(
        val stage: Int,
        val operation_id: Long,
        val wire_sequence: Long,
        val generation: Long,
        val authority_epoch: Long,
        val proof_reference: Long,
        val rx_token: Long,
        val length: Int,
        val result: Int,
        val peer_ipv4: Long,
        val peer_port: Int,
    ) : Event {
        override val id: Int get() = 2
        override fun write(out: ByteArray, base: Int) {
            require(stage in 0..255)
            put(out, base + 0, stage.toULong(), 1)
            require(operation_id in 0L..4294967295L)
            put(out, base + 1, operation_id.toULong(), 4)
            require(wire_sequence in 0L..4294967295L)
            put(out, base + 5, wire_sequence.toULong(), 4)
            require(generation in 0L..4294967295L)
            put(out, base + 9, generation.toULong(), 4)
            require(authority_epoch in 0L..4294967295L)
            put(out, base + 13, authority_epoch.toULong(), 4)
            require(proof_reference in 0L..4294967295L)
            put(out, base + 17, proof_reference.toULong(), 4)
            require(rx_token in 0L..4294967295L)
            put(out, base + 21, rx_token.toULong(), 4)
            require(length in 0..65535)
            put(out, base + 25, length.toULong(), 2)
            put(out, base + 27, result.toULong(), 4)
            require(peer_ipv4 in 0L..4294967295L)
            put(out, base + 31, peer_ipv4.toULong(), 4)
            require(peer_port in 0..65535)
            put(out, base + 35, peer_port.toULong(), 2)
        }
    }

    data class DatagramBytes(
        val operation_id: Long,
        val wire_sequence: Long,
        val rx_token: Long,
        val wire_length: Int,
        val captured_length: Int,
        val data: ByteArray,
    ) : Event {
        override val id: Int get() = 3
        override fun write(out: ByteArray, base: Int) {
            require(operation_id in 0L..4294967295L)
            put(out, base + 0, operation_id.toULong(), 4)
            require(wire_sequence in 0L..4294967295L)
            put(out, base + 4, wire_sequence.toULong(), 4)
            require(rx_token in 0L..4294967295L)
            put(out, base + 8, rx_token.toULong(), 4)
            require(wire_length in 0..65535)
            put(out, base + 12, wire_length.toULong(), 2)
            require(captured_length in 0..65535)
            put(out, base + 14, captured_length.toULong(), 2)
            require(data.size == 128)
            data.copyInto(out, base + 16)
        }
    }

    data class Admission(
        val stage: Int,
        val reason: Long,
        val wire_sequence: Long,
        val generation: Long,
        val authority_epoch: Long,
        val proof_reference: Long,
        val rx_token: Long,
        val source_epoch: Long,
        val activation_epoch: Long,
        val forward_age_ms: Long,
        val proof_age_ms: Long,
        val valid_fields: Long,
    ) : Event {
        override val id: Int get() = 4
        override fun write(out: ByteArray, base: Int) {
            require(stage in 0..255)
            put(out, base + 0, stage.toULong(), 1)
            require(reason in 0L..4294967295L)
            put(out, base + 1, reason.toULong(), 4)
            require(wire_sequence in 0L..4294967295L)
            put(out, base + 5, wire_sequence.toULong(), 4)
            require(generation in 0L..4294967295L)
            put(out, base + 9, generation.toULong(), 4)
            require(authority_epoch in 0L..4294967295L)
            put(out, base + 13, authority_epoch.toULong(), 4)
            require(proof_reference in 0L..4294967295L)
            put(out, base + 17, proof_reference.toULong(), 4)
            require(rx_token in 0L..4294967295L)
            put(out, base + 21, rx_token.toULong(), 4)
            require(source_epoch in 0L..4294967295L)
            put(out, base + 25, source_epoch.toULong(), 4)
            require(activation_epoch in 0L..4294967295L)
            put(out, base + 29, activation_epoch.toULong(), 4)
            require(forward_age_ms in 0L..4294967295L)
            put(out, base + 33, forward_age_ms.toULong(), 4)
            require(proof_age_ms in 0L..4294967295L)
            put(out, base + 37, proof_age_ms.toULong(), 4)
            require(valid_fields in 0L..4294967295L)
            put(out, base + 41, valid_fields.toULong(), 4)
        }
    }

    data class Proof(
        val stage: Int,
        val sequence: Long,
        val admitted_sequence: Long,
        val generation: Long,
        val authority_epoch: Long,
        val rx_token: Long,
        val status: Long,
        val reason: Long,
        val flags: Long,
    ) : Event {
        override val id: Int get() = 5
        override fun write(out: ByteArray, base: Int) {
            require(stage in 0..255)
            put(out, base + 0, stage.toULong(), 1)
            require(sequence in 0L..4294967295L)
            put(out, base + 1, sequence.toULong(), 4)
            require(admitted_sequence in 0L..4294967295L)
            put(out, base + 5, admitted_sequence.toULong(), 4)
            require(generation in 0L..4294967295L)
            put(out, base + 9, generation.toULong(), 4)
            require(authority_epoch in 0L..4294967295L)
            put(out, base + 13, authority_epoch.toULong(), 4)
            require(rx_token in 0L..4294967295L)
            put(out, base + 17, rx_token.toULong(), 4)
            require(status in 0L..4294967295L)
            put(out, base + 21, status.toULong(), 4)
            require(reason in 0L..4294967295L)
            put(out, base + 25, reason.toULong(), 4)
            require(flags in 0L..4294967295L)
            put(out, base + 29, flags.toULong(), 4)
        }
    }

    data class Transaction(
        val stage: Int,
        val command_id: Long,
        val transaction_id: Long,
        val record_type: Int,
        val result: Int,
        val authority_epoch: Long,
        val generation: Long,
        val reason: Long,
    ) : Event {
        override val id: Int get() = 6
        override fun write(out: ByteArray, base: Int) {
            require(stage in 0..255)
            put(out, base + 0, stage.toULong(), 1)
            require(command_id in 0L..4294967295L)
            put(out, base + 1, command_id.toULong(), 4)
            require(transaction_id in 0L..4294967295L)
            put(out, base + 5, transaction_id.toULong(), 4)
            require(record_type in 0..65535)
            put(out, base + 9, record_type.toULong(), 2)
            put(out, base + 11, result.toULong(), 4)
            require(authority_epoch in 0L..4294967295L)
            put(out, base + 15, authority_epoch.toULong(), 4)
            require(generation in 0L..4294967295L)
            put(out, base + 19, generation.toULong(), 4)
            require(reason in 0L..4294967295L)
            put(out, base + 23, reason.toULong(), 4)
        }
    }

    data class Stream(
        val stage: Int,
        val channel: Int,
        val operation_id: Long,
        val publish_sequence: ULong,
        val byte_start: ULong,
        val length: Long,
        val result: Int,
        val record_type: Int,
        val reason: Long,
    ) : Event {
        override val id: Int get() = 7
        override fun write(out: ByteArray, base: Int) {
            require(stage in 0..255)
            put(out, base + 0, stage.toULong(), 1)
            require(channel in 0..65535)
            put(out, base + 1, channel.toULong(), 2)
            require(operation_id in 0L..4294967295L)
            put(out, base + 3, operation_id.toULong(), 4)
            put(out, base + 7, publish_sequence, 8)
            put(out, base + 15, byte_start, 8)
            require(length in 0L..4294967295L)
            put(out, base + 23, length.toULong(), 4)
            put(out, base + 27, result.toULong(), 4)
            require(record_type in 0..65535)
            put(out, base + 31, record_type.toULong(), 2)
            require(reason in 0L..4294967295L)
            put(out, base + 33, reason.toULong(), 4)
        }
    }

    data class Resource(
        val stage: Int,
        val resource_id: ULong,
        val resource_kind: Int,
        val operation_id: Long,
        val result: Int,
        val owner_epoch: ULong,
    ) : Event {
        override val id: Int get() = 8
        override fun write(out: ByteArray, base: Int) {
            require(stage in 0..255)
            put(out, base + 0, stage.toULong(), 1)
            put(out, base + 1, resource_id, 8)
            require(resource_kind in 0..65535)
            put(out, base + 9, resource_kind.toULong(), 2)
            require(operation_id in 0L..4294967295L)
            put(out, base + 11, operation_id.toULong(), 4)
            put(out, base + 15, result.toULong(), 4)
            put(out, base + 19, owner_epoch, 8)
        }
    }

    data class Capacity(
        val resource_id: Long,
        val used: Long,
        val high_water: Long,
        val failures: Long,
        val service_gap_ticks: ULong,
    ) : Event {
        override val id: Int get() = 9
        override fun write(out: ByteArray, base: Int) {
            require(resource_id in 0L..4294967295L)
            put(out, base + 0, resource_id.toULong(), 4)
            require(used in 0L..4294967295L)
            put(out, base + 4, used.toULong(), 4)
            require(high_water in 0L..4294967295L)
            put(out, base + 8, high_water.toULong(), 4)
            require(failures in 0L..4294967295L)
            put(out, base + 12, failures.toULong(), 4)
            put(out, base + 16, service_gap_ticks, 8)
        }
    }

    data class TraceLoss(
        val first_missing_sequence: Long,
        val last_missing_sequence: Long,
        val dropped: Long,
        val reason: Int,
        val high_water: Long,
    ) : Event {
        override val id: Int get() = 10
        override fun write(out: ByteArray, base: Int) {
            require(first_missing_sequence in 0L..4294967295L)
            put(out, base + 0, first_missing_sequence.toULong(), 4)
            require(last_missing_sequence in 0L..4294967295L)
            put(out, base + 4, last_missing_sequence.toULong(), 4)
            require(dropped in 0L..4294967295L)
            put(out, base + 8, dropped.toULong(), 4)
            require(reason in 0..65535)
            put(out, base + 12, reason.toULong(), 2)
            require(high_water in 0L..4294967295L)
            put(out, base + 14, high_water.toULong(), 4)
        }
    }

    data class IntentState(
        val stage: Int,
        val revision: ULong,
        val generation: Long,
        val authority_epoch: Long,
        val drive: Int,
        val steering: Int,
        val lane_mask: Int,
        val data005: ByteArray,
        val data007: ByteArray,
        val data364: ByteArray,
    ) : Event {
        override val id: Int get() = 11
        override fun write(out: ByteArray, base: Int) {
            require(stage in 0..255)
            put(out, base + 0, stage.toULong(), 1)
            put(out, base + 1, revision, 8)
            require(generation in 0L..4294967295L)
            put(out, base + 9, generation.toULong(), 4)
            require(authority_epoch in 0L..4294967295L)
            put(out, base + 13, authority_epoch.toULong(), 4)
            put(out, base + 17, drive.toULong(), 4)
            put(out, base + 21, steering.toULong(), 4)
            require(lane_mask in 0..255)
            put(out, base + 25, lane_mask.toULong(), 1)
            require(data005.size == 8)
            data005.copyInto(out, base + 26)
            require(data007.size == 8)
            data007.copyInto(out, base + 34)
            require(data364.size == 8)
            data364.copyInto(out, base + 42)
        }
    }
    /** DEBUG_TRACE: preallocated slot; primitive field methods do not allocate events. */
    class Writer {
        val bytes = ByteArray(523)
        var id: Int = 0
            private set
        var producer: Int = 0
            private set
        var clock: Int = 0
            private set
        var length: Int = 0
            private set
        fun begin(producer: Int, clock: Int, sequence: Long, epoch: ULong, boot: ULong, ticks: ULong) {
            require(clockValid(producer, clock) && sequence in 0L..4294967295L)
            require(clock != 1 || ticks <= 4294967295uL)
            this.producer = producer; this.clock = clock; id = 0; length = 0
            bytes[0]=165.toByte();bytes[1]=90.toByte();bytes[2]=1;bytes[3]=RECORD_TYPE.toByte();bytes[4]=0
            put(bytes,5,(sequence and 65535L).toULong(),2)
            bytes[9]=1;bytes[10]=producer.toByte();bytes[11]=clock.toByte();bytes[12]=0
            put(bytes,13,16909060uL,4);put(bytes,17,sequence.toULong(),4)
            put(bytes,21,epoch,8);put(bytes,29,boot,8);put(bytes,37,ticks,8)
            hashBytes.copyInto(bytes,49)
        }
        fun Intent(generation: Long, authority_epoch: Long, drive: Int, steering: Int, ehb: Long, lane_mask: Int) {
            require(id == 0)
            id = 1
            val out = bytes
            val base = 81
            require(generation in 0L..4294967295L)
            put(out, base + 0, generation.toULong(), 4)
            require(authority_epoch in 0L..4294967295L)
            put(out, base + 4, authority_epoch.toULong(), 4)
            put(out, base + 8, drive.toULong(), 4)
            put(out, base + 12, steering.toULong(), 4)
            require(ehb in 0L..4294967295L)
            put(out, base + 16, ehb.toULong(), 4)
            require(lane_mask in 0..255)
            put(out, base + 20, lane_mask.toULong(), 1)
        }

        fun DatagramIo(stage: Int, operation_id: Long, wire_sequence: Long, generation: Long, authority_epoch: Long, proof_reference: Long, rx_token: Long, length: Int, result: Int, peer_ipv4: Long, peer_port: Int) {
            require(id == 0)
            id = 2
            val out = bytes
            val base = 81
            require(stage in 0..255)
            put(out, base + 0, stage.toULong(), 1)
            require(operation_id in 0L..4294967295L)
            put(out, base + 1, operation_id.toULong(), 4)
            require(wire_sequence in 0L..4294967295L)
            put(out, base + 5, wire_sequence.toULong(), 4)
            require(generation in 0L..4294967295L)
            put(out, base + 9, generation.toULong(), 4)
            require(authority_epoch in 0L..4294967295L)
            put(out, base + 13, authority_epoch.toULong(), 4)
            require(proof_reference in 0L..4294967295L)
            put(out, base + 17, proof_reference.toULong(), 4)
            require(rx_token in 0L..4294967295L)
            put(out, base + 21, rx_token.toULong(), 4)
            require(length in 0..65535)
            put(out, base + 25, length.toULong(), 2)
            put(out, base + 27, result.toULong(), 4)
            require(peer_ipv4 in 0L..4294967295L)
            put(out, base + 31, peer_ipv4.toULong(), 4)
            require(peer_port in 0..65535)
            put(out, base + 35, peer_port.toULong(), 2)
        }

        fun DatagramBytes(operation_id: Long, wire_sequence: Long, rx_token: Long, wire_length: Int, captured_length: Int, data: ByteArray, dataOffset: Int = 0) {
            require(id == 0)
            id = 3
            val out = bytes
            val base = 81
            require(operation_id in 0L..4294967295L)
            put(out, base + 0, operation_id.toULong(), 4)
            require(wire_sequence in 0L..4294967295L)
            put(out, base + 4, wire_sequence.toULong(), 4)
            require(rx_token in 0L..4294967295L)
            put(out, base + 8, rx_token.toULong(), 4)
            require(wire_length in 0..65535)
            put(out, base + 12, wire_length.toULong(), 2)
            require(captured_length in 0..65535)
            put(out, base + 14, captured_length.toULong(), 2)
            require(captured_length in 0..128 && dataOffset >= 0 && dataOffset + captured_length <= data.size)
            out.fill(0, base + 16, base + 144)
            data.copyInto(out, base + 16, dataOffset, dataOffset + captured_length)
        }

        fun Admission(stage: Int, reason: Long, wire_sequence: Long, generation: Long, authority_epoch: Long, proof_reference: Long, rx_token: Long, source_epoch: Long, activation_epoch: Long, forward_age_ms: Long, proof_age_ms: Long, valid_fields: Long) {
            require(id == 0)
            id = 4
            val out = bytes
            val base = 81
            require(stage in 0..255)
            put(out, base + 0, stage.toULong(), 1)
            require(reason in 0L..4294967295L)
            put(out, base + 1, reason.toULong(), 4)
            require(wire_sequence in 0L..4294967295L)
            put(out, base + 5, wire_sequence.toULong(), 4)
            require(generation in 0L..4294967295L)
            put(out, base + 9, generation.toULong(), 4)
            require(authority_epoch in 0L..4294967295L)
            put(out, base + 13, authority_epoch.toULong(), 4)
            require(proof_reference in 0L..4294967295L)
            put(out, base + 17, proof_reference.toULong(), 4)
            require(rx_token in 0L..4294967295L)
            put(out, base + 21, rx_token.toULong(), 4)
            require(source_epoch in 0L..4294967295L)
            put(out, base + 25, source_epoch.toULong(), 4)
            require(activation_epoch in 0L..4294967295L)
            put(out, base + 29, activation_epoch.toULong(), 4)
            require(forward_age_ms in 0L..4294967295L)
            put(out, base + 33, forward_age_ms.toULong(), 4)
            require(proof_age_ms in 0L..4294967295L)
            put(out, base + 37, proof_age_ms.toULong(), 4)
            require(valid_fields in 0L..4294967295L)
            put(out, base + 41, valid_fields.toULong(), 4)
        }

        fun Proof(stage: Int, sequence: Long, admitted_sequence: Long, generation: Long, authority_epoch: Long, rx_token: Long, status: Long, reason: Long, flags: Long) {
            require(id == 0)
            id = 5
            val out = bytes
            val base = 81
            require(stage in 0..255)
            put(out, base + 0, stage.toULong(), 1)
            require(sequence in 0L..4294967295L)
            put(out, base + 1, sequence.toULong(), 4)
            require(admitted_sequence in 0L..4294967295L)
            put(out, base + 5, admitted_sequence.toULong(), 4)
            require(generation in 0L..4294967295L)
            put(out, base + 9, generation.toULong(), 4)
            require(authority_epoch in 0L..4294967295L)
            put(out, base + 13, authority_epoch.toULong(), 4)
            require(rx_token in 0L..4294967295L)
            put(out, base + 17, rx_token.toULong(), 4)
            require(status in 0L..4294967295L)
            put(out, base + 21, status.toULong(), 4)
            require(reason in 0L..4294967295L)
            put(out, base + 25, reason.toULong(), 4)
            require(flags in 0L..4294967295L)
            put(out, base + 29, flags.toULong(), 4)
        }

        fun Transaction(stage: Int, command_id: Long, transaction_id: Long, record_type: Int, result: Int, authority_epoch: Long, generation: Long, reason: Long) {
            require(id == 0)
            id = 6
            val out = bytes
            val base = 81
            require(stage in 0..255)
            put(out, base + 0, stage.toULong(), 1)
            require(command_id in 0L..4294967295L)
            put(out, base + 1, command_id.toULong(), 4)
            require(transaction_id in 0L..4294967295L)
            put(out, base + 5, transaction_id.toULong(), 4)
            require(record_type in 0..65535)
            put(out, base + 9, record_type.toULong(), 2)
            put(out, base + 11, result.toULong(), 4)
            require(authority_epoch in 0L..4294967295L)
            put(out, base + 15, authority_epoch.toULong(), 4)
            require(generation in 0L..4294967295L)
            put(out, base + 19, generation.toULong(), 4)
            require(reason in 0L..4294967295L)
            put(out, base + 23, reason.toULong(), 4)
        }

        fun Stream(stage: Int, channel: Int, operation_id: Long, publish_sequence: ULong, byte_start: ULong, length: Long, result: Int, record_type: Int, reason: Long) {
            require(id == 0)
            id = 7
            val out = bytes
            val base = 81
            require(stage in 0..255)
            put(out, base + 0, stage.toULong(), 1)
            require(channel in 0..65535)
            put(out, base + 1, channel.toULong(), 2)
            require(operation_id in 0L..4294967295L)
            put(out, base + 3, operation_id.toULong(), 4)
            put(out, base + 7, publish_sequence, 8)
            put(out, base + 15, byte_start, 8)
            require(length in 0L..4294967295L)
            put(out, base + 23, length.toULong(), 4)
            put(out, base + 27, result.toULong(), 4)
            require(record_type in 0..65535)
            put(out, base + 31, record_type.toULong(), 2)
            require(reason in 0L..4294967295L)
            put(out, base + 33, reason.toULong(), 4)
        }

        fun Resource(stage: Int, resource_id: ULong, resource_kind: Int, operation_id: Long, result: Int, owner_epoch: ULong) {
            require(id == 0)
            id = 8
            val out = bytes
            val base = 81
            require(stage in 0..255)
            put(out, base + 0, stage.toULong(), 1)
            put(out, base + 1, resource_id, 8)
            require(resource_kind in 0..65535)
            put(out, base + 9, resource_kind.toULong(), 2)
            require(operation_id in 0L..4294967295L)
            put(out, base + 11, operation_id.toULong(), 4)
            put(out, base + 15, result.toULong(), 4)
            put(out, base + 19, owner_epoch, 8)
        }

        fun Capacity(resource_id: Long, used: Long, high_water: Long, failures: Long, service_gap_ticks: ULong) {
            require(id == 0)
            id = 9
            val out = bytes
            val base = 81
            require(resource_id in 0L..4294967295L)
            put(out, base + 0, resource_id.toULong(), 4)
            require(used in 0L..4294967295L)
            put(out, base + 4, used.toULong(), 4)
            require(high_water in 0L..4294967295L)
            put(out, base + 8, high_water.toULong(), 4)
            require(failures in 0L..4294967295L)
            put(out, base + 12, failures.toULong(), 4)
            put(out, base + 16, service_gap_ticks, 8)
        }

        fun TraceLoss(first_missing_sequence: Long, last_missing_sequence: Long, dropped: Long, reason: Int, high_water: Long) {
            require(id == 0)
            id = 10
            val out = bytes
            val base = 81
            require(first_missing_sequence in 0L..4294967295L)
            put(out, base + 0, first_missing_sequence.toULong(), 4)
            require(last_missing_sequence in 0L..4294967295L)
            put(out, base + 4, last_missing_sequence.toULong(), 4)
            require(dropped in 0L..4294967295L)
            put(out, base + 8, dropped.toULong(), 4)
            require(reason in 0..65535)
            put(out, base + 12, reason.toULong(), 2)
            require(high_water in 0L..4294967295L)
            put(out, base + 14, high_water.toULong(), 4)
        }

        fun IntentState(stage: Int, revision: ULong, generation: Long, authority_epoch: Long, drive: Int, steering: Int, lane_mask: Int, data005: ByteArray, data007: ByteArray, data364: ByteArray) {
            require(id == 0)
            id = 11
            val out = bytes
            val base = 81
            require(stage in 0..255)
            put(out, base + 0, stage.toULong(), 1)
            put(out, base + 1, revision, 8)
            require(generation in 0L..4294967295L)
            put(out, base + 9, generation.toULong(), 4)
            require(authority_epoch in 0L..4294967295L)
            put(out, base + 13, authority_epoch.toULong(), 4)
            put(out, base + 17, drive.toULong(), 4)
            put(out, base + 21, steering.toULong(), 4)
            require(lane_mask in 0..255)
            put(out, base + 25, lane_mask.toULong(), 1)
            require(data005.size == 8)
            data005.copyInto(out, base + 26)
            require(data007.size == 8)
            data007.copyInto(out, base + 34)
            require(data364.size == 8)
            data364.copyInto(out, base + 42)
        }
        fun finish(): Int {
            val body = size(id)
            require(body >= 0 && owns(id, producer))
            length = 83 + body
            put(bytes,7,(HEADER_BYTES+body).toULong(),2)
            put(bytes,45,id.toULong(),2);put(bytes,47,body.toULong(),2)
            put(bytes,length-2,crc(bytes,2,length-2).toULong(),2)
            return length
        }
    }
    fun encode(meta: Meta, event: Event, out: ByteArray): Int {
        val bodySize=size(event.id)
        val length=83+bodySize
        require(bodySize>=0 && valid(meta,event.id) && out.size>=length)
        out[0]=165.toByte();out[1]=90.toByte();out[2]=1;out[3]=RECORD_TYPE.toByte();out[4]=0
        put(out,5,(meta.traceSequence and 65535L).toULong(),2)
        put(out,7,(HEADER_BYTES+bodySize).toULong(),2)
        out[9]=1;out[10]=meta.producer.toByte();out[11]=meta.clock.toByte();out[12]=0
        put(out,13,16909060uL,4);put(out,17,meta.traceSequence.toULong(),4)
        put(out,21,meta.epoch,8);put(out,29,meta.bootId,8);put(out,37,meta.ticks,8)
        put(out,45,event.id.toULong(),2);put(out,47,bodySize.toULong(),2)
        hashBytes.copyInto(out,49)
        event.write(out,81)
        put(out,length-2,crc(out,2,length-2).toULong(),2)
        return length
    }
    fun decode(bytes: ByteArray): Pair<Meta, Event> {
        require(bytes.size in 83..523)
        require(get(bytes,0,1)==165uL && get(bytes,1,1)==90uL &&
                bytes[2].toInt()==1 && bytes[3].toInt()==RECORD_TYPE && bytes[4].toInt()==0)
        require(get(bytes,7,2).toInt()==bytes.size-11)
        require(get(bytes,bytes.size-2,2).toInt()==crc(bytes,2,bytes.size-2))
        require(bytes[9].toInt()==1 && bytes[12].toInt()==0 && get(bytes,13,4)==16909060uL)
        require(hashBytes.indices.all { bytes[49+it]==hashBytes[it] })
        val meta=Meta(bytes[10].toInt() and 255,bytes[11].toInt() and 255,
                      get(bytes,17,4).toLong(),get(bytes,21,8),get(bytes,29,8),get(bytes,37,8))
        val id=get(bytes,45,2).toInt()
        require(valid(meta,id) && get(bytes,5,2).toLong()==(meta.traceSequence and 65535L))
        require(size(id)>=0 && get(bytes,47,2).toInt()==size(id) && bytes.size==83+size(id))
        val start=81
        val event=when(id) {
            1 -> Intent(get(bytes, start + 0, 4).toLong(), get(bytes, start + 4, 4).toLong(), get(bytes, start + 8, 4).toInt(), get(bytes, start + 12, 4).toInt(), get(bytes, start + 16, 4).toLong(), get(bytes, start + 20, 1).toInt())
            2 -> DatagramIo(get(bytes, start + 0, 1).toInt(), get(bytes, start + 1, 4).toLong(), get(bytes, start + 5, 4).toLong(), get(bytes, start + 9, 4).toLong(), get(bytes, start + 13, 4).toLong(), get(bytes, start + 17, 4).toLong(), get(bytes, start + 21, 4).toLong(), get(bytes, start + 25, 2).toInt(), get(bytes, start + 27, 4).toInt(), get(bytes, start + 31, 4).toLong(), get(bytes, start + 35, 2).toInt())
            3 -> DatagramBytes(get(bytes, start + 0, 4).toLong(), get(bytes, start + 4, 4).toLong(), get(bytes, start + 8, 4).toLong(), get(bytes, start + 12, 2).toInt(), get(bytes, start + 14, 2).toInt(), bytes.copyOfRange(start + 16, start + 16 + 128))
            4 -> Admission(get(bytes, start + 0, 1).toInt(), get(bytes, start + 1, 4).toLong(), get(bytes, start + 5, 4).toLong(), get(bytes, start + 9, 4).toLong(), get(bytes, start + 13, 4).toLong(), get(bytes, start + 17, 4).toLong(), get(bytes, start + 21, 4).toLong(), get(bytes, start + 25, 4).toLong(), get(bytes, start + 29, 4).toLong(), get(bytes, start + 33, 4).toLong(), get(bytes, start + 37, 4).toLong(), get(bytes, start + 41, 4).toLong())
            5 -> Proof(get(bytes, start + 0, 1).toInt(), get(bytes, start + 1, 4).toLong(), get(bytes, start + 5, 4).toLong(), get(bytes, start + 9, 4).toLong(), get(bytes, start + 13, 4).toLong(), get(bytes, start + 17, 4).toLong(), get(bytes, start + 21, 4).toLong(), get(bytes, start + 25, 4).toLong(), get(bytes, start + 29, 4).toLong())
            6 -> Transaction(get(bytes, start + 0, 1).toInt(), get(bytes, start + 1, 4).toLong(), get(bytes, start + 5, 4).toLong(), get(bytes, start + 9, 2).toInt(), get(bytes, start + 11, 4).toInt(), get(bytes, start + 15, 4).toLong(), get(bytes, start + 19, 4).toLong(), get(bytes, start + 23, 4).toLong())
            7 -> Stream(get(bytes, start + 0, 1).toInt(), get(bytes, start + 1, 2).toInt(), get(bytes, start + 3, 4).toLong(), get(bytes, start + 7, 8), get(bytes, start + 15, 8), get(bytes, start + 23, 4).toLong(), get(bytes, start + 27, 4).toInt(), get(bytes, start + 31, 2).toInt(), get(bytes, start + 33, 4).toLong())
            8 -> Resource(get(bytes, start + 0, 1).toInt(), get(bytes, start + 1, 8), get(bytes, start + 9, 2).toInt(), get(bytes, start + 11, 4).toLong(), get(bytes, start + 15, 4).toInt(), get(bytes, start + 19, 8))
            9 -> Capacity(get(bytes, start + 0, 4).toLong(), get(bytes, start + 4, 4).toLong(), get(bytes, start + 8, 4).toLong(), get(bytes, start + 12, 4).toLong(), get(bytes, start + 16, 8))
            10 -> TraceLoss(get(bytes, start + 0, 4).toLong(), get(bytes, start + 4, 4).toLong(), get(bytes, start + 8, 4).toLong(), get(bytes, start + 12, 2).toInt(), get(bytes, start + 14, 4).toLong())
            11 -> IntentState(get(bytes, start + 0, 1).toInt(), get(bytes, start + 1, 8), get(bytes, start + 9, 4).toLong(), get(bytes, start + 13, 4).toLong(), get(bytes, start + 17, 4).toInt(), get(bytes, start + 21, 4).toInt(), get(bytes, start + 25, 1).toInt(), bytes.copyOfRange(start + 26, start + 26 + 8), bytes.copyOfRange(start + 34, start + 34 + 8), bytes.copyOfRange(start + 42, start + 42 + 8))
            else -> error("unknown event")
        }
        return meta to event
    }
}
