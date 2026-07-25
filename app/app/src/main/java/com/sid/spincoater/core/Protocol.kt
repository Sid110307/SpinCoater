package com.sid.spincoater.core

object Protocol {
    const val DEVICE_NAME = "SpinCoat"
    const val PAIRING_PIN = "123456"
    const val MAX_COMMAND_LEN = 120

    const val MIN_RPM = 200
    const val MAX_RPM = 4000
    const val MIN_ACCEL_MS = 300
    const val MAX_ACCEL_MS = 15000
    const val MIN_OPEN_LOOP_RAW = 0
    const val MAX_OPEN_LOOP_RAW = 1000
    const val MIN_SPEED_PULSES = 1
    const val MAX_SPEED_PULSES = 65535

    const val START = "start"
    const val STOP = "stop"
    const val STATUS = "status"
    const val ESTOP = "estop"
    const val CLEAR_FAULT = "clearfault"

    fun rpm(v: Int) = "rpm=${v.coerceIn(MIN_RPM, MAX_RPM)}"
    fun openLoopRaw(v: Int) = "open_loop_raw=${v.coerceIn(MIN_OPEN_LOOP_RAW, MAX_OPEN_LOOP_RAW)}"
    fun accelMs(v: Int) = "accel_ms=${v.coerceIn(MIN_ACCEL_MS, MAX_ACCEL_MS)}"
    fun speedPulsesPerRevolution(v: Int) =
        "speed_pulses_per_revolution=${v.coerceIn(MIN_SPEED_PULSES, MAX_SPEED_PULSES)}"

    fun direction(cw: Boolean) = "dir=${if (cw) 1 else 0}"
    fun mode(closedLoop: Boolean) = "mode=${if (closedLoop) "closed_loop" else "open_loop"}"
    fun explainError(code: String, field: String?): String = when (code) {
        "INVALID_CMD" -> "Invalid command: ${field ?: "unknown"}"
        "INVALID_VALUE" -> "Invalid value for ${field ?: "a field"}"
        "SAFETY_LOCKED" -> "Device is blocked due to safety lock or motor fault."
        "BUFFER_OVERFLOW" -> "Command buffer overflow."
        else -> "Device error: $code${field?.let { " ($it)" } ?: ""}"
    }
}
