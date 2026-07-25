package com.sid.spincoater.viewmodel

import android.annotation.SuppressLint
import android.app.Application
import androidx.lifecycle.AndroidViewModel
import androidx.lifecycle.viewModelScope
import com.sid.spincoater.core.AppDatabase
import com.sid.spincoater.core.BtDevice
import com.sid.spincoater.core.BtManager
import com.sid.spincoater.core.ConnState
import com.sid.spincoater.core.ControlMode
import com.sid.spincoater.core.DeviceStatus
import com.sid.spincoater.core.LineDir
import com.sid.spincoater.core.LogLine
import com.sid.spincoater.core.Prefs
import com.sid.spincoater.core.Protocol
import com.sid.spincoater.core.Recipe
import com.sid.spincoater.core.RecipeRepo
import com.sid.spincoater.core.RecipeStep
import com.sid.spincoater.core.RecipeWithSteps
import kotlinx.coroutines.Job
import kotlinx.coroutines.delay
import kotlinx.coroutines.flow.MutableSharedFlow
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.SharedFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.isActive
import kotlinx.coroutines.launch
import kotlin.time.Duration.Companion.milliseconds

data class RunProgress(
    val recipeName: String,
    val stepIndex: Int,
    val stepCount: Int,
    val stepLabel: String,
    val elapsedMs: Long,
    val totalMs: Long
) {
    val fraction: Float
        get() = if (totalMs <= 0) 0f else (elapsedMs.toFloat() / totalMs).coerceIn(
            0f, 1f
        )
}

@SuppressLint("MissingPermission")
class MainViewModel(app: Application, previewMode: Boolean = false) : AndroidViewModel(app) {

    private val isPreview = previewMode

    private val db by lazy { AppDatabase.get(getApplication()) }
    private val repo by lazy { RecipeRepo(db.recipeDao()) }
    private val prefs by lazy { Prefs(getApplication()) }
    val bt by lazy { BtManager(getApplication()) }

    private val _connState = MutableStateFlow(ConnState.Disconnected)
    val connState: StateFlow<ConnState> = _connState.asStateFlow()

    private val _connectedAddress = MutableStateFlow<String?>(null)
    val connectedAddress: StateFlow<String?> = _connectedAddress.asStateFlow()

    private val _devices = MutableStateFlow<List<BtDevice>>(emptyList())
    val devices: StateFlow<List<BtDevice>> = _devices.asStateFlow()

    private val _discovering = MutableStateFlow(false)
    val discovering: StateFlow<Boolean> = _discovering.asStateFlow()

    private val _status = MutableStateFlow<DeviceStatus?>(null)
    val status: StateFlow<DeviceStatus?> = _status.asStateFlow()

    private val _log = MutableStateFlow<List<LogLine>>(emptyList())
    val log: StateFlow<List<LogLine>> = _log.asStateFlow()

    private val _rpmHistory = MutableStateFlow<List<Double>>(emptyList())
    val rpmHistory: StateFlow<List<Double>> = _rpmHistory.asStateFlow()

    private val _toast = MutableSharedFlow<String>(extraBufferCapacity = 8)
    val toast: SharedFlow<String> = _toast

    private val _recipes = MutableStateFlow<List<RecipeWithSteps>>(emptyList())
    val recipes: StateFlow<List<RecipeWithSteps>> = _recipes.asStateFlow()

    private val _runProgress = MutableStateFlow<RunProgress?>(null)
    val runProgress: StateFlow<RunProgress?> = _runProgress.asStateFlow()

    private var runJob: Job? = null

    private val _autoConnect = MutableStateFlow(true)
    val autoConnect: StateFlow<Boolean> = _autoConnect.asStateFlow()

    private val _pollIntervalMs = MutableStateFlow(750L)
    val pollIntervalMs: StateFlow<Long> = _pollIntervalMs.asStateFlow()

    private val _confirmBeforeStart = MutableStateFlow(false)
    val confirmBeforeStart: StateFlow<Boolean> = _confirmBeforeStart.asStateFlow()

    private val _keepScreenOn = MutableStateFlow(true)
    val keepScreenOn: StateFlow<Boolean> = _keepScreenOn.asStateFlow()

    init {
        if (!previewMode) {
            _autoConnect.value = prefs.autoConnect
            _pollIntervalMs.value = prefs.pollIntervalMs
            _confirmBeforeStart.value = prefs.confirmBeforeStart
            _keepScreenOn.value = prefs.keepScreenOn
            bt.pollIntervalMs = prefs.pollIntervalMs

            viewModelScope.launch { bt.state.collect { _connState.value = it } }
            viewModelScope.launch {
                bt.connectedAddress.collect {
                    _connectedAddress.value = it
                    if (it != null) prefs.lastAddress = it
                }
            }
            viewModelScope.launch { bt.devices.collect { _devices.value = it } }
            viewModelScope.launch { bt.discovering.collect { _discovering.value = it } }
            viewModelScope.launch {
                bt.status.collect { s ->
                    _status.value = s
                    if (s != null) {
                        _rpmHistory.value = (_rpmHistory.value + s.displayRpm).takeLast(60)
                        if (s.emergency || s.alarm) abortRun("Run aborted: device fault")
                    }
                }
            }
            viewModelScope.launch {
                bt.log.collect { line -> _log.value = (_log.value + line).takeLast(1000) }
            }
            viewModelScope.launch { bt.errors.collect { _toast.emit(it) } }

            loadRecipes()
            bt.registerReceivers()
        } else {
            _connState.value = ConnState.Connected
            _connectedAddress.value = "AA:BB:CC:DD:EE:01"
            _devices.value = listOf(
                BtDevice("SpinCoat", "AA:BB:CC:DD:EE:01", bonded = true),
                BtDevice("ESP32-Test", "AA:BB:CC:DD:EE:02", bonded = false)
            )
            _status.value = DeviceStatus(
                rpmTarget = 1500,
                openLoopRaw = 0,
                controlMode = ControlMode.ClosedLoop,
                speedPulsesPerRevolution = 4,
                accelMs = 1200,
                status = "running_cw",
                direction = "cw",
                running = true,
                btConnected = true,
                speedRpm = 1487.0,
                motorVoltage = 24,
                motorCurrent = 1.32,
                motorPower = 31.7,
                motorSpeed = 1492
            )
            _rpmHistory.value = List(30) { 1400.0 + (it % 7) * 15 }
            _log.value = listOf(
                LogLine(dir = LineDir.Tx, text = "status"),
                LogLine(dir = LineDir.Rx, text = "{\"status\":\"running_cw\"}")
            )
        }
    }

    constructor(app: Application) : this(app, false)

    override fun onCleared() {
        super.onCleared()
        runJob?.cancel()
        if (!isPreview) bt.shutdown()
    }

    fun refreshDevices() {
        if (isPreview || !bt.hasConnectPermission()) return
        bt.refreshBonded()
    }

    fun startDiscovery() {
        if (isPreview || !bt.hasScanPermission() || !bt.hasConnectPermission()) return
        bt.startDiscovery()
    }

    fun stopDiscovery() {
        if (isPreview || !bt.hasScanPermission()) return
        bt.stopDiscovery()
    }

    fun connectTo(address: String) {
        if (isPreview || !bt.hasConnectPermission()) return
        bt.connect(address)
    }

    fun pair(address: String) {
        if (isPreview || !bt.hasConnectPermission()) return
        if (!bt.bond(address)) viewModelScope.launch { _toast.emit("Could not pair with $address") }
    }

    fun disconnect() {
        abortRun(null)
        if (isPreview) return
        bt.disconnect()
    }

    fun autoConnect() {
        if (isPreview || !_autoConnect.value) return
        if (_connState.value != ConnState.Disconnected) return

        val last = prefs.lastAddress ?: return
        if (!bt.hasConnectPermission() || !bt.isBluetoothOn()) return
        bt.connect(last)
    }

    private fun send(command: String) {
        if (isPreview) return
        bt.enqueue(command)
    }

    private fun sendNow(command: String) {
        if (isPreview) return
        bt.sendImmediate(command)
    }

    fun start() {
        val s = _status.value
        if (s != null && !s.canStart) {
            viewModelScope.launch { _toast.emit("Clear the fault before starting") }
            return
        }

        send(Protocol.START)
    }

    fun stop() {
        abortRun(null)
        sendNow(Protocol.STOP)
        send(Protocol.STATUS)
    }

    fun emergencyStop() {
        abortRun("Emergency stop")
        sendNow(Protocol.ESTOP)
        send(Protocol.STATUS)
    }

    fun clearFault() {
        send(Protocol.CLEAR_FAULT)
        send(Protocol.STATUS)
    }

    fun refreshStatus() = send(Protocol.STATUS)

    fun setRpm(rpm: Int) = send(Protocol.rpm(rpm))
    fun setOpenLoopRaw(raw: Int) = send(Protocol.openLoopRaw(raw))
    fun setAccelMs(ms: Int) = send(Protocol.accelMs(ms))
    fun setSpeedPulsesPerRevolution(count: Int) = send(Protocol.speedPulsesPerRevolution(count))

    fun setDirection(clockwise: Boolean) {
        send(Protocol.direction(clockwise))
        send(Protocol.STATUS)
    }

    fun setControlMode(closedLoop: Boolean) {
        send(Protocol.mode(closedLoop))
        send(Protocol.STATUS)
    }

    fun clearLog() {
        _log.value = emptyList()
    }

    fun exportLog(): String = _log.value.joinToString("\n") { entry ->
        val tag = when (entry.dir) {
            LineDir.Tx -> ">>"
            LineDir.Rx -> "<<"
            LineDir.Info -> "--"
            LineDir.Error -> "!!"
        }
        "${entry.at} $tag ${entry.text}"
    }

    fun runRecipe(recipe: RecipeWithSteps) {
        if (_connState.value != ConnState.Connected) {
            viewModelScope.launch { _toast.emit("Device not connected") }
            return
        }
        val steps = recipe.ordered
        if (steps.isEmpty()) {
            viewModelScope.launch { _toast.emit("This recipe has no steps") }
            return
        }
        if (_status.value?.canStart == false) {
            viewModelScope.launch { _toast.emit("Clear the fault before starting") }
            return
        }

        runJob?.cancel()
        runJob = viewModelScope.launch {
            val total = steps.sumOf { (it.accelMs + it.holdMs).toLong() }
            var elapsed = 0L

            try {
                steps.forEachIndexed { index, step ->
                    if (!isActive) return@launch
                    _runProgress.value = RunProgress(
                        recipeName = recipe.recipe.name,
                        stepIndex = index,
                        stepCount = steps.size,
                        stepLabel = "${step.rpm} rpm - ramp ${step.accelMs}ms - hold ${step.holdMs}ms",
                        elapsedMs = elapsed,
                        totalMs = total
                    )

                    if (index == 0 && _status.value?.clockwise != step.clockwise) {
                        send(Protocol.direction(step.clockwise))
                        delay(200.milliseconds)
                    }

                    send(Protocol.accelMs(step.accelMs))
                    send(Protocol.rpm(step.rpm))
                    if (index == 0) send(Protocol.START)

                    val stepDuration = (step.accelMs + step.holdMs).toLong()
                    val tick = 100L
                    var spent = 0L

                    while (spent < stepDuration) {
                        if (!isActive) return@launch

                        val s = _status.value
                        if (s != null && (s.emergency || s.alarm)) {
                            abortRun("Device fault during run")
                            return@launch
                        }

                        delay(tick.milliseconds)
                        spent += tick
                        elapsed += tick
                        _runProgress.value = _runProgress.value?.copy(elapsedMs = elapsed)
                    }
                }

                sendNow(Protocol.STOP)
                send(Protocol.STATUS)
                _toast.emit("Recipe \"${recipe.recipe.name}\" complete")
            } finally {
                _runProgress.value = null
            }
        }
    }

    fun abortRun(message: String?) {
        val wasRunning = runJob?.isActive == true
        runJob?.cancel()
        runJob = null
        _runProgress.value = null
        if (wasRunning && message != null) viewModelScope.launch { _toast.emit(message) }
    }

    fun cancelRun() {
        abortRun("Run cancelled")
        sendNow(Protocol.STOP)
        send(Protocol.STATUS)
    }

    fun loadRecipes() = viewModelScope.launch {
        if (isPreview) return@launch
        _recipes.value = repo.list()
    }

    fun saveRecipe(existingId: Long?, name: String, notes: String, steps: List<RecipeStep>) =
        viewModelScope.launch {
            if (isPreview) return@launch
            repo.save(existingId, name.trim(), notes.trim(), steps)
            _recipes.value = repo.list()
        }

    fun deleteRecipe(recipe: Recipe) = viewModelScope.launch {
        if (isPreview) return@launch
        repo.delete(recipe)
        _recipes.value = repo.list()
    }

    fun setAutoConnect(value: Boolean) {
        _autoConnect.value = value
        if (!isPreview) prefs.autoConnect = value
    }

    fun setPollInterval(ms: Long) {
        val clamped = ms.coerceIn(250L, 5000L)
        _pollIntervalMs.value = clamped
        if (isPreview) return
        prefs.pollIntervalMs = clamped
        bt.pollIntervalMs = clamped
    }

    fun setConfirmBeforeStart(value: Boolean) {
        _confirmBeforeStart.value = value
        if (!isPreview) prefs.confirmBeforeStart = value
    }

    fun setKeepScreenOn(value: Boolean) {
        _keepScreenOn.value = value
        if (!isPreview) prefs.keepScreenOn = value
    }

    companion object {
        fun preview() = MainViewModel(Application(), true)
    }
}
