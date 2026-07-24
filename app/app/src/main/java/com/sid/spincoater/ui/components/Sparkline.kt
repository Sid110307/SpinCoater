package com.sid.spincoater.ui.components

import androidx.compose.foundation.Canvas
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.material3.MaterialTheme
import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.Path
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.tooling.preview.Preview
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.dp

@Composable
fun Sparkline(
    values: List<Double>,
    modifier: Modifier = Modifier,
    fixedMax: Double? = null,
    height: Dp = 72.dp,
    color: Color? = null
) {
    val fg = color ?: MaterialTheme.colorScheme.primary

    Canvas(
        modifier
            .height(height)
            .fillMaxWidth()
    ) {
        if (values.size < 2) return@Canvas

        val max = fixedMax ?: values.max()
        val min = if (fixedMax != null) 0.0 else values.min()
        val range = (max - min).takeIf { it != 0.0 } ?: 1.0

        val stepX = size.width / (values.size - 1).coerceAtLeast(1)
        val path = Path()
        values.forEachIndexed { i, v ->
            val x = i * stepX
            val y =
                size.height - ((v - min) / range * size.height).toFloat().coerceIn(0f, size.height)
            if (i == 0) path.moveTo(x, y) else path.lineTo(x, y)
        }

        drawPath(path, color = fg.copy(alpha = 0.22f), style = Stroke(width = 10f))
        drawPath(path, color = fg, style = Stroke(width = 3.5f))
    }
}

@Preview
@Composable
private fun SparklinePreview() {
    Sparkline(
        values = listOf(0.0, 400.0, 900.0, 1400.0, 1490.0, 1500.0, 1498.0, 1502.0),
        fixedMax = 4000.0
    )
}
