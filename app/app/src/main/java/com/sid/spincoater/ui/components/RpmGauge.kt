package com.sid.spincoater.ui.components

import androidx.compose.animation.core.animateFloatAsState
import androidx.compose.animation.core.tween
import androidx.compose.foundation.Canvas
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.aspectRatio
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.size
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.geometry.Size
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.StrokeCap
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.tooling.preview.Preview
import androidx.compose.ui.unit.dp
import com.sid.spincoater.ui.theme.StateIdle
import com.sid.spincoater.ui.theme.StateRunning

@Composable
fun RpmGauge(
    rpm: Double,
    target: Int,
    maxRpm: Int,
    accentColor: Color,
    modifier: Modifier = Modifier,
    caption: String? = null
) {
    val track = MaterialTheme.colorScheme.onSurface.copy(alpha = 0.10f)
    val tick = MaterialTheme.colorScheme.onSurface.copy(alpha = 0.55f)

    val progress = (rpm / maxRpm.toDouble()).coerceIn(0.0, 1.0).toFloat()
    val animated by animateFloatAsState(
        targetValue = progress, animationSpec = tween(400), label = "rpm"
    )

    val startAngle = 135f
    val sweepTotal = 270f

    Box(modifier = modifier, contentAlignment = Alignment.Center) {
        Canvas(
            Modifier
                .fillMaxWidth()
                .aspectRatio(1f)
        ) {
            val stroke = size.minDimension * 0.085f
            val inset = stroke / 2f
            val arcSize = Size(size.width - stroke, size.height - stroke)
            val topLeft = Offset(inset, inset)

            drawArc(
                color = track,
                startAngle = startAngle,
                sweepAngle = sweepTotal,
                useCenter = false,
                topLeft = topLeft,
                size = arcSize,
                style = Stroke(width = stroke, cap = StrokeCap.Round)
            )
            drawArc(
                color = accentColor,
                startAngle = startAngle,
                sweepAngle = sweepTotal * animated,
                useCenter = false,
                topLeft = topLeft,
                size = arcSize,
                style = Stroke(width = stroke, cap = StrokeCap.Round)
            )
            if (target > 0) drawArc(
                color = tick,
                startAngle = startAngle + sweepTotal * (target / maxRpm.toFloat()).coerceIn(
                    0f, 1f
                ) - 0.8f,
                sweepAngle = 1.6f,
                useCenter = false,
                topLeft = topLeft,
                size = arcSize,
                style = Stroke(width = stroke * 1.35f, cap = StrokeCap.Butt)
            )
        }
        Column(
            horizontalAlignment = Alignment.CenterHorizontally,
            verticalArrangement = Arrangement.spacedBy(2.dp)
        ) {
            Text(
                text = "%.0f".format(rpm),
                style = MaterialTheme.typography.displaySmall,
                fontFamily = FontFamily.Monospace,
                fontWeight = FontWeight.Bold
            )
            Text("RPM", style = MaterialTheme.typography.labelMedium)
            if (target > 0) Text(
                text = "Target: $target",
                style = MaterialTheme.typography.labelSmall,
                fontFamily = FontFamily.Monospace
            )
            caption?.let {
                Text(it, style = MaterialTheme.typography.labelSmall, color = StateIdle)
            }
        }
    }
}

@Preview
@Composable
private fun RpmGaugePreview() {
    RpmGauge(
        rpm = 1487.0,
        target = 1500,
        maxRpm = 4000,
        accentColor = StateRunning,
        modifier = Modifier.size(220.dp)
    )
}
