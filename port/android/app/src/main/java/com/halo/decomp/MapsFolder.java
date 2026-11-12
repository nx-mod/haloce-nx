package com.halo.decomp;

import android.Manifest;
import android.app.Activity;
import android.content.Context;
import android.content.pm.PackageManager;
import android.net.Uri;
import android.os.Build;
import android.os.Environment;
import android.os.storage.StorageManager;
import android.os.storage.StorageVolume;
import android.provider.DocumentsContract;

import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;

/**
 * A folder of the player's own, outside the app's storage, for the game's maps, the Custom Edition maps and the
 * mods: the launcher's "Use another folder" (LauncherActivity). The folder chosen holds maps, custom_maps
 * and mods, which config.toml's game.maps.path, game.custom_maps.path and game.mods.path then name (the game's
 * reading of them: port/linux/src/xbox_files.c and port_mods.c; the host's check of the maps:
 * port/android/host/host_main.c). The disc image's maps are extracted into the first, the map lists' IMPORT copies
 * into the second, and the Mods menu's IMPORT into the third.
 *
 * The game reads the maps with plain file calls, not through the system's document provider, so a folder the
 * document picker grants is not enough: from Android 11 on the app needs All files access
 * (MANAGE_EXTERNAL_STORAGE), on Android 9 and 10 storage access (WRITE_EXTERNAL_STORAGE, with
 * requestLegacyExternalStorage for 10). The picker only names the folder, whose path is worked out from it.
 */
final class MapsFolder {
    static final String MAPS_KEY = "maps.path";
    static final String CUSTOM_MAPS_KEY = "custom_maps.path";
    static final String MODS_KEY = "mods.path";
    /** the folders made in the one chosen, and the settings that name them */
    private static final String[][] FOLDERS = {
        { "maps", MAPS_KEY }, { "custom_maps", CUSTOM_MAPS_KEY }, { "mods", MODS_KEY } };
    private static final String SECTION = "game";
    private static final String EXTERNAL_STORAGE_AUTHORITY = "com.android.externalstorage.documents";

    private MapsFolder() {
    }

    /** whether the app may read and write anywhere on the phone's storage (see the class's comment) */
    static boolean hasStorageAccess(Context context) {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R)
            return Environment.isExternalStorageManager();
        return context.checkSelfPermission(Manifest.permission.WRITE_EXTERNAL_STORAGE)
            == PackageManager.PERMISSION_GRANTED;
    }

    /** config.toml's string setting [game] key, unquoted; empty when it is not there or empty */
    static String setting(File dataRoot, String key) {
        File config = new File(dataRoot, "config.toml");
        String value = config.isFile() ? Updater.readSetting(config, SECTION, key) : null;

        return value != null ? unquote(value) : "";
    }

    /** the folder a setting names (an absolute path, or one under the data folder), null when it names none */
    static File configured(File dataRoot, String key) {
        String path = setting(dataRoot, key);

        if (path.isEmpty())
            return null;
        return path.startsWith("/") ? new File(path) : new File(dataRoot, path);
    }

    /** the maps of every Xbox disc's maps folder, campaign, multiplayer and the menus' (without .map) */
    static final String[] STOCK_MAPS = {
        "a10", "a30", "a50", "b30", "b40", "c10", "c20", "c40", "d20", "d40",
        "beavercreek", "bloodgulch", "boardingaction", "carousel", "chillout", "damnation", "hangemhigh", "longest",
        "prisoner", "putput", "ratrace", "sidewinder", "wizard", "ui" };

    /** the stock maps (STOCK_MAPS) that folder has no file of, whatever its case: none when the game data is there */
    static java.util.List<String> missingMaps(File folder) {
        java.util.Set<String> present = new java.util.HashSet<>();
        File[] files = folder.listFiles();
        if (files != null) {
            for (File file : files) {
                if (file.isFile())
                    present.add(file.getName().toLowerCase(java.util.Locale.ROOT));
            }
        }
        java.util.List<String> missing = new java.util.ArrayList<>();
        for (String map : STOCK_MAPS) {
            if (!present.contains(map + ".map"))
                missing.add(map);
        }
        return missing;
    }

    /** the folder the game's maps are read from: game.maps.path's, else the data folder's maps */
    static File maps(File dataRoot) {
        File folder = configured(dataRoot, MAPS_KEY);

        return folder != null ? folder : new File(dataRoot, "maps");
    }

    /**
     * folder's maps, custom_maps and mods made and written into config.toml as game.maps.path, game.custom_maps.path
     * and game.mods.path; why not, else null
     */
    static String use(File dataRoot, File folder) {
        for (String[] entry : FOLDERS) {
            File made = new File(folder, entry[0]);
            if (!made.isDirectory() && !made.mkdirs())
                return "Could not create " + made.getPath() + ".";
        }
        // (a folder the app may list but not write to, a memory card's on some phones, would fail the extraction
        // halfway through)
        File probe = new File(folder, "maps/.write-test");
        try (FileOutputStream out = new FileOutputStream(probe)) {
            out.write(0);
        } catch (IOException e) {
            return "The app cannot write to " + folder.getPath() + " (" + e.getMessage() + ").";
        } finally {
            probe.delete();
        }
        File config = new File(dataRoot, "config.toml");
        for (String[] entry : FOLDERS) {
            if (!Updater.writeSetting(config, SECTION, entry[1], quote(new File(folder, entry[0]).getAbsolutePath())))
                return "Could not write " + config.getPath() + ".";
        }
        return null;
    }

    /** the three settings emptied: the maps and mods in the app's storage again; false if not written */
    static boolean forget(File dataRoot) {
        File config = new File(dataRoot, "config.toml");

        for (String[] entry : FOLDERS) {
            if (!Updater.writeSetting(config, SECTION, entry[1], "\"\""))
                return false;
        }
        return true;
    }

    /**
     * the path of a folder the document picker chose (ACTION_OPEN_DOCUMENT_TREE), null for one that is not on the
     * phone's storage or a memory card (a cloud provider's): the external storage provider's document ids are the
     * volume, a colon and the path in it ("primary:halo" is /storage/emulated/0/halo)
     */
    static File fromTree(Context context, Uri tree) {
        if (tree == null || !EXTERNAL_STORAGE_AUTHORITY.equals(tree.getAuthority()))
            return null;
        String id;
        try {
            id = DocumentsContract.getTreeDocumentId(tree);
        } catch (IllegalArgumentException e) {
            return null;
        }
        int colon = id.indexOf(':');
        if (colon < 0)
            return null;
        String volume = id.substring(0, colon);
        String path = id.substring(colon + 1);
        File base = null;
        if (volume.equalsIgnoreCase("primary")) {
            base = Environment.getExternalStorageDirectory();
        } else if (volume.equalsIgnoreCase("home")) {
            // (some phones' Documents shortcut)
            base = Environment.getExternalStoragePublicDirectory(Environment.DIRECTORY_DOCUMENTS);
        } else if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
            StorageManager storage = (StorageManager) context.getSystemService(Context.STORAGE_SERVICE);
            if (storage != null) {
                for (StorageVolume candidate : storage.getStorageVolumes()) {
                    if (volume.equalsIgnoreCase(candidate.getUuid()))
                        base = candidate.getDirectory();
                }
            }
        } else {
            // (a memory card is mounted under its volume id)
            base = new File("/storage", volume);
        }
        if (base == null)
            return null;
        return path.isEmpty() ? base : new File(base, path);
    }

    /** a TOML basic string of text */
    private static String quote(String text) {
        return "\"" + text.replace("\\", "\\\\").replace("\"", "\\\"") + "\"";
    }

    /** a TOML string's text, as config.toml has it (Updater.readSetting: quotes, maybe a comment after) */
    private static String unquote(String value) {
        if (value.startsWith("'")) {
            int end = value.indexOf('\'', 1);
            return end > 0 ? value.substring(1, end) : "";
        }
        if (!value.startsWith("\""))
            return "";
        StringBuilder text = new StringBuilder();
        for (int index = 1; index < value.length(); index++) {
            char c = value.charAt(index);
            if (c == '"')
                break;
            if (c == '\\' && index + 1 < value.length())
                c = value.charAt(++index);
            text.append(c);
        }
        return text.toString();
    }
}
