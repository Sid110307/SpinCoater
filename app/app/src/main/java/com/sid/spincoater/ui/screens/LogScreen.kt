package com.sid.spincoater.ui.screens

import android.content.Intent
import android.text.format.DateFormat
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.lazy.rememberLazyListState
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.FilterChip
import androidx.compose.material3.HorizontalDivider
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Text
import androidx.compose.material3.TopAppBar
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.res.painterResource
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.tooling.preview.Preview
import androidx.compose.ui.unit.dp
import com.sid.spincoater.R
import com.sid.spincoater.core.LineDir
import com.sid.spincoater.ui.theme.StateEmergency
import com.sid.spincoater.ui.theme.StateIdle
import com.sid.spincoater.ui.theme.StateRunning
import com.sid.spincoater.viewmodel.MainViewModel

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun LogScreen(vm: MainViewModel, onBack: () -> Unit) {
    val log by vm.log.collectAsState()
    val listState = rememberLazyListState()
    var autoScroll by remember { mutableStateOf(true) }
    var showTx by remember { mutableStateOf(true) }
    var showRx by remember { mutableStateOf(true) }

    val context = LocalContext.current
    val filtered = remember(log, showTx, showRx) {
        log.filter {
            when (it.dir) {
                LineDir.Tx -> showTx
                LineDir.Rx -> showRx
                else -> true
            }
        }
    }

    LaunchedEffect(filtered.size, autoScroll) {
        if (autoScroll && filtered.isNotEmpty()) listState.animateScrollToItem(filtered.lastIndex)
    }

    Scaffold(topBar = {
        TopAppBar(title = { Text("Serial log") }, navigationIcon = {
            IconButton(onClick = onBack) {
                Icon(
                    painterResource(R.drawable.nav_arrow),
                    contentDescription = "Back",
                    modifier = Modifier.height(24.dp)
                )
            }
        }, actions = {
            IconButton(onClick = {
                val send = Intent(Intent.ACTION_SEND).apply {
                    type = "text/plain"
                    putExtra(Intent.EXTRA_TEXT, vm.exportLog())
                    putExtra(Intent.EXTRA_SUBJECT, "SpinCoater serial log")
                }
                context.startActivity(Intent.createChooser(send, "Export log"))
            }) {
                Icon(
                    painterResource(R.drawable.share),
                    contentDescription = "Share log",
                    modifier = Modifier.height(20.dp)
                )
            }
            IconButton(onClick = { vm.clearLog() }) {
                Icon(
                    painterResource(R.drawable.trash),
                    contentDescription = "Clear log",
                    tint = MaterialTheme.colorScheme.error,
                    modifier = Modifier.height(20.dp)
                )
            }
        })
    }) { pad ->
        Column(
            Modifier
                .fillMaxSize()
                .padding(pad)
                .padding(horizontal = 12.dp)
        ) {
            Row(
                Modifier
                    .fillMaxWidth()
                    .padding(vertical = 8.dp),
                horizontalArrangement = Arrangement.spacedBy(8.dp)
            ) {
                FilterChip(
                    selected = showTx,
                    onClick = { showTx = !showTx },
                    label = { Text("Sent") })
                FilterChip(
                    selected = showRx,
                    onClick = { showRx = !showRx },
                    label = { Text("Received") })
                FilterChip(
                    selected = autoScroll,
                    onClick = { autoScroll = !autoScroll },
                    label = { Text("Follow") })
            }

            LazyColumn(Modifier.fillMaxSize(), state = listState) {
                items(filtered, key = { it.id }) { entry ->
                    val time = remember(entry.at) {
                        DateFormat.format("HH:mm:ss", entry.at).toString()
                    }
                    val (marker, tone) = when (entry.dir) {
                        LineDir.Tx -> ">>" to MaterialTheme.colorScheme.primary
                        LineDir.Rx -> "<<" to StateRunning
                        LineDir.Info -> "--" to StateIdle
                        LineDir.Error -> "!!" to StateEmergency
                    }

                    Row(
                        Modifier
                            .fillMaxWidth()
                            .padding(vertical = 4.dp),
                        horizontalArrangement = Arrangement.spacedBy(8.dp)
                    ) {
                        Text(time, style = MaterialTheme.typography.labelSmall, color = StateIdle)
                        Text(
                            marker,
                            style = MaterialTheme.typography.labelSmall,
                            fontFamily = FontFamily.Monospace,
                            color = tone
                        )
                        Text(
                            entry.text,
                            style = MaterialTheme.typography.bodySmall,
                            fontFamily = FontFamily.Monospace,
                            modifier = Modifier.weight(1f)
                        )
                    }
                    HorizontalDivider(color = MaterialTheme.colorScheme.outlineVariant.copy(alpha = 0.3f))
                }
            }

            if (filtered.isEmpty()) Text(
                "No logs yet.",
                Modifier
                    .fillMaxWidth()
                    .padding(24.dp),
                style = MaterialTheme.typography.bodyMedium,
                textAlign = TextAlign.Center
            )
        }
    }
}

@Preview
@Composable
private fun LogScreenPreview() {
    LogScreen(vm = MainViewModel.preview(), onBack = {})
}
