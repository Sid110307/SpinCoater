package com.sid.spincoater.ui.components

import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.Button
import androidx.compose.material3.ButtonDefaults
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.tooling.preview.Preview
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.sid.spincoater.ui.theme.StateEmergency
import com.sid.spincoater.ui.theme.StateRunning
import com.sid.spincoater.ui.theme.StateWarning

@Composable
fun MetricTile(
    label: String,
    value: String,
    modifier: Modifier = Modifier,
    unit: String? = null,
    emphasis: Color? = null
) {
    Column(
        modifier = modifier
            .background(
                MaterialTheme.colorScheme.onSurface.copy(alpha = 0.05f), RoundedCornerShape(10.dp)
            )
            .padding(horizontal = 12.dp, vertical = 10.dp),
        verticalArrangement = Arrangement.spacedBy(2.dp)
    ) {
        Text(
            label,
            style = MaterialTheme.typography.labelSmall,
            color = MaterialTheme.colorScheme.onSurface.copy(alpha = 0.65f)
        )
        Row(verticalAlignment = Alignment.Bottom) {
            Text(
                value,
                style = MaterialTheme.typography.titleMedium,
                fontFamily = FontFamily.Monospace,
                color = emphasis ?: MaterialTheme.colorScheme.onSurface
            )
            unit?.let {
                Text(
                    " $it",
                    style = MaterialTheme.typography.labelSmall,
                    color = MaterialTheme.colorScheme.onSurface.copy(alpha = 0.65f)
                )
            }
        }
    }
}

@Composable
fun StatusChip(label: String, color: Color, modifier: Modifier = Modifier) {
    Row(
        modifier = modifier
            .background(color.copy(alpha = 0.14f), RoundedCornerShape(999.dp))
            .padding(horizontal = 12.dp, vertical = 6.dp),
        verticalAlignment = Alignment.CenterVertically,
        horizontalArrangement = Arrangement.spacedBy(8.dp)
    ) {
        Box(
            Modifier
                .size(8.dp)
                .background(color, RoundedCornerShape(4.dp))
        )
        Text(label, style = MaterialTheme.typography.labelLarge, color = color)
    }
}

@Composable
fun FaultBanner(
    title: String,
    detail: String,
    actionLabel: String?,
    onAction: (() -> Unit)?,
    modifier: Modifier = Modifier,
    severe: Boolean = true
) {
    val tone = if (severe) StateEmergency else StateWarning
    Column(
        modifier = modifier
            .fillMaxWidth()
            .background(tone.copy(alpha = 0.14f), RoundedCornerShape(12.dp))
            .padding(16.dp),
        verticalArrangement = Arrangement.spacedBy(4.dp)
    ) {
        Text(
            title,
            style = MaterialTheme.typography.titleMedium,
            fontWeight = FontWeight.Bold,
            color = tone
        )
        Text(detail, style = MaterialTheme.typography.bodyMedium)
        if (actionLabel != null && onAction != null)
            TextButton(onClick = onAction) {
                Text(actionLabel, style = MaterialTheme.typography.labelLarge)
            }
    }
}

@Composable
fun EStopBar(onEStop: () -> Unit, modifier: Modifier = Modifier, enabled: Boolean = false) {
    Button(
        onClick = onEStop,
        enabled = enabled,
        modifier = modifier
            .fillMaxWidth()
            .height(64.dp),
        shape = RoundedCornerShape(12.dp),
        colors = ButtonDefaults.buttonColors(
            containerColor = StateEmergency, contentColor = Color.White
        )
    ) {
        Text(
            "EMERGENCY STOP",
            fontSize = 20.sp,
            fontWeight = FontWeight.Black,
            letterSpacing = 1.5.sp
        )
    }
}

@Preview
@Composable
private fun WidgetsPreview() {
    Column(Modifier.padding(16.dp), verticalArrangement = Arrangement.spacedBy(12.dp)) {
        StatusChip("Running CW", StateRunning)
        Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
            MetricTile("Voltage", "24", Modifier.weight(1f), "V")
            MetricTile("Current", "1.32", Modifier.weight(1f), "A")
        }
        FaultBanner(
            title = "Emergency stop engaged",
            detail = "Release the physical button, then clear the fault to continue.",
            actionLabel = "Clear fault",
            onAction = {})
        EStopBar(enabled = true, onEStop = {})
    }
}
