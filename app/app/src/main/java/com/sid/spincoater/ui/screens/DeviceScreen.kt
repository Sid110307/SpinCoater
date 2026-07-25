package com.sid.spincoater.ui.screens

import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.CircularProgressIndicator
import androidx.compose.material3.ElevatedCard
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.FilledTonalButton
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Text
import androidx.compose.material3.TopAppBar
import androidx.compose.runtime.Composable
import androidx.compose.runtime.DisposableEffect
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.res.painterResource
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.tooling.preview.Preview
import androidx.compose.ui.unit.dp
import com.sid.spincoater.R
import com.sid.spincoater.core.BtDevice
import com.sid.spincoater.core.ConnState
import com.sid.spincoater.ui.theme.StateConnected
import com.sid.spincoater.viewmodel.MainViewModel

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun DeviceScreen(vm: MainViewModel, onBack: () -> Unit) {
    val devices by vm.devices.collectAsState()
    val connected by vm.connectedAddress.collectAsState()
    val connState by vm.connState.collectAsState()
    val discovering by vm.discovering.collectAsState()

    LaunchedEffect(Unit) {
        vm.refreshDevices()
        vm.startDiscovery()
    }
    LaunchedEffect(connState) { if (connState == ConnState.Connected) onBack() }
    DisposableEffect(Unit) { onDispose { vm.stopDiscovery() } }

    Scaffold(topBar = {
        TopAppBar(title = {
            Column {
                Text("Select device")
                if (connected != null) Text(
                    "Connected: $connected", style = MaterialTheme.typography.bodySmall
                )
            }
        }, navigationIcon = {
            IconButton(onClick = onBack) {
                Icon(
                    painterResource(R.drawable.nav_arrow),
                    contentDescription = "Back",
                    modifier = Modifier.height(24.dp)
                )
            }
        }, actions = {
            IconButton(onClick = { vm.refreshDevices(); vm.startDiscovery() }) {
                Icon(
                    painterResource(R.drawable.refresh),
                    contentDescription = "Rescan",
                    tint = MaterialTheme.colorScheme.primary,
                    modifier = Modifier.height(20.dp)
                )
            }
        })
    }) { pad ->
        Column(
            Modifier
                .fillMaxSize()
                .padding(pad)
                .padding(16.dp),
            verticalArrangement = Arrangement.spacedBy(12.dp)
        ) {
            if (discovering) Row(
                Modifier.fillMaxWidth(),
                verticalAlignment = Alignment.CenterVertically,
                horizontalArrangement = Arrangement.Center
            ) {
                CircularProgressIndicator(Modifier.size(16.dp))
                Spacer(Modifier.width(8.dp))
                Text("Searching for nearby devices...", style = MaterialTheme.typography.bodyMedium)
            }

            LazyColumn(verticalArrangement = Arrangement.spacedBy(8.dp)) {
                items(devices, key = { it.address }) { dev ->
                    DeviceRow(
                        dev = dev,
                        isConnected = dev.address == connected,
                        isBusy = connState != ConnState.Disconnected && dev.address == connected,
                        onPair = { vm.pair(dev.address) },
                        onConnect = { vm.connectTo(dev.address) },
                        onDisconnect = { vm.disconnect() })
                }
            }

            if (devices.isEmpty() && !discovering) Text(
                "No devices found.",
                style = MaterialTheme.typography.bodyMedium,
                modifier = Modifier.fillMaxWidth()
            )
        }
    }
}

@Composable
private fun DeviceRow(
    dev: BtDevice,
    isConnected: Boolean,
    isBusy: Boolean,
    onPair: () -> Unit,
    onConnect: () -> Unit,
    onDisconnect: () -> Unit
) {
    ElevatedCard(
        Modifier
            .fillMaxWidth()
            .border(
                width = if (dev.isSpinCoater) 1.5.dp else 1.dp, color = when {
                    isConnected -> MaterialTheme.colorScheme.primary
                    dev.isSpinCoater -> StateConnected.copy(alpha = 0.6f)
                    else -> MaterialTheme.colorScheme.outlineVariant
                }, shape = RoundedCornerShape(12.dp)
            )
    ) {
        Row(
            Modifier
                .fillMaxWidth()
                .padding(16.dp),
            horizontalArrangement = Arrangement.SpaceBetween,
            verticalAlignment = Alignment.CenterVertically
        ) {
            Column(Modifier.weight(1f)) {
                Row(verticalAlignment = Alignment.CenterVertically) {
                    Text(
                        buildString {
                            append(dev.name ?: "Unnamed")
                            append(if (dev.bonded) " (Paired)" else "")
                        },
                        style = MaterialTheme.typography.titleMedium
                    )
                    if (isConnected) Box(
                        Modifier
                            .padding(start = 8.dp)
                            .size(8.dp)
                            .background(MaterialTheme.colorScheme.primary, RoundedCornerShape(4.dp))
                    )
                }
                Text(
                    dev.address,
                    style = MaterialTheme.typography.bodySmall,
                    fontFamily = FontFamily.Monospace
                )
            }
            Spacer(Modifier.width(12.dp))

            when {
                isBusy && !isConnected -> CircularProgressIndicator(Modifier.size(24.dp))
                isConnected -> OutlinedButton(onClick = onDisconnect) { Text("Disconnect") }
                !dev.bonded -> FilledTonalButton(onClick = onPair) { Text("Pair") }
                else -> FilledTonalButton(onClick = onConnect) { Text("Connect") }
            }
        }
    }
}

@Preview
@Composable
private fun DeviceScreenPreview() {
    DeviceScreen(vm = MainViewModel.preview(), onBack = {})
}
