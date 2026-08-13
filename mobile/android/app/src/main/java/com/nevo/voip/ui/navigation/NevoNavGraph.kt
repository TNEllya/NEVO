package com.nevo.voip.ui.navigation

import androidx.compose.runtime.Composable
import androidx.navigation.NavHostController
import androidx.navigation.NavType
import androidx.navigation.compose.NavHost
import androidx.navigation.compose.composable
import androidx.navigation.navArgument

sealed class Screen(val route: String) {
    object Home : Screen("home")
    object Connection : Screen("connection")
    object Channel : Screen("channel/{serverId}") {
        fun createRoute(serverId: String) = "channel/$serverId"
    }
    object Chat : Screen("chat/{channelId}") {
        fun createRoute(channelId: Long) = "chat/$channelId"
    }
    object Settings : Screen("settings")
    object ScreenShare : Screen("screen_share/{userId}") {
        fun createRoute(userId: Long) = "screen_share/$userId"
    }
}

@Composable
fun NevoNavGraph(navController: NavHostController) {
    NavHost(
        navController = navController,
        startDestination = Screen.Home.route
    ) {
        composable(Screen.Home.route) {
            HomeScreen(navController)
        }
        composable(Screen.Connection.route) {
            ConnectionScreen(navController)
        }
        composable(
            route = Screen.Channel.route,
            arguments = listOf(navArgument("serverId") { type = NavType.StringType })
        ) { backStackEntry ->
            val serverId = backStackEntry.arguments?.getString("serverId") ?: ""
            ChannelScreen(navController = navController, serverId = serverId)
        }
        composable(
            route = Screen.Chat.route,
            arguments = listOf(navArgument("channelId") { type = NavType.LongType })
        ) { backStackEntry ->
            val channelId = backStackEntry.arguments?.getLong("channelId") ?: 0L
            ChatScreen(navController = navController, channelId = channelId)
        }
        composable(Screen.Settings.route) {
            SettingsScreen(navController)
        }
        composable(
            route = Screen.ScreenShare.route,
            arguments = listOf(navArgument("userId") { type = NavType.LongType })
        ) { backStackEntry ->
            val userId = backStackEntry.arguments?.getLong("userId") ?: 0L
            ScreenShareScreen(navController = navController, userId = userId)
        }
    }
}

@Composable
fun HomeScreen(navController: NavHostController) {
    com.nevo.voip.feature.connection.ui.HomeContent(
        onConnectClick = { navController.navigate(Screen.Connection.route) },
        onServerClick = { host, port ->
            navController.navigate(Screen.Channel.createRoute("$host:$port"))
        },
        onSettingsClick = { navController.navigate(Screen.Settings.route) }
    )
}

@Composable
fun ConnectionScreen(navController: NavHostController) {
    com.nevo.voip.feature.connection.ui.ConnectionContent(
        onConnected = { host, port ->
            navController.navigate(Screen.Channel.createRoute("$host:$port")) {
                popUpTo(Screen.Home.route)
            }
        },
        onBack = { navController.popBackStack() }
    )
}

@Composable
fun ChannelScreen(navController: NavHostController, serverId: String) {
    com.nevo.voip.feature.channel.ui.ChannelContent(
        onChannelClick = { channelId ->
            navController.navigate(Screen.Chat.createRoute(channelId))
        },
        onScreenShareClick = { userId ->
            navController.navigate(Screen.ScreenShare.createRoute(userId))
        },
        onBack = { navController.popBackStack() }
    )
}

@Composable
fun ChatScreen(navController: NavHostController, channelId: Long) {
    com.nevo.voip.feature.chat.ui.ChatContent(
        channelId = channelId,
        onBack = { navController.popBackStack() }
    )
}

@Composable
fun SettingsScreen(navController: NavHostController) {
    com.nevo.voip.feature.settings.ui.SettingsContent(
        onBack = { navController.popBackStack() }
    )
}

@Composable
fun ScreenShareScreen(navController: NavHostController, userId: Long) {
    com.nevo.voip.feature.screen_share.ui.ScreenShareViewer(
        userId = userId,
        onBack = { navController.popBackStack() }
    )
}