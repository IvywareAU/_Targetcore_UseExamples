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
// RouteLoopbackTest.cs  (C# -- TargetCom)
//
// FOUR HUBS IN A TWO-LEVEL TREE, one process, driven through COM from managed
// code.
//
//              Root
//             /    \
//            A      B
//            |
//          Leaf
//
// As in the two rewrites before it: the ORIGINAL of this harness is not an MSCS
// test at all -- it drives treehub_runtime's PeerNetwork::route(), the
// clean-room in-process router, with no P2PeerHub anywhere. There is nothing in
// it to put a COM layer over. So this asks the original's QUESTION of the real
// kernel instead.
//
// WHAT THIS VERSION SHOWS THAT THE C++ COM ONE DID NOT. The C++ harness's point
// was four hub objects, four connection points, four dispatch threads inside the
// DLL, all marshalling into ONE apartment -- every [deliver] line on the same
// thread as main. Run this one and look at the tids: they are FOUR DIFFERENT
// THREADS, because a managed sink is agile and no marshalling happens at all
// (common\ComHarness.cs has the measurement). Same server, same GIT, same STA
// declaration, opposite outcome -- decided entirely by what kind of object the
// sink is.
//
// Which is why Node.Count is Interlocked here and a plain LONG there. This is
// the harness where getting that wrong would actually lose a count.
//
//   [1] Leaf -> B      : non-adjacent. Up to the root, then down.
//   [2] Root -> Leaf   : straight down the tree.
//   [3] Leaf -> Z      : nobody by that address -- must go nowhere.
//   [4] Root broadcast : relayed to every descendant. Broadcast is also the one
//                        method whose COM signature had to change shape -- it
//                        returns Delivered as a value, which in C# is simply
//                        `bool Broadcast(...)`, because the facade's S_FALSE
//                        ("nobody was up") is invisible to automation.
//
// Verdict by EXIT CODE:
//   0 = SUCCESS : all four scenarios behaved as expected.
//   3 = FAIL    : a message went somewhere it should not have, or failed to arrive.
//   1 = SETUP   : the COM server is not registered, or the mesh could not be built.

using System;
using System.Threading;

using Com;
using TargetCom;

static class RouteLoopbackTest
{
    const string Root = "RouteMesh.Root";
    const string A    = "RouteMesh.Root.A";
    const string B    = "RouteMesh.Root.B";
    const string Leaf = "RouteMesh.Root.A.Leaf";
    const string Z    = "RouteMesh.Root.Z";

    const string SvcRootA = "RM_RootA_Net";
    const string SvcRootB = "RM_RootB_Net";
    const string SvcALeaf = "RM_ALeaf_Net";

    const string Topic = "route";

    sealed class Node
    {
        public readonly string Name;
        public readonly Gate   Gate = new Gate();
        private int m_count;

        public Node (string name) { Name = name; }

        // Bumped from the hub's own dispatch thread, read from main.
        public int  Count      { get { return Volatile.Read(ref m_count); } }
        public void Bump ()    { Interlocked.Increment(ref m_count); }
    }

    static void Attach (Hub hub, Node node)
    {
        hub.OnTopic(Topic, m =>
        {
            node.Bump();
            Harness.Print("    [deliver] {0} received \"{1}\" (src={2}{3}) [tid={4}]",
                          node.Name, m.Text, m.Source,
                          m.Broadcast ? ", broadcast" : "", Harness.Tid);
            node.Gate.Open();
        });
        hub.OnError(what => Harness.Print("    [error]   at {0}: {1}", node.Name, what));
    }

    static bool Check (string label, bool ok)
    {
        Harness.Print("    => {0}: {1}\n", ok ? "PASS" : "FAIL", label);
        return ok;
    }

    [STAThread]
    static int Main ()
    {
        Harness.InitConsole();
        Harness.Print("=== RouteLoopbackTest (C#) - four hubs, four dispatch threads, tree routing ===");
        Harness.Print("        {0}\n       /        \\\n    {1}   {2}\n      |\n   {3}\n", Root, A, B, Leaf);
        Harness.Print("main tid = {0}  (compare with the [deliver] tids below)\n", Harness.MainTid);

        if (!Apartment.IsSta) return Harness.ExitSetup;

        int hr;
        using var net = Network.Create(out hr);
        if (net == null) return Harness.SetupFailure("CoCreateInstance(TargetCom.P2PNetwork)", hr);

        var nRoot = new Node(Root);
        var nA    = new Node(A);
        var nB    = new Node(B);
        var nLeaf = new Node(Leaf);

        // Composed once: each service name is named by BOTH ends of its link.
        string epRootA = Endpoint.Dmx(SvcRootA);
        string epRootB = Endpoint.Dmx(SvcRootB);
        string epALeaf = Endpoint.Dmx(SvcALeaf);

        // ---- Build the tree. Listeners first: dmx:// dials do not retry. -----
        //
        // This is the one harness in the tree whose addresses are HIERARCHICAL,
        // so every arm below is a parent naming a child or a child naming its
        // parent, and none of them trips the sibling warning the others get.
        // Which is exactly the property this harness goes on to prove: Leaf -> B
        // works only BECAUSE every edge here is a real parent/child edge the
        // kernel will relay over.
        using var root = net.CreateHub(Root, out hr);
        if (root == null) return Harness.SetupFailure("CreateHub(Root)", hr);
        Attach(root, nRoot);
        hr = root.Listen(A, epRootA);
        if (!Hr.Failed(hr)) hr = root.Listen(B, epRootB);
        if (Hr.Failed(hr)) return Harness.SetupFailure("Root Listen(dmx)", hr);

        using var a = net.CreateHub(A, out hr);
        if (a == null) return Harness.SetupFailure("CreateHub(A)", hr);
        Attach(a, nA);
        hr = a.Listen(Leaf, epALeaf);
        if (!Hr.Failed(hr)) hr = a.Connect(Root, epRootA);
        if (Hr.Failed(hr)) return Harness.SetupFailure("A wiring", hr);

        using var b = net.CreateHub(B, out hr);
        if (b == null) return Harness.SetupFailure("CreateHub(B)", hr);
        Attach(b, nB);
        hr = b.Connect(Root, epRootB);
        if (Hr.Failed(hr)) return Harness.SetupFailure("B wiring", hr);

        using var leaf = net.CreateHub(Leaf, out hr);
        if (leaf == null) return Harness.SetupFailure("CreateHub(Leaf)", hr);
        Attach(leaf, nLeaf);
        hr = leaf.Connect(A, epALeaf);
        if (Hr.Failed(hr)) return Harness.SetupFailure("Leaf wiring", hr);

        Pump.For(1500);                 // let the three links log in
        Harness.Print("Links: Root<->A {0}   Root<->B {1}   A<->Leaf {2}\n",
                      root.IsPeerUp(A)    ? "up" : "DOWN",
                      root.IsPeerUp(B)    ? "up" : "DOWN",
                      a.IsPeerUp   (Leaf) ? "up" : "DOWN");

        bool allOk = true;

        // ---- [1] Non-adjacent, up then down -----------------------------------
        Harness.Print("[1] Leaf -> {0}  (non-adjacent: up to the root, then down)", B);
        hr = leaf.SendText(B, Topic, "hello from the leaf");
        Harness.Print("    SendText returned {0}", Hr.Name(hr));
        bool got1 = nB.Gate.Wait(5000);
        allOk &= Check("Leaf -> B delivered across two hops", got1 && nB.Count == 1);

        // ---- [2] Straight down -------------------------------------------------
        Harness.Print("[2] Root -> {0}  (straight down the tree)", Leaf);
        hr = root.SendText(Leaf, Topic, "ping, down we go");
        Harness.Print("    SendText returned {0}", Hr.Name(hr));
        bool got2 = nLeaf.Gate.Wait(5000);
        allOk &= Check("Root -> Leaf delivered via A", got2 && nLeaf.Count == 1);

        // ---- [3] No such address ------------------------------------------------
        Harness.Print("[3] Leaf -> {0}  (unknown branch; must not be delivered)", Z);
        int before = nRoot.Count + nA.Count + nB.Count + nLeaf.Count;
        hr = leaf.SendText(Z, Topic, "lost, no such node");
        Harness.Print("    SendText returned {0}", Hr.Name(hr));
        Pump.For(1500);
        int after = nRoot.Count + nA.Count + nB.Count + nLeaf.Count;
        allOk &= Check("Leaf -> Z delivered to nobody", before == after);

        // ---- [4] Broadcast, and its Delivered return value -----------------------
        Harness.Print("[4] Root broadcast  (relayed down; Delivered comes back as a VALUE)");
        int aBefore = nA.Count, bBefore = nB.Count, leafBefore = nLeaf.Count;

        // Same bytes as the C++ tree: the string plus its NUL terminator.
        byte[] allHands = System.Text.Encoding.Unicode.GetBytes("all hands\0");

        bool delivered;
        hr = root.Broadcast(Topic, allHands, out delivered);
        Harness.Print("    Broadcast returned {0}, Delivered={1}", Hr.Name(hr), delivered ? "True" : "False");
        Pump.For(2000);
        Harness.Print("    reached: A {0}   B {1}   Leaf {2}",
                      nA.Count    > aBefore    ? "yes" : "no",
                      nB.Count    > bBefore    ? "yes" : "no",
                      nLeaf.Count > leafBefore ? "yes" : "no");
        allOk &= Check("broadcast reached both children and reported Delivered",
                       delivered && nA.Count > aBefore && nB.Count > bBefore);

        Harness.Log("MAIN", "shutdown begin");
        return Harness.Verdict(allOk,
                               "the tree routed every scenario as expected",
                               "a route did not match expectations");
    }
}
