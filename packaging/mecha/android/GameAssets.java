package org.de_xr.mechahack;

import android.content.Context;
import java.io.*;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;

final class GameAssets {
    static void prepare(Context context) throws IOException {
        byte[] revision;
        try (InputStream stream = context.getAssets().open("game/revision.txt");
             ByteArrayOutputStream bytes = new ByteArrayOutputStream()) {
            byte[] buffer = new byte[1024]; int n;
            while ((n = stream.read(buffer)) != -1) bytes.write(buffer, 0, n);
            revision = bytes.toByteArray();
        }
        File marker = new File(context.getFilesDir(), "game/revision.txt");
        if (!marker.isFile() || !java.util.Arrays.equals(revision, Files.readAllBytes(marker.toPath()))) {
            marker.delete(); // Interrupted extraction must be retried at next launch.
            copy(context, "game");
            Files.write(marker.toPath(), revision); // Commit only after all assets are present.
        }
    }
    private static void copy(Context context, String path) throws IOException {
        if (path.equals("game/revision.txt")) return;
        String[] children = context.getAssets().list(path);
        File destination = new File(context.getFilesDir(), path);
        if (children != null && children.length > 0) {
            if (!destination.isDirectory() && !destination.mkdirs())
                throw new IOException("Cannot create " + destination);
            for (String child : children) copy(context, path + "/" + child);
        } else {
            try (InputStream input = context.getAssets().open(path);
                 OutputStream output = new FileOutputStream(destination)) {
                byte[] buffer = new byte[65536]; int count;
                while ((count = input.read(buffer)) != -1) output.write(buffer, 0, count);
            }
        }
    }
}
