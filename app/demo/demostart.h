#pragma once

class Account;
class History;
class QGuiApplication;
class QQmlApplicationEngine;
class SyncBridge;

// Wires the demo world into the app and, with SHOT_PLAN/SHOT_DIR, runs the screenshots.
// Call before the engine loads Main.qml. Only in builds with KLADDE_DEMO.
void startDemo(QGuiApplication &app, QQmlApplicationEngine &engine, Account &account, History &history,
               SyncBridge &sync);
