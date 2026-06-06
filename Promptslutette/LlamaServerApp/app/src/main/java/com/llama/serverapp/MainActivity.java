package com.llama.serverapp;

import android.Manifest;
import android.content.Context;
import android.content.Intent;
import android.content.pm.PackageManager;
import android.net.Uri;
import android.os.Build;
import android.os.Bundle;
import android.os.Environment;
import android.os.PowerManager;
import android.provider.Settings;
import android.text.method.ScrollingMovementMethod;
import android.view.View;
import android.widget.ArrayAdapter;
import android.widget.Button;
import android.widget.CheckBox;
import android.widget.EditText;
import android.widget.ScrollView;
import android.widget.Spinner;
import android.widget.TextView;
import android.widget.Toast;

import androidx.annotation.NonNull;
import androidx.appcompat.app.AlertDialog;
import androidx.appcompat.app.AppCompatActivity;
import androidx.core.app.ActivityCompat;
import androidx.core.content.ContextCompat;

import java.io.File;
import java.util.ArrayList;
import java.util.List;

public class MainActivity extends AppCompatActivity {
    private static final int PERMISSION_REQUEST_CODE = 2002;

    private EditText etPort;
    private EditText etModelPath;
    private Button btnBrowseModel;
    private Spinner spContextSize;
    private CheckBox cbMtp;
    private CheckBox cbNgram;
    private CheckBox cbDisableReasoning;
    private CheckBox cbFlashAttn;
    private Button btnStart;
    private Button btnStop;
    private Button btnOpenWeb;
    private TextView tvLog;
    private ScrollView scrollLog;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        setContentView(R.layout.activity_main);

        // Bind views
        etPort = findViewById(R.id.etPort);
        etModelPath = findViewById(R.id.etModelPath);
        btnBrowseModel = findViewById(R.id.btnBrowseModel);
        spContextSize = findViewById(R.id.spContextSize);
        cbMtp = findViewById(R.id.cbMtp);
        cbNgram = findViewById(R.id.cbNgram);
        cbDisableReasoning = findViewById(R.id.cbDisableReasoning);
        cbFlashAttn = findViewById(R.id.cbFlashAttn);
        btnStart = findViewById(R.id.btnStart);
        btnStop = findViewById(R.id.btnStop);
        btnOpenWeb = findViewById(R.id.btnOpenWeb);
        tvLog = findViewById(R.id.tvLog);
        scrollLog = findViewById(R.id.scrollLog);

        tvLog.setMovementMethod(new ScrollingMovementMethod());

        // Setup Context Size Spinner
        String[] contextOptions = {"4k", "8k", "16k", "32k"};
        ArrayAdapter<String> spinnerAdapter = new ArrayAdapter<>(this, android.R.layout.simple_spinner_item, contextOptions);
        spinnerAdapter.setDropDownViewResource(android.R.layout.simple_spinner_dropdown_item);
        spContextSize.setAdapter(spinnerAdapter);
        spContextSize.setSelection(1); // Default to "8k"

        // Setup Browse Button
        btnBrowseModel.setOnClickListener(v -> {
            if (checkStoragePermission()) {
                showGgufFilePicker();
            } else {
                requestStoragePermission();
            }
        });

        // Setup Start Button
        btnStart.setOnClickListener(v -> {
            if (!checkStoragePermission()) {
                requestStoragePermission();
                return;
            }
            if (!checkNotificationPermission()) {
                requestNotificationPermission();
                return;
            }
            checkAndRequestBatteryOptimizations();
            startServerService();
        });

        // Setup Stop Button
        btnStop.setOnClickListener(v -> {
            stopServerService();
        });

        // Setup Open Web UI Button
        btnOpenWeb.setOnClickListener(v -> {
            String port = etPort.getText().toString().trim();
            String url = "http://localhost:" + port;
            Intent intent = new Intent(Intent.ACTION_VIEW, Uri.parse(url));
            startActivity(intent);
        });

        // Request permissions on startup
        requestAllRequiredPermissions();

        // Start Promptslutette RAG Sync Service automatically on boot
        Intent syncIntent = new Intent(this, PromptslutetteService.class);
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
            startForegroundService(syncIntent);
        } else {
            startService(syncIntent);
        }
    }

    @Override
    protected void onResume() {
        super.onResume();
        // Bind service log listener
        LlamaServerService.setLogListener(new LlamaServerService.LogListener() {
            @Override
            public void onLog(final String line) {
                runOnUiThread(() -> {
                    tvLog.append(line + "\n");
                    // Auto scroll to bottom
                    scrollLog.post(() -> scrollLog.fullScroll(View.FOCUS_DOWN));
                });
            }

            @Override
            public void onServerStopped() {
                runOnUiThread(() -> {
                    tvLog.append("[System] Server has stopped.\n");
                    btnStart.setEnabled(true);
                    btnStop.setEnabled(false);
                });
            }
        });

        // Update initial button states based on log buffer
        boolean isCurrentlyRunning = !LlamaServerService.getLogBuffer().isEmpty();
        if (isCurrentlyRunning) {
            btnStart.setEnabled(false);
            btnStop.setEnabled(true);
        } else {
            btnStart.setEnabled(true);
            btnStop.setEnabled(false);
        }
    }

    @Override
    protected void onPause() {
        super.onResume();
        // Remove listener when not visible to save memory
        LlamaServerService.setLogListener(null);
    }

    private void startServerService() {
        String modelPath = etModelPath.getText().toString().trim();
        String port = etPort.getText().toString().trim();
        boolean enableMtp = cbMtp.isChecked();
        boolean enableNgram = cbNgram.isChecked();
        boolean disableReasoning = cbDisableReasoning.isChecked();
        boolean enableFlashAttn = cbFlashAttn.isChecked();

        String contextStr = spContextSize.getSelectedItem().toString();
        int contextSize = 8192;
        if ("4k".equals(contextStr)) {
            contextSize = 4096;
        } else if ("8k".equals(contextStr)) {
            contextSize = 8192;
        } else if ("16k".equals(contextStr)) {
            contextSize = 16384;
        } else if ("32k".equals(contextStr)) {
            contextSize = 32768;
        }

        if (modelPath.isEmpty() || port.isEmpty()) {
            Toast.makeText(this, "Model path and Port cannot be empty!", Toast.LENGTH_SHORT).show();
            return;
        }

        File modelFile = new File(modelPath);
        if (!modelFile.exists()) {
            Toast.makeText(this, "Model file not found at path!", Toast.LENGTH_SHORT).show();
            return;
        }

        tvLog.setText("[System] Starting server service...\n");

        Intent serviceIntent = new Intent(this, LlamaServerService.class);
        serviceIntent.putExtra("model_path", modelPath);
        serviceIntent.putExtra("port", port);
        serviceIntent.putExtra("context_size", contextSize);
        serviceIntent.putExtra("enable_mtp", enableMtp);
        serviceIntent.putExtra("enable_ngram", enableNgram);
        serviceIntent.putExtra("disable_reasoning", disableReasoning);
        serviceIntent.putExtra("enable_flash_attn", enableFlashAttn);

        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
            startForegroundService(serviceIntent);
        } else {
            startService(serviceIntent);
        }

        btnStart.setEnabled(false);
        btnStop.setEnabled(true);
    }

    private void stopServerService() {
        tvLog.append("[System] Stopping server service...\n");
        Intent serviceIntent = new Intent(this, LlamaServerService.class);
        stopService(serviceIntent);
        btnStart.setEnabled(true);
        btnStop.setEnabled(false);
    }

    private void showGgufFilePicker() {
        File downloadDir = Environment.getExternalStoragePublicDirectory(Environment.DIRECTORY_DOWNLOADS);
        if (!downloadDir.exists()) {
            Toast.makeText(this, "Download folder not found!", Toast.LENGTH_SHORT).show();
            return;
        }

        File[] files = downloadDir.listFiles();
        if (files == null || files.length == 0) {
            Toast.makeText(this, "No files found in Download folder!", Toast.LENGTH_SHORT).show();
            return;
        }

        List<String> ggufFiles = new ArrayList<>();
        for (File f : files) {
            if (f.isFile() && f.getName().toLowerCase().endsWith(".gguf")) {
                ggufFiles.add(f.getName());
            }
        }

        if (ggufFiles.isEmpty()) {
            Toast.makeText(this, "No .gguf files found in Download folder!", Toast.LENGTH_SHORT).show();
            return;
        }

        final String[] items = ggufFiles.toArray(new String[0]);
        new AlertDialog.Builder(this)
                .setTitle("Select GGUF Model")
                .setItems(items, (dialog, which) -> {
                    File selectedFile = new File(downloadDir, items[which]);
                    etModelPath.setText(selectedFile.getAbsolutePath());
                })
                .show();
    }

    // Permission Checking

    private boolean checkStoragePermission() {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
            return Environment.isExternalStorageManager();
        } else {
            int write = ContextCompat.checkSelfPermission(this, Manifest.permission.WRITE_EXTERNAL_STORAGE);
            int read = ContextCompat.checkSelfPermission(this, Manifest.permission.READ_EXTERNAL_STORAGE);
            return write == PackageManager.PERMISSION_GRANTED && read == PackageManager.PERMISSION_GRANTED;
        }
    }

    private void requestStoragePermission() {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
            try {
                Intent intent = new Intent(Settings.ACTION_MANAGE_APP_ALL_FILES_ACCESS_PERMISSION);
                intent.addCategory("android.intent.category.DEFAULT");
                intent.setData(Uri.parse(String.format("package:%s", getApplicationContext().getPackageName())));
                startActivity(intent);
                Toast.makeText(this, "Please enable 'All Files Access' permission for Llama Server", Toast.LENGTH_LONG).show();
            } catch (Exception e) {
                Intent intent = new Intent();
                intent.setAction(Settings.ACTION_MANAGE_ALL_FILES_ACCESS_PERMISSION);
                startActivity(intent);
            }
        } else {
            ActivityCompat.requestPermissions(
                    this,
                    new String[]{Manifest.permission.READ_EXTERNAL_STORAGE, Manifest.permission.WRITE_EXTERNAL_STORAGE},
                    PERMISSION_REQUEST_CODE
            );
        }
    }

    private boolean checkNotificationPermission() {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
            return ContextCompat.checkSelfPermission(this, Manifest.permission.POST_NOTIFICATIONS) == PackageManager.PERMISSION_GRANTED;
        }
        return true;
    }

    private void requestNotificationPermission() {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
            ActivityCompat.requestPermissions(
                    this,
                    new String[]{Manifest.permission.POST_NOTIFICATIONS},
                    PERMISSION_REQUEST_CODE + 1
            );
        }
    }

    private void checkAndRequestBatteryOptimizations() {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.M) {
            PowerManager pm = (PowerManager) getSystemService(Context.POWER_SERVICE);
            if (pm != null && !pm.isIgnoringBatteryOptimizations(getPackageName())) {
                Intent intent = new Intent();
                intent.setAction(Settings.ACTION_REQUEST_IGNORE_BATTERY_OPTIMIZATIONS);
                intent.setData(Uri.parse("package:" + getPackageName()));
                startActivity(intent);
                Toast.makeText(this, "Please disable battery optimization to keep the server running 24/7.", Toast.LENGTH_LONG).show();
            }
        }
    }

    private void requestAllRequiredPermissions() {
        if (!checkStoragePermission()) {
            requestStoragePermission();
        } else if (!checkNotificationPermission()) {
            requestNotificationPermission();
        }
    }

    @Override
    public void onRequestPermissionsResult(int requestCode, @NonNull String[] permissions, @NonNull int[] grantResults) {
        super.onRequestPermissionsResult(requestCode, permissions, grantResults);
        if (grantResults.length > 0 && grantResults[0] == PackageManager.PERMISSION_GRANTED) {
            Toast.makeText(this, "Permission granted!", Toast.LENGTH_SHORT).show();
        }
    }
}
