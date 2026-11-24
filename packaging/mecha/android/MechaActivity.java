package org.de_xr.mechahack;

import java.io.File;
import org.libsdl.app.SDLActivity;

public final class MechaActivity extends SDLActivity {
    @Override protected String[] getLibraries() {
        return new String[]{"SDL2", "openxr_loader", "mecha-hack-xr"};
    }
    @Override protected String getMainSharedObject() {
        return getApplicationInfo().nativeLibraryDir + "/libmecha-hack-xr.so";
    }
    @Override protected String[] getArguments() {
        return new String[]{"--xr", "--assets", new File(getFilesDir(), "game/assets/mecha").getAbsolutePath()};
    }
}
