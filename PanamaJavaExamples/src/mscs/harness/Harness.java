// Copyright © 2026 Khrustal & Mann
//              MELBOURNE, VICTORIA, AUSTRALIA, 3000
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or
// implied. See the License for the specific language governing
// permissions and limitations under the License.
package mscs.harness;

import java.io.PrintStream;
import java.nio.charset.StandardCharsets;
import java.time.LocalTime;
import java.time.format.DateTimeFormatter;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.TimeUnit;

/**
 * The small amount of scaffolding the Java harnesses still need, once the facade
 * has absorbed everything else. The counterpart of
 * {@code FacadeExamples/common/LightHarness.h}.
 *
 * <p>What is NOT here is the point. The originals in {@code DirectExamples}
 * each carried a {@code stdafx.h} pulling in half of MFC, a {@code CWinApp
 * theApp}, a {@code _CrtSetReportHook} assert trap, the
 * {@code StartupP2Pmsg}/{@code WSAStartup}/{@code SpawnHub}/{@code CloseHub}/
 * {@code CleanupP2Pmsg}/{@code WSACleanup} sequence on every exit path, and a
 * {@code P2PeerHub} subclass with seven {@code On_Con*} overrides. None of it
 * survives. What is left is: print a line, and wait for a thing.
 *
 * <h2>Exit-code contract</h2>
 * Kept identical to the originals so all five trees are directly comparable:
 * <pre>
 *   0 = SUCCESS
 *   1 = SETUP    (startup/arming failure)
 *   3 = TIMEOUT  (no assert, but the expected traffic never arrived)
 *   2 = was "an MFC/CRT assertion fired". Unreachable here by construction:
 *       there is no MFC in the Java process's own code, and the facade reports
 *       failures as HRESULTs and OnError callbacks rather than modal boxes.
 * </pre>
 */
public final class Harness {

    private Harness() { }

    public static final int EXIT_SUCCESS = 0;
    public static final int EXIT_SETUP   = 1;
    public static final int EXIT_TIMEOUT = 3;

    private static final DateTimeFormatter TS = DateTimeFormatter.ofPattern("HH:mm:ss.SSS");

    /**
     * A UTF-8 stdout, replacing the originals' {@code _setmode(_O_U16TEXT)}.
     *
     * <p>The console's default encoding on this machine is a legacy code page,
     * and every address and payload in this tree is Unicode. Wrapping stdout
     * once here is the Java spelling of what the C++ harnesses do on their first
     * line — and it has to happen before anything is printed, not after.
     */
    private static final PrintStream OUT =
            new PrintStream(new java.io.FileOutputStream(java.io.FileDescriptor.out),
                            true, StandardCharsets.UTF_8);

    /** All output goes through one lock, so two pump threads cannot interleave a line. */
    private static final Object LOCK = new Object();

    public static void banner(String line) {
        synchronized (LOCK) { OUT.println(line); OUT.flush(); }
    }

    /** Timestamped, flushed milestone log — same shape as the originals' {@code LogAt()}. */
    public static void log(String role, String fmt, Object... args) {
        String body = (args == null || args.length == 0) ? fmt : String.format(fmt, args);
        synchronized (LOCK) {
            OUT.printf("[%s tid=%d %s] %s%n",
                       LocalTime.now().format(TS), Thread.currentThread().threadId(), role, body);
            OUT.flush();
        }
    }

    /** Print an arriving message the way the originals printed one. */
    public static void logMessage(String role, String kind, String source, String text) {
        synchronized (LOCK) {
            OUT.printf("%n[%s] %s from '%s':%n  > %s%n%n", role, kind, source, text);
            OUT.flush();
        }
    }

    /**
     * A manual "did the thing happen yet" event. Replaces the raw
     * {@code CreateEvent}/{@code WaitForSingleObject}/{@code CloseHandle}
     * triplet each original repeated.
     */
    public static final class Gate {
        private final CountDownLatch latch = new CountDownLatch(1);
        public void open() { latch.countDown(); }
        /** Named {@code await}, not {@code wait}: {@code Object.wait(long)} is final. */
        public boolean await(long millis) {
            try { return latch.await(millis, TimeUnit.MILLISECONDS); }
            catch (InterruptedException e) { Thread.currentThread().interrupt(); return false; }
        }
        public boolean isOpen() { return latch.getCount() == 0; }
    }

    /** Wait for several gates at once (the "both hubs got it" case). */
    public static boolean awaitAll(long millis, Gate... gates) {
        long deadline = System.nanoTime() + TimeUnit.MILLISECONDS.toNanos(millis);
        for (Gate g : gates) {
            long left = TimeUnit.NANOSECONDS.toMillis(deadline - System.nanoTime());
            if (left <= 0 || !g.await(left)) return false;
        }
        return true;
    }

    /** Announce the verdict in the originals' words, and return the exit code. */
    public static int verdict(boolean ok, String okMsg, String failMsg) {
        log("MAIN", ok ? "SUCCESS - %s" : "TIMEOUT - %s", ok ? okMsg : failMsg);
        int code = ok ? EXIT_SUCCESS : EXIT_TIMEOUT;
        banner("Done (exit=" + code + ").");
        return code;
    }

    /** Verdict with an explicit code, for the harnesses that check more than one thing. */
    public static int verdict(int code, String msg) {
        log("MAIN", "%s - %s", code == EXIT_SUCCESS ? "SUCCESS" : "FAILED", msg);
        banner("Done (exit=" + code + ").");
        return code;
    }

    public static void sleep(long millis) {
        try { Thread.sleep(millis); }
        catch (InterruptedException e) { Thread.currentThread().interrupt(); }
    }

    /**
     * Run a harness body and turn anything that escapes into exit 1.
     *
     * <p>The originals reported a startup failure by returning 1 from
     * {@code main} after a {@code FATAL:} line. The facade signals the same
     * class of failure by throwing out of {@code Network.open()} or
     * {@code createHub}, so this is where that becomes an exit code.
     */
    public static void run(String name, HarnessBody body) {
        int code;
        try {
            code = body.run();
        } catch (Throwable t) {
            synchronized (LOCK) {
                OUT.println("FATAL: " + t);
                OUT.flush();
            }
            t.printStackTrace();
            code = EXIT_SETUP;
        }
        System.out.flush();
        System.exit(code);
    }

    @FunctionalInterface
    public interface HarnessBody { int run() throws Exception; }
}
