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
// PipeMsgMapTest.java  (Java -- Panama FFI over TargetFacade)
//
// TWO HUBS, ONE PROCESS, NAMED PIPE, request/response routed BY MESSAGE NAME.
//
// The sharpest before/after in the set. The original demonstrated P2PeerMsg_MAP
// routing with a subclass and three macros:
//
//     class PipeMapHub : public P2PeerHub
//     {
//         DECLARE_P2PeerMsg_MAP()
//         msgRESULT On_HubPing(P2PeerMsg* pMsg) { ... }
//         msgRESULT On_HubPong(P2PeerMsg* pMsg) { ... }
//     };
//     BEGIN_P2PeerMsg_MAP(PipeMapHub, P2PeerHub)
//         ON_P2PeerMsg(kMsgPing, On_HubPing)
//         ON_P2PeerMsg(kMsgPong, On_HubPong)
//     END_P2PeerMsg_MAP()
//
// The Java version is the same routing as a map lookup and a lambda:
//
//     server.onTopic(MSG_PING, m -> { ... });
//     client.onTopic(MSG_PONG, m -> { ... });
//
// A facade "topic" IS the kernel message name -- dispatched by the kernel's own
// wildcard map machinery inside the facade, which is why the routing semantics
// are identical rather than merely similar. Names beginning with "P2Pmsg" stay
// kernel-reserved and are refused with P2PF_E_RESERVED_TOPIC; the last check
// below proves it.
//
// This harness is where a compile-time map becoming a run-time one is most
// visible. The macro version resolved names when the C++ was compiled; here the
// registry is a ConcurrentHashMap read on the pump thread, so a topic can be
// registered, replaced or removed while the hub runs. Nothing in this tree needs
// that -- but it is the difference, and it is the reason the map had to move out
// of the class and into an object.
//
//   client --HubPing--> [pipe] --> server's onTopic(HubPing)
//   server --HubPong--> [pipe] --> client's onTopic(HubPong)  => DONE
//
// Verdict by EXIT CODE:
//   0 = SUCCESS : the client received the server's HubPong reply.
//   3 = TIMEOUT : the round trip did not complete in time.
//   1 = SETUP   : the network or a hub could not be created / armed.
package mscs.examples;

import mscs.harness.Harness;
import mscs.p2pf.Abi;
import mscs.p2pf.Hub;
import mscs.p2pf.Network;

public final class PipeMsgMapTest {

    private static final String PIPE_NAME   = "\\\\.\\pipe\\P2PmsgMapProbeJava";
    private static final String SERVER_ADDR = "MsgMap.Server";
    private static final String CLIENT_ADDR = "MsgMap.Client";

    // Application-defined message names -- topics, in facade terms.
    private static final String MSG_PING = "HubPing";
    private static final String MSG_PONG = "HubPong";

    public static void main(String[] args) {
        Harness.run("PipeMsgMapTest", () -> {
            Harness.banner("=== PipeMsgMapTest (Java) - named-message routing, no macros ===");
            Harness.banner("Pipe   : " + PIPE_NAME);
            Harness.banner("Server : " + SERVER_ADDR);
            Harness.banner("Client : " + CLIENT_ADDR);
            Harness.banner("");

            Harness.Gate done = new Harness.Gate();

            try (Network net = Network.open()) {
                Harness.log("MAIN", "facade : %s", net.versionString());

                try (Hub server = net.createHub(SERVER_ADDR);
                     Hub client = net.createHub(CLIENT_ADDR)) {

                    // ---- SERVER: intercepts HubPing, replies with HubPong ----
                    server.onTopic(MSG_PING, m -> {
                        Harness.logMessage("SERVER", "onTopic('" + m.topic + "')", m.source, m.text());

                        // The reply is addressed straight back at the sender. No
                        // factory, no seed template, and none of the original's
                        // ResponseFactory caveat about inheriting the request's
                        // routing prefix -- there is no envelope to inherit,
                        // only a destination and a topic.
                        int hrReply = server.sendText(m.source, MSG_PONG,
                                                      "Pong: server got your ping.");
                        Harness.log("SERVER", "replied '%s' -> '%s' : %s",
                                    MSG_PONG, m.source, Abi.name(hrReply));
                    });
                    server.onPeerUp(p -> Harness.log("SERVER", "peer up : %s", p));
                    server.onError (w -> Harness.log("SERVER", "error   : %s", w));

                    int hr = server.listen(CLIENT_ADDR, Abi.pipe(PIPE_NAME));
                    if (Abi.failed(hr)) {
                        Harness.log("SERVER", "FATAL: pipe listen failed (%s)", Abi.name(hr));
                        return Harness.EXIT_SETUP;
                    }

                    // ---- CLIENT: sends HubPing, intercepts HubPong ----------
                    client.onTopic(MSG_PONG, m -> {
                        Harness.logMessage("CLIENT", "onTopic('" + m.topic + "')", m.source, m.text());
                        done.open();
                    });

                    // A message that matches no registered topic lands here
                    // instead of being dropped -- the equivalent of falling
                    // through the map to On_P2PeerUCast.
                    client.onMessage(m ->
                        Harness.log("CLIENT", "unrouted topic '%s' from '%s'", m.topic, m.source));

                    client.onPeerUp(p -> {
                        Harness.log("CLIENT", "peer up : %s - pipe ready, sending ping", p);
                        int sent = client.sendText(SERVER_ADDR, MSG_PING,
                                                   "Ping: hello Server, this is Client.");
                        Harness.log("CLIENT", "sent '%s' -> '%s' : %s",
                                    MSG_PING, SERVER_ADDR, Abi.name(sent));
                    });
                    client.onError(w -> Harness.log("CLIENT", "error   : %s", w));

                    hr = client.connect(SERVER_ADDR, Abi.pipe(PIPE_NAME));
                    if (Abi.failed(hr)) {
                        Harness.log("CLIENT", "FATAL: pipe connect failed (%s)", Abi.name(hr));
                        return Harness.EXIT_SETUP;
                    }

                    Harness.log("MAIN", "waiting up to 10s for the HubPing -> HubPong round trip...");
                    boolean ok = done.await(10_000);

                    // ---- The reserved-namespace rule, made visible ----------
                    int hrReserved = client.sendText(SERVER_ADDR, "P2Pmsg_Nope", "x");
                    Harness.log("MAIN", "topic 'P2Pmsg_Nope' -> %s (kernel-reserved namespace)",
                                Abi.name(hrReserved));
                    ok = ok && (hrReserved == Abi.E_RESERVED_TOPIC);

                    Harness.log("MAIN", "shutdown begin");
                    return Harness.verdict(ok,
                            "client received the server's HubPong reply",
                            "no reply delivered (routing did not complete)");
                }
            }
        });
    }
}
