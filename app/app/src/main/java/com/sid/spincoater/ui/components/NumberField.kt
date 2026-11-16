package com.sid.spincoater.ui.components

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.material3.FilledTonalIconButton
import androidx.compose.material3.Icon
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.res.painterResource
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.input.KeyboardType
import androidx.compose.ui.tooling.preview.Preview
import androidx.compose.ui.unit.dp
import com.sid.spincoater.R
import kotlinx.coroutines.delay
import kotlin.time.Duration.Companion.milliseconds

@Composable
fun NumberField(
    label: String,
    value: Int,
    onCommit: (Int) -> Unit,
    range: IntRange,
    modifier: Modifier = Modifier,
    step: Int = 1,
    suffix: String? = null,
    supporting: String? = null,
    enabled: Boolean = true,
    debounceMs: Long = 600L
) {
    var text by remember(value) { mutableStateOf(value.toString()) }
    var dirty by remember { mutableStateOf(false) }

    val parsed = text.toIntOrNull()
    val outOfRange = parsed != null && parsed !in range
    val invalid = text.isNotBlank() && parsed == null

    LaunchedEffect(text, dirty) {
        if (!dirty) return@LaunchedEffect
        delay(debounceMs.milliseconds)
        val v = text.toIntOrNull() ?: return@LaunchedEffect
        if (v in range && v != value) {
            onCommit(v)
            dirty = false
        }
    }

    Row(
        modifier = modifier,
        verticalAlignment = Alignment.Top,
        horizontalArrangement = Arrangement.spacedBy(8.dp)
    ) {
        OutlinedTextField(
            value = text,
            onValueChange = {
                if (it.isEmpty() || it.all { c -> c.isDigit() }) {
                    text = it; dirty = true
                }
            },
            label = { Text(label) },
            enabled = enabled,
            singleLine = true,
            isError = invalid || outOfRange,
            textStyle = MaterialTheme.typography.bodyLarge.copy(fontFamily = FontFamily.Monospace),
            suffix = suffix?.let { { Text(it) } },
            supportingText = {
                when {
                    invalid -> Text("Enter a number", color = MaterialTheme.colorScheme.error)
                    outOfRange -> Text(
                        "Allowed ${range.first}-${range.last}",
                        color = MaterialTheme.colorScheme.error
                    )

                    supporting != null -> Text(supporting)
                }
            },
            keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.Number),
            modifier = Modifier.weight(1f)
        )
        FilledTonalIconButton(
            modifier = Modifier.padding(top = 12.dp), onClick = {
                val next = ((parsed ?: value) - step).coerceIn(range.first, range.last)
                text = next.toString(); dirty = false; onCommit(next)
            }, enabled = enabled
        ) {
            Icon(
                painterResource(R.drawable.minus),
                contentDescription = "Decrease $label",
                modifier = Modifier
                    .height(20.dp)
                    .padding(2.dp)
            )
        }
        FilledTonalIconButton(
            modifier = Modifier.padding(top = 12.dp), onClick = {
                val next = ((parsed ?: value) + step).coerceIn(range.first, range.last)
                text = next.toString(); dirty = false; onCommit(next)
            }, enabled = enabled
        ) {
            Icon(
                painterResource(R.drawable.plus),
                contentDescription = "Increase $label",
                modifier = Modifier
                    .height(20.dp)
                    .padding(2.dp)
            )
        }
    }
}

@Preview
@Composable
private fun NumberFieldPreview() {
    NumberField(
        label = "Target speed",
        value = 1500,
        onCommit = {},
        range = 200..4000,
        step = 50,
        suffix = "rpm"
    )
}
