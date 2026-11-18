package com.halo.decomp;

import android.os.StatFs;

import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.util.ArrayList;
import java.util.List;
import java.util.Locale;

/**
 * The game's files moved: maps, custom_maps, mods and mcc_maps, to the app's storage (APP DATA) or into a folder of
 * the player's own (OTHER FOLDER), from the game's Settings > Gameplay > GAME FILES (port/linux/game/menu_functions.c,
 * HaloActivity.moveGameFiles). mcc_staging, the MCC downloads' and imports' staging, stays in the data folder: a
 * package published from it to another storage is copied (port/linux/src/posix_mcc_share.c).
 *
 * The game reads where they are once, as it starts (config.toml's game.maps.path, game.custom_maps.path and
 * game.mods.path, which name the folders, and game.mcc.path, which names the folder holding mcc_maps: its name is
 * what the MCC code's checks know it by), and has them open while it runs, so the game only asks for the move
 * (request: MOVE_FILE in the data folder names where to) and starts again; the launcher moves them before the game
 * runs (LauncherActivity, run), then writes the settings. A folder is renamed when it can be (the same storage), else
 * moved file by file, each copied, synced, put in place and only then deleted from where it was, so that a move cut
 * short (the app closed, the storage full) loses nothing: the request stays, and the next start goes on, or the
 * player moves back what had gone (back). The settings name the old places until every file is moved.
 */
final class GameFiles {
    /** the request: "app", or the folder's absolute path */
    static final String MOVE_FILE = "game_files_move.txt";
    /** the folders moved, and the [game] settings naming them (MapsFolder's; mcc_maps' names its parent) */
    static final String[][] FOLDERS = {
        { "maps", MapsFolder.MAPS_KEY }, { "custom_maps", MapsFolder.CUSTOM_MAPS_KEY },
        { "mods", MapsFolder.MODS_KEY }, { "mcc_maps", MapsFolder.MCC_KEY } };
    private static final int MCC = 3;
    /** room left over on the storage moved to, besides the files */
    private static final long SPACE_MARGIN = 64L << 20;
    private static final String PART = ".moving";

    interface Progress {
        /** text for the screen, and permille done (-1: unknown) */
        void report(String text, int permille);
    }

    private GameFiles() {
    }

    /** where a folder is now: its setting's, else the data folder's */
    static File current(File dataRoot, int index) {
        File configured = MapsFolder.configured(dataRoot, FOLDERS[index][1]);

        if (index == MCC)
            return new File(configured != null ? configured : dataRoot, FOLDERS[index][0]);
        return configured != null ? configured : new File(dataRoot, FOLDERS[index][0]);
    }

    /** whether the files are in the app's storage now (no setting names another place) */
    static boolean inAppData(File dataRoot) {
        for (int index = 0; index < FOLDERS.length; index++) {
            if (!same(current(dataRoot, index), new File(dataRoot, FOLDERS[index][0])))
                return false;
        }
        return true;
    }

    /** the request written: base null for the app's storage; why not, else null */
    static String request(File dataRoot, File base) {
        if (base != null) {
            String problem = refused(dataRoot, base);
            if (problem != null)
                return problem;
        }
        try (OutputStream out = new FileOutputStream(new File(dataRoot, MOVE_FILE))) {
            out.write((base != null ? base.getAbsolutePath() : "app").getBytes("UTF-8"));
        } catch (IOException e) {
            return "The move could not be asked for (" + e.getMessage() + ").";
        }
        return null;
    }

    /** the folder asked to move to (the data folder for the app's storage), or null for none */
    static File requested(File dataRoot) {
        File file = new File(dataRoot, MOVE_FILE);
        if (!file.isFile())
            return null;
        String text;
        try (InputStream in = new FileInputStream(file)) {
            byte[] bytes = new byte[4096];
            int length = in.read(bytes);
            text = length > 0 ? new String(bytes, 0, length, "UTF-8").trim() : "";
        } catch (IOException e) {
            return null;
        }
        if (text.equals("app"))
            return dataRoot;
        return text.startsWith("/") ? new File(text) : null;
    }

    static void forgetRequest(File dataRoot) {
        new File(dataRoot, MOVE_FILE).delete();
    }

    /** why a folder of the player's cannot take the files, else null */
    static String refused(File dataRoot, File base) {
        if (same(base, dataRoot))
            return null;
        for (int index = 0; index < FOLDERS.length; index++) {
            File folder = current(dataRoot, index);
            if (inside(base, folder))
                return "Choose a folder outside " + folder.getAbsolutePath() + ".";
        }
        if (inside(base, dataRoot))
            return "Choose APP DATA for the app's storage, or a folder outside it.";
        return null;
    }

    /** the folders' moves to base: pairs of where each is and where it goes, those already there left out */
    static List<File[]> moves(File dataRoot, File base) {
        List<File[]> moves = new ArrayList<>();
        for (int index = 0; index < FOLDERS.length; index++) {
            File from = current(dataRoot, index);
            File to = new File(base, FOLDERS[index][0]);
            if (!same(from, to))
                moves.add(new File[] { from, to });
        }
        return moves;
    }

    /**
     * the files moved to base (the data folder: the app's storage), then the settings written and the request
     * forgotten; why not, else null (the request kept: the next start goes on)
     */
    static String run(File dataRoot, File base, Progress progress) {
        String problem = refused(dataRoot, base);
        if (problem != null)
            return problem;
        if (!base.isDirectory() && !base.mkdirs())
            return "Could not create " + base.getAbsolutePath() + ".";
        problem = move(moves(dataRoot, base), base, progress);
        if (problem != null)
            return problem;
        File config = new File(dataRoot, "config.toml");
        boolean app = same(base, dataRoot);
        for (int index = 0; index < FOLDERS.length; index++) {
            String[] folder = FOLDERS[index];
            new File(base, folder[0]).mkdirs();
            File named = index == MCC ? base : new File(base, folder[0]);
            String value = app ? "\"\"" : quote(named.getAbsolutePath());
            if (!Updater.writeSetting(config, "game", folder[1], value))
                return "The files are in " + base.getAbsolutePath() + ", but " + config.getPath()
                    + " could not be written.";
        }
        forgetRequest(dataRoot);
        return null;
    }

    /**
     * a move cut short undone: whatever reached base put back where the settings still say it is (they change only
     * once every file is moved), and the request forgotten; why not, else null
     */
    static String back(File dataRoot, File base, Progress progress) {
        List<File[]> moves = new ArrayList<>();
        for (File[] move : moves(dataRoot, base))
            moves.add(new File[] { move[1], move[0] });
        String problem = move(moves, dataRoot, progress);
        if (problem == null)
            forgetRequest(dataRoot);
        return problem;
    }

    /** each folder renamed, else its files moved one by one; why not, else null */
    private static String move(List<File[]> moves, File base, Progress progress) {
        List<File[]> copies = new ArrayList<>();
        for (File[] move : moves) {
            File from = move[0], to = move[1];
            if (!from.exists())
                continue;
            // (the same storage: at once; an empty folder in the way, made by a start in between, is no matter)
            if (to.isDirectory()) {
                String[] there = to.list();
                if (there != null && there.length == 0)
                    to.delete();
            }
            if (!to.exists()) {
                to.getParentFile().mkdirs();
                if (from.renameTo(to))
                    continue;
            }
            copies.add(move);
        }
        if (copies.isEmpty())
            return null;
        long total = 0;
        for (File[] move : copies)
            total += remaining(move[0], move[1]);
        long free = freeSpace(base);
        if (free >= 0 && total + SPACE_MARGIN > free)
            return "Not enough space in " + base.getAbsolutePath() + ": the files need " + size(total) + ", and it has "
                + size(free) + " free.";
        long[] done = { 0 };
        byte[] buffer = new byte[1 << 20];
        for (File[] move : copies) {
            String problem = moveTree(move[0], move[1], buffer, done, total, progress);
            if (problem != null)
                return problem;
        }
        return null;
    }

    /** a folder's files moved into another, the folders left empty removed; why not, else null */
    private static String moveTree(File from, File to, byte[] buffer, long[] done, long total, Progress progress) {
        if (from.isFile())
            return moveFile(from, to, buffer, done, total, progress);
        if (!to.isDirectory() && !to.mkdirs())
            return "Could not create " + to.getAbsolutePath() + ".";
        File[] children = from.listFiles();
        if (children == null)
            return "Could not read " + from.getAbsolutePath() + ".";
        for (File child : children) {
            // (a part of a move before this, cut short: copied again)
            if (child.getName().endsWith(PART)) {
                child.delete();
                continue;
            }
            String problem = moveTree(child, new File(to, child.getName()), buffer, done, total, progress);
            if (problem != null)
                return problem;
        }
        from.delete();
        return null;
    }

    /** a file copied beside where it goes, synced, put there, then deleted where it was; why not, else null */
    private static String moveFile(File from, File to, byte[] buffer, long[] done, long total, Progress progress) {
        long length = from.length();
        // (moved before a move cut short, its source not deleted yet)
        if (to.isFile() && to.length() == length) {
            from.delete();
            done[0] += length;
            return null;
        }
        File part = new File(to.getParentFile(), to.getName() + PART);
        try (InputStream in = new FileInputStream(from); FileOutputStream out = new FileOutputStream(part)) {
            int read;
            long reported = 0;
            while ((read = in.read(buffer)) > 0) {
                out.write(buffer, 0, read);
                done[0] += read;
                if (done[0] - reported >= (8L << 20)) {
                    reported = done[0];
                    progress.report(to.getName(), total > 0 ? (int) (done[0] * 1000 / total) : -1);
                }
            }
            out.getFD().sync();
        } catch (IOException e) {
            part.delete();
            return "Could not move " + from.getAbsolutePath() + " (" + e.getMessage() + ").";
        }
        if (to.exists())
            to.delete();
        if (!part.renameTo(to)) {
            part.delete();
            return "Could not put " + to.getAbsolutePath() + " in place.";
        }
        to.setLastModified(from.lastModified());
        from.delete();
        progress.report(to.getName(), total > 0 ? (int) (done[0] * 1000 / total) : -1);
        return null;
    }

    /** the bytes of a folder's files that are not where they go already */
    private static long remaining(File from, File to) {
        if (from.isFile())
            return to.isFile() && to.length() == from.length() ? 0 : from.length();
        long sum = 0;
        File[] children = from.listFiles();
        if (children != null) {
            for (File child : children)
                sum += remaining(child, new File(to, child.getName()));
        }
        return sum;
    }

    /** the free bytes of the storage a folder is on (its nearest existing folder's), -1 unknown */
    private static long freeSpace(File folder) {
        File existing = folder;
        while (existing != null && !existing.exists())
            existing = existing.getParentFile();
        if (existing == null)
            return -1;
        try {
            return new StatFs(existing.getAbsolutePath()).getAvailableBytes();
        } catch (IllegalArgumentException e) {
            return -1;
        }
    }

    /** the bytes of every file in the folders as they are now (for the game's screen) */
    static long totalSize(File dataRoot) {
        long sum = 0;
        for (int index = 0; index < FOLDERS.length; index++)
            sum += remaining(current(dataRoot, index), new File("/nonexistent"));
        return sum;
    }

    static String size(long bytes) {
        if (bytes >= (1L << 30))
            return String.format(Locale.ROOT, "%.1f GB", bytes / (double) (1L << 30));
        return String.format(Locale.ROOT, "%d MB", bytes >> 20);
    }

    private static boolean same(File a, File b) {
        try {
            return a.getCanonicalPath().equals(b.getCanonicalPath());
        } catch (IOException e) {
            return a.getAbsolutePath().equals(b.getAbsolutePath());
        }
    }

    /** whether inner is folder or inside it */
    private static boolean inside(File inner, File folder) {
        try {
            String a = inner.getCanonicalPath() + "/", b = folder.getCanonicalPath() + "/";
            return a.startsWith(b);
        } catch (IOException e) {
            return false;
        }
    }

    private static String quote(String text) {
        return "\"" + text.replace("\\", "\\\\").replace("\"", "\\\"") + "\"";
    }
}
