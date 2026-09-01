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
// PipeMeshTest.java  (Java -- Panama FFI over TargetFacade)
//
// SINGLE-PROCESS, TWO-HUB named-pipe connectivity probe.
//
// Identical in every respect to WsaMeshTest (Java) except ONE STRING: the
// endpoint reads "pipe://..." instead of "tcp://...". Not a different method,
// not a different overload -- a different value. That is the whole diff, and it
// is the point. In the original tree the same change meant a different header
// (P2PeerConPipe.h), a different factory class, different factory arguments, and
// a different endpoint type threaded through PostP2PeerCon.
//
//   original: P2PeerConPipe::ServiceFactory(kClientAddr, kPipeName)
//             oServer.PostP2PeerCon(pSvcCon)
//   java    : server.listen(CLIENT_ADDR, Abi.pipe(PIPE_NAME))
//
// It matters more from Java than from C++. A binding has to be written for each
// entry point it exposes; a transport that is a STRING costs the binding
// nothing, while a transport that is a class would have meant four more factory
// signatures in mscs.p2pf. All four transports in this tree go through the same
// two methods on IP2PHub, so the binding never grew for any of them.
//
// The original also needed Sleep(750) between arming the pipe and opening it,
// because a raw ClientFactory con calls CreateFile(OPEN_EXISTING) exactly once.
// Facade pipe dials retry, so that race is gone.
//
// Verdict by EXIT CODE:
//   0 = SUCCESS : the server hub received the client's message over the pipe.
//   3 = TIMEOUT : handshake or delivery did not complete in time.
//   1 = SETUP   : the network or a hub could not be created / armed.
package mscs.examples;

import mscs.harness.Harness;
import mscs.p2pf.Abi;
import mscs.p2pf.Hub;
import mscs.p2pf.Network;

public final class PipeMeshTest {

    private static final String PIPE_NAME   = "\\\\.\\pipe\\P2PmeshProbeJava";
    private static final String SERVER_ADDR = "PipeMesh.Server";
    private static final String CLIENT_ADDR = "PipeMesh.Client";
    private static final String TOPIC       = "mesh";

    public static void main(String[] args) {
        Harness.run("PipeMeshTest", () -> {
            Harness.banner("=== PipeMeshTest (Java) - two hubs, one process, named pipe ===");
            Harness.banner("Pipe : " + PIPE_NAME);
            Harness.banner("");

            Harness.Gate done = new Harness.Gate();

            try (Network net = Network.open()) {
                Harness.log("MAIN", "facade : %s", net.versionString());

                try (Hub server = net.createHub(SERVER_ADDR);
                     Hub client = net.createHub(CLIENT_ADDR)) {

                    // ---- Hub A: SERVER (creates + listens on the named pipe) ----
                    server.onTopic(TOPIC, m -> {
                        Harness.logMessage("SERVER", "message", m.source, m.text());
                        done.open();
                    });
                    server.onPeerUp  (p -> Harness.log("SERVER", "peer up   : %s", p));
                    server.onPeerDown(p -> Harness.log("SERVER", "peer down : %s", p));
                    server.onError   (w -> Harness.log("SERVER", "error     : %s", w));

                    int hr = server.listen(CLIENT_ADDR, Abi.pipe(PIPE_NAME));
                    if (Abi.failed(hr)) {
                        Harness.log("SERVER", "FATAL: pipe listen failed (%s)", Abi.name(hr));
                        return Harness.EXIT_SETUP;
                    }
                    Harness.log("SERVER", "pipe armed for '%s'", CLIENT_ADDR);

                    // ---- Hub B: CLIENT (opens the named pipe) ----------------
                    client.onPeerUp(p -> {
                        Harness.log("CLIENT", "peer up   : %s - pipe ready", p);
                        int sent = client.sendText(SERVER_ADDR, TOPIC,
                                                   "Hello over a named pipe, in one process!");
                        Harness.log("CLIENT", "sendText  : %s", Abi.name(sent));
                    });
                    client.onPeerDown(p -> Harness.log("CLIENT", "peer down : %s", p));
                    client.onError   (w -> Harness.log("CLIENT", "error     : %s", w));

                    hr = client.connect(SERVER_ADDR, Abi.pipe(PIPE_NAME));
                    if (Abi.failed(hr)) {
                        Harness.log("CLIENT", "FATAL: pipe connect failed (%s)", Abi.name(hr));
                        return Harness.EXIT_SETUP;
                    }
                    Harness.log("CLIENT", "opening the pipe (retries until it exists)");

                    Harness.log("MAIN", "waiting up to 10s for connect + delivery...");
                    boolean ok = done.await(10_000);

                    Harness.log("MAIN", "shutdown begin");
                    return Harness.verdict(ok,
                            "server received the client's message over the pipe",
                            "no message delivered (handshake did not complete)");
                }
            }
        });
    }
}
