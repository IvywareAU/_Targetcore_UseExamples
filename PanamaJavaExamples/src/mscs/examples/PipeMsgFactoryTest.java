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
//
// PipeMsgFactoryTest.java  (Java -- Panama FFI over TargetFacade)
//
// The same HubPing -> HubPong round trip as PipeMsgMapTest, with the SEND SITE
// changed -- which is exactly the relationship the two originals had.
//
// WHAT THE ORIGINAL WAS ABOUT, AND WHAT HAPPENED TO IT. The original existed to
// show that the framework's message FACTORIES produce something that routes
// identically to a hand-built P2PeerMsg32:
//
//     P2PeerMsg32 oSeed(kClientAddr, kClientAddr, kMsgPing, lpszMsg, nBytes);
//     P2PeerMsg*  pMsg = oSeed.RedirectFactory(kServerAddr, kMsgPing, lpszMsg, nBytes);
//     PostP2PeerMsg(pMsg);
//
// ...and it carried a long warning about why RedirectFactory and NOT
// ResponseFactory: ResponseFactory reverses the addressing but also inherits the
// received message's routing prefix, so standalone-posting one loops the reply
// back into the current hub's own map instead of sending it across the pipe.
//
// The facade has no factory family, so that entire class of decision is gone:
// there is ONE send verb, it takes a destination and a topic, and there is no
// envelope to inherit. This file therefore keeps what the original was really
// demonstrating -- a caller-constructed binary payload routed to a named handler
// -- and drops the part that only existed to navigate the factory API.
//
// THE OWNERSHIP STORY IS THE SHARPEST HERE OF ANY HARNESS IN THIS TREE, because
// Java is the first of the five languages with no way to express the original's
// bug. The factory returned a heap message the caller owned and PostP2PeerMsg
// then took; a leak or a double-free lived in that handoff. send() copies the
// caller's bytes before it returns, so there is no handoff -- and on this side of
// the FFI the bytes are a byte[] the collector owns anyway. The Arena that holds
// the native copy lives exactly as long as the call.
//
// The receive side keeps the rule the C++ version keeps, and for the same
// reason: the payload pointer is valid only inside the callback. mscs.p2pf.Hub
// copies it into a byte[] on the way in rather than trusting a handler not to
// stash a MemorySegment -- a stashed one would read freed memory later without
// any error, since the segment's bounds say nothing about the kernel's lifetime.
//
//   client --HubPing(binary struct)--> [pipe] --> server's onTopic(HubPing)
//   server --HubPong(binary struct)--> [pipe] --> client's onTopic(HubPong)
//
// Verdict by EXIT CODE:
//   0 = SUCCESS : the client received the server's HubPong reply, and the struct
//                 came back byte-for-byte with its sequence incremented.
//   3 = TIMEOUT : the round trip did not complete in time.
//   1 = SETUP   : the network or a hub could not be created / armed.
package mscs.examples;

import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.nio.charset.StandardCharsets;
import java.util.concurrent.atomic.AtomicBoolean;

import mscs.harness.Harness;
import mscs.p2pf.Abi;
import mscs.p2pf.Hub;
import mscs.p2pf.Network;

public final class PipeMsgFactoryTest {

    private static final String PIPE_NAME   = "\\\\.\\pipe\\P2PmsgFactoryProbeJava";
    private static final String SERVER_ADDR = "MsgFac.Server";
    private static final String CLIENT_ADDR = "MsgFac.Client";

    private static final String MSG_PING = "HubPing";
    private static final String MSG_PONG = "HubPong";

    private static final int PROBE_MAGIC = 0x50524F42;   // 'PROB'

    /**
     * The caller-built payload, laid out to match the C++ harnesses' packed
     * struct byte for byte:
     * <pre>
     *   unsigned int magic;        //  4
     *   unsigned int sequence;     //  4
     *   wchar_t      note[48];     // 96   (UTF-16LE)
     * </pre>
     * Little-endian explicitly, not by luck: {@code ByteBuffer} defaults to
     * BIG_ENDIAN, which would put the magic on the wire backwards. Any POD
     * travels as-is -- the facade copies the bytes and hands the receiver a
     * pointer valid for the callback's duration.
     */
    private static final int NOTE_CHARS = 48;
    private static final int PROBE_SIZE = 4 + 4 + NOTE_CHARS * 2;   // 104

    private static byte[] probe(int magic, int sequence, String note) {
        ByteBuffer b = ByteBuffer.allocate(PROBE_SIZE).order(ByteOrder.LITTLE_ENDIAN);
        b.putInt(magic);
        b.putInt(sequence);
        byte[] n = note.getBytes(StandardCharsets.UTF_16LE);
        b.put(n, 0, Math.min(n.length, (NOTE_CHARS - 1) * 2));      // always NUL-terminated
        return b.array();
    }

    private static int    magicOf(byte[] p) { return ByteBuffer.wrap(p).order(ByteOrder.LITTLE_ENDIAN).getInt(0); }
    private static int    seqOf  (byte[] p) { return ByteBuffer.wrap(p).order(ByteOrder.LITTLE_ENDIAN).getInt(4); }
    private static String noteOf (byte[] p) {
        String s = new String(p, 8, NOTE_CHARS * 2, StandardCharsets.UTF_16LE);
        int nul = s.indexOf('\0');
        return nul >= 0 ? s.substring(0, nul) : s;
    }

    public static void main(String[] args) {
        Harness.run("PipeMsgFactoryTest", () -> {
            Harness.banner("=== PipeMsgFactoryTest (Java) - caller-built payloads, one send verb ===");
            Harness.banner("Pipe   : " + PIPE_NAME);
            Harness.banner("Payload: struct Probe (" + PROBE_SIZE
                           + " bytes), sent with send(), not sendText()");
            Harness.banner("");

            Harness.Gate  done      = new Harness.Gate();
            // Written on the client's pump thread, read on main after the gate.
            AtomicBoolean replyGood = new AtomicBoolean(false);

            try (Network net = Network.open()) {
                Harness.log("MAIN", "facade : %s", net.versionString());

                try (Hub server = net.createHub(SERVER_ADDR);
                     Hub client = net.createHub(CLIENT_ADDR)) {

                    // ---- SERVER: receives the struct, bumps it, sends it back ----
                    server.onTopic(MSG_PING, m -> {
                        if (m.payload.length != PROBE_SIZE) {
                            Harness.log("SERVER", "unexpected payload size %d", m.payload.length);
                            return;
                        }
                        Harness.banner(String.format("%n[SERVER] onTopic('%s')  from='%s'  magic=%08X seq=%d%n  > %s%n",
                                m.topic, m.source, magicOf(m.payload), seqOf(m.payload), noteOf(m.payload)));

                        byte[] out = probe(magicOf(m.payload), seqOf(m.payload) + 1,
                                           "Pong: server bumped your sequence.");
                        int hr = server.send(m.source, MSG_PONG, out);
                        Harness.log("SERVER", "replied '%s' -> '%s' (seq %d) : %s",
                                    MSG_PONG, m.source, seqOf(out), Abi.name(hr));
                    });
                    server.onPeerUp(p -> Harness.log("SERVER", "peer up : %s", p));
                    server.onError (w -> Harness.log("SERVER", "error   : %s", w));

                    int hr = server.listen(CLIENT_ADDR, Abi.pipe(PIPE_NAME));
                    if (Abi.failed(hr)) {
                        Harness.log("SERVER", "FATAL: pipe listen failed (%s)", Abi.name(hr));
                        return Harness.EXIT_SETUP;
                    }

                    // ---- CLIENT: sends the struct, verifies what comes back ----
                    client.onTopic(MSG_PONG, m -> {
                        if (m.payload.length != PROBE_SIZE) {
                            Harness.log("CLIENT", "unexpected payload size %d", m.payload.length);
                            done.open();
                            return;
                        }
                        Harness.banner(String.format("%n[CLIENT] onTopic('%s')  from='%s'  magic=%08X seq=%d%n  > %s%n",
                                m.topic, m.source, magicOf(m.payload), seqOf(m.payload), noteOf(m.payload)));

                        replyGood.set(magicOf(m.payload) == PROBE_MAGIC && seqOf(m.payload) == 2);
                        done.open();
                    });

                    client.onPeerUp(p -> {
                        Harness.log("CLIENT", "peer up : %s - pipe ready, sending ping", p);
                        byte[] ping = probe(PROBE_MAGIC, 1, "Ping: hello Server, this is Client.");
                        int sent = client.send(SERVER_ADDR, MSG_PING, ping);
                        Harness.log("CLIENT", "sent '%s' -> '%s' (seq %d) : %s",
                                    MSG_PING, SERVER_ADDR, seqOf(ping), Abi.name(sent));
                    });
                    client.onError(w -> Harness.log("CLIENT", "error   : %s", w));

                    hr = client.connect(SERVER_ADDR, Abi.pipe(PIPE_NAME));
                    if (Abi.failed(hr)) {
                        Harness.log("CLIENT", "FATAL: pipe connect failed (%s)", Abi.name(hr));
                        return Harness.EXIT_SETUP;
                    }

                    Harness.log("MAIN", "waiting up to 10s for the binary round trip...");
                    boolean ok = done.await(10_000) && replyGood.get();

                    Harness.log("MAIN", "shutdown begin");
                    return Harness.verdict(ok,
                            "the struct round-tripped with its sequence incremented",
                            "no valid reply delivered (round trip did not complete)");
                }
            }
        });
    }
}
