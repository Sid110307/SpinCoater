package com.sid.spincoater.core

object Protocol {
    const val DEVICE_NAME = "SpinCoat"
    const val PAIRING_PIN = "123456"

    const val MAX_COMMAND_LEN = 120
    const val MAX_PASSES = 5

    const val MIN_RPM = 200
    const val MAX_RPM = 4000

    const val MIN_DUTY = 10.0
    const val MAX_DUTY = 100.0

    const val MIN_ACCEL_MS = 300
    const val MAX_ACCEL_MS = 15000

    val VALID_PWM_PINS = listOf(13, 16, 17, 22, 32)

    const val START = "start"
    const val STOP = "stop"
    const val STATUS = "status"
    const val ESTOP = "estop"
    const val CLEAR_FAULT = "clearfault"

    fun rpm(value: Int) = "freq=${value.coerceIn(MIN_RPM, MAX_RPM)}"
    fun duty(value: Double) = "duty=${trim(value.coerceIn(MIN_DUTY, MAX_DUTY))}"
    fun accelMs(value: Int) = "pwmdur=${value.coerceIn(MIN_ACCEL_MS, MAX_ACCEL_MS)}"
    fun lengthMm(value: Double) = "move=${trim(value.coerceAtLeast(0.0))}"
    fun pwmPin(pin: Int) = "pwmpin=$pin"
    fun direction(clockwise: Boolean) = "dir=${if (clockwise) 1 else 0}"
    fun polePairs(count: Int) = "polepairs=${count.coerceAtLeast(1)}"
    fun addPass(rpm: Double, accel: Double, delayMs: Double) =
        "addpass=${trim(rpm)},${trim(accel)},${trim(delayMs)}"

    private fun trim(v: Double): String =
        if (v == v.toLong().toDouble()) v.toLong().toString() else "%.3f".format(v).trimEnd('0')

    fun explainError(code: String, field: String?): String = when (code) {
        "INVALID_CMD" -> "Invalid command: ${field ?: "unknown"}"
        "INVALID_VALUE" -> "Invalid value for ${field ?: "a field"}"
        "SAFETY_LOCKED" -> "Device is blocked due to safety lock or motor fault."
        "BUFFER_OVERFLOW" -> "Command buffer overflow."
        else -> "Device error: $code${field?.let { " ($it)" } ?: ""}"
    }
}
