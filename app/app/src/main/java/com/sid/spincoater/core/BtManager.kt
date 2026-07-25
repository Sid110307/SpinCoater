package com.sid.spincoater.core

import android.Manifest
import android.annotation.SuppressLint
import android.bluetooth.BluetoothAdapter
import android.bluetooth.BluetoothDevice
import android.bluetooth.BluetoothManager
import android.bluetooth.BluetoothSocket
import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import android.content.IntentFilter
import android.content.pm.PackageManager
import android.os.Build
import androidx.annotation.RequiresPermission
import androidx.core.app.ActivityCompat
import androidx.core.content.ContextCompat
import kotlinx.coroutines.CompletableDeferred
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.Job
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.channels.BufferOverflow
import kotlinx.coroutines.channels.Channel
import kotlinx.coroutines.delay
import kotlinx.coroutines.flow.MutableSharedFlow
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.SharedFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.isActive
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import kotlinx.coroutines.withTimeoutOrNull
import org.json.JSONObject
import timber.log.Timber
import java.io.IOException
import java.io.InputStream
import java.io.OutputStream
import java.util.UUID
import java.util.concurrent.atomic.AtomicInteger
import kotlin.time.Duration.Companion.milliseconds

enum class ConnState { Disconnected, Connecting, Connected, Reconnecting }
enum class LineDir { Tx, Rx, Info, Error }

data class BtDevice(
    val name: String?,
    val address: String,
    val bonded: Boolean,
) {
    val isSpinCoater: Boolean get() = name?.contains(Protocol.DEVICE_NAME, true) == true
}

data class LogLine(
    val id: Long = System.nanoTime(),
    val at: Long = System.currentTimeMillis(),
    val dir: LineDir,
    val text: String
)

@SuppressLint("MissingPermission")
class BtManager(private val context: Context) {
    companion object {
        val SPP_UUID: UUID = UUID.fromString("00001101-0000-1000-8000-00805F9B34FB")

        private const val ACK_TIMEOUT_MS = 2500L
        private const val INTER_COMMAND_GAP_MS = 60L
        private const val RECONNECT_BASE_MS = 1500L
        private const val MAX_RECONNECT_ATTEMPTS = 5
    }

    private val scope = CoroutineScope(SupervisorJob() + Dispatchers.IO)
    private val adapter: BluetoothAdapter?
        get() = (context.getSystemService(Context.BLUETOOTH_SERVICE) as? BluetoothManager)?.adapter

    private var socket: BluetoothSocket? = null
    private var input: InputStream? = null
    private var output: OutputStream? = null
    private val writeLock = Any()

    private var readJob: Job? = null
    private var writeJob: Job? = null
    private var pollJob: Job? = null

    private val queue = Channel<String>(capacity = Channel.UNLIMITED)
    private val queueDepth = AtomicInteger(0)

    @Volatile
    private var pendingAck: CompletableDeferred<Unit>? = null

    private val _state = MutableStateFlow(ConnState.Disconnected)
    val state: StateFlow<ConnState> = _state.asStateFlow()

    private val _connectedAddress = MutableStateFlow<String?>(null)
    val connectedAddress: StateFlow<String?> = _connectedAddress.asStateFlow()

    private val _devices = MutableStateFlow<List<BtDevice>>(emptyList())
    val devices: StateFlow<List<BtDevice>> = _devices.asStateFlow()

    private val _discovering = MutableStateFlow(false)
    val discovering: StateFlow<Boolean> = _discovering.asStateFlow()

    private val _status = MutableStateFlow<DeviceStatus?>(null)
    val status: StateFlow<DeviceStatus?> = _status.asStateFlow()

    private val _log = MutableSharedFlow<LogLine>(
        replay = 0, extraBufferCapacity = 256, onBufferOverflow = BufferOverflow.DROP_OLDEST
    )
    val log: SharedFlow<LogLine> = _log

    private val _errors = MutableSharedFlow<String>(
        replay = 0, extraBufferCapacity = 16, onBufferOverflow = BufferOverflow.DROP_OLDEST
    )
    val errors: SharedFlow<String> = _errors

    var pollIntervalMs: Long = 750L
    private val seen = LinkedHashMap<String, BtDevice>()
    private var wantedAddress: String? = null
    private var reconnectAttempts = 0
    private var userInitiatedDisconnect = false
    private var receiverRegistered = false

    fun hasConnectPermission(): Boolean =
        if (Build.VERSION.SDK_INT >= 31) ActivityCompat.checkSelfPermission(
            context, Manifest.permission.BLUETOOTH_CONNECT
        ) == PackageManager.PERMISSION_GRANTED else true

    fun hasScanPermission(): Boolean = when {
        Build.VERSION.SDK_INT >= 31 -> ActivityCompat.checkSelfPermission(
            context, Manifest.permission.BLUETOOTH_SCAN
        ) == PackageManager.PERMISSION_GRANTED

        else -> ActivityCompat.checkSelfPermission(
            context, Manifest.permission.ACCESS_FINE_LOCATION
        ) == PackageManager.PERMISSION_GRANTED
    }

    fun isBluetoothOn(): Boolean = adapter?.isEnabled == true

    private val discoveryReceiver = object : BroadcastReceiver() {
        override fun onReceive(ctx: Context?, intent: Intent?) {
            when (intent?.action) {
                BluetoothDevice.ACTION_FOUND -> {
                    if (!hasConnectPermission()) return
                    val device: BluetoothDevice? =
                        if (Build.VERSION.SDK_INT >= 33) intent.getParcelableExtra(
                            BluetoothDevice.EXTRA_DEVICE, BluetoothDevice::class.java
                        ) else @Suppress("DEPRECATION") intent.getParcelableExtra(
                            BluetoothDevice.EXTRA_DEVICE
                        )

                    val dev = device ?: return
                    val entry = BtDevice(
                        name = runCatching { dev.name }.getOrNull(),
                        address = dev.address,
                        bonded = dev.bondState == BluetoothDevice.BOND_BONDED
                    )

                    seen[entry.address] = entry
                    publishDevices()
                }

                BluetoothAdapter.ACTION_DISCOVERY_STARTED -> _discovering.value = true
                BluetoothAdapter.ACTION_DISCOVERY_FINISHED -> _discovering.value = false
                BluetoothDevice.ACTION_BOND_STATE_CHANGED -> refreshBonded()
            }
        }
    }

    fun registerReceivers() {
        if (receiverRegistered) return

        val filter = IntentFilter().apply {
            addAction(BluetoothDevice.ACTION_FOUND)
            addAction(BluetoothDevice.ACTION_BOND_STATE_CHANGED)
            addAction(BluetoothAdapter.ACTION_DISCOVERY_STARTED)
            addAction(BluetoothAdapter.ACTION_DISCOVERY_FINISHED)
        }
        ContextCompat.registerReceiver(
            context, discoveryReceiver, filter, ContextCompat.RECEIVER_NOT_EXPORTED
        )
        receiverRegistered = true
    }

    fun unregisterReceivers() {
        if (!receiverRegistered) return
        runCatching { context.unregisterReceiver(discoveryReceiver) }
        receiverRegistered = false
    }

    @RequiresPermission(Manifest.permission.BLUETOOTH_CONNECT)
    fun refreshBonded() {
        if (!hasConnectPermission()) return
        adapter?.bondedDevices?.forEach { dev ->
            seen[dev.address] = BtDevice(
                name = runCatching { dev.name }.getOrNull(),
                address = dev.address,
                bonded = true,
            )
        }

        publishDevices()
    }

    private fun publishDevices() {
        _devices.value =
            seen.values.sortedWith(compareByDescending<BtDevice> { it.isSpinCoater }.thenByDescending { it.bonded })
    }

    @RequiresPermission(allOf = [Manifest.permission.BLUETOOTH_SCAN, Manifest.permission.BLUETOOTH_CONNECT])
    fun startDiscovery() {
        if (!hasScanPermission() || !isBluetoothOn()) return

        registerReceivers()
        seen.keys.retainAll { seen[it]?.bonded == true }
        refreshBonded()

        adapter?.let { a ->
            if (a.isDiscovering) a.cancelDiscovery()
            if (!a.startDiscovery()) Timber.e("startDiscovery() rejected")
        }
    }

    @RequiresPermission(Manifest.permission.BLUETOOTH_SCAN)
    fun stopDiscovery() {
        runCatching { if (adapter?.isDiscovering == true) adapter?.cancelDiscovery() }
        _discovering.value = false
    }

    @RequiresPermission(Manifest.permission.BLUETOOTH_CONNECT)
    fun bond(address: String): Boolean {
        if (!hasConnectPermission()) return false

        val dev = runCatching { adapter?.getRemoteDevice(address) }.getOrNull() ?: return false
        if (dev.bondState == BluetoothDevice.BOND_BONDED) return true

        return runCatching { dev.createBond() }.getOrDefault(false)
    }

    @RequiresPermission(allOf = [Manifest.permission.BLUETOOTH_CONNECT, Manifest.permission.BLUETOOTH_SCAN])
    fun connect(address: String) {
        if (!hasConnectPermission() || !isBluetoothOn()) return

        userInitiatedDisconnect = false
        wantedAddress = address
        reconnectAttempts = 0

        scope.launch { openSocket(address) }
    }

    @RequiresPermission(allOf = [Manifest.permission.BLUETOOTH_CONNECT, Manifest.permission.BLUETOOTH_SCAN])
    private suspend fun openSocket(address: String) {
        closeSocket()
        _state.value = if (reconnectAttempts == 0) ConnState.Connecting else ConnState.Reconnecting
        emitLog(LineDir.Info, "Connecting to $address")
        stopDiscovery()

        val device = runCatching { adapter?.getRemoteDevice(address) }.getOrNull() ?: run {
            fail("Invalid address $address")
            return
        }

        if (device.bondState != BluetoothDevice.BOND_BONDED) {
            emitLog(
                LineDir.Info, "Device not paired. Bonding... (PIN ${Protocol.PAIRING_PIN})"
            )
            runCatching { device.createBond() }
        }

        val result = withContext(Dispatchers.IO) {
            runCatching {
                val s = device.createRfcommSocketToServiceRecord(SPP_UUID)
                s.connect()
                s
            }.recoverCatching {
                Timber.w(it, "createRfcommSocketToServiceRecord failed, trying reflection")

                val m =
                    device.javaClass.getMethod("createRfcommSocket", Int::class.javaPrimitiveType)
                val s = m.invoke(device, 1) as BluetoothSocket
                s.connect()
                s
            }
        }

        val sock = result.getOrElse { e ->
            Timber.e(e, "RFCOMM connect failed")
            fail("Could not connect. Make sure the device is powered on and paired.")
            scheduleReconnect()

            return
        }

        socket = sock
        input = sock.inputStream
        output = sock.outputStream
        reconnectAttempts = 0

        _connectedAddress.value = address
        _state.value = ConnState.Connected
        emitLog(LineDir.Info, "Connected")

        startReadLoop()
        startWriteLoop()
        startPolling()

        enqueue(Protocol.STATUS)
    }

    fun disconnect() {
        userInitiatedDisconnect = true
        wantedAddress = null

        scope.launch {
            closeSocket()
            _state.value = ConnState.Disconnected
            _connectedAddress.value = null
            _status.value = null
            emitLog(LineDir.Info, "Disconnected")
        }
    }

    private fun fail(message: String) {
        _state.value = ConnState.Disconnected
        _connectedAddress.value = null
        emitLog(LineDir.Error, message)
        _errors.tryEmit(message)
    }

    @RequiresPermission(allOf = [Manifest.permission.BLUETOOTH_CONNECT, Manifest.permission.BLUETOOTH_SCAN])
    private fun scheduleReconnect() {
        val target = wantedAddress ?: return
        if (userInitiatedDisconnect) return

        if (reconnectAttempts >= MAX_RECONNECT_ATTEMPTS) {
            emitLog(LineDir.Error, "Giving up after $MAX_RECONNECT_ATTEMPTS reconnect attempts")
            return
        }
        reconnectAttempts++
        val backoff = RECONNECT_BASE_MS * reconnectAttempts
        _state.value = ConnState.Reconnecting

        scope.launch {
            delay(backoff.milliseconds)
            if (!userInitiatedDisconnect) openSocket(target)
        }
    }

    private suspend fun closeSocket() = withContext(Dispatchers.IO) {
        readJob?.cancel()
        readJob = null
        writeJob?.cancel()
        writeJob = null
        pollJob?.cancel()
        pollJob = null

        runCatching { input?.close() }
        runCatching { output?.close() }
        runCatching { socket?.close() }

        input = null
        output = null
        socket = null
    }

    @RequiresPermission(allOf = [Manifest.permission.BLUETOOTH_CONNECT, Manifest.permission.BLUETOOTH_SCAN])
    private fun startReadLoop() {
        readJob = scope.launch {
            val buffer = ByteArray(1024)
            val acc = StringBuilder(512)
            val stream = input ?: return@launch

            while (isActive) {
                val read = try {
                    stream.read(buffer)
                } catch (e: IOException) {
                    if (isActive && !userInitiatedDisconnect) {
                        Timber.w(e, "read failed")
                        emitLog(LineDir.Error, "Link dropped")
                        scheduleReconnect()
                    }
                    return@launch
                }
                if (read <= 0) continue

                acc.append(String(buffer, 0, read, Charsets.US_ASCII))
                if (acc.length > 8192) acc.delete(0, acc.length - 2048)

                while (true) {
                    val nl = acc.indexOf("\n")
                    if (nl < 0) break
                    val line = acc.substring(0, nl).trim()
                    acc.delete(0, nl + 1)
                    if (line.isNotEmpty()) handleLine(line)
                }
            }
        }
    }

    private fun handleLine(line: String) {
        emitLog(LineDir.Rx, line)

        when {
            line.startsWith("{") -> {
                val obj = runCatching { JSONObject(line) }.getOrNull()
                if (obj == null) {
                    releaseAck(); return
                }
                if (obj.has("error")) {
                    val msg = Protocol.explainError(
                        obj.optString("error"), obj.optString("field").takeIf { it.isNotBlank() })
                    _errors.tryEmit(msg)
                    emitLog(LineDir.Error, msg)
                } else {
                    DeviceStatus.fromJson(obj)?.let { _status.value = it }
                }
                releaseAck()
            }

            line.equals("OK", true) -> releaseAck()
            line.equals("Ready", true) -> {
                emitLog(LineDir.Info, "Device reported ready")
                enqueue(Protocol.STATUS)
            }
        }
    }

    private fun releaseAck() {
        pendingAck?.complete(Unit)
    }

    private fun startWriteLoop() {
        writeJob = scope.launch {
            for (cmd in queue) {
                queueDepth.decrementAndGet()
                if (!isActive) break
                val ack = CompletableDeferred<Unit>()
                pendingAck = ack

                if (!writeRaw(cmd)) {
                    pendingAck = null
                    continue
                }

                withTimeoutOrNull(ACK_TIMEOUT_MS.milliseconds) { ack.await() }
                pendingAck = null
                delay(INTER_COMMAND_GAP_MS.milliseconds)
            }
        }
    }

    private fun writeRaw(cmd: String): Boolean {
        val out = output ?: return false
        if (cmd.length > Protocol.MAX_COMMAND_LEN) {
            emitLog(LineDir.Error, "Refused oversized command (${cmd.length} chars)")
            return false
        }

        return try {
            synchronized(writeLock) {
                out.write((cmd + "\n").toByteArray(Charsets.US_ASCII))
                out.flush()
            }
            emitLog(LineDir.Tx, cmd)
            true
        } catch (e: IOException) {
            Timber.e(e, "write failed")
            emitLog(LineDir.Error, "Write failed")
            false
        }
    }

    fun enqueue(command: String) {
        if (_state.value != ConnState.Connected) return
        if (queue.trySend(command).isSuccess) queueDepth.incrementAndGet()
    }

    fun sendImmediate(command: String) {
        if (_state.value != ConnState.Connected) return
        writeRaw(command)
    }

    private fun startPolling() {
        pollJob = scope.launch {
            while (isActive) {
                delay(pollIntervalMs.milliseconds)
                if (_state.value == ConnState.Connected && queueDepth.get() == 0) enqueue(Protocol.STATUS)
            }
        }
    }

    private fun emitLog(dir: LineDir, text: String) {
        _log.tryEmit(LogLine(dir = dir, text = text))
    }

    fun shutdown() {
        unregisterReceivers()
        scope.launch { closeSocket() }
    }
}
