package com.sid.spincoater.ui.screens

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.Button
import androidx.compose.material3.ElevatedCard
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.FloatingActionButton
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.Scaffold
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
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.tooling.preview.Preview
import androidx.compose.ui.unit.dp
import com.sid.spincoater.R
import com.sid.spincoater.core.ConnState
import com.sid.spincoater.core.Recipe
import com.sid.spincoater.core.RecipeWithSteps
import com.sid.spincoater.viewmodel.MainViewModel

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun ListScreen(
    vm: MainViewModel,
    onAdd: () -> Unit,
    onEdit: (Recipe) -> Unit,
    onBack: () -> Unit,
    onRunStarted: () -> Unit
) {
    val recipes by vm.recipes.collectAsState()
    val connState by vm.connState.collectAsState()
    val connected = connState == ConnState.Connected

    Scaffold(topBar = {
        TopAppBar(title = { Text("Recipes") }, navigationIcon = {
            IconButton(onClick = onBack) {
                Icon(
                    painterResource(R.drawable.nav_arrow),
                    contentDescription = "Back",
                    modifier = Modifier.height(24.dp)
                )
            }
        })
    }, floatingActionButton = {
        FloatingActionButton(onClick = onAdd) {
            Icon(
                painterResource(R.drawable.plus),
                contentDescription = "New recipe",
                modifier = Modifier.height(24.dp)
            )
        }
    }) { pad ->
        LazyColumn(
            Modifier
                .padding(pad)
                .fillMaxSize()
                .padding(12.dp),
            verticalArrangement = Arrangement.spacedBy(10.dp)
        ) {
            items(recipes, key = { it.recipe.id }) { item ->
                RecipeRow(
                    item = item,
                    canRun = connected,
                    onRun = { vm.runRecipe(item); onRunStarted() },
                    onEdit = { onEdit(item.recipe) },
                    onDelete = { vm.deleteRecipe(item.recipe) })
            }
        }

        if (recipes.isEmpty()) Text(
            "No recipes yet. Tap + to add one.",
            Modifier
                .fillMaxSize()
                .padding(pad)
                .padding(24.dp),
            style = MaterialTheme.typography.bodyMedium,
            textAlign = TextAlign.Center
        )
    }
}

@Composable
private fun RecipeRow(
    item: RecipeWithSteps,
    canRun: Boolean,
    onRun: () -> Unit,
    onEdit: () -> Unit,
    onDelete: () -> Unit
) {
    var confirmDelete by remember { mutableStateOf(false) }

    if (confirmDelete) AlertDialog(
        onDismissRequest = { confirmDelete = false },
        title = { Text("Delete recipe") },
        text = { Text("Delete \"${item.recipe.name}\"? This cannot be undone.") },
        confirmButton = {
            Button(onClick = { confirmDelete = false; onDelete() }) { Text("Delete") }
        },
        dismissButton = {
            OutlinedButton(onClick = { confirmDelete = false }) { Text("Cancel") }
        })

    ElevatedCard(Modifier.fillMaxWidth()) {
        Column(Modifier.padding(16.dp), verticalArrangement = Arrangement.spacedBy(8.dp)) {
            Row(verticalAlignment = Alignment.CenterVertically) {
                Column(Modifier.weight(1f)) {
                    Text(item.recipe.name, style = MaterialTheme.typography.titleMedium)
                    Text(
                        "${item.steps.size} step${if (item.steps.size == 1) "" else "s"} - " +
                                "${item.totalMs / 1000}s total",
                        style = MaterialTheme.typography.bodySmall
                    )
                }
                IconButton(onClick = onEdit) {
                    Icon(
                        painterResource(R.drawable.edit_pencil),
                        contentDescription = "Edit",
                        tint = MaterialTheme.colorScheme.secondary,
                        modifier = Modifier.height(22.dp)
                    )
                }
                IconButton(onClick = { confirmDelete = true }) {
                    Icon(
                        painterResource(R.drawable.trash),
                        contentDescription = "Delete",
                        tint = MaterialTheme.colorScheme.error,
                        modifier = Modifier.height(22.dp)
                    )
                }
            }
            item.ordered.forEachIndexed { i, step ->
                Text(
                    "${i + 1}. ${step.rpm} RPM - ${step.accelMs / 1000}s ramp - ${step.dwellMs / 1000}s hold - " + if (step.clockwise) "CW" else "CCW",
                    style = MaterialTheme.typography.bodySmall,
                    fontFamily = FontFamily.Monospace
                )
            }
            if (item.recipe.notes.isNotBlank()) Text(
                item.recipe.notes, style = MaterialTheme.typography.bodySmall
            )

            Button(
                onClick = onRun,
                enabled = canRun && item.steps.isNotEmpty(),
                modifier = Modifier.fillMaxWidth()
            ) {
                Icon(
                    painterResource(R.drawable.play),
                    contentDescription = null,
                    modifier = Modifier
                        .height(18.dp)
                        .padding(end = 8.dp)
                )
                Text("Run", style = MaterialTheme.typography.bodyLarge)
            }
        }
    }
}

@Preview
@Composable
private fun ListScreenPreview() {
    ListScreen(
        vm = MainViewModel.preview(), onAdd = {}, onEdit = {}, onBack = {}, onRunStarted = {})
}
