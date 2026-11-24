package org.de_xr.mechahack;

import android.Manifest;
import android.app.Activity;
import android.content.Intent;
import android.content.pm.PackageManager;
import android.os.Bundle;
import android.widget.TextView;

// Resolve microphone permission before starting SDL's native game thread.
public final class LauncherActivity extends Activity {
    @Override public void onCreate(Bundle state) {
        super.onCreate(state);
        if (checkSelfPermission(Manifest.permission.RECORD_AUDIO) != PackageManager.PERMISSION_GRANTED)
            requestPermissions(new String[]{Manifest.permission.RECORD_AUDIO}, 1);
        else launch();
    }
    @Override public void onRequestPermissionsResult(int request, String[] permissions, int[] results) {
        super.onRequestPermissionsResult(request, permissions, results);
        if (request == 1) launch(); // Denied permission starts in listen-only mode.
    }
    private void launch() {
        TextView progress = new TextView(this);
        progress.setText("Preparing game assets…");
        progress.setGravity(android.view.Gravity.CENTER);
        setContentView(progress);
        new Thread(() -> {
            try {
                GameAssets.prepare(this);
                runOnUiThread(() -> {
                    if (!isFinishing() && !isDestroyed()) {
                        Intent game = new Intent(this, MechaActivity.class);
                        game.setAction(Intent.ACTION_MAIN);
                        game.addCategory("com.oculus.intent.category.VR");
                        game.addCategory("org.khronos.openxr.intent.category.IMMERSIVE_HMD");
                        startActivity(game);
                        finish();
                    }
                });
            } catch (java.io.IOException error) {
                runOnUiThread(() -> progress.setText("Cannot prepare game assets: " + error.getMessage()));
            }
        }, "Mecha Hack assets").start();
    }
}
