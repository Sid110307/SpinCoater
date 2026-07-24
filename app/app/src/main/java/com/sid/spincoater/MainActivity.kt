package com.sid.spincoater

import android.Manifest
import android.bluetooth.BluetoothAdapter
import android.bluetooth.BluetoothManager
import android.content.Intent
import android.os.Build
import android.os.Bundle
import android.view.WindowManager
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.activity.enableEdgeToEdge
import androidx.activity.result.contract.ActivityResultContracts
import androidx.activity.result.contract.ActivityResultContracts.RequestMultiplePermissions
import androidx.activity.viewModels
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.ui.tooling.preview.Preview
import androidx.navigation.NavType
import androidx.navigation.compose.NavHost
import androidx.navigation.compose.composable
import androidx.navigation.compose.rememberNavController
import androidx.navigation.navArgument
import com.sid.spincoater.ui.screens.DeviceScreen
import com.sid.spincoater.ui.screens.EditScreen
import com.sid.spincoater.ui.screens.HomeScreen
import com.sid.spincoater.ui.screens.ListScreen
import com.sid.spincoater.ui.screens.LogScreen
import com.sid.spincoater.ui.screens.SettingsScreen
import com.sid.spincoater.ui.theme.SpinCoaterTheme
import com.sid.spincoater.viewmodel.MainViewModel

class MainActivity : ComponentActivity() {
    private val vm: MainViewModel by viewModels()

    private val permissionLauncher =
        registerForActivityResult(RequestMultiplePermissions()) { result ->
            val granted = if (Build.VERSION.SDK_INT >= 31) {
                result[Manifest.permission.BLUETOOTH_SCAN] == true && result[Manifest.permission.BLUETOOTH_CONNECT] == true
            } else {
                result[Manifest.permission.ACCESS_FINE_LOCATION] == true
            }
            if (granted && isBluetoothOn()) {
                vm.refreshDevices()
                vm.autoConnect()
            }
        }

    private val enableBtLauncher =
        registerForActivityResult(ActivityResultContracts.StartActivityForResult()) {
            requestBtPerms()
        }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        enableEdgeToEdge()
        setContent { SpinCoaterApp(vm, ::applyKeepScreenOn) }

        if (!isBluetoothOn()) enableBtLauncher.launch(Intent(BluetoothAdapter.ACTION_REQUEST_ENABLE))
        else requestBtPerms()
    }

    override fun onDestroy() {
        super.onDestroy()
        vm.bt.unregisterReceivers()
    }

    private fun requestBtPerms() {
        val perms = mutableListOf<String>()
        if (Build.VERSION.SDK_INT >= 31) {
            perms += Manifest.permission.BLUETOOTH_SCAN
            perms += Manifest.permission.BLUETOOTH_CONNECT
        } else {
            perms += Manifest.permission.ACCESS_FINE_LOCATION
        }
        permissionLauncher.launch(perms.toTypedArray())
    }

    private fun isBluetoothOn(): Boolean {
        val mgr = getSystemService(BLUETOOTH_SERVICE) as BluetoothManager
        return mgr.adapter?.isEnabled == true
    }

    private fun applyKeepScreenOn(on: Boolean) {
        if (on) window.addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)
        else window.clearFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)
    }
}

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun SpinCoaterApp(vm: MainViewModel, onKeepScreenOn: (Boolean) -> Unit = {}) {
    SpinCoaterTheme {
        val nav = rememberNavController()
        val keepOn by vm.keepScreenOn.collectAsState()
        val run by vm.runProgress.collectAsState()
        val status by vm.status.collectAsState()

        LaunchedEffect(keepOn, run, status?.running) {
            onKeepScreenOn(keepOn && (run != null || status?.running == true))
        }

        NavHost(navController = nav, startDestination = "home") {
            composable("home") {
                HomeScreen(
                    vm = vm,
                    onOpenLog = { nav.navigate("log") },
                    onOpenRecipes = { nav.navigate("recipes") },
                    onOpenDevices = { nav.navigate("devices") },
                    onOpenSettings = { nav.navigate("settings") })
            }
            composable("devices") { DeviceScreen(vm = vm, onBack = { nav.popBackStack() }) }
            composable("log") { LogScreen(vm = vm, onBack = { nav.popBackStack() }) }
            composable("settings") { SettingsScreen(vm = vm, onBack = { nav.popBackStack() }) }
            composable("recipes") {
                ListScreen(
                    vm = vm,
                    onAdd = { nav.navigate("recipes/edit") },
                    onEdit = { recipe -> nav.navigate("recipes/edit/${recipe.id}") },
                    onBack = { nav.popBackStack() },
                    onRunStarted = { nav.popBackStack("home", inclusive = false) })
            }
            composable("recipes/edit") {
                EditScreen(vm = vm, existing = null, onClose = { nav.popBackStack() })
            }
            composable(
                route = "recipes/edit/{id}",
                arguments = listOf(navArgument("id") { type = NavType.LongType })
            ) { entry ->
                val id = entry.arguments?.getLong("id") ?: -1L
                EditScreen(
                    vm = vm,
                    existing = vm.recipes.collectAsState().value.firstOrNull { it.recipe.id == id }?.recipe,
                    onClose = { nav.popBackStack() })
            }
        }
    }
}

@Preview
@Composable
private fun PreviewApp() {
    SpinCoaterApp(MainViewModel.preview())
}
