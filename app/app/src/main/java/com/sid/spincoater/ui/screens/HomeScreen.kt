package com.sid.spincoater.ui.screens

import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.FlowRow
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.Button
import androidx.compose.material3.ButtonDefaults
import androidx.compose.material3.CircularProgressIndicator
import androidx.compose.material3.ElevatedCard
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.FilledTonalButton
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.LinearProgressIndicator
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.Scaffold
import androidx.compose.material3.SegmentedButton
import androidx.compose.material3.SegmentedButtonDefaults
import androidx.compose.material3.SingleChoiceSegmentedButtonRow
import androidx.compose.material3.SnackbarHost
import androidx.compose.material3.SnackbarHostState
import androidx.compose.material3.Text
import androidx.compose.material3.TopAppBar
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.platform.LocalInspectionMode
import androidx.compose.ui.res.painterResource
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.tooling.preview.Preview
import androidx.compose.ui.unit.dp
import com.sid.spincoater.R
import com.sid.spincoater.core.ConnState
import com.sid.spincoater.core.Protocol
import com.sid.spincoater.core.motorFaultMap
import com.sid.spincoater.ui.components.CollapsibleCard
import com.sid.spincoater.ui.components.EStopBar
import com.sid.spincoater.ui.components.FaultBanner
import com.sid.spincoater.ui.components.MetricTile
import com.sid.spincoater.ui.components.NumberField
import com.sid.spincoater.ui.components.RpmGauge
import com.sid.spincoater.ui.components.Sparkline
import com.sid.spincoater.ui.components.StatusChip
import com.sid.spincoater.ui.theme.StateConnected
import com.sid.spincoater.ui.theme.StateEmergency
import com.sid.spincoater.ui.theme.StateIdle
import com.sid.spincoater.ui.theme.StateRunning
import com.sid.spincoater.ui.theme.StateWarning
import com.sid.spincoater.viewmodel.MainViewModel

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun HomeScreen(
    vm: MainViewModel,
    onOpenLog: () -> Unit,
    onOpenRecipes: () -> Unit,
    onOpenDevices: () -> Unit,
    onOpenSettings: () -> Unit
) {
    val status by vm.status.collectAsState()
    val connState by vm.connState.collectAsState()
    val address by vm.connectedAddress.collectAsState()
    val rpmHistory by vm.rpmHistory.collectAsState()
    val run by vm.runProgress.collectAsState()
    val confirmStart by vm.confirmBeforeStart.collectAsState()

    val snackbar = remember { SnackbarHostState() }
    var showStartConfirm by remember { mutableStateOf(false) }

    LaunchedEffect(Unit) { vm.toast.collect { snackbar.showSnackbar(it) } }

    val connected = connState == ConnState.Connected
    val running = status?.running == true

    val stateColor = when {
        status?.emergency == true -> StateEmergency
        status?.alarm == true -> StateWarning
        running -> StateRunning
        connected -> StateConnected
        else -> StateIdle
    }

    val context = LocalContext.current
    val isPreview = LocalInspectionMode.current
    val versionName = remember {
        if (isPreview) "0.0.0"
        else context.packageManager.getPackageInfo(context.packageName, 0).versionName
    }

    Scaffold(snackbarHost = { SnackbarHost(snackbar) }, topBar = {
        TopAppBar(title = {
            Column(Modifier.padding(vertical = 8.dp)) {
                Text(
                    "SpinCoater",
                    style = MaterialTheme.typography.headlineSmall,
                    fontWeight = FontWeight.Bold
                )
                Text(
                    "Indriya Sensotech Private Limited", style = MaterialTheme.typography.labelLarge
                )
            }
        }, actions = {
            IconButton(onClick = onOpenSettings) {
                Icon(
                    painterResource(R.drawable.settings),
                    contentDescription = "Settings",
                    modifier = Modifier.height(22.dp)
                )
            }
        })
    }, bottomBar = {
        if (connected) Box(
            Modifier
                .background(MaterialTheme.colorScheme.background)
                .padding(horizontal = 16.dp, vertical = 12.dp)
        ) { EStopBar(onEStop = { vm.emergencyStop() }) }
    }) { pad ->
        Column(
            modifier = Modifier
                .fillMaxSize()
                .background(MaterialTheme.colorScheme.background)
                .padding(pad)
                .padding(horizontal = 16.dp, vertical = 12.dp)
                .verticalScroll(rememberScrollState()),
            verticalArrangement = Arrangement.spacedBy(16.dp)
        ) {
            ElevatedCard(Modifier.fillMaxWidth()) {
                Row(
                    Modifier
                        .fillMaxWidth()
                        .padding(16.dp),
                    verticalAlignment = Alignment.CenterVertically,
                    horizontalArrangement = Arrangement.SpaceBetween
                ) {
                    Column(Modifier.weight(1f)) {
                        StatusChip(
                            label = when (connState) {
                                ConnState.Connected -> status?.statusLabel ?: "Connected"
                                ConnState.Connecting -> "Connecting..."
                                ConnState.Reconnecting -> "Reconnecting..."
                                ConnState.Disconnected -> "Not connected"
                            }, color = stateColor
                        )
                        if (address != null) Text(
                            address!!,
                            style = MaterialTheme.typography.bodySmall,
                            modifier = Modifier.padding(top = 6.dp)
                        )
                    }
                    if (connState == ConnState.Connecting || connState == ConnState.Reconnecting) {
                        CircularProgressIndicator(Modifier.size(20.dp))
                        Spacer(Modifier.size(12.dp))
                    }
                    if (connected) OutlinedButton(onClick = { vm.disconnect() }) { Text("Disconnect") }
                    else FilledTonalButton(onClick = onOpenDevices) { Text("Connect") }
                }
            }
            status?.let { s ->
                when {
                    s.emergency -> FaultBanner(
                        title = "Emergency stop engaged",
                        detail = "The emergency stop button is pressed. Release the button and clear the fault to continue.",
                        actionLabel = "Clear fault",
                        onAction = { vm.clearFault() })

                    s.alarm -> FaultBanner(
                        title = "Driver alarm",
                        detail = "The driver has reported an alarm condition. Check the driver and wiring.",
                        actionLabel = "Clear fault",
                        onAction = { vm.clearFault() },
                        severe = false
                    )

                    s.motorFault != 0 -> FaultBanner(
                        title = "Driver fault code ${s.motorFault} (${motorFaultMap[s.motorFault]})",
                        detail = "The driver has reported a fault. Check the driver and wiring.",
                        actionLabel = "Clear fault",
                        onAction = { vm.clearFault() },
                        severe = false
                    )
                }
            }
            ElevatedCard(Modifier.fillMaxWidth()) {
                Column(
                    Modifier
                        .fillMaxWidth()
                        .padding(16.dp),
                    horizontalAlignment = Alignment.CenterHorizontally,
                    verticalArrangement = Arrangement.spacedBy(12.dp)
                ) {
                    RpmGauge(
                        rpm = status?.displayRpm ?: 0.0,
                        target = status?.rpmTarget ?: 0,
                        maxRpm = Protocol.MAX_RPM,
                        accentColor = stateColor,
                        modifier = Modifier.fillMaxWidth(0.62f),
                        caption = status?.let { if (it.clockwise) "clockwise" else "counter-clockwise" })
                    if (rpmHistory.size > 1) Sparkline(
                        values = rpmHistory,
                        fixedMax = Protocol.MAX_RPM.toDouble(),
                        color = stateColor
                    )

                    Row(
                        Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(12.dp)
                    ) {
                        Button(
                            onClick = {
                                if (confirmStart) showStartConfirm = true else vm.start()
                            },
                            enabled = connected && !running && status?.canStart != false,
                            modifier = Modifier
                                .weight(1f)
                                .height(52.dp)
                        ) {
                            Icon(
                                painterResource(R.drawable.play),
                                contentDescription = null,
                                modifier = Modifier
                                    .height(18.dp)
                                    .padding(end = 8.dp)
                            )
                            Text("Start")
                        }
                        Button(
                            onClick = { vm.stop() },
                            enabled = connected && running,
                            colors = ButtonDefaults.buttonColors(
                                containerColor = MaterialTheme.colorScheme.secondary
                            ),
                            modifier = Modifier
                                .weight(1f)
                                .height(52.dp)
                        ) {
                            Icon(
                                painterResource(R.drawable.stop),
                                contentDescription = null,
                                modifier = Modifier
                                    .height(18.dp)
                                    .padding(end = 8.dp)
                            )
                            Text("Stop")
                        }
                    }
                }
            }
            run?.let { r ->
                ElevatedCard(Modifier.fillMaxWidth()) {
                    Column(
                        Modifier.padding(16.dp), verticalArrangement = Arrangement.spacedBy(8.dp)
                    ) {
                        Text(
                            "Running \"${r.recipeName}\" (${r.stepIndex + 1}/${r.stepCount})",
                            style = MaterialTheme.typography.titleMedium
                        )
                        Text(
                            r.stepLabel, style = MaterialTheme.typography.bodyMedium
                        )
                        LinearProgressIndicator(
                            progress = { r.fraction }, modifier = Modifier.fillMaxWidth()
                        )
                        Text(
                            "${r.elapsedMs / 1000}s / ${r.totalMs / 1000}s",
                            style = MaterialTheme.typography.labelMedium
                        )
                        OutlinedButton(onClick = { vm.cancelRun() }) { Text("Cancel run") }
                    }
                }
            }
            ElevatedCard(Modifier.fillMaxWidth()) {
                Column(
                    Modifier.padding(16.dp), verticalArrangement = Arrangement.spacedBy(12.dp)
                ) {
                    Text("Motor settings", style = MaterialTheme.typography.titleMedium)
                    NumberField(
                        label = "Target speed",
                        value = status?.rpmTarget ?: Protocol.MIN_RPM,
                        onCommit = { vm.setRpm(it) },
                        range = Protocol.MIN_RPM..Protocol.MAX_RPM,
                        step = 50,
                        suffix = "rpm",
                        supporting = "Target speed for the motor.",
                        enabled = connected
                    )
                    NumberField(
                        label = "Ramp time",
                        value = (status?.accelMs ?: Protocol.MIN_ACCEL_MS.toDouble()).toInt(),
                        onCommit = { vm.setAccelMs(it) },
                        range = Protocol.MIN_ACCEL_MS..Protocol.MAX_ACCEL_MS,
                        step = 100,
                        suffix = "ms",
                        supporting = "Acceleration time to reach the target speed.",
                        enabled = connected
                    )

                    Text("Direction", style = MaterialTheme.typography.labelLarge)
                    SingleChoiceSegmentedButtonRow(Modifier.fillMaxWidth()) {
                        SegmentedButton(
                            selected = status?.clockwise != false,
                            onClick = { vm.setDirection(true) },
                            enabled = connected,
                            shape = SegmentedButtonDefaults.itemShape(0, 2)
                        ) { Text("Clockwise") }
                        SegmentedButton(
                            selected = status?.clockwise == false,
                            onClick = { vm.setDirection(false) },
                            enabled = connected,
                            shape = SegmentedButtonDefaults.itemShape(1, 2)
                        ) { Text("Anticlockwise") }
                    }
                    if (running) Text(
                        "Changing direction while running will stop the motor.",
                        style = MaterialTheme.typography.bodySmall,
                        color = StateWarning
                    )
                }
            }
            CollapsibleCard(
                modifier = Modifier.fillMaxWidth(),
                key = "metrics",
                initiallyExpanded = true,
                title = { Text("Metrics", style = MaterialTheme.typography.titleMedium) }) {
                FlowRow(
                    Modifier.fillMaxWidth(),
                    horizontalArrangement = Arrangement.spacedBy(8.dp),
                    verticalArrangement = Arrangement.spacedBy(8.dp)
                ) {
                    MetricTile("Bus voltage", status?.motorVoltage?.toString() ?: "--", unit = "V")
                    MetricTile(
                        "Current",
                        status?.let { "%.2f".format(it.motorCurrent) } ?: "--",
                        unit = "A")
                    MetricTile(
                        "Power",
                        status?.let { "%.1f".format(it.motorPower) } ?: "--",
                        unit = "W")
                    MetricTile("Drive speed", status?.motorSpeed?.toString() ?: "--", unit = "rpm")
                    MetricTile(
                        "Hall speed",
                        status?.let { "%.0f".format(it.speedRpm) } ?: "--",
                        unit = "rpm")
                    MetricTile(
                        "Fault code",
                        status?.motorFault?.toString() ?: "--",
                        emphasis = if ((status?.motorFault ?: 0) != 0) StateEmergency else null
                    )
                }
                status?.let {
                    Text(
                        "Updated ${
                            ((System.currentTimeMillis() - it.receivedAt) / 1000).coerceAtLeast(
                                0
                            )
                        }s ago", style = MaterialTheme.typography.labelSmall
                    )
                }
            }
            FlowRow(
                Modifier.fillMaxWidth(),
                horizontalArrangement = Arrangement.spacedBy(8.dp, Alignment.CenterHorizontally)
            ) {
                FilledTonalButton(onClick = onOpenRecipes) {
                    Icon(
                        painterResource(R.drawable.layers),
                        contentDescription = null,
                        modifier = Modifier.size(24.dp)
                    )
                    Spacer(Modifier.size(8.dp))
                    Text("Recipes")
                }
                FilledTonalButton(onClick = onOpenLog) {
                    Icon(
                        painterResource(R.drawable.notes),
                        contentDescription = null,
                        modifier = Modifier.size(24.dp)
                    )
                    Spacer(Modifier.size(8.dp))
                    Text("Log")
                }
                OutlinedButton(onClick = { vm.refreshStatus() }, enabled = connected) {
                    Icon(
                        painterResource(R.drawable.refresh),
                        contentDescription = null,
                        modifier = Modifier
                            .height(18.dp)
                            .padding(end = 8.dp)
                    )
                    Text("Refresh")
                }
            }
            CollapsibleCard(
                modifier = Modifier
                    .fillMaxWidth()
                    .padding(bottom = 16.dp),
                key = "about",
                initiallyExpanded = false,
                title = { Text("About", style = MaterialTheme.typography.titleMedium) }) {
                Text(
                    "Version v$versionName",
                    style = MaterialTheme.typography.bodyMedium,
                    modifier = Modifier.padding(bottom = 2.dp)
                )
                Text(
                    "Developed by Siddharth Praveen Bharadwaj",
                    style = MaterialTheme.typography.bodyMedium
                )
            }
        }
    }

    if (showStartConfirm) AlertDialog(
        onDismissRequest = { showStartConfirm = false },
        title = { Text("Confirm start") },
        text = { Text("Are you sure you want to start the motor?") },
        confirmButton = {
            Button(onClick = { showStartConfirm = false; vm.start() }) { Text("Start") }
        },
        dismissButton = {
            OutlinedButton(onClick = { showStartConfirm = false }) { Text("Cancel") }
        })
}

@Preview
@Composable
private fun HomeScreenPreview() {
    HomeScreen(
        vm = MainViewModel.preview(),
        onOpenLog = {},
        onOpenRecipes = {},
        onOpenDevices = {},
        onOpenSettings = {})
}
