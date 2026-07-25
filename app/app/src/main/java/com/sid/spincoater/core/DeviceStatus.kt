package com.sid.spincoater.core

import org.json.JSONObject

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

enum class ControlMode {
    OpenLoop, ClosedLoop, Unknown;

    val label: String
        get() = when (this) {
            OpenLoop -> "Open loop"
            ClosedLoop -> "Closed loop"
            Unknown -> "Unknown"
        }

    companion object {
        fun parse(s: String?) = when (s?.lowercase()) {
            "open_loop", "openloop", "0" -> OpenLoop
            "closed_loop", "closedloop", "1" -> ClosedLoop
            else -> Unknown
        }
    }
}

data class DeviceStatus(
    val rpmTarget: Int = 0,
    val openLoopRaw: Int = 0,
    val controlMode: ControlMode = ControlMode.Unknown,
    val accelMs: Int = 0,
    val status: String = "unknown",
    val direction: String = "cw",
    val running: Boolean = false,
    val emergency: Boolean = false,
    val alarm: Boolean = false,
    val btConnected: Boolean = false,
    val speedPulsesPerRevolution: Int = 4,
    val modbusAddress: Int = 1,
    val speedPulses: Long = 0,
    val speedRpm: Double = 0.0,
    val motorVoltage: Int = 0,
    val motorCurrent: Double = 0.0,
    val motorPower: Double = 0.0,
    val motorSpeed: Int = 0,
    val motorFault: Int = 0,
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
            return DeviceStatus(
                rpmTarget = json.optInt("target_rpm", 0),
                openLoopRaw = json.optInt("open_loop_raw", 0),
                controlMode = ControlMode.parse(json.optString("control_mode")),
                accelMs = json.optInt("accel_ms", 0),
                status = json.optString("status", "unknown"),
                direction = json.optString("direction", "cw"),
                running = json.optBoolean("running", false),
                emergency = json.optBoolean("emergency", false),
                alarm = json.optBoolean("alarm", false),
                btConnected = json.optBoolean("bt_connected", false),
                speedPulsesPerRevolution = json.optInt("speed_pulses_per_revolution", 4),
                modbusAddress = json.optInt("modbus_address", 1),
                speedPulses = json.optLong("speed_pulses", 0L),
                speedRpm = json.optDouble("speed_rpm", 0.0),
                motorVoltage = json.optInt("motor_voltage", 0),
                motorCurrent = json.optDouble("motor_current", 0.0),
                motorPower = json.optDouble("motor_power", 0.0),
                motorSpeed = json.optInt("motor_speed", 0),
                motorFault = json.optInt("motor_fault", 0),
                receivedAt = System.currentTimeMillis()
            )
        }
    }
}
