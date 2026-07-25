package com.sid.spincoater.ui.screens

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.Button
import androidx.compose.material3.ElevatedCard
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Switch
import androidx.compose.material3.Text
import androidx.compose.material3.TopAppBar
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateListOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.res.painterResource
import androidx.compose.ui.text.input.ImeAction
import androidx.compose.ui.text.input.KeyboardCapitalization
import androidx.compose.ui.text.input.KeyboardType
import androidx.compose.ui.tooling.preview.Preview
import androidx.compose.ui.unit.dp
import com.sid.spincoater.R
import com.sid.spincoater.core.Protocol
import com.sid.spincoater.core.Recipe
import com.sid.spincoater.core.RecipeStep
import com.sid.spincoater.viewmodel.MainViewModel

private data class DraftStep(
    val rpm: String = "1500",
    val accelMs: String = "1000",
    val holdMs: String = "30000",
    val clockwise: Boolean = true
)

private fun stepError(step: DraftStep): String? {
    val rpm = step.rpm.toIntOrNull() ?: return "Speed must be a number"
    val accel = step.accelMs.toIntOrNull() ?: return "Ramp must be a number"
    val hold = step.holdMs.toIntOrNull() ?: return "Hold must be a number"

    if (rpm !in Protocol.MIN_RPM..Protocol.MAX_RPM) return "Speed must be ${Protocol.MIN_RPM}-${Protocol.MAX_RPM} rpm"
    if (accel !in Protocol.MIN_ACCEL_MS..Protocol.MAX_ACCEL_MS) return "Ramp must be ${Protocol.MIN_ACCEL_MS}-${Protocol.MAX_ACCEL_MS}ms"
    if (hold < 0) return "Hold cannot be negative"

    return null
}

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun EditScreen(vm: MainViewModel, existing: Recipe?, onClose: () -> Unit) {
    val recipes by vm.recipes.collectAsState()

    var name by remember(existing?.id) { mutableStateOf(existing?.name ?: "") }
    var notes by remember(existing?.id) { mutableStateOf(existing?.notes ?: "") }
    val steps = remember(existing?.id) { mutableStateListOf<DraftStep>() }
    var seeded by remember(existing?.id) { mutableStateOf(false) }

    LaunchedEffect(existing?.id, recipes) {
        if (seeded) return@LaunchedEffect

        val loaded = recipes.firstOrNull { it.recipe.id == existing?.id }?.ordered
        steps.clear()
        if (!loaded.isNullOrEmpty()) {
            loaded.forEach {
                steps += DraftStep(
                    it.rpm.toString(), it.accelMs.toString(), it.holdMs.toString(), it.clockwise
                )
            }
        } else steps += DraftStep()

        seeded = true
    }

    val canSave = name.isNotBlank() && steps.isNotEmpty() && steps.all {
        stepError(
            it
        ) == null
    }

    Scaffold(topBar = {
        TopAppBar(
            title = { Text(if (existing != null) "Edit recipe" else "New recipe") },
            navigationIcon = {
                IconButton(onClick = onClose) {
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
                .padding(16.dp)
                .verticalScroll(rememberScrollState()),
            verticalArrangement = Arrangement.spacedBy(12.dp)
        ) {
            OutlinedTextField(
                value = name,
                onValueChange = { name = it },
                label = { Text("Recipe name") },
                isError = name.isBlank(),
                singleLine = true,
                supportingText = {
                    if (name.isBlank()) Text(
                        "Name cannot be empty.", color = MaterialTheme.colorScheme.error
                    )
                },
                keyboardOptions = KeyboardOptions(
                    capitalization = KeyboardCapitalization.Words, imeAction = ImeAction.Next
                ),
                modifier = Modifier.fillMaxWidth()
            )
            OutlinedTextField(
                value = notes,
                onValueChange = { notes = it },
                label = { Text("Notes") },
                supportingText = { Text("Solvent, substrate, target thickness, etc.") },
                modifier = Modifier.fillMaxWidth(),
                minLines = 4
            )

            steps.forEachIndexed { index, step ->
                ElevatedCard(Modifier.fillMaxWidth()) {
                    Column(
                        Modifier.padding(16.dp), verticalArrangement = Arrangement.spacedBy(8.dp)
                    ) {
                        Row(verticalAlignment = Alignment.CenterVertically) {
                            Text(
                                "Step ${index + 1}",
                                style = MaterialTheme.typography.titleMedium,
                                modifier = Modifier.weight(1f)
                            )
                            if (steps.size > 1) IconButton(onClick = { steps.removeAt(index) }) {
                                Icon(
                                    painterResource(R.drawable.trash),
                                    contentDescription = "Remove step ${index + 1}",
                                    tint = MaterialTheme.colorScheme.error,
                                    modifier = Modifier.height(18.dp)
                                )
                            }
                        }
                        Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                            OutlinedTextField(
                                value = step.rpm,
                                onValueChange = { steps[index] = step.copy(rpm = it) },
                                label = { Text("Speed") },
                                suffix = { Text("rpm") },
                                singleLine = true,
                                keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.Number),
                                modifier = Modifier.weight(1f)
                            )
                            Column(horizontalAlignment = Alignment.CenterHorizontally) {
                                Text("CW", style = MaterialTheme.typography.labelSmall)
                                Switch(
                                    checked = step.clockwise,
                                    onCheckedChange = { steps[index] = step.copy(clockwise = it) })
                            }
                        }
                        Row(
                            horizontalArrangement = Arrangement.spacedBy(8.dp),
                            verticalAlignment = Alignment.CenterVertically
                        ) {
                            OutlinedTextField(
                                value = step.accelMs,
                                onValueChange = { steps[index] = step.copy(accelMs = it) },
                                label = { Text("Ramp") },
                                suffix = { Text("ms") },
                                singleLine = true,
                                keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.Number),
                                modifier = Modifier.weight(1f)
                            )
                            OutlinedTextField(
                                value = step.holdMs,
                                onValueChange = { steps[index] = step.copy(holdMs = it) },
                                label = { Text("Hold") },
                                suffix = { Text("ms") },
                                singleLine = true,
                                keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.Number),
                                modifier = Modifier.weight(1f)
                            )
                        }
                        stepError(step)?.let {
                            Text(
                                it,
                                style = MaterialTheme.typography.bodySmall,
                                color = MaterialTheme.colorScheme.error
                            )
                        }
                    }
                }
            }
            OutlinedButton(onClick = { steps += DraftStep() }, modifier = Modifier.fillMaxWidth()) {
                Icon(
                    painterResource(R.drawable.plus),
                    contentDescription = null,
                    modifier = Modifier
                        .height(18.dp)
                        .padding(end = 8.dp)
                )
                Text("Add step")
            }
            Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                Button(enabled = canSave, onClick = {
                    vm.saveRecipe(
                        existingId = existing?.id,
                        name = name,
                        notes = notes,
                        steps = steps.mapIndexed { i, s ->
                            RecipeStep(
                                recipeId = existing?.id ?: 0L,
                                position = i,
                                rpm = s.rpm.toInt(),
                                accelMs = s.accelMs.toInt(),
                                holdMs = s.holdMs.toInt(),
                                clockwise = s.clockwise
                            )
                        })
                    onClose()
                }) { Text(if (existing != null) "Update" else "Save") }
                OutlinedButton(onClick = onClose) { Text("Cancel") }
            }
        }
    }
}

@Preview
@Composable
private fun EditScreenPreview() {
    EditScreen(vm = MainViewModel.preview(), existing = null, onClose = {})
}
