package com.sid.spincoater.core

import org.json.JSONArray
import org.json.JSONObject

data class DevicePass(val rpm: Double, val accel: Double, val delayMs: Double)

val motorFaultMap = mapOf(
    2 to "Motor overloaded",
    3 to "Motor protection triggered",
    4 to "Low voltage",
    5 to "Over voltage",
    6 to "Motor blocked",
    7 to "Motor connection problem",
    8 to "Motor wiring problem",
    9 to "Overheating",
)

data class DeviceStatus(
    val rpmTarget: Int = 0,
    val duty: Double = 0.0,
    val lengthMm: Double = 0.0,
    val accelMs: Double = 0.0,
    val pwmPin: Int = 13,
    val status: String = "unknown",
    val direction: String = "cw",
    val running: Boolean = false,
    val emergency: Boolean = false,
    val alarm: Boolean = false,
    val btConnected: Boolean = false,
    val polePairs: Int = 4,
    val speedPulses: Long = 0,
    val speedRpm: Double = 0.0,
    val motorVoltage: Int = 0,
    val motorCurrent: Double = 0.0,
    val motorPower: Double = 0.0,
    val motorSpeed: Int = 0,
    val motorFault: Int = 0,
    val passes: List<DevicePass> = emptyList(),
    val receivedAt: Long = System.currentTimeMillis()
) {
    val clockwise: Boolean get() = direction.equals("cw", true)
    val canStart: Boolean get() = !emergency && !alarm
    val displayRpm: Double get() = if (motorSpeed > 0) motorSpeed.toDouble() else speedRpm

    val statusLabel: String
        get() = when (status) {
            "idle" -> "Idle"
            "running_cw" -> "Running"
            "running_ccw" -> "Running"
            "emergency_stop" -> "Emergency Stop"
            "alarm_fault" -> "Alarm Fault"
            else -> "Unknown"
        }

    companion object {
        fun fromJson(json: JSONObject): DeviceStatus? {
            if (!json.has("status") || !json.has("running")) return null

            val passes = mutableListOf<DevicePass>()
            (json.optJSONArray("passes") ?: JSONArray()).let { arr ->
                for (i in 0 until arr.length()) {
                    val p = arr.optJSONObject(i) ?: continue
                    passes += DevicePass(
                        rpm = p.optDouble("rpm", 0.0),
                        accel = p.optDouble("accel", 0.0),
                        delayMs = p.optDouble("delay", 0.0)
                    )
                }
            }

            return DeviceStatus(
                rpmTarget = json.optInt("freq", 0),
                duty = json.optDouble("duty", 0.0),
                lengthMm = json.optDouble("length_mm", json.optDouble("move", 0.0)),
                accelMs = json.optDouble("duration_ms", json.optDouble("pwmdur", 0.0)),
                pwmPin = json.optInt("pwmpin", 13),
                status = json.optString("status", "unknown"),
                direction = json.optString("direction", "cw"),
                running = json.optBoolean("running", false),
                emergency = json.optBoolean("emergency", false),
                alarm = json.optBoolean("alarm", false),
                btConnected = json.optBoolean("bt_connected", false),
                polePairs = json.optInt("pole_pairs", 4),
                speedPulses = json.optLong("speed_pulses", 0L),
                speedRpm = json.optDouble("speed_rpm", 0.0),
                motorVoltage = json.optInt("motor_voltage", 0),
                motorCurrent = json.optDouble("motor_current", 0.0),
                motorPower = json.optDouble("motor_power", 0.0),
                motorSpeed = json.optInt("motor_speed", 0),
                motorFault = json.optInt("motor_fault", 0),
                passes = passes
            )
        }

        fun passesFromTelemetry(arr: JSONArray): List<DevicePass> = buildList {
            for (i in 0 until arr.length()) {
                val p = arr.optJSONObject(i) ?: continue
                add(
                    DevicePass(
                        rpm = p.optDouble("rpm", 0.0),
                        accel = p.optDouble("accel", 0.0),
                        delayMs = p.optDouble("delay", 0.0)
                    )
                )
            }
        }
    }
}
