package com.llama.serverapp;

import android.app.Notification;
import android.app.NotificationChannel;
import android.app.NotificationManager;
import android.app.PendingIntent;
import android.app.Service;
import android.content.Context;
import android.content.Intent;
import android.os.Build;
import android.os.IBinder;
import android.os.PowerManager;
import android.util.Log;

import androidx.annotation.Nullable;
import androidx.core.app.NotificationCompat;

import java.io.BufferedReader;
import java.io.File;
import java.io.FileOutputStream;
import java.io.InputStream;
import java.io.InputStreamReader;
import java.io.OutputStream;
import java.util.ArrayList;
import java.util.List;

public class LlamaServerService extends Service {
    private static final String TAG = "LlamaServerService";
    private static final String CHANNEL_ID = "LlamaServerChannel";
    private static final int NOTIFICATION_ID = 1001;

    private PowerManager.WakeLock wakeLock;
    private Process serverProcess;
    private Thread serverThread;
    private boolean isRunning = false;

    // Buffer to hold logs so they can be retrieved when UI attaches
    private static final int MAX_LOG_LINES = 1000;
    private static final List<String> logBuffer = new ArrayList<>();
    private static LogListener logListener;

    public interface LogListener {
        void onLog(String line);
        void onServerStopped();
    }

    public static synchronized void setLogListener(LogListener listener) {
        logListener = listener;
        if (listener != null) {
            // Replay existing logs to the listener
            for (String line : logBuffer) {
                listener.onLog(line);
            }
        }
    }

    public static synchronized List<String> getLogBuffer() {
        return new ArrayList<>(logBuffer);
    }

    private static synchronized void addLog(String line) {
        if (logBuffer.size() >= MAX_LOG_LINES) {
            logBuffer.remove(0);
        }
        logBuffer.add(line);
        if (logListener != null) {
            logListener.onLog(line);
        }
    }

    private static synchronized void clearLogs() {
        logBuffer.clear();
    }

    @Override
    public void onCreate() {
        super.onCreate();
        createNotificationChannel();
    }

    @Override
    public int onStartCommand(Intent intent, int flags, int startId) {
        if (intent == null) {
            stopSelf();
            return START_NOT_STICKY;
        }

        String action = intent.getAction();
        if ("STOP".equals(action)) {
            stopServer();
            stopSelf();
            return START_NOT_STICKY;
        }

        // Retrieve arguments
        String modelPath = intent.getStringExtra("model_path");
        String port = intent.getStringExtra("port");
        int contextSize = intent.getIntExtra("context_size", 8192);
        boolean enableMtp = intent.getBooleanExtra("enable_mtp", false);
        boolean enableNgram = intent.getBooleanExtra("enable_ngram", false);
        boolean disableReasoning = intent.getBooleanExtra("disable_reasoning", false);
        boolean enableFlashAttn = intent.getBooleanExtra("enable_flash_attn", true);

        // Start Foreground Notification
        Notification notification = buildNotification("Llama Server starting...");
        startForeground(NOTIFICATION_ID, notification);

        // Start Server Execution
        startServer(modelPath, port, contextSize, enableMtp, enableNgram, disableReasoning, enableFlashAttn);

        return START_STICKY;
    }

    private void startServer(String modelPath, String port, int contextSize, boolean enableMtp, boolean enableNgram, boolean disableReasoning, boolean enableFlashAttn) {
        if (isRunning) {
            addLog("[Service] Server is already running.");
            return;
        }

        isRunning = true;
        clearLogs();
        addLog("[Service] Preparing server binary...");

        // Acquire WakeLock to keep CPU running 24/7
        PowerManager pm = (PowerManager) getSystemService(Context.POWER_SERVICE);
        if (pm != null) {
            wakeLock = pm.newWakeLock(PowerManager.PARTIAL_WAKE_LOCK, "LlamaServer::WakeLock");
            wakeLock.acquire();
            addLog("[Service] WakeLock acquired.");
        }

        serverThread = new Thread(() -> {
            try {
                File binaryFile = new File(getApplicationInfo().nativeLibraryDir, "libllama-server.so");

                if (!binaryFile.exists()) {
                    addLog("[Service] ERROR: Native binary not found at " + binaryFile.getAbsolutePath());
                    stopSelf();
                    return;
                }

                // Verify model file exists
                File modelFile = new File(modelPath);
                if (!modelFile.exists()) {
                    addLog("[Service] ERROR: Model file does not exist at path: " + modelPath);
                    stopSelf();
                    return;
                }

                // Build command
                List<String> command = new ArrayList<>();
                command.add(binaryFile.getAbsolutePath());
                command.add("-m");
                command.add(modelPath);
                command.add("--host");
                command.add("0.0.0.0");
                command.add("--port");
                command.add(port);
                command.add("-c");
                command.add(String.valueOf(contextSize));
                command.add("-ctk");
                command.add("f16");
                command.add("-ctv");
                command.add("f16");

                if (enableFlashAttn) {
                    command.add("-fa");
                    command.add("on");
                }

                List<String> specTypes = new ArrayList<>();
                if (enableMtp) {
                    specTypes.add("draft-mtp");
                }
                if (enableNgram) {
                    specTypes.add("ngram-mod");
                }
                if (!specTypes.isEmpty()) {
                    command.add("--spec-type");
                    command.add(String.join(",", specTypes));
                }

                if (disableReasoning) {
                    command.add("--reasoning");
                    command.add("off");
                    command.add("--reasoning-budget");
                    command.add("0");
                }

                addLog("[Service] Launching command: " + String.join(" ", command));

                ProcessBuilder pb = new ProcessBuilder(command);
                pb.environment().put("LD_LIBRARY_PATH", getApplicationInfo().nativeLibraryDir);
                pb.redirectErrorStream(true); // Combine stdout and stderr
                serverProcess = pb.start();

                // Update notification state
                updateNotification("Llama Server running on port " + port);

                // Read output
                try (InputStream is = serverProcess.getInputStream();
                     BufferedReader reader = new BufferedReader(new InputStreamReader(is))) {
                    String line;
                    while ((line = reader.readLine()) != null) {
                        addLog(line);
                    }
                }

                int exitCode = serverProcess.waitFor();
                addLog("[Service] Server process exited with code " + exitCode);

            } catch (Exception e) {
                addLog("[Service] ERROR during execution: " + e.getMessage());
                Log.e(TAG, "Error in server thread", e);
            } finally {
                cleanUpResources();
                stopSelf();
            }
        });

        serverThread.start();
    }

    private void stopServer() {
        addLog("[Service] Stopping server...");
        if (serverProcess != null) {
            serverProcess.destroy();
        }
        cleanUpResources();
    }

    private void cleanUpResources() {
        isRunning = false;
        if (wakeLock != null && wakeLock.isHeld()) {
            wakeLock.release();
            addLog("[Service] WakeLock released.");
        }
        if (logListener != null) {
            logListener.onServerStopped();
        }
        updateNotification("Llama Server is stopped.");
    }

    private void createNotificationChannel() {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
            NotificationChannel serviceChannel = new NotificationChannel(
                    CHANNEL_ID,
                    "Llama Server Service Channel",
                    NotificationManager.IMPORTANCE_DEFAULT
            );
            NotificationManager manager = getSystemService(NotificationManager.class);
            if (manager != null) {
                manager.createNotificationChannel(serviceChannel);
            }
        }
    }

    private Notification buildNotification(String text) {
        Intent notificationIntent = new Intent(this, MainActivity.class);
        PendingIntent pendingIntent = PendingIntent.getActivity(
                this, 0, notificationIntent,
                PendingIntent.FLAG_IMMUTABLE | PendingIntent.FLAG_UPDATE_CURRENT
        );

        Intent stopIntent = new Intent(this, LlamaServerService.class);
        stopIntent.setAction("STOP");
        PendingIntent stopPendingIntent = PendingIntent.getService(
                this, 1, stopIntent,
                PendingIntent.FLAG_IMMUTABLE | PendingIntent.FLAG_UPDATE_CURRENT
        );

        return new NotificationCompat.Builder(this, CHANNEL_ID)
                .setContentTitle("Llama Local Server")
                .setContentText(text)
                .setSmallIcon(android.R.drawable.stat_sys_upload_done)
                .setContentIntent(pendingIntent)
                .addAction(android.R.drawable.ic_menu_close_clear_cancel, "Stop Server", stopPendingIntent)
                .setOngoing(true)
                .build();
    }

    private void updateNotification(String text) {
        NotificationManager manager = (NotificationManager) getSystemService(Context.NOTIFICATION_SERVICE);
        if (manager != null) {
            manager.notify(NOTIFICATION_ID, buildNotification(text));
        }
    }

    @Nullable
    @Override
    public IBinder onBind(Intent intent) {
        return null;
    }

    @Override
    public void onDestroy() {
        stopServer();
        super.onDestroy();
    }
}
