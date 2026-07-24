package com.sid.spincoater.ui.screens

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.DropdownMenu
import androidx.compose.material3.DropdownMenuItem
import androidx.compose.material3.ElevatedCard
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Switch
import androidx.compose.material3.Text
import androidx.compose.material3.TopAppBar
import androidx.compose.runtime.Composable
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.res.painterResource
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.tooling.preview.Preview
import androidx.compose.ui.unit.dp
import com.sid.spincoater.R
import com.sid.spincoater.core.ConnState
import com.sid.spincoater.core.DevicePass
import com.sid.spincoater.core.Protocol
import com.sid.spincoater.ui.components.CollapsibleCard
import com.sid.spincoater.ui.components.NumberField
import com.sid.spincoater.viewmodel.MainViewModel

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun SettingsScreen(vm: MainViewModel, onBack: () -> Unit) {
    val status by vm.status.collectAsState()
    val connState by vm.connState.collectAsState()
    val autoConnect by vm.autoConnect.collectAsState()
    val pollMs by vm.pollIntervalMs.collectAsState()
    val confirmStart by vm.confirmBeforeStart.collectAsState()
    val keepScreenOn by vm.keepScreenOn.collectAsState()

    val connected = connState == ConnState.Connected
    var expanded by remember { mutableStateOf(false) }

    Scaffold(topBar = {
        TopAppBar(title = { Text("Settings") }, navigationIcon = {
            IconButton(onClick = onBack) {
                Icon(
                    painterResource(R.drawable.nav_arrow),
                    contentDescription = "Back",
                    modifier = Modifier.height(24.dp)
                )
            }
        })
    }) { pad ->
        Column(
            Modifier
                .fillMaxSize()
                .padding(pad)
                .padding(horizontal = 16.dp, vertical = 12.dp)
                .verticalScroll(rememberScrollState()),
            verticalArrangement = Arrangement.spacedBy(12.dp)
        ) {
            ElevatedCard(Modifier.fillMaxWidth()) {
                Column(Modifier.padding(16.dp), verticalArrangement = Arrangement.spacedBy(12.dp)) {
                    Text("App", style = MaterialTheme.typography.titleMedium)

                    ToggleRow(
                        "Reconnect on launch",
                        "Automatically reconnect to the last device on app launch.",
                        autoConnect
                    ) { vm.setAutoConnect(it) }
                    ToggleRow(
                        "Confirm before starting",
                        "Ask for confirmation before starting the motor.",
                        confirmStart
                    ) { vm.setConfirmBeforeStart(it) }
                    ToggleRow(
                        "Keep screen on",
                        "Prevent the screen from sleeping and disconnecting while the app is open.",
                        keepScreenOn
                    ) { vm.setKeepScreenOn(it) }
                    NumberField(
                        label = "Status poll interval",
                        value = pollMs.toInt(),
                        onCommit = { vm.setPollInterval(it.toLong()) },
                        range = 250..5000,
                        step = 250,
                        suffix = "ms",
                        supporting = "Interval at which the app polls the device for status updates."
                    )
                }
            }

            if (connected) {
                ElevatedCard(Modifier.fillMaxWidth()) {
                    Column(
                        Modifier.padding(16.dp), verticalArrangement = Arrangement.spacedBy(12.dp)
                    ) {
                        Text("Device configuration", style = MaterialTheme.typography.titleMedium)
                        NumberField(
                            label = "Sensor pole pairs",
                            value = status?.polePairs ?: 4,
                            onCommit = { vm.setPolePairs(it) },
                            range = 1..64,
                            suffix = "pairs",
                            supporting = "Number of pole pairs in the motor."
                        )

                        Text("PWM output pin", style = MaterialTheme.typography.labelLarge)
                        Row(
                            Modifier.fillMaxWidth(),
                            verticalAlignment = Alignment.CenterVertically,
                            horizontalArrangement = Arrangement.SpaceBetween
                        ) {
                            Text(
                                "Pin ${status?.pwmPin ?: 13}",
                                style = MaterialTheme.typography.bodyLarge
                            )
                            IconButton(onClick = { expanded = true }) {
                                Icon(
                                    painterResource(R.drawable.expand_more),
                                    contentDescription = "Select pin",
                                    modifier = Modifier.height(24.dp)
                                )
                            }
                            DropdownMenu(
                                expanded = expanded, onDismissRequest = { expanded = false }) {
                                Protocol.VALID_PWM_PINS.forEach { pin ->
                                    DropdownMenuItem(text = { Text("Pin $pin") }, onClick = {
                                        vm.setPwmPin(pin)
                                        expanded = false
                                    })
                                }
                            }
                        }
                    }
                }
            }
            CollapsibleCard(
                modifier = Modifier.fillMaxWidth(), key = "device_passes", title = {
                    Text("Passes", style = MaterialTheme.typography.titleMedium)
                }) {
                val passes = status?.passes.orEmpty()
                if (passes.isEmpty()) Text(
                    "No passes.",
                    style = MaterialTheme.typography.bodySmall,
                )
                else passes.forEachIndexed { i, p ->
                    val pass = DevicePass(
                        rpm = p.rpm / 10.0, accel = p.accel / 10.0, delayMs = p.delayMs
                    )
                    InfoRow(
                        label = "Pass ${i + 1}",
                        value = "${pass.rpm} RPM, ${pass.accel} RPM/s, ${pass.delayMs} ms"
                    )
                }
            }
        }
    }
}

@Composable
private fun ToggleRow(
    title: String, subtitle: String, checked: Boolean, onChange: (Boolean) -> Unit
) {
    Row(
        Modifier.fillMaxWidth(),
        verticalAlignment = Alignment.CenterVertically,
        horizontalArrangement = Arrangement.SpaceBetween,
    ) {
        Column(
            Modifier
                .weight(1f)
                .padding(end = 8.dp)
        ) {
            Text(title, style = MaterialTheme.typography.bodyLarge)
            Text(subtitle, style = MaterialTheme.typography.bodySmall)
        }
        Switch(checked = checked, onCheckedChange = onChange)
    }
}

@Composable
private fun InfoRow(label: String, value: String) {
    Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.SpaceBetween) {
        Text(label, style = MaterialTheme.typography.bodySmall)
        Text(
            value, style = MaterialTheme.typography.bodySmall, fontFamily = FontFamily.Monospace
        )
    }
}

@Preview
@Composable
private fun SettingsScreenPreview() {
    SettingsScreen(vm = MainViewModel.preview(), onBack = {})
}
